/**
 * main.c - Entry point and FUSE operations
 * Simplified version without fuse_opt for custom options
 */

#include "cftpfs.h"
#include <fuse3/fuse_opt.h>
#include <stddef.h>

// In mock mode, we don't need curl
#ifdef USE_MOCK_FTP
#define curl_global_init(x) do {} while(0)
#define curl_global_cleanup() do {} while(0)
#define CURL_GLOBAL_DEFAULT 0
#endif

cftpfs_context_t *g_context = NULL;

static struct options {
    const char *host;
    const char *mountpoint;
    int port;
    const char *user;
    const char *password;
    const char *encoding;
    int debug;
    int foreground;
    int cache_timeout;  // Cache timeout in seconds
} options;

static void show_help_text(const char *progname) {
    printf("Usage: %s [options] <host> <mountpoint>\n\n", progname);
    printf("Options:\n");
    printf("    -p, --port=PORT          FTP Port (default: 21)\n");
    printf("    -u, --user=USER          FTP User (default: anonymous)\n");
    printf("    -P, --password=PASS      FTP Password\n");
    printf("    -e, --encoding=ENC       Encoding (default: utf-8)\n");
    printf("    -c, --cache-timeout=SEC  Cache timeout in seconds (default: %d, min: %d, max: %d)\n",
           CACHE_TIMEOUT_DEFAULT, CACHE_TIMEOUT_MIN, CACHE_TIMEOUT_MAX);
    printf("    --vscode                 Optimized mode for VS Code (extended cache)\n");
    printf("    -d, --debug              Debug mode with detailed logs\n");
    printf("    -f, --foreground         Run in foreground\n");
    printf("    -h, --help               Show this help\n\n");
    printf("Example:\n");
    printf("    %s ftp.example.com /mnt/ftp -u user -P password -f\n", progname);
    printf("    %s ftp.example.com /mnt/ftp -u user -P password --vscode -f\n", progname);
}

static int parse_args(int argc, char *argv[]) {
    // Default values
    options.port = 21;
    options.user = "anonymous";
    options.password = "";
    options.encoding = "utf-8";
    options.debug = 0;
    options.foreground = 0;
    options.cache_timeout = CACHE_TIMEOUT_DEFAULT;
    
    // First pass: process all options (in any position)
    int i = 1;
    while (i < argc) {
        if (argv[i][0] != '-') {
            i++;
            continue;
        }
        
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            return -1;
        } else if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--debug") == 0) {
            options.debug = 1;
            i++;
        } else if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--foreground") == 0) {
            options.foreground = 1;
            i++;
        } else if ((strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--port") == 0)) {
            if (i + 1 >= argc || argv[i+1][0] == '-') {
                fprintf(stderr, "Error: %s requires a value\n", argv[i]);
                return -1;
            }
            options.port = atoi(argv[++i]);
            i++;
        } else if ((strcmp(argv[i], "-u") == 0 || strcmp(argv[i], "--user") == 0)) {
            if (i + 1 >= argc || argv[i+1][0] == '-') {
                fprintf(stderr, "Error: %s requires a value\n", argv[i]);
                return -1;
            }
            options.user = argv[++i];
            i++;
        } else if ((strcmp(argv[i], "-P") == 0 || strcmp(argv[i], "--password") == 0)) {
            if (i + 1 >= argc || argv[i+1][0] == '-') {
                fprintf(stderr, "Error: %s requires a value\n", argv[i]);
                return -1;
            }
            options.password = argv[++i];
            i++;
        } else if ((strcmp(argv[i], "-e") == 0 || strcmp(argv[i], "--encoding") == 0)) {
            if (i + 1 >= argc || argv[i+1][0] == '-') {
                fprintf(stderr, "Error: %s requires a value\n", argv[i]);
                return -1;
            }
            options.encoding = argv[++i];
            i++;
        } else if ((strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--cache-timeout") == 0)) {
            if (i + 1 >= argc || argv[i+1][0] == '-') {
                fprintf(stderr, "Error: %s requires a value\n", argv[i]);
                return -1;
            }
            options.cache_timeout = atoi(argv[++i]);
            if (options.cache_timeout < CACHE_TIMEOUT_MIN) {
                options.cache_timeout = CACHE_TIMEOUT_MIN;
            } else if (options.cache_timeout > CACHE_TIMEOUT_MAX) {
                options.cache_timeout = CACHE_TIMEOUT_MAX;
            }
            i++;
        } else if (strcmp(argv[i], "--vscode") == 0) {
            // VS Code mode: more aggressive cache for better performance
            options.cache_timeout = 60;  // 1 minute cache
            i++;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            return -1;
        }
    }
    
    // Second pass: process host and mountpoint (args that are not options or values)
    int non_option_count = 0;
    char *non_options[2] = {NULL, NULL};
    
    i = 1;
    while (i < argc && non_option_count < 2) {
        if (argv[i][0] != '-') {
            // Check if it is an option value
            if (i > 1 && argv[i-1][0] == '-') {
                // It is an option value, skip
                i++;
                continue;
            }
            non_options[non_option_count++] = argv[i];
        }
        i++;
    }
    
    if (non_option_count != 2) {
        fprintf(stderr, "Error: host and mountpoint required (found: %d)\n", non_option_count);
        return -1;
    }
    
    options.host = non_options[0];
    options.mountpoint = non_options[1];
    
    return 0;
}

