/**
 * cache.c - Cache system for directory listings
 */

#include "cftpfs.h"

void cache_init(cftpfs_context_t *ctx) {
    ctx->dir_cache = NULL;
}

void cache_clear(cftpfs_context_t *ctx) {
    pthread_mutex_lock(&ctx->cache_lock);
    
    cache_entry_t *current = ctx->dir_cache;
    while (current) {
        cache_entry_t *next = current->next;
        if (current->items) {
            free(current->items);
        }
        free(current);
        current = next;
    }
    ctx->dir_cache = NULL;
    
    pthread_mutex_unlock(&ctx->cache_lock);
}

cache_entry_t* cache_get(cftpfs_context_t *ctx, const char *path) {
    pthread_mutex_lock(&ctx->cache_lock);
    
    time_t now = time(NULL);
    cache_entry_t *current = ctx->dir_cache;
    cache_entry_t *prev = NULL;
    
    while (current) {
        if (strcmp(current->path, path) == 0) {
            // Check if cache expired (use context timeout)
            int timeout = ctx->cache_timeout > 0 ? ctx->cache_timeout : CACHE_TIMEOUT_DEFAULT;
            if (now - current->timestamp > timeout) {
                // Cache expired, remove
                if (prev) {
                    prev->next = current->next;
                } else {
                    ctx->dir_cache = current->next;
                }
                if (current->items) {
                    free(current->items);
                }
                free(current);
                pthread_mutex_unlock(&ctx->cache_lock);
                return NULL;
            }
            pthread_mutex_unlock(&ctx->cache_lock);
            return current;
        }
        prev = current;
        current = current->next;
    }
    
    pthread_mutex_unlock(&ctx->cache_lock);
    return NULL;
}

void cache_put(cftpfs_context_t *ctx, const char *path, ftp_item_t *items, int count) {
    pthread_mutex_lock(&ctx->cache_lock);
    
    // Remove existing entry if present
    cache_entry_t *current = ctx->dir_cache;
    cache_entry_t *prev = NULL;
    
    while (current) {
        if (strcmp(current->path, path) == 0) {
            if (prev) {
                prev->next = current->next;
            } else {
                ctx->dir_cache = current->next;
            }
            if (current->items) {
                free(current->items);
            }
            free(current);
            break;
        }
        prev = current;
        current = current->next;
    }
    
    // Create new entry
    cache_entry_t *entry = calloc(1, sizeof(cache_entry_t));
    if (!entry) {
        pthread_mutex_unlock(&ctx->cache_lock);
        return;
    }
    
    strncpy(entry->path, path, MAX_PATH_LEN - 1);
    entry->path[MAX_PATH_LEN - 1] = '\0';
    entry->timestamp = time(NULL);
    entry->item_count = count;
    
    if (count > 0 && items) {
        // Take ownership of items (do not copy)
        entry->items = items;
    }
    
    // Insert at the beginning of the list
    entry->next = ctx->dir_cache;
    ctx->dir_cache = entry;
    
    pthread_mutex_unlock(&ctx->cache_lock);
}

void cache_invalidate(cftpfs_context_t *ctx, const char *path) {
    pthread_mutex_lock(&ctx->cache_lock);
    
    cache_entry_t *current = ctx->dir_cache;
    cache_entry_t *prev = NULL;
    
    while (current) {
        // Invalidate if it is the exact path or a subdirectory
        if (strcmp(current->path, path) == 0 || 
            strncmp(current->path, path, strlen(path)) == 0) {
            cache_entry_t *next = current->next;
            
            if (prev) {
                prev->next = next;
            } else {
                ctx->dir_cache = next;
            }
            
            if (current->items) {
                free(current->items);
            }
            free(current);
            
            current = next;
        } else {
            prev = current;
            current = current->next;
        }
    }
    
    pthread_mutex_unlock(&ctx->cache_lock);
}