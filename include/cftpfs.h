/**
 * cftpfs.h - Main definitions for the FTP filesystem in C
 * 
 * Based on PyFtpfs - FUSE implementation for mounting FTP servers
 */

#ifndef CFTPFS_H
#define CFTPFS_H

#define FUSE_USE_VERSION 31

#include <fuse3/fuse.h>

// Include curl only in real version (not mock)
#ifndef USE_MOCK_FTP
#include <curl/curl.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pthread.h>
#include <time.h>
#include <stdbool.h>
#include <ctype.h>

#define CFTPFS_VERSION "1.0.0"
#define MAX_PATH_LEN 4096

// Default cache timeout: 30 seconds (configurable with --cache-timeout)
#define CACHE_TIMEOUT_DEFAULT 30
#define CACHE_TIMEOUT_MIN 5
#define CACHE_TIMEOUT_MAX 300
#define MAX_FTP_LINE 4096
#define MAX_HANDLES 1024
#define TEMP_DIR_PREFIX "/tmp/cftpfs_"

typedef enum {
    FTP_TYPE_UNKNOWN = 0,
    FTP_TYPE_FILE,
    FTP_TYPE_DIR,
    FTP_TYPE_LINK
} ftp_item_type_t;

typedef struct {
    char name[MAX_PATH_LEN];
    ftp_item_type_t type;
    off_t size;
    time_t mtime;
    mode_t mode;
} ftp_item_t;

typedef struct cache_entry {
    char path[MAX_PATH_LEN];
    ftp_item_t *items;
    int item_count;
    time_t timestamp;
    struct cache_entry *next;
} cache_entry_t;

typedef struct {
    int fd;
    char path[MAX_PATH_LEN];
    char temp_path[MAX_PATH_LEN];
    int flags;
    bool dirty;
    bool is_new;
    pthread_mutex_t lock;
} file_handle_t;

typedef struct {
    char host[256];
    int port;
    char user[128];
    char password[128];
    char encoding[32];
    bool debug;
    int cache_timeout;  // Cache timeout in seconds
    
    bool conn_active;   // Indicates if the FTP connection is active
    
    void *curl;  // CURL* when using libcurl
    pthread_mutex_t ftp_lock;
    
    cache_entry_t *dir_cache;
    pthread_mutex_t cache_lock;
    
    file_handle_t *file_handles[MAX_HANDLES];
    pthread_mutex_t handles_lock;
    int next_handle;
    
    char temp_dir[MAX_PATH_LEN];
} cftpfs_context_t;

typedef struct {
    char *data;
    size_t size;
    size_t capacity;
} response_buffer_t;

extern cftpfs_context_t *g_context;

// FTP Functions
int ftp_connect(cftpfs_context_t *ctx);
void ftp_disconnect(cftpfs_context_t *ctx);
int ftp_list_dir(cftpfs_context_t *ctx, const char *path, ftp_item_t **items, int *count);
int ftp_download(cftpfs_context_t *ctx, const char *remote_path, const char *local_path);
int ftp_upload(cftpfs_context_t *ctx, const char *local_path, const char *remote_path);
int ftp_delete(cftpfs_context_t *ctx, const char *path);
int ftp_mkdir(cftpfs_context_t *ctx, const char *path);
int ftp_rmdir(cftpfs_context_t *ctx, const char *path);
int ftp_rename(cftpfs_context_t *ctx, const char *old_path, const char *new_path);

// Cache Functions
void cache_init(cftpfs_context_t *ctx);
void cache_clear(cftpfs_context_t *ctx);
cache_entry_t* cache_get(cftpfs_context_t *ctx, const char *path);
void cache_put(cftpfs_context_t *ctx, const char *path, ftp_item_t *items, int count);
void cache_invalidate(cftpfs_context_t *ctx, const char *path);

// FTP Listing Parser
int parse_ftp_listing(const char *line, ftp_item_t *item);
int parse_unix_listing(const char *line, ftp_item_t *item);
int parse_windows_listing(const char *line, ftp_item_t *item);

// Handle Management
file_handle_t* handle_create(cftpfs_context_t *ctx, const char *path, int flags);
file_handle_t* handle_get(cftpfs_context_t *ctx, int fh);
void handle_release(cftpfs_context_t *ctx, int fh);

// Curl callbacks
size_t write_callback(void *ptr, size_t size, size_t nmemb, void *userdata);
size_t read_callback(void *ptr, size_t size, size_t nmemb, void *userdata);

#endif