static int cftpfs_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi) {
    (void) fi;
    
    if (options.debug) {
        fprintf(stderr, "[DEBUG] getattr: %s\n", path);
    }
    
    memset(stbuf, 0, sizeof(struct stat));
    
    if (strcmp(path, "/") == 0) {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
        stbuf->st_uid = getuid();
        stbuf->st_gid = getgid();
        return 0;
    }
    
    pthread_mutex_lock(&g_context->ftp_lock);
    
    char *parent_path = strdup(path);
    char *basename = strrchr(parent_path, '/');
    
    if (basename) {
        *basename = '\0';
        basename++;
        if (*basename == '\0') {
            free(parent_path);
            pthread_mutex_unlock(&g_context->ftp_lock);
            return -ENOENT;
        }
    } else {
        free(parent_path);
        pthread_mutex_unlock(&g_context->ftp_lock);
        return -ENOENT;
    }
    
    ftp_item_t *items = NULL;
    int count = 0;
    int found = 0;
    int items_need_free = 0;
    
    cache_entry_t *cached = cache_get(g_context, parent_path);
    if (cached) {
        count = cached->item_count;
        if (count > 0) {
            items = malloc(count * sizeof(ftp_item_t));
            if (items) {
                memcpy(items, cached->items, count * sizeof(ftp_item_t));
                items_need_free = 1;
            }
        }
    } else {
        if (ftp_list_dir(g_context, parent_path, &items, &count) == 0) {
            cache_put(g_context, parent_path, items, count);
            // cache_put takes ownership of items, DO NOT free here
            items_need_free = 0;
        }
    }
    
    if (items) {
        for (int i = 0; i < count; i++) {
            if (strcmp(items[i].name, basename) == 0) {
                stbuf->st_mode = items[i].mode;
                stbuf->st_size = items[i].size;
                stbuf->st_mtime = items[i].mtime;
                stbuf->st_nlink = (items[i].type == FTP_TYPE_DIR) ? 2 : 1;
                stbuf->st_uid = getuid();
                stbuf->st_gid = getgid();
                found = 1;
                break;
            }
        }
    }
    
    if (items_need_free && items) {
        free(items);
    }
    
    free(parent_path);
    pthread_mutex_unlock(&g_context->ftp_lock);
    
    return found ? 0 : -ENOENT;
}

