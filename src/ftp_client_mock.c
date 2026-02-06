/**
 * ftp_client_mock.c - Implementación mock del cliente FTP para testing/compilación
 * 
 * Este archivo es temporal para permitir la compilación sin libcurl instalado.
 * En un sistema con libcurl, usar ftp_client.c
 */

#include "cftpfs.h"

typedef void CURL;

// Mock de funciones de curl
void curl_global_init(long flags) { (void)flags; }
void curl_global_cleanup(void) {}
CURL* curl_easy_init(void) { return (CURL*)1; }
void curl_easy_cleanup(CURL *curl) { (void)curl; }
CURL* curl_easy_duphandle(CURL *curl) { (void)curl; return (CURL*)1; }
char* curl_easy_escape(CURL *curl, const char *string, int length) {
    (void)curl; (void)length;
    return strdup(string);
}
void curl_free(void *p) { free(p); }
int curl_easy_setopt(CURL *curl, int option, ...) { (void)curl; (void)option; return 0; }
int curl_easy_perform(CURL *curl) { (void)curl; return 0; }
const char* curl_easy_strerror(int code) { (void)code; return "mock error"; }
struct curl_slist* curl_slist_append(struct curl_slist *list, const char *string) {
    (void)string; return list;
}
void curl_slist_free_all(struct curl_slist *list) { (void)list; }

// Constantes de curl
#define CURLE_OK 0
#define CURL_GLOBAL_DEFAULT 0
#define CURLOPT_URL 1
#define CURLOPT_USERNAME 2
#define CURLOPT_PASSWORD 3
#define CURLOPT_FTP_SKIP_PASV_IP 4
#define CURLOPT_FTP_FILEMETHOD 5
#define CURLOPT_CONNECTTIMEOUT 6
#define CURLOPT_TIMEOUT 7
#define CURLOPT_VERBOSE 8
#define CURLOPT_WRITEFUNCTION 9
#define CURLOPT_WRITEDATA 10
#define CURLOPT_DIRLISTONLY 11
#define CURLOPT_READFUNCTION 12
#define CURLOPT_READDATA 13
#define CURLOPT_INFILESIZE_LARGE 14
#define CURLOPT_FTP_CREATE_MISSING_DIRS 15
#define CURLOPT_UPLOAD 16
#define CURLOPT_CUSTOMREQUEST 17
#define CURLOPT_QUOTE 18
#define CURLOPT_NOBODY 19
#define CURLFTPMETHOD_MULTICWD 0
#define CURLFTPMETHOD_NOCWD 1
#define CURLFTP_CREATE_DIR 0
#define CURLFTP_CREATE_DIR_RETRY 1

typedef long curl_off_t;

// Implementaciones
size_t write_callback(void *ptr, size_t size, size_t nmemb, void *userdata) {
    (void)ptr;
    response_buffer_t *buf = (response_buffer_t *)userdata;
    size_t total_size = size * nmemb;
    
    if (buf->size + total_size >= buf->capacity) {
        buf->capacity = (buf->size + total_size) * 2 + 1024;
        buf->data = realloc(buf->data, buf->capacity);
        if (!buf->data) return 0;
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

int ftp_connect(cftpfs_context_t *ctx) {
    fprintf(stderr, "[MOCK] ftp_connect a %s:%d\n", ctx->host, ctx->port);
    ctx->curl = (void*)1; // Mock pointer
    return 0;
}

void ftp_disconnect(cftpfs_context_t *ctx) {
    fprintf(stderr, "[MOCK] ftp_disconnect\n");
    ctx->curl = NULL;
}

int ftp_list_dir(cftpfs_context_t *ctx, const char *path, ftp_item_t **items, int *count) {
    (void)ctx;
    fprintf(stderr, "[MOCK] ftp_list_dir: %s\n", path);
    
    // Crear algunos items de ejemplo
    *count = 3;
    *items = calloc(*count, sizeof(ftp_item_t));
    if (!*items) return -1;
    
    strcpy((*items)[0].name, "archivo1.txt");
    (*items)[0].type = FTP_TYPE_FILE;
    (*items)[0].size = 1234;
    (*items)[0].mode = S_IFREG | 0644;
    (*items)[0].mtime = time(NULL);
    
    strcpy((*items)[1].name, "archivo2.txt");
    (*items)[1].type = FTP_TYPE_FILE;
    (*items)[1].size = 5678;
    (*items)[1].mode = S_IFREG | 0644;
    (*items)[1].mtime = time(NULL);
    
    strcpy((*items)[2].name, "directorio");
    (*items)[2].type = FTP_TYPE_DIR;
    (*items)[2].size = 4096;
    (*items)[2].mode = S_IFDIR | 0755;
    (*items)[2].mtime = time(NULL);
    
    return 0;
}

int ftp_download(cftpfs_context_t *ctx, const char *remote_path, const char *local_path) {
    (void)ctx;
    fprintf(stderr, "[MOCK] ftp_download: %s -> %s\n", remote_path, local_path);
    // Crear archivo vacío
    FILE *fp = fopen(local_path, "w");
    if (fp) fclose(fp);
    return 0;
}

int ftp_upload(cftpfs_context_t *ctx, const char *local_path, const char *remote_path) {
    (void)ctx;
    fprintf(stderr, "[MOCK] ftp_upload: %s -> %s\n", local_path, remote_path);
    return 0;
}

int ftp_delete(cftpfs_context_t *ctx, const char *path) {
    (void)ctx;
    fprintf(stderr, "[MOCK] ftp_delete: %s\n", path);
    return 0;
}

int ftp_mkdir(cftpfs_context_t *ctx, const char *path) {
    (void)ctx;
    fprintf(stderr, "[MOCK] ftp_mkdir: %s\n", path);
    return 0;
}

int ftp_rmdir(cftpfs_context_t *ctx, const char *path) {
    (void)ctx;
    fprintf(stderr, "[MOCK] ftp_rmdir: %s\n", path);
    return 0;
}

int ftp_rename(cftpfs_context_t *ctx, const char *old_path, const char *new_path) {
    (void)ctx;
    fprintf(stderr, "[MOCK] ftp_rename: %s -> %s\n", old_path, new_path);
    return 0;
}