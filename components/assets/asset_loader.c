#include "asset_loader.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_err.h"
#include "esp_vfs.h"
#include "esp_spiffs.h"
#include "esp_crc.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>
#include <math.h>

static const char *TAG = "asset_loader";

// Asset loader configuration
static asset_config_t asset_config = {0};
static SemaphoreHandle_t cache_mutex = NULL;

// Asset cache
static asset_cache_entry_t asset_cache[ASSET_MAX_CACHED_ASSETS] = {0};
static uint32_t cache_hits = 0;
static uint32_t cache_misses = 0;
static uint32_t memory_usage = 0;

// PNG header signature
static const uint8_t PNG_SIGNATURE[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};

// BMP header signature
static const uint8_t BMP_SIGNATURE[2] = {0x42, 0x4D};

// Initialize asset loader
esp_err_t asset_loader_init(const asset_config_t *config)
{
    if (!config) {
        ESP_LOGE(TAG, "Invalid configuration");
        return ESP_ERR_INVALID_ARG;
    }

    // Create mutex for cache access
    if (cache_mutex == NULL) {
        cache_mutex = xSemaphoreCreateMutex();
        if (!cache_mutex) {
            ESP_LOGE(TAG, "Failed to create cache mutex");
            return ESP_FAIL;
        }
    }

    // Copy configuration
    memcpy(&asset_config, config, sizeof(asset_config_t));

    // Initialize cache
    memset(asset_cache, 0, sizeof(asset_cache));
    cache_hits = cache_misses = memory_usage = 0;

    ESP_LOGI(TAG, "Asset loader initialized with max memory: %lu bytes", asset_config.max_memory_usage);
    return ESP_OK;
}

// Deinitialize asset loader
void asset_loader_deinit(void)
{
    if (cache_mutex) {
        xSemaphoreTake(cache_mutex, portMAX_DELAY);
        
        // Clear all cached assets
        for (int i = 0; i < asset_config.max_cached_assets; i++) {
            if (asset_cache[i].data) {
                heap_caps_free(asset_cache[i].data);
            }
        }
        
        memset(asset_cache, 0, sizeof(asset_cache));
        cache_hits = cache_misses = memory_usage = 0;
        
        xSemaphoreGive(cache_mutex);
        vSemaphoreDelete(cache_mutex);
        cache_mutex = NULL;
    }

    ESP_LOGI(TAG, "Asset loader deinitialized");
}

// Validate asset header
bool asset_validate_header(const asset_header_t *header)
{
    if (!header) return false;
    
    // Check magic number
    if (header->magic != 0x41535420) { // "AST "
        ESP_LOGE(TAG, "Invalid asset magic: 0x%08X", header->magic);
        return false;
    }
    
    // Check version
    if (header->version != 1) {
        ESP_LOGE(TAG, "Unsupported asset version: %u", header->version);
        return false;
    }
    
    // Check type
    if (header->type > ASSET_TYPE_TRACK) {
        ESP_LOGE(TAG, "Invalid asset type: %u", header->type);
        return false;
    }
    
    // Validate dimensions
    if (header->width == 0 || header->height == 0 ||
        header->width > MAX_TEXTURE_WIDTH || header->height > MAX_TEXTURE_HEIGHT) {
        ESP_LOGE(TAG, "Invalid dimensions: %ux%u", header->width, header->height);
        return false;
    }
    
    return true;
}

// Calculate CRC32 checksum
uint32_t asset_calculate_checksum(const uint8_t *data, size_t size)
{
    return esp_crc32_le(0xFFFFFFFF, data, size);
}

// Load file from SPIFFS
static uint8_t* asset_load_file(const char *filename, size_t *out_size)
{
    FILE *file = fopen(filename, "rb");
    if (!file) {
        ESP_LOGE(TAG, "Failed to open file: %s", filename);
        return NULL;
    }

    fseek(file, 0, SEEK_END);
    size_t file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (file_size == 0) {
        ESP_LOGE(TAG, "Empty file: %s", filename);
        fclose(file);
        return NULL;
    }

    uint8_t *buffer = heap_caps_malloc(file_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buffer) {
        ESP_LOGE(TAG, "Failed to allocate memory for file: %s", filename);
        fclose(file);
        return NULL;
    }

    size_t bytes_read = fread(buffer, 1, file_size, file);
    fclose(file);

    if (bytes_read != file_size) {
        ESP_LOGE(TAG, "Failed to read complete file: %s", filename);
        heap_caps_free(buffer);
        return NULL;
    }

    *out_size = file_size;
    return buffer;
}

