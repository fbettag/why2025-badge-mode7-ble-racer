#ifndef _ASSET_LOADER_H_
#define _ASSET_LOADER_H_

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

// Asset types
#define ASSET_TYPE_TEXTURE   0
#define ASSET_TYPE_TILEMAP   1
#define ASSET_TYPE_HEIGHTMAP 2
#define ASSET_TYPE_PALETTE   3
#define ASSET_TYPE_SPRITE    4
#define ASSET_TYPE_SOUND     5
#define ASSET_TYPE_TRACK     6

// Asset formats supported
#define ASSET_FORMAT_PNG     0
#define ASSET_FORMAT_BMP     1
#define ASSET_FORMAT_TGA     2
#define ASSET_FORMAT_RAW     3
#define ASSET_FORMAT_RLE     4
#define ASSET_FORMAT_LZ4     5

// Asset compression types
#define ASSET_COMPRESSION_NONE 0
#define ASSET_COMPRESSION_RLE  1
#define ASSET_COMPRESSION_LZ4  2

// Maximum asset sizes
#define MAX_TEXTURE_WIDTH  512
#define MAX_TEXTURE_HEIGHT 512
#define MAX_PALETTE_SIZE   256
#define MAX_SPRITE_FRAMES  64
#define MAX_SOUND_SAMPLES  32768

// Asset header structure
typedef struct __attribute__((packed)) {
    uint32_t magic;        // Asset magic number (0x41535420 = "AST ")
    uint32_t version;      // Asset version
    uint32_t type;         // Asset type
    uint32_t format;       // Asset format
    uint32_t compression;  // Compression type
    uint32_t width;        // Width in pixels/tiles
    uint32_t height;       // Height in pixels/tiles
    uint32_t size;         // Uncompressed size
    uint32_t compressed_size; // Compressed size
    uint32_t checksum;     // CRC32 checksum
    uint32_t flags;        // Asset flags
} asset_header_t;

// Texture data structure
typedef struct {
    uint16_t *pixels;      // RGB565 pixel data
    uint16_t width;
    uint16_t height;
    uint16_t flags;
    uint16_t palette_id;
} texture_t;

// Palette data structure
typedef struct {
    uint16_t colors[MAX_PALETTE_SIZE]; // RGB565 colors
    uint8_t transparent_color;
    uint8_t reserved[3];
} palette_t;

// Sprite animation structure
typedef struct {
    uint16_t frame_width;
    uint16_t frame_height;
    uint16_t frame_count;
    uint16_t frame_delay;
    uint16_t *frames[MAX_SPRITE_FRAMES];
    uint8_t loop_mode;
} sprite_t;

// Asset loading configuration
typedef struct {
    bool enable_compression;
    bool enable_caching;
    uint32_t max_memory_usage;
    uint8_t max_cached_assets;
    bool preload_textures;
} asset_config_t;

// Asset cache entry
typedef struct {
    char filename[64];
    void *data;
    uint32_t size;
    uint32_t last_access;
    uint32_t access_count;
} asset_cache_entry_t;

// Asset loader functions
esp_err_t asset_loader_init(const asset_config_t *config);
void asset_loader_deinit(void);

// Texture loading
texture_t* asset_load_texture(const char *filename);
texture_t* asset_load_texture_from_memory(const uint8_t *data, size_t size);
esp_err_t asset_save_texture(const char *filename, const texture_t *texture);

// Palette management
palette_t* asset_load_palette(const char *filename);
esp_err_t asset_save_palette(const char *filename, const palette_t *palette);

// Sprite loading
sprite_t* asset_load_sprite(const char *filename);
esp_err_t asset_save_sprite(const char *filename, const sprite_t *sprite);

// Utility functions
uint16_t* asset_convert_to_rgb565(const uint8_t *rgba_data, uint32_t width, uint32_t height);
uint8_t* asset_convert_from_rgb565(const uint16_t *rgb565_data, uint32_t width, uint32_t height);

// Asset validation
bool asset_validate_header(const asset_header_t *header);
uint32_t asset_calculate_checksum(const uint8_t *data, size_t size);

// Memory management
void asset_free_texture(texture_t *texture);
void asset_free_palette(palette_t *palette);
void asset_free_sprite(sprite_t *sprite);

// Asset cache management
texture_t* asset_cache_get_texture(const char *filename);
esp_err_t asset_cache_add_texture(const char *filename, texture_t *texture);
void asset_cache_clear(void);

// Performance monitoring
uint32_t asset_get_memory_usage(void);
uint32_t asset_get_cache_hits(void);
uint32_t asset_get_cache_misses(void);

// Asset enumeration
int asset_list_textures(const char *directory, char ***filenames);
int asset_list_palettes(const char *directory, char ***filenames);

#define ASSET_MAX_CACHED_ASSETS 16

#endif // _ASSET_LOADER_H_