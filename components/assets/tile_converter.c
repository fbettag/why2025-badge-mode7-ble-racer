#include "asset_loader.h"
#include "track_format.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_err.h"
#include "esp_crc.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "tile_converter";

// Tile dimension constants
#define TILE_WIDTH 16
#define TILE_HEIGHT 16
#define TILES_PER_ROW 32
#define MAX_TILE_COUNT 1024

// Track tile types
#define TILE_TYPE_GRASS     0
#define TILE_TYPE_ROAD      1
#define TILE_TYPE_WATER     2
#define TILE_TYPE_SAND      3
#define TILE_TYPE_WALL      4
#define TILE_TYPE_START     5
#define TILE_TYPE_CHECKPOINT 6
#define TILE_TYPE_FINISH    7

// Convert ASCII track to binary format
esp_err_t tile_convert_ascii_to_binary(const char *ascii_filename, const char *binary_filename)
{
    if (!ascii_filename || !binary_filename) {
        return ESP_ERR_INVALID_ARG;
    }

    FILE *ascii_file = fopen(ascii_filename, "r");
    if (!ascii_file) {
        ESP_LOGE(TAG, "Failed to open ASCII file: %s", ascii_filename);
        return ESP_FAIL;
    }

    // Read ASCII track data
    char line[256];
    uint8_t *track_data = NULL;
    uint32_t width = 0, height = 0;
    uint32_t max_width = 0;
    
    // First pass: determine dimensions
    while (fgets(line, sizeof(line), ascii_file)) {
        uint32_t line_len = strlen(line);
        if (line_len > 0 && line[line_len - 1] == '\n') {
            line_len--;
        }
        max_width = (line_len > max_width) ? line_len : max_width;
        height++;
    }
    
    width = max_width;
    rewind(ascii_file);
    
    // Allocate track data
    track_data = heap_caps_malloc(width * height, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!track_data) {
        ESP_LOGE(TAG, "Failed to allocate track data");
        fclose(ascii_file);
        return ESP_FAIL;
    }
    
    memset(track_data, 0, width * height);
    
    // Second pass: read tile data
    uint32_t y = 0;
    while (fgets(line, sizeof(line), ascii_file) && y < height) {
        uint32_t line_len = strlen(line);
        if (line_len > 0 && line[line_len - 1] == '\n') {
            line_len--;
        }
        
        for (uint32_t x = 0; x < line_len && x < width; x++) {
            char c = line[x];
            uint8_t tile_type = TILE_TYPE_GRASS; // Default
            
            switch (c) {
                case 'G': case 'g': tile_type = TILE_TYPE_GRASS; break;
                case 'R': case 'r': tile_type = TILE_TYPE_ROAD; break;
                case 'W': case 'w': tile_type = TILE_TYPE_WATER; break;
                case 'S': case 's': tile_type = TILE_TYPE_SAND; break;
                case '#': tile_type = TILE_TYPE_WALL; break;
                case 'X': case 'x': tile_type = TILE_TYPE_START; break;
                case 'C': case 'c': tile_type = TILE_TYPE_CHECKPOINT; break;
                case 'F': case 'f': tile_type = TILE_TYPE_FINISH; break;
                default: tile_type = TILE_TYPE_GRASS; break;
            }
            
            track_data[y * width + x] = tile_type;
        }
        y++;
    }
    
    fclose(ascii_file);
    
    // Create track header
    track_header_t header = {
        .magic = TRACK_MAGIC,
        .version = TRACK_VERSION,
        .width = width,
        .height = height,
        .tile_width = TILE_WIDTH,
        .tile_height = TILE_HEIGHT,
        .checkpoint_count = 0,
        .start_x = 0,
        .start_y = 0,
        .start_angle = 0,
        .flags = 0
    };
    
    // Find start position and checkpoints
    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            uint8_t tile = track_data[y * width + x];
            if (tile == TILE_TYPE_START) {
                header.start_x = x * TILE_WIDTH + TILE_WIDTH / 2;
                header.start_y = y * TILE_HEIGHT + TILE_HEIGHT / 2;
                header.start_angle = 0; // Default facing right
            }
            if (tile == TILE_TYPE_CHECKPOINT) {
                header.checkpoint_count++;
            }
        }
    }
    
    // Create binary file
    FILE *binary_file = fopen(binary_filename, "wb");
    if (!binary_file) {
        ESP_LOGE(TAG, "Failed to create binary file: %s", binary_filename);
        heap_caps_free(track_data);
        return ESP_FAIL;
    }
    
    // Write header
    fwrite(&header, 1, sizeof(header), binary_file);
    
    // Write track data
    fwrite(track_data, 1, width * height, binary_file);
    
    // Write checkpoint positions
    if (header.checkpoint_count > 0) {
        checkpoint_data_t *checkpoints = heap_caps_malloc(
            header.checkpoint_count * sizeof(checkpoint_data_t), 
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        
        if (checkpoints) {
            uint32_t checkpoint_index = 0;
            for (uint32_t y = 0; y < height && checkpoint_index < header.checkpoint_count; y++) {
                for (uint32_t x = 0; x < width && checkpoint_index < header.checkpoint_count; x++) {
                    if (track_data[y * width + x] == TILE_TYPE_CHECKPOINT) {
                        checkpoints[checkpoint_index].x = x * TILE_WIDTH + TILE_WIDTH / 2;
                        checkpoints[checkpoint_index].y = y * TILE_HEIGHT + TILE_HEIGHT / 2;
                        checkpoints[checkpoint_index].radius = TILE_WIDTH / 2;
                        checkpoints[checkpoint_index].index = checkpoint_index;
                        checkpoint_index++;
                    }
                }
            }
            
            fwrite(checkpoints, 1, header.checkpoint_count * sizeof(checkpoint_data_t), binary_file);
            heap_caps_free(checkpoints);
        }
    }
    
    fclose(binary_file);
    heap_caps_free(track_data);
    
    ESP_LOGI(TAG, "Converted ASCII to binary: %s -> %s (%ux%u)", 
             ascii_filename, binary_filename, width, height);
    
    return ESP_OK;
}

