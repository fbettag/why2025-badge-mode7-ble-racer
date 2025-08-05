#include "track_cache.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "track_cache";

// Cache management
static track_cache_entry_t track_cache[MAX_CACHE_ENTRIES] = {0};
static SemaphoreHandle_t cache_mutex = NULL;
static uint32_t cache_hits = 0;
static uint32_t cache_misses = 0;
static uint32_t cache_evictions = 0;

// Initialize track cache
esp_err_t track_cache_init(void)
{
    if (cache_mutex == NULL) {
        cache_mutex = xSemaphoreCreateMutex();
        if (!cache_mutex) {
            ESP_LOGE(TAG, "Failed to create cache mutex");
            return ESP_FAIL;
        }
    }

    memset(track_cache, 0, sizeof(track_cache));
    cache_hits = cache_misses = cache_evictions = 0;

    ESP_LOGI(TAG, "Track cache initialized");
    return ESP_OK;
}

// Deinitialize track cache
void track_cache_deinit(void)
{
    if (cache_mutex) {
        track_cache_clear();
        vSemaphoreDelete(cache_mutex);
        cache_mutex = NULL;
    }

    ESP_LOGI(TAG, "Track cache deinitialized");
}

// Get track from cache
track_data_t* track_cache_get(const char *filename)
{
    if (!filename || !cache_mutex) return NULL;

    xSemaphoreTake(cache_mutex, portMAX_DELAY);

    for (int i = 0; i < MAX_CACHE_ENTRIES; i++) {
        if (track_cache[i].track && 
            strcmp(track_cache[i].filename, filename) == 0) {
            
            // Update access time
            track_cache[i].last_access = esp_timer_get_time() / 1000;
            track_cache[i].access_count++;
            
            track_data_t *track = track_cache[i].track;
            
            xSemaphoreGive(cache_mutex);
            cache_hits++;
            ESP_LOGD(TAG, "Cache hit: %s", filename);
            return track;
        }
    }

    xSemaphoreGive(cache_mutex);
    cache_misses++;
    ESP_LOGD(TAG, "Cache miss: %s", filename);
    return NULL;
}

// Add track to cache
esp_err_t track_cache_add(const char *filename, track_data_t *track)
{
    if (!filename || !track || !cache_mutex) return ESP_ERR_INVALID_ARG;

    xSemaphoreTake(cache_mutex, portMAX_DELAY);

    // Check if already in cache
    for (int i = 0; i < MAX_CACHE_ENTRIES; i++) {
        if (track_cache[i].track && 
            strcmp(track_cache[i].filename, filename) == 0) {
            xSemaphoreGive(cache_mutex);
            return ESP_OK;  // Already cached
        }
    }

    // Find empty slot or evict oldest
    int slot = -1;
    uint64_t oldest_time = UINT64_MAX;

    for (int i = 0; i < MAX_CACHE_ENTRIES; i++) {
        if (!track_cache[i].track) {
            slot = i;
            break;
        }
        
        if (track_cache[i].last_access < oldest_time) {
            oldest_time = track_cache[i].last_access;
            slot = i;
        }
    }

    if (slot >= 0) {
        // Evict existing track if necessary
        if (track_cache[slot].track) {
            ESP_LOGI(TAG, "Evicting track from cache: %s", track_cache[slot].filename);
            track_unload(track_cache[slot].track);
            cache_evictions++;
        }

        // Add new track
        strncpy(track_cache[slot].filename, filename, sizeof(track_cache[slot].filename) - 1);
        track_cache[slot].track = track;
        track_cache[slot].last_access = esp_timer_get_time() / 1000;
        track_cache[slot].access_count = 1;
        track_cache[slot].loaded_time = track_cache[slot].last_access;

        ESP_LOGI(TAG, "Added track to cache: %s", filename);
    }

    xSemaphoreGive(cache_mutex);
    return ESP_OK;
}

