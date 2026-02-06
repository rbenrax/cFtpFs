/**
 * handles.c - File handle management
 */

#include "cftpfs.h"
#include <sys/stat.h>

file_handle_t* handle_create(cftpfs_context_t *ctx, const char *path, int flags) {
    file_handle_t *fh = calloc(1, sizeof(file_handle_t));
    if (!fh) {
        return NULL;
    }
    
    strncpy(fh->path, path, MAX_PATH_LEN - 1);
    fh->path[MAX_PATH_LEN - 1] = '\0';
    fh->flags = flags;
    fh->fd = -1;
    fh->dirty = false;
    fh->is_new = false;
    
    // Create temporary file
    snprintf(fh->temp_path, MAX_PATH_LEN, "%s/fh_%d_%ld_%p", 
             ctx->temp_dir, getpid(), (long)time(NULL), (void*)fh);
    
    // Create empty file
    int fd = open(fh->temp_path, O_CREAT | O_RDWR, 0600);
    if (fd < 0) {
        free(fh);
        return NULL;
    }
    close(fd);
    
    pthread_mutex_init(&fh->lock, NULL);
    
    return fh;
}

file_handle_t* handle_get(cftpfs_context_t *ctx, int fh_id) {
    if (fh_id < 0 || fh_id >= MAX_HANDLES) {
        return NULL;
    }
    
    pthread_mutex_lock(&ctx->handles_lock);
    file_handle_t *fh = ctx->file_handles[fh_id];
    pthread_mutex_unlock(&ctx->handles_lock);
    
    return fh;
}

void handle_release(cftpfs_context_t *ctx, int fh_id) {
    if (fh_id < 0 || fh_id >= MAX_HANDLES) {
        return;
    }
    
    file_handle_t *fh = ctx->file_handles[fh_id];
    if (!fh) {
        return;
    }
    
    pthread_mutex_destroy(&fh->lock);
    
    // Remove temporary file
    if (strlen(fh->temp_path) > 0) {
        unlink(fh->temp_path);
    }
    
    free(fh);
    ctx->file_handles[fh_id] = NULL;
}