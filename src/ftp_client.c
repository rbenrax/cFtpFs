/**
 * ftp_client.c - FTP client using libcurl
 */

#include "cftpfs.h"

size_t write_callback(void *ptr, size_t size, size_t nmemb, void *userdata) {
    response_buffer_t *buf = (response_buffer_t *)userdata;
    size_t total_size = size * nmemb;
    
    if (buf->size + total_size >= buf->capacity) {
        buf->capacity = (buf->size + total_size) * 2 + 1024;
        buf->data = realloc(buf->data, buf->capacity);
        if (!buf->data) {
            return 0;
        }
    }
    
    memcpy(buf->data + buf->size, ptr, total_size);
    buf->size += total_size;
    buf->data[buf->size] = '\0';
    
    return total_size;
}

size_t read_callback(void *ptr, size_t size, size_t nmemb, void *userdata) {
    FILE *fp = (FILE *)userdata;
    return fread(ptr, size, nmemb, fp);
}

// Helper function to encode FTP path (keeps "/" as separators)
static int encode_ftp_path(CURL *curl, const char *path, char *out, size_t out_len, bool is_dir) {
    if (!path || !out || out_len == 0) return -1;
    
    size_t out_pos = 0;
    const char *p = path;
    
    // Always start with /
    if (out_pos < out_len - 1) {
        out[out_pos++] = '/';
    }
    
    // Skip initial / if it exists
    if (*p == '/') p++;
    
    while (*p && out_pos < out_len - 1) {
        // Find next /
        const char *slash = strchr(p, '/');
        size_t component_len = slash ? (size_t)(slash - p) : strlen(p);
        
        if (component_len > 0) {
            // Encode this component
            char component[256];
            if (component_len >= sizeof(component)) component_len = sizeof(component) - 1;
            strncpy(component, p, component_len);
            component[component_len] = '\0';
            
            char *encoded = curl_easy_escape(curl, component, 0);
            if (encoded) {
                size_t enc_len = strlen(encoded);
                if (out_pos + enc_len < out_len - 1) {
                    strcpy(out + out_pos, encoded);
                    out_pos += enc_len;
                }
                curl_free(encoded);
            }
        }
        
        if (slash) {
            // Add separator /
            if (out_pos < out_len - 1) {
                out[out_pos++] = '/';
            }
            p = slash + 1;
        } else {
            break;
        }
    }
    
    // Add / at the end ONLY if it is a directory and doesn't have it already
    if (is_dir && out_pos > 0 && out[out_pos - 1] != '/' && out_pos < out_len - 1) {
        out[out_pos++] = '/';
    }
    
    out[out_pos] = '\0';
    return 0;
}

static void setup_common_curl_options(cftpfs_context_t *ctx, CURL *curl) {
    if (ctx->debug) {
        curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
    }
    curl_easy_setopt(curl, CURLOPT_USERNAME, ctx->user);
    curl_easy_setopt(curl, CURLOPT_PASSWORD, ctx->password);
    curl_easy_setopt(curl, CURLOPT_FTP_SKIP_PASV_IP, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L);
    // Enable TCP Keep-Alive
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPIDLE, 120L);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPINTVL, 60L);
}

int ftp_connect(cftpfs_context_t *ctx) {
    if (ctx->curl && ctx->conn_active) {
        return 0;
    }

    if (ctx->curl) {
        curl_easy_cleanup(ctx->curl);
    }
    
    ctx->curl = curl_easy_init();
    if (!ctx->curl) {
        fprintf(stderr, "Error: Could not initialize curl\n");
        return -1;
    }
    
    setup_common_curl_options(ctx, ctx->curl);
    ctx->conn_active = true;
    
    return 0;
}

void ftp_disconnect(cftpfs_context_t *ctx) {
    if (ctx->curl) {
        curl_easy_cleanup(ctx->curl);
        ctx->curl = NULL;
    }
    ctx->conn_active = false;
}

