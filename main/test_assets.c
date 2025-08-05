#include "asset_loader.h"
#include "tile_converter.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "test_assets";

void test_asset_system(void)
{
    ESP_LOGI(TAG, "Starting asset system test...");
    
    // Initialize asset system
    asset_config_t config = {
        .enable_compression = false,
        .enable_caching = true,
        .max_memory_usage = 512 * 1024,
        .max_cached_assets = 4,
        .preload_textures = true
    };
    
    if (asset_loader_init(&config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize asset loader");
        return;
    }
    
    // Test tilesheet generation
    texture_t *tilesheet = tile_generate_tilesheet(8);
    if (tilesheet) {
        ESP_LOGI(TAG, "Generated tilesheet: %ux%u", tilesheet->width, tilesheet->height);
        
        // Test saving tilesheet
        if (tile_save_tilesheet("/spiffs/assets/test_tilesheet.ast", tilesheet) == ESP_OK) {
            ESP_LOGI(TAG, "Saved test tilesheet");
        }
        
        asset_free_texture(tilesheet);
    }
    
    // Test texture loading
    texture_t *loaded_texture = asset_load_texture("/spiffs/assets/test_tilesheet.ast");
    if (loaded_texture) {
        ESP_LOGI(TAG, "Loaded texture: %ux%u", loaded_texture->width, loaded_texture->height);
        asset_free_texture(loaded_texture);
    }
    
    // Test palette generation
    palette_t *palette = asset_load_palette("/spiffs/assets/test_palette.pal");
    if (!palette) {
        // Create default palette
        palette = heap_caps_malloc(sizeof(palette_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (palette) {
            for (int i = 0; i < 256; i++) {
                palette->colors[i] = ((i & 0x1F) << 11) | (((i & 0x3F) << 5)) | (i & 0x1F);
            }
            palette->transparent_color = 0;
            
            if (asset_save_palette("/spiffs/assets/test_palette.pal", palette) == ESP_OK) {
                ESP_LOGI(TAG, "Saved test palette");
            }
            
            asset_free_palette(palette);
        }
    }
    
    // Test ASCII to binary track conversion
    const char *test_track = 
        "###\n"
        "#X#\n"
        "#R#\n"
        "###\n";
    
    FILE *temp_file = fopen("/tmp/test_track.txt", "w");
    if (temp_file) {
        fputs(test_track, temp_file);
        fclose(temp_file);
        
        if (tile_convert_ascii_to_binary("/tmp/test_track.txt", "/spiffs/tracks/test.trk") == ESP_OK) {
            ESP_LOGI(TAG, "Converted test track");
        }
        
        remove("/tmp/test_track.txt");
    }
    
    ESP_LOGI(TAG, "Asset system test completed");
    ESP_LOGI(TAG, "Memory usage: %lu bytes", asset_get_memory_usage());
    ESP_LOGI(TAG, "Cache hits: %lu, misses: %lu", asset_get_cache_hits(), asset_get_cache_misses());
    
    asset_loader_deinit();
}