static int cftpfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                          off_t offset, struct fuse_file_info *fi,
                          enum fuse_readdir_flags flags) {
    (void) offset;
    (void) fi;
    (void) flags;
    
    if (options.debug) {
        fprintf(stderr, "[DEBUG] readdir: %s\n", path);
    }
    
    filler(buf, ".", NULL, 0, 0);
    filler(buf, "..", NULL, 0, 0);
    
    pthread_mutex_lock(&g_context->ftp_lock);
    
    ftp_item_t *items = NULL;
    int count = 0;
    int items_need_free = 0;
    
    cache_entry_t *cached = cache_get(g_context, path);
    if (cached) {
        count = cached->item_count;
        if (count > 0) {
            items = malloc(count * sizeof(ftp_item_t));
            if (items) {
                memcpy(items, cached->items, count * sizeof(ftp_item_t));
                items_need_free = 1;
            }
        }
    } else {
        if (ftp_list_dir(g_context, path, &items, &count) == 0) {
            cache_put(g_context, path, items, count);
            // cache_put takes ownership of items, DO NOT free here
            items_need_free = 0;
        } else {
            pthread_mutex_unlock(&g_context->ftp_lock);
            return -EIO;
        }
    }
    
    if (items) {
        for (int i = 0; i < count; i++) {
            struct stat st;
            memset(&st, 0, sizeof(st));
            st.st_mode = items[i].mode;
            st.st_size = items[i].size;
            st.st_mtime = items[i].mtime;
            filler(buf, items[i].name, &st, 0, 0);
        }
        
        if (items_need_free) {
            free(items);
        }
    }
    
    pthread_mutex_unlock(&g_context->ftp_lock);
    
    return 0;
}

static int cftpfs_open(const char *path, struct fuse_file_info *fi) {
    if (options.debug) {
        fprintf(stderr, "[DEBUG] open: %s (flags: %d)\n", path, fi->flags);
    }
    
    if ((fi->flags & O_ACCMODE) == O_RDONLY) {
        (void)path;  // Avoid unused parameter warning
        return 0;
    }
    
    pthread_mutex_lock(&g_context->handles_lock);
    
    file_handle_t *fh = handle_create(g_context, path, fi->flags);
    if (!fh) {
        pthread_mutex_unlock(&g_context->handles_lock);
        return -EMFILE;
    }
    
    int handle_id = -1;
    for (int i = 0; i < MAX_HANDLES; i++) {
        if (g_context->file_handles[i] == NULL) {
            g_context->file_handles[i] = fh;
            handle_id = i;
            break;
        }
    }
    
    if (handle_id < 0) {
        pthread_mutex_destroy(&fh->lock);
        if (strlen(fh->temp_path) > 0) {
            unlink(fh->temp_path);
        }
        free(fh);
        pthread_mutex_unlock(&g_context->handles_lock);
        return -EMFILE;
    }
    
    if (!(fi->flags & O_CREAT) || (fi->flags & O_TRUNC)) {
        pthread_mutex_lock(&g_context->ftp_lock);
        ftp_download(g_context, path, fh->temp_path);
        pthread_mutex_unlock(&g_context->ftp_lock);
    } else {
        fh->is_new = true;
    }
    
    pthread_mutex_unlock(&g_context->handles_lock);
    
    fi->fh = handle_id;
    
    return 0;
}

static int cftpfs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
    (void) mode;
    
    if (options.debug) {
        fprintf(stderr, "[DEBUG] create: %s\n", path);
    }
    
    return cftpfs_open(path, fi);
}

static int cftpfs_read(const char *path, char *buf, size_t size, off_t offset,
                       struct fuse_file_info *fi) {
    if (options.debug) {
        fprintf(stderr, "[DEBUG] read: %s (size: %zu, offset: %ld)\n", path, size, offset);
    }
    
    int fd;
    char temp_path[MAX_PATH_LEN];
    
    if (fi->fh < MAX_HANDLES && g_context->file_handles[fi->fh]) {
        file_handle_t *fh = g_context->file_handles[fi->fh];
        strncpy(temp_path, fh->temp_path, MAX_PATH_LEN - 1);
        temp_path[MAX_PATH_LEN - 1] = '\0';
    } else {
        snprintf(temp_path, MAX_PATH_LEN, "%s/read_%p_%lu", 
                 g_context->temp_dir, (void*)pthread_self(), time(NULL));
        
        pthread_mutex_lock(&g_context->ftp_lock);
        if (ftp_download(g_context, path, temp_path) != 0) {
            pthread_mutex_unlock(&g_context->ftp_lock);
            return -EIO;
        }
        pthread_mutex_unlock(&g_context->ftp_lock);
    }
    
    fd = open(temp_path, O_RDONLY);
    if (fd < 0) {
        return -errno;
    }
    
    if (lseek(fd, offset, SEEK_SET) < 0) {
        close(fd);
        return -errno;
    }
    
    ssize_t bytes_read = read(fd, buf, size);
    close(fd);
    
    if (!(fi->fh < MAX_HANDLES && g_context->file_handles[fi->fh])) {
        unlink(temp_path);
    }
    
    return (bytes_read >= 0) ? bytes_read : -errno;
}