// Detect file format from data
static uint32_t asset_detect_format(const uint8_t *data, size_t size)
{
    if (size < 8) return ASSET_FORMAT_RAW;

    // Check PNG signature
    if (memcmp(data, PNG_SIGNATURE, 8) == 0) {
        return ASSET_FORMAT_PNG;
    }

    // Check BMP signature
    if (size >= 2 && memcmp(data, BMP_SIGNATURE, 2) == 0) {
        return ASSET_FORMAT_BMP;
    }

    return ASSET_FORMAT_RAW;
}

// Convert RGBA8888 to RGB565
static uint16_t rgba8888_to_rgb565(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    if (a < 128) return 0x0000; // Transparent as black
    
    uint16_t r5 = (r >> 3) & 0x1F;
    uint16_t g6 = (g >> 2) & 0x3F;
    uint16_t b5 = (b >> 3) & 0x1F;
    
    return (r5 << 11) | (g6 << 5) | b5;
}

// Parse PNG file and convert to RGB565
static texture_t* asset_parse_png(const uint8_t *data, size_t size)
{
    if (size < 8) return NULL;

    // Simple PNG parser - look for IHDR chunk and image data
    const uint8_t *ptr = data + 8; // Skip PNG signature
    const uint8_t *end = data + size;

    uint32_t width = 0, height = 0, bit_depth = 0, color_type = 0;
    
    while (ptr < end - 12) {
        uint32_t chunk_length = (ptr[0] << 24) | (ptr[1] << 16) | (ptr[2] << 8) | ptr[3];
        uint32_t chunk_type = (ptr[4] << 24) | (ptr[5] << 16) | (ptr[6] << 8) | ptr[7];
        
        if (chunk_type == 0x52444849) { // "IHDR"
            if (chunk_length >= 13) {
                width = (ptr[8] << 24) | (ptr[9] << 16) | (ptr[10] << 8) | ptr[11];
                height = (ptr[12] << 24) | (ptr[13] << 16) | (ptr[14] << 8) | ptr[15];
                bit_depth = ptr[16];
                color_type = ptr[17];
                
                ESP_LOGI(TAG, "PNG: %ux%u, depth: %u, color type: %u", 
                        width, height, bit_depth, color_type);
                break;
            }
        }
        
        ptr += 12 + chunk_length;
    }

    if (width == 0 || height == 0 || width > MAX_TEXTURE_WIDTH || height > MAX_TEXTURE_HEIGHT) {
        ESP_LOGE(TAG, "Invalid PNG dimensions");
        return NULL;
    }

    // Allocate texture
    texture_t *texture = heap_caps_malloc(sizeof(texture_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!texture) {
        ESP_LOGE(TAG, "Failed to allocate texture structure");
        return NULL;
    }

    texture->width = width;
    texture->height = height;
    texture->flags = 0;
    texture->palette_id = 0xFFFF; // No palette

    // Allocate pixel data
    size_t pixel_size = width * height * sizeof(uint16_t);
    texture->pixels = heap_caps_malloc(pixel_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!texture->pixels) {
        ESP_LOGE(TAG, "Failed to allocate pixel data");
        heap_caps_free(texture);
        return NULL;
    }

    // For now, create a simple gradient pattern as placeholder
    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            uint8_t r = (x * 255) / width;
            uint8_t g = (y * 255) / height;
            uint8_t b = 128;
            texture->pixels[y * width + x] = rgba8888_to_rgb565(r, g, b, 255);
        }
    }

    return texture;
}

// Load texture from file
texture_t* asset_load_texture(const char *filename)
{
    if (!filename) return NULL;

    // Check cache first
    texture_t *cached = asset_cache_get_texture(filename);
    if (cached) {
        ESP_LOGD(TAG, "Using cached texture: %s", filename);
        return cached;
    }

    size_t file_size = 0;
    uint8_t *file_data = asset_load_file(filename, &file_size);
    if (!file_data) {
        return NULL;
    }

    texture_t *texture = NULL;
    uint32_t format = asset_detect_format(file_data, file_size);

    switch (format) {
        case ASSET_FORMAT_PNG:
            texture = asset_parse_png(file_data, file_size);
            break;
        case ASSET_FORMAT_RAW:
        case ASSET_FORMAT_BMP:
        default:
            ESP_LOGW(TAG, "Format not supported yet: %u", format);
            break;
    }

    heap_caps_free(file_data);

    if (texture) {
        // Cache the texture
        asset_cache_add_texture(filename, texture);
    }

    return texture;
}

// Load texture from memory
texture_t* asset_load_texture_from_memory(const uint8_t *data, size_t size)
{
    if (!data || size == 0) return NULL;

    uint32_t format = asset_detect_format(data, size);
    texture_t *texture = NULL;

    switch (format) {
        case ASSET_FORMAT_PNG:
            texture = asset_parse_png(data, size);
            break;
        default:
            ESP_LOGW(TAG, "Format not supported: %u", format);
            break;
    }

    return texture;
}