// Generate tilesheet from tile definitions
texture_t* tile_generate_tilesheet(uint32_t tile_count)
{
    if (tile_count == 0 || tile_count > MAX_TILE_COUNT) {
        tile_count = 8; // Default tile types
    }
    
    uint32_t tiles_per_row = TILES_PER_ROW;
    uint32_t rows = (tile_count + tiles_per_row - 1) / tiles_per_row;
    uint32_t sheet_width = tiles_per_row * TILE_WIDTH;
    uint32_t sheet_height = rows * TILE_HEIGHT;
    
    texture_t *tilesheet = heap_caps_malloc(sizeof(texture_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!tilesheet) {
        ESP_LOGE(TAG, "Failed to allocate tilesheet structure");
        return NULL;
    }
    
    tilesheet->width = sheet_width;
    tilesheet->height = sheet_height;
    tilesheet->flags = 0;
    tilesheet->palette_id = 0xFFFF;
    
    size_t pixel_size = sheet_width * sheet_height * sizeof(uint16_t);
    tilesheet->pixels = heap_caps_malloc(pixel_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!tilesheet->pixels) {
        ESP_LOGE(TAG, "Failed to allocate tilesheet pixel data");
        heap_caps_free(tilesheet);
        return NULL;
    }
    
    // Generate tile patterns
    for (uint32_t tile_idx = 0; tile_idx < tile_count; tile_idx++) {
        uint32_t tile_row = tile_idx / tiles_per_row;
        uint32_t tile_col = tile_idx % tiles_per_row;
        uint32_t tile_x = tile_col * TILE_WIDTH;
        uint32_t tile_y = tile_row * TILE_HEIGHT;
        
        uint16_t base_color = 0x0000;
        uint16_t accent_color = 0xFFFF;
        
        switch (tile_idx) {
            case TILE_TYPE_GRASS:
                base_color = 0x07E0; // Green
                accent_color = 0x0200;
                break;
            case TILE_TYPE_ROAD:
                base_color = 0x8410; // Gray
                accent_color = 0x4208;
                break;
            case TILE_TYPE_WATER:
                base_color = 0x001F; // Blue
                accent_color = 0x0008;
                break;
            case TILE_TYPE_SAND:
                base_color = 0xFFE0; // Yellow
                accent_color = 0xBA20;
                break;
            case TILE_TYPE_WALL:
                base_color = 0xF800; // Red
                accent_color = 0x9800;
                break;
            case TILE_TYPE_START:
                base_color = 0x07E0; // Green with checkerboard
                accent_color = 0xFFFF; // White
                break;
            case TILE_TYPE_CHECKPOINT:
                base_color = 0xFFE0; // Yellow with checkerboard
                accent_color = 0x001F; // Blue
                break;
            case TILE_TYPE_FINISH:
                base_color = 0x0000; // Black with checkerboard
                accent_color = 0xFFFF; // White
                break;
            default:
                base_color = 0x8410; // Gray
                accent_color = 0x4208;
                break;
        }
        
        // Fill tile with pattern
        for (uint32_t py = 0; py < TILE_HEIGHT; py++) {
            for (uint32_t px = 0; px < TILE_WIDTH; px++) {
                uint32_t abs_x = tile_x + px;
                uint32_t abs_y = tile_y + py;
                
                uint16_t color = base_color;
                
                // Add patterns based on tile type
                switch (tile_idx) {
                    case TILE_TYPE_GRASS:
                        // Random grass texture
                        if ((px + py) % 3 == 0) color = accent_color;
                        break;
                    case TILE_TYPE_ROAD:
                        // Road with center line
                        if (py == TILE_HEIGHT / 2) color = 0xFFFF;
                        break;
                    case TILE_TYPE_WATER:
                        // Water waves
                        if ((px + py) % 4 == 0) color = accent_color;
                        break;
                    case TILE_TYPE_SAND:
                        // Sand texture
                        if ((px * py) % 5 == 0) color = accent_color;
                        break;
                    case TILE_TYPE_WALL:
                        // Brick pattern
                        if ((px % 4 == 0) || (py % 4 == 0)) color = accent_color;
                        break;
                    case TILE_TYPE_START:
                    case TILE_TYPE_CHECKPOINT:
                    case TILE_TYPE_FINISH:
                        // Checkerboard pattern for special tiles
                        if ((px / 2 + py / 2) % 2 == 0) color = accent_color;
                        break;
                }
                
                tilesheet->pixels[abs_y * sheet_width + abs_x] = color;
            }
        }
    }
    
    ESP_LOGI(TAG, "Generated tilesheet: %ux%u (%u tiles)", 
             sheet_width, sheet_height, tile_count);
    
    return tilesheet;
}

// Save tilesheet to file
esp_err_t tile_save_tilesheet(const char *filename, texture_t *tilesheet)
{
    if (!filename || !tilesheet) {
        return ESP_ERR_INVALID_ARG;
    }
    
    return asset_save_texture(filename, tilesheet);
}

// Create default track files
esp_err_t tile_create_default_tracks(void)
{
    // Create simple default track
    const char *default_track_ascii = 
        "##################################################\n"
        "#GGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGG#\n"
        "#G                                              G#\n"
        "#G  RRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRR  G#\n"
        "#G  R                                        R  G#\n"
        "#G  R  SSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSS  R  G#\n"
        "#G  R  S                                    S  R  G#\n"
        "#G  R  S  RRRRRRRRRRRRRRRRRRRRRRRRRRRRRRR  S  R  G#\n"
        "#G  R  S  R                              R  S  R  G#\n"
        "#G  R  S  R  CCCCCCCCCCCCCCCCCCCCCCCCC  R  S  R  G#\n"
        "#G  R  S  R  C                          C  R  S  R  G#\n"
        "#G  R  S  R  C  RRRRRRRRRRRRRRRRRRRRR  C  R  S  R  G#\n"
        "#G  R  S  R  C  R                    R  C  R  S  R  G#\n"
        "#G  R  S  R  C  R  FFFFFFFFFFFFFFF  R  C  R  S  R  G#\n"
        "#G  R  S  R  C  R  F              F  R  C  R  S  R  G#\n"
        "#G  R  S  R  C  R  F  XXXXXXXXX  F  R  C  R  S  R  G#\n"
        "#G  R  S  R  C  R  F  X        X  F  R  C  R  S  R  G#\n"
        "#G  R  S  R  C  R  F  X        X  F  R  C  R  S  R  G#\n"
        "#G  R  S  R  C  R  F  XXXXXXXXX  F  R  C  R  S  R  G#\n"
        "#G  R  S  R  C  R  F              F  R  C  R  S  R  G#\n"
        "#G  R  S  R  C  R  FFFFFFFFFFFFFFF  R  C  R  S  R  G#\n"
        "#G  R  S  R  C  R                    R  C  R  S  R  G#\n"
        "#G  R  S  R  C  RRRRRRRRRRRRRRRRRRRRR  C  R  S  R  G#\n"
        "#G  R  S  R  C                          C  R  S  R  G#\n"
        "#G  R  S  R  CCCCCCCCCCCCCCCCCCCCCCCCC  R  S  R  G#\n"
        "#G  R  S  R                              R  S  R  G#\n"
        "#G  R  S  RRRRRRRRRRRRRRRRRRRRRRRRRRRRRRR  S  R  G#\n"
        "#G  R  S                                    S  R  G#\n"
        "#G  R  SSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSS  R  G#\n"
        "#G  R                                        R  G#\n"
        "#G  RRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRR  G#\n"
        "#G                                              G#\n"
        "#GGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGG#\n"
        "##################################################\n";
    
    // Write to temporary file and convert
    FILE *temp_file = fopen("/tmp/default_track.txt", "w");
    if (temp_file) {
        fputs(default_track_ascii, temp_file);
        fclose(temp_file);
        
        esp_err_t ret = tile_convert_ascii_to_binary("/tmp/default_track.txt", "/spiffs/tracks/default.trk");
        remove("/tmp/default_track.txt");
        
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Created default track file");
        }
        return ret;
    }
    
    return ESP_FAIL;
}