static int cftpfs_write(const char *path, const char *buf, size_t size,
                        off_t offset, struct fuse_file_info *fi) {
    if (options.debug) {
        fprintf(stderr, "[DEBUG] write: %s (size: %zu, offset: %ld)\n", path, size, offset);
    }
    
    if (fi->fh >= MAX_HANDLES || !g_context->file_handles[fi->fh]) {
        return -EBADF;
    }
    
    file_handle_t *fh = g_context->file_handles[fi->fh];
    pthread_mutex_lock(&fh->lock);
    
    int fd = open(fh->temp_path, O_WRONLY | O_CREAT, 0644);
    if (fd < 0) {
        pthread_mutex_unlock(&fh->lock);
        return -errno;
    }
    
    if (lseek(fd, offset, SEEK_SET) < 0) {
        close(fd);
        pthread_mutex_unlock(&fh->lock);
        return -errno;
    }
    
    ssize_t bytes_written = write(fd, buf, size);
    if (bytes_written > 0) {
        fh->dirty = true;
    }
    
    close(fd);
    pthread_mutex_unlock(&fh->lock);
    
    return (bytes_written >= 0) ? bytes_written : -errno;
}

static int cftpfs_flush(const char *path, struct fuse_file_info *fi) {
    (void) path;
    (void) fi;
    
    return 0;
}

static int cftpfs_fsync(const char *path, int isdatasync, struct fuse_file_info *fi) {
    (void) path;
    (void) isdatasync;
    (void) fi;
    
    return 0;
}

static int cftpfs_release(const char *path, struct fuse_file_info *fi) {
    if (options.debug) {
        fprintf(stderr, "[DEBUG] release: %s\n", path);
    }
    
    if (fi->fh >= MAX_HANDLES || !g_context->file_handles[fi->fh]) {
        return 0;
    }
    
    file_handle_t *fh = g_context->file_handles[fi->fh];
    
    pthread_mutex_lock(&fh->lock);
    
    if (fh->dirty || fh->is_new) {
        pthread_mutex_lock(&g_context->ftp_lock);
        ftp_upload(g_context, fh->temp_path, path);
        pthread_mutex_unlock(&g_context->ftp_lock);
        
        char *parent = strdup(path);
        char *last_slash = strrchr(parent, '/');
        if (last_slash && last_slash != parent) {
            *last_slash = '\0';
            cache_invalidate(g_context, parent);
        }
        free(parent);
    }
    
    pthread_mutex_unlock(&fh->lock);
    
    pthread_mutex_lock(&g_context->handles_lock);
    handle_release(g_context, fi->fh);
    pthread_mutex_unlock(&g_context->handles_lock);
    
    return 0;
}

static int cftpfs_unlink(const char *path) {
    if (options.debug) {
        fprintf(stderr, "[DEBUG] unlink: %s\n", path);
    }
    
    pthread_mutex_lock(&g_context->ftp_lock);
    int ret = ftp_delete(g_context, path);
    pthread_mutex_unlock(&g_context->ftp_lock);
    
    if (ret == 0) {
        char *parent = strdup(path);
        char *last_slash = strrchr(parent, '/');
        if (last_slash && last_slash != parent) {
            *last_slash = '\0';
            cache_invalidate(g_context, parent);
        }
        free(parent);
    }
    
    return ret;
}

