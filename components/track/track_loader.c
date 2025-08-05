#include "track_loader.h"
#include "track_format.h"
#include "esp_log.h"
#include "esp_vfs.h"
#include "esp_spiffs.h"
#include "esp_heap_caps.h"
#include "utils.h"
#include "math.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "track_loader";

// Track cache
static track_data_t *track_cache[4] = {0};
static uint8_t cache_count = 0;
static track_loader_config_t loader_config = {
    .enable_cache = true,
    .enable_compression = false,
    .max_memory_usage = 1024 * 1024,  // 1MB max
    .max_tracks_cached = 4
};

// Initialize track loader
esp_err_t track_loader_init(const track_loader_config_t *config)
{
    if (config) {
        memcpy(&loader_config, config, sizeof(loader_config));
    }

    // Initialize SPIFFS for track storage
    esp_vfs_spiffs_conf_t spiffs_conf = {
        .base_path = "/tracks",
        .partition_label = "tracks",
        .max_files = 5,
        .format_if_mount_failed = true
    };

    esp_err_t ret = esp_vfs_spiffs_register(&spiffs_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPIFFS for tracks: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Track loader initialized");
    return ESP_OK;
}

// Deinitialize track loader
void track_loader_deinit(void)
{
    // Free all cached tracks
    for (int i = 0; i < cache_count; i++) {
        track_unload(track_cache[i]);
    }
    cache_count = 0;

    esp_vfs_spiffs_unregister("tracks");
    ESP_LOGI(TAG, "Track loader deinitialized");
}

// Load track from file
track_data_t* track_loader_load(const char *filename)
{
    if (!filename) {
        ESP_LOGE(TAG, "Invalid filename");
        return NULL;
    }

    // Check cache first
    if (loader_config.enable_cache) {
        for (int i = 0; i < cache_count; i++) {
            if (track_cache[i] && strcmp(track_cache[i]->name, filename) == 0) {
                ESP_LOGI(TAG, "Track found in cache: %s", filename);
                return track_cache[i];
            }
        }
    }

    char filepath[64];
    snprintf(filepath, sizeof(filepath), "/tracks/%s", filename);

    FILE *file = fopen(filepath, "rb");
    if (!file) {
        ESP_LOGE(TAG, "Failed to open track file: %s", filename);
        return NULL;
    }

    track_header_t header;
    size_t read = fread(&header, 1, sizeof(header), file);
    if (read != sizeof(header)) {
        ESP_LOGE(TAG, "Failed to read track header");
        fclose(file);
        return NULL;
    }

    // Validate header
    if (header.magic != TRACK_MAGIC_HEADER) {
        ESP_LOGE(TAG, "Invalid track file format");
        fclose(file);
        return NULL;
    }

    if (header.version != TRACK_VERSION) {
        ESP_LOGE(TAG, "Track version mismatch: %d != %d", header.version, TRACK_VERSION);
        fclose(file);
        return NULL;
    }

    // Calculate and verify checksum
    uint32_t calculated_crc = crc32((uint8_t *)&header, sizeof(header) - sizeof(uint32_t));
    if (header.checksum != calculated_crc) {
        ESP_LOGE(TAG, "Track checksum mismatch");
        fclose(file);
        return NULL;
    }

    // Allocate track data structure
    track_data_t *track = (track_data_t *)heap_caps_malloc(sizeof(track_data_t), MALLOC_CAP_SPIRAM);
    if (!track) {
        ESP_LOGE(TAG, "Failed to allocate track data");
        fclose(file);
        return NULL;
    }

    memset(track, 0, sizeof(track_data_t));
    strncpy(track->name, filename, TRACK_MAX_NAME_LEN - 1);
    track->width = header.width;
    track->height = header.height;
    track->tile_size = header.tile_size;
    track->lap_count = header.lap_count;
    track->checkpoint_count = header.checkpoint_count;
    track->track_length = header.track_length;

    // Load tilemap
    if (header.tilemap_size > 0) {
        track->tilemap = (uint8_t *)heap_caps_malloc(header.tilemap_size, MALLOC_CAP_SPIRAM);
        if (!track->tilemap) {
            ESP_LOGE(TAG, "Failed to allocate tilemap");
            track_unload(track);
            fclose(file);
            return NULL;
        }

        fseek(file, header.tilemap_offset, SEEK_SET);
        read = fread(track->tilemap, 1, header.tilemap_size, file);
        if (read != header.tilemap_size) {
            ESP_LOGE(TAG, "Failed to read tilemap");
            track_unload(track);
            fclose(file);
            return NULL;
        }
    }

    // Load heightmap
    if (header.heightmap_size > 0) {
        track->heightmap = (int8_t *)heap_caps_malloc(header.heightmap_size, MALLOC_CAP_SPIRAM);
        if (!track->heightmap) {
            ESP_LOGE(TAG, "Failed to allocate heightmap");
            track_unload(track);
            fclose(file);
            return NULL;
        }

        fseek(file, header.heightmap_offset, SEEK_SET);
        read = fread(track->heightmap, 1, header.heightmap_size, file);
        if (read != header.heightmap_size) {
            ESP_LOGE(TAG, "Failed to read heightmap");
            track_unload(track);
            fclose(file);
            return NULL;
        }
    }

    // Load checkpoints
    if (header.checkpoint_size > 0) {
        fseek(file, header.checkpoint_offset, SEEK_SET);
        read = fread(track->checkpoints, 1, header.checkpoint_size, file);
        if (read != header.checkpoint_size) {
            ESP_LOGE(TAG, "Failed to read checkpoints");
            track_unload(track);
            fclose(file);
            return NULL;
        }
    }

    // Load collision data
    if (header.collision_size > 0) {
        track->collision_count = header.collision_size / sizeof(track_collision_t);
        track->collision_data = (track_collision_t *)heap_caps_malloc(header.collision_size, MALLOC_CAP_SPIRAM);
        if (!track->collision_data) {
            ESP_LOGE(TAG, "Failed to allocate collision data");
            track_unload(track);
            fclose(file);
            return NULL;
        }

        fseek(file, header.collision_offset, SEEK_SET);
        read = fread(track->collision_data, 1, header.collision_size, file);
        if (read != header.collision_size) {
            ESP_LOGE(TAG, "Failed to read collision data");
            track_unload(track);
            fclose(file);
            return NULL;
        }
    }

    // Load thumbnail if available
    if (header.thumbnail_size > 0) {
        track->thumbnail = (uint8_t *)heap_caps_malloc(header.thumbnail_size, MALLOC_CAP_SPIRAM);
        if (track->thumbnail) {
            fseek(file, header.thumbnail_offset, SEEK_SET);
            read = fread(track->thumbnail, 1, header.thumbnail_size, file);
            if (read != header.thumbnail_size) {
                ESP_LOGW(TAG, "Failed to read thumbnail");
                free(track->thumbnail);
                track->thumbnail = NULL;
            }
        }
    }

    fclose(file);
    track->loaded = true;
    track->memory_usage = sizeof(track_data_t) + 
                         header.tilemap_size + 
                         header.heightmap_size + 
                         header.collision_size + 
                         header.thumbnail_size;

    ESP_LOGI(TAG, "Track loaded: %s (%dx%d, %d checkpoints, %dKB)", 
             filename, track->width, track->height, track->checkpoint_count, 
             track->memory_usage / 1024);

    // Add to cache
    if (loader_config.enable_cache && cache_count < loader_config.max_tracks_cached) {
        track_cache[cache_count++] = track;
    }

    return track;
}

// Unload track and free memory
void track_unload(track_data_t *track)
{
    if (!track) return;

    // Remove from cache
    if (loader_config.enable_cache) {
        for (int i = 0; i < cache_count; i++) {
            if (track_cache[i] == track) {
                for (int j = i; j < cache_count - 1; j++) {
                    track_cache[j] = track_cache[j + 1];
                }
                cache_count--;
                break;
            }
        }
    }

    if (track->tilemap) {
        heap_caps_free(track->tilemap);
    }
    if (track->heightmap) {
        heap_caps_free(track->heightmap);
    }
    if (track->collision_data) {
        heap_caps_free(track->collision_data);
    }
    if (track->thumbnail) {
        heap_caps_free(track->thumbnail);
    }
    if (track->texture_indices) {
        heap_caps_free(track->texture_indices);
    }

    heap_caps_free(track);
    ESP_LOGI(TAG, "Track unloaded");
}

// Get track information without loading full data
track_info_t track_loader_get_info(const char *filename)
{
    track_info_t info = {0};
    
    if (!filename) return info;

    char filepath[64];
    snprintf(filepath, sizeof(filepath), "/tracks/%s", filename);

    FILE *file = fopen(filepath, "rb");
    if (!file) {
        ESP_LOGE(TAG, "Failed to open track file for info: %s", filename);
        return info;
    }

    track_header_t header;
    size_t read = fread(&header, 1, sizeof(header), file);
    fclose(file);

    if (read != sizeof(header) || header.magic != TRACK_MAGIC_HEADER) {
        return info;
    }

    strncpy(info.name, filename, sizeof(info.name) - 1);
    info.width = header.width;
    info.height = header.height;
    info.lap_count = header.lap_count;
    info.checkpoint_count = header.checkpoint_count;
    info.track_length = header.track_length;
    info.file_size = sizeof(header) + header.tilemap_size + header.heightmap_size + 
                    header.checkpoint_size + header.collision_size + header.thumbnail_size;
    info.valid = true;

    return info;
}

// List available tracks
int track_loader_list_tracks(track_info_t *tracks, int max_tracks)
{
    if (!tracks || max_tracks <= 0) return 0;

    DIR *dir = opendir("/tracks");
    if (!dir) {
        ESP_LOGE(TAG, "Failed to open tracks directory");
        return 0;
    }

    struct dirent *entry;
    int count = 0;

    while ((entry = readdir(dir)) != NULL && count < max_tracks) {
        if (entry->d_type == DT_REG && strstr(entry->d_name, ".trk")) {
            tracks[count] = track_loader_get_info(entry->d_name);
            if (tracks[count].valid) {
                count++;
            }
        }
    }

    closedir(dir);
    ESP_LOGI(TAG, "Found %d tracks", count);
    return count;
}

// Validate track data
bool track_validate(const track_data_t *track)
{
    if (!track || !track->loaded) return false;
    if (track->width == 0 || track->height == 0) return false;
    if (track->tilemap == NULL) return false;
    if (track->checkpoint_count == 0 || track->checkpoint_count > TRACK_MAX_CHECKPOINTS) return false;
    
    // Validate checkpoints
    for (int i = 0; i < track->checkpoint_count; i++) {
        if (track->checkpoints[i].x < 0 || track->checkpoints[i].x >= track->width * track->tile_size) {
            return false;
        }
        if (track->checkpoints[i].y < 0 || track->checkpoints[i].y >= track->height * track->tile_size) {
            return false;
        }
        if (track->checkpoints[i].radius == 0) return false;
    }
    
    return true;
}

// Get tile at specific coordinates
uint8_t track_get_tile(const track_data_t *track, int x, int y)
{
    if (!track || !track->tilemap) return TILE_OFFROAD;
    
    int tile_x = x / track->tile_size;
    int tile_y = y / track->tile_size;
    
    if (tile_x < 0 || tile_x >= track->width || tile_y < 0 || tile_y >= track->height) {
        return TILE_OFFROAD;
    }
    
    return track->tilemap[tile_y * track->width + tile_x];
}

// Get height at specific coordinates
int8_t track_get_height(const track_data_t *track, int x, int y)
{
    if (!track || !track->heightmap) return 0;
    
    int height_x = (x * TRACK_HEIGHTMAP_SIZE) / (track->width * track->tile_size);
    int height_y = (y * TRACK_HEIGHTMAP_SIZE) / (track->height * track->tile_size);
    
    height_x = MAX(0, MIN(TRACK_HEIGHTMAP_SIZE - 1, height_x));
    height_y = MAX(0, MIN(TRACK_HEIGHTMAP_SIZE - 1, height_y));
    
    return track->heightmap[height_y * TRACK_HEIGHTMAP_SIZE + height_x];
}

// Check collision at specific coordinates
bool track_check_collision(const track_data_t *track, int x, int y)
{
    if (!track || !track->collision_data) return false;
    
    for (int i = 0; i < track->collision_count; i++) {
        const track_collision_t *collision = &track->collision_data[i];
        if (x >= collision->x && x < collision->x + collision->width &&
            y >= collision->y && y < collision->y + collision->height) {
            return true;
        }
    }
    
    // Check tile-based collisions
    uint8_t tile = track_get_tile(track, x, y);
    return tile == TILE_WALL_CONCRETE || tile == TILE_WALL_BARRIER || 
           tile == TILE_WALL_FENCE || tile == TILE_WALL_TREES ||
           tile == TILE_WATER || tile == TILE_OFFROAD;
}