// Save texture to file
esp_err_t asset_save_texture(const char *filename, const texture_t *texture)
{
    if (!filename || !texture || !texture->pixels) {
        return ESP_ERR_INVALID_ARG;
    }

    // Create asset header
    asset_header_t header = {
        .magic = 0x41535420,
        .version = 1,
        .type = ASSET_TYPE_TEXTURE,
        .format = ASSET_FORMAT_RAW,
        .compression = ASSET_COMPRESSION_NONE,
        .width = texture->width,
        .height = texture->height,
        .size = texture->width * texture->height * sizeof(uint16_t),
        .compressed_size = 0,
        .checksum = 0,
        .flags = texture->flags
    };

    // Calculate checksum
    header.checksum = asset_calculate_checksum((uint8_t*)texture->pixels, header.size);

    FILE *file = fopen(filename, "wb");
    if (!file) {
        ESP_LOGE(TAG, "Failed to create file: %s", filename);
        return ESP_FAIL;
    }

    // Write header
    size_t written = fwrite(&header, 1, sizeof(header), file);
    if (written != sizeof(header)) {
        ESP_LOGE(TAG, "Failed to write header");
        fclose(file);
        return ESP_FAIL;
    }

    // Write pixel data
    written = fwrite(texture->pixels, 1, header.size, file);
    if (written != header.size) {
        ESP_LOGE(TAG, "Failed to write pixel data");
        fclose(file);
        return ESP_FAIL;
    }

    fclose(file);
    ESP_LOGI(TAG, "Saved texture: %s (%ux%u)", filename, texture->width, texture->height);
    return ESP_OK;
}