static int cftpfs_mkdir(const char *path, mode_t mode) {
    (void) mode;
    
    if (options.debug) {
        fprintf(stderr, "[DEBUG] mkdir: %s\n", path);
    }
    
    pthread_mutex_lock(&g_context->ftp_lock);
    int ret = ftp_mkdir(g_context, path);
    pthread_mutex_unlock(&g_context->ftp_lock);
    
    if (ret == 0) {
        char *parent = strdup(path);
        char *last_slash = strrchr(parent, '/');
        if (last_slash && last_slash != parent) {
            *last_slash = '\0';
            cache_invalidate(g_context, parent);
        }
        free(parent);
    }
    
    return ret;
}

static int cftpfs_rmdir(const char *path) {
    if (options.debug) {
        fprintf(stderr, "[DEBUG] rmdir: %s\n", path);
    }
    
    pthread_mutex_lock(&g_context->ftp_lock);
    int ret = ftp_rmdir(g_context, path);
    pthread_mutex_unlock(&g_context->ftp_lock);
    
    if (ret == 0) {
        char *parent = strdup(path);
        char *last_slash = strrchr(parent, '/');
        if (last_slash && last_slash != parent) {
            *last_slash = '\0';
            cache_invalidate(g_context, parent);
        }
        free(parent);
    }
    
    return ret;
}

static int cftpfs_rename(const char *from, const char *to, unsigned int flags) {
    (void) flags;
    
    if (options.debug) {
        fprintf(stderr, "[DEBUG] rename: %s -> %s\n", from, to);
    }
    
    pthread_mutex_lock(&g_context->ftp_lock);
    int ret = ftp_rename(g_context, from, to);
    pthread_mutex_unlock(&g_context->ftp_lock);
    
    if (ret == 0) {
        cache_invalidate(g_context, "/");
    }
    
    return ret;
}

static int cftpfs_truncate(const char *path, off_t size, struct fuse_file_info *fi) {
    (void) fi;
    
    if (options.debug) {
        fprintf(stderr, "[DEBUG] truncate: %s (size: %ld)\n", path, size);
    }
    
    char temp_path[MAX_PATH_LEN];
    snprintf(temp_path, MAX_PATH_LEN, "%s/trunc_%p_%lu", 
             g_context->temp_dir, (void*)pthread_self(), time(NULL));
    
    pthread_mutex_lock(&g_context->ftp_lock);
    
    if (ftp_download(g_context, path, temp_path) == 0) {
        truncate(temp_path, size);
        ftp_upload(g_context, temp_path, path);
        unlink(temp_path);
    } else {
        int fd = open(temp_path, O_WRONLY | O_CREAT, 0644);
        if (fd >= 0) {
            ftruncate(fd, size);
            close(fd);
            ftp_upload(g_context, temp_path, path);
            unlink(temp_path);
        }
    }
    
    pthread_mutex_unlock(&g_context->ftp_lock);
    
    return 0;
}

static int cftpfs_chmod(const char *path, mode_t mode, struct fuse_file_info *fi) {
    (void) path;
    (void) mode;
    (void) fi;
    return 0;
}

static int cftpfs_chown(const char *path, uid_t uid, gid_t gid, struct fuse_file_info *fi) {
    (void) path;
    (void) uid;
    (void) gid;
    (void) fi;
    return 0;
}

static int cftpfs_utimens(const char *path, const struct timespec tv[2],
                          struct fuse_file_info *fi) {
    (void) path;
    (void) tv;
    (void) fi;
    return 0;
}

static void* cftpfs_init(struct fuse_conn_info *conn, struct fuse_config *cfg) {
    (void) conn;
    (void) cfg;
    
    if (options.debug) {
        fprintf(stderr, "[DEBUG] init\n");
    }
    
    return g_context;
}

static void cftpfs_destroy(void *private_data) {
    (void) private_data;
    
    if (options.debug) {
        fprintf(stderr, "[DEBUG] destroy\n");
    }
}