int ftp_list_dir(cftpfs_context_t *ctx, const char *path, ftp_item_t **items, int *count) {
    if (!ctx->curl || !ctx->conn_active) {
        if (ftp_connect(ctx) < 0) return -1;
    }
    
    CURL *curl = ctx->curl;
    curl_easy_reset(curl);
    setup_common_curl_options(ctx, curl);
    
    char url[MAX_PATH_LEN * 2];
    char encoded_path[MAX_PATH_LEN];
    
    // Encode path keeping / as separators (IT IS A DIRECTORY)
    if (encode_ftp_path(curl, path, encoded_path, sizeof(encoded_path), true) < 0) {
        return -1;
    }
    
    snprintf(url, sizeof(url), "ftp://%s:%d%s", ctx->host, ctx->port, encoded_path);
    
    response_buffer_t buf = {0};
    buf.capacity = 65536;  // Increase buffer to 64KB
    buf.data = malloc(buf.capacity);
    if (!buf.data) {
        return -1;
    }
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(curl, CURLOPT_DIRLISTONLY, 0L);
    // Use MULTICWD for compatibility
    curl_easy_setopt(curl, CURLOPT_FTP_FILEMETHOD, CURLFTPMETHOD_MULTICWD);
    
    CURLcode res = curl_easy_perform(curl);
    
    if (res != CURLE_OK) {
        fprintf(stderr, "Error FTP list: %s\n", curl_easy_strerror(res));
        free(buf.data);
        // If connection error, mark as inactive to reconnect next time
        if (res == CURLE_COULDNT_CONNECT || res == CURLE_OPERATION_TIMEDOUT || res == CURLE_FTP_ACCEPT_FAILED) {
             ftp_disconnect(ctx);
        }
        return -1;
    }
    
    // Parse listing - make copy of buffer because strtok modifies the string
    char *data_copy = strdup(buf.data);
    if (!data_copy) {
        free(buf.data);
        return -1;
    }
    
    // First pass: count lines
    *count = 0;
    char *line = strtok(data_copy, "\n");
    while (line) {
        (*count)++;
        line = strtok(NULL, "\n");
    }
    
    *items = calloc(*count, sizeof(ftp_item_t));
    if (!*items) {
        free(data_copy);
        free(buf.data);
        return -1;
    }
    
    // Second pass: parse (restore copy first)
    strcpy(data_copy, buf.data);
    int idx = 0;
    line = strtok(data_copy, "\n");
    while (line && idx < *count) {
        if (parse_ftp_listing(line, &(*items)[idx]) == 0) {
            idx++;
        }
        line = strtok(NULL, "\n");
    }
    
    *count = idx;
    free(data_copy);
    free(buf.data);
    
    return 0;
}

int ftp_download(cftpfs_context_t *ctx, const char *remote_path, const char *local_path) {
    if (!ctx->curl || !ctx->conn_active) {
        if (ftp_connect(ctx) < 0) return -1;
    }
    
    CURL *curl = ctx->curl;
    curl_easy_reset(curl);
    setup_common_curl_options(ctx, curl);
    
    char url[MAX_PATH_LEN * 2];
    char encoded_path[MAX_PATH_LEN];
    
    // Encode path keeping / as separators (IT IS A FILE)
    if (encode_ftp_path(curl, remote_path, encoded_path, sizeof(encoded_path), false) < 0) {
        return -1;
    }
    
    snprintf(url, sizeof(url), "ftp://%s:%d%s", ctx->host, ctx->port, encoded_path);
    
    FILE *fp = fopen(local_path, "wb");
    if (!fp) {
        return -1;
    }
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, NULL);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
    curl_easy_setopt(curl, CURLOPT_FTP_FILEMETHOD, CURLFTPMETHOD_NOCWD);
    
    CURLcode res = curl_easy_perform(curl);
    fclose(fp);
    
    if (res != CURLE_OK) {
        fprintf(stderr, "Error FTP download: %s\n", curl_easy_strerror(res));
        unlink(local_path);
        if (res == CURLE_COULDNT_CONNECT || res == CURLE_OPERATION_TIMEDOUT || res == CURLE_FTP_ACCEPT_FAILED) {
             ftp_disconnect(ctx);
        }
        return -1;
    }
    
    return 0;
}

int ftp_upload(cftpfs_context_t *ctx, const char *local_path, const char *remote_path) {
    if (!ctx->curl || !ctx->conn_active) {
        if (ftp_connect(ctx) < 0) return -1;
    }
    
    CURL *curl = ctx->curl;
    curl_easy_reset(curl);
    setup_common_curl_options(ctx, curl);
    
    char url[MAX_PATH_LEN * 2];
    char encoded_path[MAX_PATH_LEN];
    
    // Encode path keeping / as separators (IT IS A FILE)
    if (encode_ftp_path(curl, remote_path, encoded_path, sizeof(encoded_path), false) < 0) {
        return -1;
    }
    
    snprintf(url, sizeof(url), "ftp://%s:%d%s", ctx->host, ctx->port, encoded_path);
    
    FILE *fp = fopen(local_path, "rb");
    if (!fp) {
        return -1;
    }
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
    curl_easy_setopt(curl, CURLOPT_READFUNCTION, read_callback);
    curl_easy_setopt(curl, CURLOPT_READDATA, fp);
    curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, (curl_off_t)-1);
    curl_easy_setopt(curl, CURLOPT_FTP_CREATE_MISSING_DIRS, CURLFTP_CREATE_DIR);
    
    CURLcode res = curl_easy_perform(curl);
    fclose(fp);
    
    if (res != CURLE_OK) {
        fprintf(stderr, "Error FTP upload: %s\n", curl_easy_strerror(res));
        if (res == CURLE_COULDNT_CONNECT || res == CURLE_OPERATION_TIMEDOUT || res == CURLE_FTP_ACCEPT_FAILED) {
             ftp_disconnect(ctx);
        }
        return -1;
    }
    
    return 0;
}