// Load palette from file
palette_t* asset_load_palette(const char *filename)
{
    if (!filename) return NULL;

    size_t file_size = 0;
    uint8_t *file_data = asset_load_file(filename, &file_size);
    if (!file_data) {
        return NULL;
    }

    palette_t *palette = heap_caps_malloc(sizeof(palette_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!palette) {
        heap_caps_free(file_data);
        return NULL;
    }

    // Simple palette loading - first 256 colors from file
    if (file_size >= 512) { // 256 * 2 bytes (RGB565)
        memcpy(palette->colors, file_data, 512);
        palette->transparent_color = 0;
        memset(palette->reserved, 0, sizeof(palette->reserved));
    } else {
        // Generate default grayscale palette
        for (int i = 0; i < 256; i++) {
            uint8_t gray = i;
            palette->colors[i] = rgba8888_to_rgb565(gray, gray, gray, 255);
        }
        palette->transparent_color = 0;
    }

    heap_caps_free(file_data);
    return palette;
}

// Save palette to file
esp_err_t asset_save_palette(const char *filename, const palette_t *palette)
{
    if (!filename || !palette) {
        return ESP_ERR_INVALID_ARG;
    }

    FILE *file = fopen(filename, "wb");
    if (!file) {
        ESP_LOGE(TAG, "Failed to create file: %s", filename);
        return ESP_FAIL;
    }

    size_t written = fwrite(palette->colors, 1, sizeof(palette->colors), file);
    fclose(file);

    if (written != sizeof(palette->colors)) {
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Saved palette: %s", filename);
    return ESP_OK;
}

// Convert RGBA8888 to RGB565
uint16_t* asset_convert_to_rgb565(const uint8_t *rgba_data, uint32_t width, uint32_t height)
{
    if (!rgba_data || width == 0 || height == 0) return NULL;

    size_t pixel_count = width * height;
    uint16_t *rgb565 = heap_caps_malloc(pixel_count * sizeof(uint16_t), 
                                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!rgb565) return NULL;

    for (uint32_t i = 0; i < pixel_count; i++) {
        const uint8_t *rgba = rgba_data + (i * 4);
        rgb565[i] = rgba8888_to_rgb565(rgba[0], rgba[1], rgba[2], rgba[3]);
    }

    return rgb565;
}

// Convert RGB565 back to RGBA8888
uint8_t* asset_convert_from_rgb565(const uint16_t *rgb565_data, uint32_t width, uint32_t height)
{
    if (!rgb565_data || width == 0 || height == 0) return NULL;

    size_t pixel_count = width * height;
    uint8_t *rgba = heap_caps_malloc(pixel_count * 4, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!rgba) return NULL;

    for (uint32_t i = 0; i < pixel_count; i++) {
        uint16_t rgb = rgb565_data[i];
        uint8_t r = (rgb >> 11) & 0x1F;
        uint8_t g = (rgb >> 5) & 0x3F;
        uint8_t b = rgb & 0x1F;

        // Scale up to 8-bit
        rgba[i * 4 + 0] = (r << 3) | (r >> 2);
        rgba[i * 4 + 1] = (g << 2) | (g >> 4);
        rgba[i * 4 + 2] = (b << 3) | (b >> 2);
        rgba[i * 4 + 3] = 255;
    }

    return rgba;
}

// Free texture memory
void asset_free_texture(texture_t *texture)
{
    if (texture) {
        if (texture->pixels) {
            heap_caps_free(texture->pixels);
        }
        heap_caps_free(texture);
    }
}

// Free palette memory
void asset_free_palette(palette_t *palette)
{
    if (palette) {
        heap_caps_free(palette);
    }
}

// Get texture from cache
texture_t* asset_cache_get_texture(const char *filename)
{
    if (!filename || !cache_mutex) return NULL;

    xSemaphoreTake(cache_mutex, portMAX_DELAY);

    for (int i = 0; i < asset_config.max_cached_assets; i++) {
        if (asset_cache[i].data && 
            strcmp(asset_cache[i].filename, filename) == 0) {
            
            asset_cache[i].last_access = esp_timer_get_time() / 1000;
            asset_cache[i].access_count++;
            
            texture_t *texture = (texture_t*)asset_cache[i].data;
            
            xSemaphoreGive(cache_mutex);
            cache_hits++;
            return texture;
        }
    }

    xSemaphoreGive(cache_mutex);
    cache_misses++;
    return NULL;
}

// Add texture to cache
esp_err_t asset_cache_add_texture(const char *filename, texture_t *texture)
{
    if (!filename || !texture || !cache_mutex) return ESP_ERR_INVALID_ARG;

    xSemaphoreTake(cache_mutex, portMAX_DELAY);

    // Check if already cached
    for (int i = 0; i < asset_config.max_cached_assets; i++) {
        if (asset_cache[i].data && 
            strcmp(asset_cache[i].filename, filename) == 0) {
            xSemaphoreGive(cache_mutex);
            return ESP_OK;
        }
    }

    // Find empty slot or evict oldest
    int slot = -1;
    uint64_t oldest_time = UINT64_MAX;

    for (int i = 0; i < asset_config.max_cached_assets; i++) {
        if (!asset_cache[i].data) {
            slot = i;
            break;
        }
        
        if (asset_cache[i].last_access < oldest_time) {
            oldest_time = asset_cache[i].last_access;
            slot = i;
        }
    }

    if (slot >= 0) {
        // Evict existing asset
        if (asset_cache[slot].data) {
            ESP_LOGD(TAG, "Evicting asset from cache: %s", asset_cache[slot].filename);
            asset_free_texture((texture_t*)asset_cache[slot].data);
            memory_usage -= asset_cache[slot].size;
        }

        // Add new texture
        strncpy(asset_cache[slot].filename, filename, sizeof(asset_cache[slot].filename) - 1);
        asset_cache[slot].data = texture;
        asset_cache[slot].size = texture->width * texture->height * sizeof(uint16_t) + sizeof(texture_t);
        asset_cache[slot].last_access = esp_timer_get_time() / 1000;
        asset_cache[slot].access_count = 1;

        memory_usage += asset_cache[slot].size;
        ESP_LOGD(TAG, "Added texture to cache: %s", filename);
    }

    xSemaphoreGive(cache_mutex);
    return ESP_OK;
}

// Clear asset cache
void asset_cache_clear(void)
{
    if (!cache_mutex) return;

    xSemaphoreTake(cache_mutex, portMAX_DELAY);

    for (int i = 0; i < asset_config.max_cached_assets; i++) {
        if (asset_cache[i].data) {
            asset_free_texture((texture_t*)asset_cache[i].data);
            memset(&asset_cache[i], 0, sizeof(asset_cache_entry_t));
        }
    }

    memory_usage = 0;
    xSemaphoreGive(cache_mutex);

    ESP_LOGI(TAG, "Asset cache cleared");
}

// Get memory usage
uint32_t asset_get_memory_usage(void)
{
    return memory_usage;
}

// Get cache hits
uint32_t asset_get_cache_hits(void)
{
    return cache_hits;
}

// Get cache misses
uint32_t asset_get_cache_misses(void)
{
    return cache_misses;
}

// List textures in directory
int asset_list_textures(const char *directory, char ***filenames)
{
    if (!directory || !filenames) return 0;

    // For now, return empty list - implement directory scanning later
    *filenames = NULL;
    return 0;
}

// List palettes in directory
int asset_list_palettes(const char *directory, char ***filenames)
{
    if (!directory || !filenames) return 0;

    // For now, return empty list - implement directory scanning later
    *filenames = NULL;
    return 0;
}