static const struct fuse_operations cftpfs_oper = {
    .getattr     = cftpfs_getattr,
    .readdir     = cftpfs_readdir,
    .open        = cftpfs_open,
    .create      = cftpfs_create,
    .read        = cftpfs_read,
    .write       = cftpfs_write,
    .flush       = cftpfs_flush,
    .fsync       = cftpfs_fsync,
    .release     = cftpfs_release,
    .unlink      = cftpfs_unlink,
    .mkdir       = cftpfs_mkdir,
    .rmdir       = cftpfs_rmdir,
    .rename      = cftpfs_rename,
    .truncate    = cftpfs_truncate,
    .chmod       = cftpfs_chmod,
    .chown       = cftpfs_chown,
    .utimens     = cftpfs_utimens,
    .init        = cftpfs_init,
    .destroy     = cftpfs_destroy,
};

int main(int argc, char *argv[]) {
    // Parse arguments manually
    if (parse_args(argc, argv) < 0) {
        show_help_text(argv[0]);
        return 1;
    }
    
    printf("cFtpfs v%s - Mounting %s on %s\n", CFTPFS_VERSION, options.host, options.mountpoint);
    printf("User: %s, Port: %d\n", options.user, options.port);
    
    // Initialize context
    g_context = calloc(1, sizeof(cftpfs_context_t));
    if (!g_context) {
        fprintf(stderr, "Error: Could not allocate memory\n");
        return 1;
    }
    
    strncpy(g_context->host, options.host, sizeof(g_context->host) - 1);
    g_context->port = options.port;
    strncpy(g_context->user, options.user, sizeof(g_context->user) - 1);
    strncpy(g_context->password, options.password, sizeof(g_context->password) - 1);
    strncpy(g_context->encoding, options.encoding, sizeof(g_context->encoding) - 1);
    g_context->debug = options.debug;
    g_context->cache_timeout = options.cache_timeout;
    g_context->next_handle = 1;
    
    pthread_mutex_init(&g_context->ftp_lock, NULL);
    pthread_mutex_init(&g_context->cache_lock, NULL);
    pthread_mutex_init(&g_context->handles_lock, NULL);
    
    // Create temporary directory
    snprintf(g_context->temp_dir, MAX_PATH_LEN, "%s%d_%lu", 
             TEMP_DIR_PREFIX, getpid(), time(NULL));
    if (mkdir(g_context->temp_dir, 0700) < 0) {
        fprintf(stderr, "Error: Could not create temporary directory %s\n", g_context->temp_dir);
        free(g_context);
        return 1;
    }
    
    // Initialize cURL
    curl_global_init(CURL_GLOBAL_DEFAULT);
    
    // Initialize cache
    cache_init(g_context);
    
    // Create FUSE arguments
    struct fuse_args args = FUSE_ARGS_INIT(0, NULL);
    fuse_opt_add_arg(&args, argv[0]);
    fuse_opt_add_arg(&args, options.mountpoint);
    
    if (options.foreground) {
        fuse_opt_add_arg(&args, "-f");
    }
    fuse_opt_add_arg(&args, "-s");
    
    // Optimization: Configure kernel cache timeouts
    char opt_buf[64];
    snprintf(opt_buf, sizeof(opt_buf), "-oattr_timeout=%d", options.cache_timeout);
    fuse_opt_add_arg(&args, opt_buf);
    snprintf(opt_buf, sizeof(opt_buf), "-oentry_timeout=%d", options.cache_timeout);
    fuse_opt_add_arg(&args, opt_buf);
    
    int ret = fuse_main(args.argc, args.argv, &cftpfs_oper, g_context);
    
    // Cleanup
    // NOTE: Do not call fuse_opt_free_args, fuse_main handles args memory
    cache_clear(g_context);
    curl_global_cleanup();
    
    // Clean temporary directory
    char cmd[MAX_PATH_LEN + 50];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", g_context->temp_dir);
    system(cmd);
    
    pthread_mutex_destroy(&g_context->ftp_lock);
    pthread_mutex_destroy(&g_context->cache_lock);
    pthread_mutex_destroy(&g_context->handles_lock);
    
    free(g_context);
    
    return ret;
}