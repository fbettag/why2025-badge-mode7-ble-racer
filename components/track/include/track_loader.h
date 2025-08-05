#ifndef _TRACK_LOADER_H_
#define _TRACK_LOADER_H_

#include <stdint.h>
#include <stdbool.h>
#include <dirent.h>
#include "esp_err.h"
#include "track_format.h"

// Track information structure (lightweight)
typedef struct {
    char name[TRACK_MAX_NAME_LEN];
    uint16_t width;
    uint16_t height;
    uint16_t lap_count;
    uint16_t checkpoint_count;
    uint32_t track_length;
    uint32_t file_size;
    bool valid;
} track_info_t;

// Initialize track loader system
esp_err_t track_loader_init(const track_loader_config_t *config);

// Deinitialize track loader system
void track_loader_deinit(void);

// Load a track from file system
track_data_t* track_loader_load(const char *filename);

// Unload a track and free memory
void track_unload(track_data_t *track);

// Get track information without loading full data
track_info_t track_loader_get_info(const char *filename);

// List available tracks in the tracks directory
int track_loader_list_tracks(track_info_t *tracks, int max_tracks);

// Validate track data integrity
bool track_validate(const track_data_t *track);

// Access track data
uint8_t track_get_tile(const track_data_t *track, int x, int y);
int8_t track_get_height(const track_data_t *track, int x, int y);
bool track_check_collision(const track_data_t *track, int x, int y);

// Track creation utilities
esp_err_t track_create_default(const char *filename);

// Memory management
uint32_t track_get_memory_usage(const track_data_t *track);
void track_clear_cache(void);

#endif // _TRACK_LOADER_H_