int ftp_delete(cftpfs_context_t *ctx, const char *path) {
    if (!ctx->curl || !ctx->conn_active) {
        if (ftp_connect(ctx) < 0) return -1;
    }
    
    CURL *curl = ctx->curl;
    curl_easy_reset(curl);
    setup_common_curl_options(ctx, curl);
    
    char url[MAX_PATH_LEN * 2];
    char encoded_path[MAX_PATH_LEN];
    
    // Encode path keeping / as separators (IT IS A FILE)
    if (encode_ftp_path(curl, path, encoded_path, sizeof(encoded_path), false) < 0) {
        return -1;
    }
    
    snprintf(url, sizeof(url), "ftp://%s:%d%s", ctx->host, ctx->port, encoded_path);
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_QUOTE, NULL);
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELE");
    
    CURLcode res = curl_easy_perform(curl);
    
    if (res != CURLE_OK) {
        fprintf(stderr, "Error FTP delete: %s\n", curl_easy_strerror(res));
        if (res == CURLE_COULDNT_CONNECT || res == CURLE_OPERATION_TIMEDOUT || res == CURLE_FTP_ACCEPT_FAILED) {
             ftp_disconnect(ctx);
        }
        return -EIO;
    }
    
    return 0;
}

int ftp_mkdir(cftpfs_context_t *ctx, const char *path) {
    if (!ctx->curl || !ctx->conn_active) {
        if (ftp_connect(ctx) < 0) return -1;
    }
    
    CURL *curl = ctx->curl;
    curl_easy_reset(curl);
    setup_common_curl_options(ctx, curl);
    
    char url[MAX_PATH_LEN * 2];
    char encoded_path[MAX_PATH_LEN];
    
    // Encode path keeping / as separators (IT IS A DIRECTORY)
    if (encode_ftp_path(curl, path, encoded_path, sizeof(encoded_path), true) < 0) {
        return -1;
    }
    
    snprintf(url, sizeof(url), "ftp://%s:%d%s", ctx->host, ctx->port, encoded_path);
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_FTP_CREATE_MISSING_DIRS, CURLFTP_CREATE_DIR_RETRY);
    curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
    curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, (curl_off_t)0);
    
    CURLcode res = curl_easy_perform(curl);
    
    if (res != CURLE_OK) {
        fprintf(stderr, "Error FTP mkdir: %s\n", curl_easy_strerror(res));
        if (res == CURLE_COULDNT_CONNECT || res == CURLE_OPERATION_TIMEDOUT || res == CURLE_FTP_ACCEPT_FAILED) {
             ftp_disconnect(ctx);
        }
        return -EIO;
    }
    
    return 0;
}

int ftp_rmdir(cftpfs_context_t *ctx, const char *path) {
    if (!ctx->curl || !ctx->conn_active) {
        if (ftp_connect(ctx) < 0) return -1;
    }
    
    CURL *curl = ctx->curl;
    curl_easy_reset(curl);
    setup_common_curl_options(ctx, curl);
    
    char url[MAX_PATH_LEN * 2];
    char encoded_path[MAX_PATH_LEN];
    
    // Encode path keeping / as separators (IT IS A DIRECTORY)
    if (encode_ftp_path(curl, path, encoded_path, sizeof(encoded_path), true) < 0) {
        return -1;
    }
    
    snprintf(url, sizeof(url), "ftp://%s:%d%s", ctx->host, ctx->port, encoded_path);
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "RMD");
    
    CURLcode res = curl_easy_perform(curl);
    
    if (res != CURLE_OK) {
        fprintf(stderr, "Error FTP rmdir: %s\n", curl_easy_strerror(res));
        if (res == CURLE_COULDNT_CONNECT || res == CURLE_OPERATION_TIMEDOUT || res == CURLE_FTP_ACCEPT_FAILED) {
             ftp_disconnect(ctx);
        }
        return -EIO;
    }
    
    return 0;
}

int ftp_rename(cftpfs_context_t *ctx, const char *old_path, const char *new_path) {
    if (!ctx->curl || !ctx->conn_active) {
        if (ftp_connect(ctx) < 0) return -1;
    }
    
    CURL *curl = ctx->curl;
    curl_easy_reset(curl);
    setup_common_curl_options(ctx, curl);
    
    char cmd[MAX_PATH_LEN * 4];
    snprintf(cmd, sizeof(cmd), "RNFR %s", old_path);
    
    struct curl_slist *cmds = NULL;
    cmds = curl_slist_append(cmds, cmd);
    
    snprintf(cmd, sizeof(cmd), "RNTO %s", new_path);
    cmds = curl_slist_append(cmds, cmd);
    
    char url[MAX_PATH_LEN];
    snprintf(url, sizeof(url), "ftp://%s:%d/", ctx->host, ctx->port);
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_QUOTE, cmds);
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    
    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(cmds);
    
    if (res != CURLE_OK) {
        fprintf(stderr, "Error FTP rename: %s\n", curl_easy_strerror(res));
        if (res == CURLE_COULDNT_CONNECT || res == CURLE_OPERATION_TIMEDOUT || res == CURLE_FTP_ACCEPT_FAILED) {
             ftp_disconnect(ctx);
        }
        return -EIO;
    }
    
    return 0;
}