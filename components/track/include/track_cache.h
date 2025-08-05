#ifndef _TRACK_CACHE_H_
#define _TRACK_CACHE_H_

#include "track_loader.h"
#include "esp_err.h"

// Cache configuration
#define MAX_CACHE_ENTRIES 4
#define CACHE_PREFETCH_THRESHOLD 2

// Cache entry structure
typedef struct {
    char filename[TRACK_MAX_NAME_LEN];
    track_data_t *track;
    uint64_t last_access;
    uint32_t access_count;
    uint64_t loaded_time;
} track_cache_entry_t;

// Cache statistics structure
typedef struct {
    uint32_t entries;
    uint32_t hits;
    uint32_t misses;
    uint32_t evictions;
    uint32_t memory_usage;
} track_cache_stats_t;

// Cache information structure
typedef struct {
    char filename[TRACK_MAX_NAME_LEN];
    uint32_t access_count;
    uint64_t last_access;
    uint64_t loaded_time;
    uint32_t memory_usage;
} track_cache_info_t;

// Cache management functions
esp_err_t track_cache_init(void);
void track_cache_deinit(void);

// Cache operations
track_data_t* track_cache_get(const char *filename);
esp_err_t track_cache_add(const char *filename, track_data_t *track);
esp_err_t track_cache_remove(const char *filename);
void track_cache_clear(void);

// Cache utilities
esp_err_t track_cache_prefetch(const char *filename);
void track_cache_optimize(void);
void track_cache_get_stats(track_cache_stats_t *stats);
bool track_cache_get_info(const char *filename, track_cache_info_t *info);

#endif // _TRACK_CACHE_H_