// Generate heightmap from track
texture_t* tile_generate_heightmap(track_data_t *track)
{
    if (!track) return NULL;
    
    texture_t *heightmap = heap_caps_malloc(sizeof(texture_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!heightmap) {
        ESP_LOGE(TAG, "Failed to allocate heightmap structure");
        return NULL;
    }
    
    heightmap->width = track->width;
    heightmap->height = track->height;
    heightmap->flags = 0;
    heightmap->palette_id = 0xFFFF;
    
    size_t pixel_size = track->width * track->height * sizeof(uint16_t);
    heightmap->pixels = heap_caps_malloc(pixel_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!heightmap->pixels) {
        ESP_LOGE(TAG, "Failed to allocate heightmap pixel data");
        heap_caps_free(heightmap);
        return NULL;
    }
    
    // Generate height values based on tile type
    for (uint32_t y = 0; y < track->height; y++) {
        for (uint32_t x = 0; x < track->width; x++) {
            uint8_t tile_type = track->tile_data[y * track->width + x];
            uint16_t height_value = 0;
            
            switch (tile_type) {
                case TILE_TYPE_GRASS: height_value = 0x0100; break;
                case TILE_TYPE_ROAD: height_value = 0x0000; break;
                case TILE_TYPE_WATER: height_value = 0xFF00; break;
                case TILE_TYPE_SAND: height_value = 0x0050; break;
                case TILE_TYPE_WALL: height_value = 0x0200; break;
                case TILE_TYPE_START: height_value = 0x0000; break;
                case TILE_TYPE_CHECKPOINT: height_value = 0x0000; break;
                case TILE_TYPE_FINISH: height_value = 0x0000; break;
                default: height_value = 0x0000; break;
            }
            
            heightmap->pixels[y * track->width + x] = height_value;
        }
    }
    
    return heightmap;
}

// Initialize asset system with defaults
esp_err_t tile_system_init(void)
{
    // Create default tilesheet
    texture_t *tilesheet = tile_generate_tilesheet(8);
    if (tilesheet) {
        tile_save_tilesheet("/spiffs/assets/tilesheet.ast", tilesheet);
        asset_free_texture(tilesheet);
    }
    
    // Create default tracks
    tile_create_default_tracks();
    
    return ESP_OK;
}