// Remove track from cache
esp_err_t track_cache_remove(const char *filename)
{
    if (!filename || !cache_mutex) return ESP_ERR_INVALID_ARG;

    xSemaphoreTake(cache_mutex, portMAX_DELAY);

    for (int i = 0; i < MAX_CACHE_ENTRIES; i++) {
        if (track_cache[i].track && 
            strcmp(track_cache[i].filename, filename) == 0) {
            
            ESP_LOGI(TAG, "Removing track from cache: %s", filename);
            track_unload(track_cache[i].track);
            memset(&track_cache[i], 0, sizeof(track_cache_entry_t));
            
            xSemaphoreGive(cache_mutex);
            return ESP_OK;
        }
    }

    xSemaphoreGive(cache_mutex);
    return ESP_ERR_NOT_FOUND;
}

// Clear entire cache
void track_cache_clear(void)
{
    if (!cache_mutex) return;

    xSemaphoreTake(cache_mutex, portMAX_DELAY);

    for (int i = 0; i < MAX_CACHE_ENTRIES; i++) {
        if (track_cache[i].track) {
            ESP_LOGI(TAG, "Unloading cached track: %s", track_cache[i].filename);
            track_unload(track_cache[i].track);
            memset(&track_cache[i], 0, sizeof(track_cache_entry_t));
        }
    }

    xSemaphoreGive(cache_mutex);
    ESP_LOGI(TAG, "Track cache cleared");
}

// Get cache statistics
void track_cache_get_stats(track_cache_stats_t *stats)
{
    if (!stats || !cache_mutex) return;

    xSemaphoreTake(cache_mutex, portMAX_DELAY);

    stats->entries = 0;
    stats->hits = cache_hits;
    stats->misses = cache_misses;
    stats->evictions = cache_evictions;
    stats->memory_usage = 0;

    for (int i = 0; i < MAX_CACHE_ENTRIES; i++) {
        if (track_cache[i].track) {
            stats->entries++;
            stats->memory_usage += track_cache[i].track->memory_usage;
        }
    }

    xSemaphoreGive(cache_mutex);
}

// Get cache entry info
bool track_cache_get_info(const char *filename, track_cache_info_t *info)
{
    if (!filename || !info || !cache_mutex) return false;

    xSemaphoreTake(cache_mutex, portMAX_DELAY);

    for (int i = 0; i < MAX_CACHE_ENTRIES; i++) {
        if (track_cache[i].track && 
            strcmp(track_cache[i].filename, filename) == 0) {
            
            strncpy(info->filename, track_cache[i].filename, sizeof(info->filename) - 1);
            info->access_count = track_cache[i].access_count;
            info->last_access = track_cache[i].last_access;
            info->loaded_time = track_cache[i].loaded_time;
            info->memory_usage = track_cache[i].track->memory_usage;
            
            xSemaphoreGive(cache_mutex);
            return true;
        }
    }

    xSemaphoreGive(cache_mutex);
    return false;
}

// Prefetch track to cache
esp_err_t track_cache_prefetch(const char *filename)
{
    if (!filename) return ESP_ERR_INVALID_ARG;

    // Check if already cached
    if (track_cache_get(filename) != NULL) {
        return ESP_OK;
    }

    // Load track and add to cache
    track_data_t *track = track_loader_load(filename);
    if (!track) {
        return ESP_FAIL;
    }

    return track_cache_add(filename, track);
}

// Optimize cache based on access patterns
void track_cache_optimize(void)
{
    if (!cache_mutex) return;

    xSemaphoreTake(cache_mutex, portMAX_DELAY);

    // Simple optimization: sort by access count and last access time
    for (int i = 0; i < MAX_CACHE_ENTRIES - 1; i++) {
        for (int j = i + 1; j < MAX_CACHE_ENTRIES; j++) {
            if (track_cache[i].track && track_cache[j].track) {
                // Compare by access count, then by last access time
                bool should_swap = false;
                if (track_cache[i].access_count != track_cache[j].access_count) {
                    should_swap = track_cache[i].access_count < track_cache[j].access_count;
                } else {
                    should_swap = track_cache[i].last_access > track_cache[j].last_access;
                }

                if (should_swap) {
                    track_cache_entry_t temp = track_cache[i];
                    track_cache[i] = track_cache[j];
                    track_cache[j] = temp;
                }
            }
        }
    }

    xSemaphoreGive(cache_mutex);
}