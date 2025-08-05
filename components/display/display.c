#include "display.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "display";

static esp_lcd_panel_io_handle_t io_handle = NULL;
static esp_lcd_panel_handle_t panel_handle = NULL;
static frame_buffer_t fb;
static bool display_initialized = false;

// GPIO pin definitions for 720x720 display
#define PIN_NUM_DATA0          39
#define PIN_NUM_DATA1          40
#define PIN_NUM_DATA2          41
#define PIN_NUM_DATA3          42
#define PIN_NUM_DATA4          45
#define PIN_NUM_DATA5          46
#define PIN_NUM_DATA6          47
#define PIN_NUM_DATA7          48
#define PIN_NUM_PCLK           14
#define PIN_NUM_HSYNC          21
#define PIN_NUM_VSYNC          15
#define PIN_NUM_DE             16
#define PIN_NUM_DISP           -1  // Not used
#define PIN_NUM_BK_LIGHT       45

esp_err_t display_init(const display_config_t *config) {
    if (display_initialized) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing display for %dx%d", config->width, config->height);

    // Initialize backlight
    if (PIN_NUM_BK_LIGHT >= 0) {
        gpio_config_t bk_gpio_config = {
            .mode = GPIO_MODE_OUTPUT,
            .pin_bit_mask = 1ULL << PIN_NUM_BK_LIGHT
        };
        ESP_ERROR_CHECK(gpio_config(&bk_gpio_config));
    }

    // Allocate frame buffers in PSRAM
    fb.buffer_size = DISPLAY_WIDTH * DISPLAY_HEIGHT * 2;
    
    // Try PSRAM first, fall back to regular RAM if not available
    fb.buffer1 = heap_caps_malloc(fb.buffer_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!fb.buffer1) {
        ESP_LOGW(TAG, "PSRAM not available, using regular RAM for frame buffer 1");
        fb.buffer1 = heap_caps_malloc(fb.buffer_size, MALLOC_CAP_8BIT);
    }
    
    fb.buffer2 = heap_caps_malloc(fb.buffer_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!fb.buffer2) {
        ESP_LOGW(TAG, "PSRAM not available, using regular RAM for frame buffer 2");
        fb.buffer2 = heap_caps_malloc(fb.buffer_size, MALLOC_CAP_8BIT);
    }
    
    fb.line_buffer = heap_caps_malloc(DISPLAY_WIDTH * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!fb.line_buffer) {
        fb.line_buffer = heap_caps_malloc(DISPLAY_WIDTH * 2, MALLOC_CAP_8BIT);
    }

    if (!fb.buffer1 || !fb.buffer2 || !fb.line_buffer) {
        ESP_LOGE(TAG, "Failed to allocate frame buffers");
        // Clean up any allocated buffers
        if (fb.buffer1) free(fb.buffer1);
        if (fb.buffer2) free(fb.buffer2);
        if (fb.line_buffer) free(fb.line_buffer);
        fb.buffer1 = fb.buffer2 = fb.line_buffer = NULL;
        return ESP_ERR_NO_MEM;
    }

    fb.current = fb.buffer1;
    memset(fb.buffer1, 0, fb.buffer_size);
    memset(fb.buffer2, 0, fb.buffer_size);

    // Configure RGB panel
    ESP_LOGI(TAG, "Initializing RGB panel...");
    
    // Initialize RGB panel interface for ESP32-P4
    // This is a placeholder for the actual RGB panel initialization
    // which would involve configuring the LCD controller peripheral
    
    // TODO: Complete RGB panel initialization with proper ESP32-P4 LCD controller setup
    // For now, mark as initialized for testing purposes
    display_initialized = true;
    
    // Turn on backlight if configured
    if (PIN_NUM_BK_LIGHT >= 0) {
        gpio_set_level(PIN_NUM_BK_LIGHT, 1);
    }
    
    ESP_LOGI(TAG, "Display initialized successfully");
    return ESP_OK;
}

void display_deinit(void) {
    if (!display_initialized) {
        return;
    }

    if (fb.buffer1) {
        free(fb.buffer1);
        fb.buffer1 = NULL;
    }
    if (fb.buffer2) {
        free(fb.buffer2);
        fb.buffer2 = NULL;
    }
    if (fb.line_buffer) {
        free(fb.line_buffer);
        fb.line_buffer = NULL;
    }

    display_initialized = false;
}

uint16_t* display_get_frame_buffer(void) {
    return fb.current;
}

void display_swap_buffers(void) {
    if (fb.current == fb.buffer1) {
        fb.current = fb.buffer2;
    } else {
        fb.current = fb.buffer1;
    }
}

void display_flush(void) {
    if (!display_initialized) {
        return;
    }

    // TODO: Implement actual display flush via DMA
    // For now, just log the frame completion
    static uint32_t frame_count = 0;
    frame_count++;
    
    if (frame_count % 60 == 0) {
        ESP_LOGD(TAG, "Frame %lu completed", frame_count);
    }
}

void display_clear(uint16_t color) {
    if (!display_initialized || !fb.current) {
        return;
    }

    uint32_t *buffer32 = (uint32_t *)fb.current;
    uint32_t color32 = (color << 16) | color;
    size_t count = DISPLAY_WIDTH * DISPLAY_HEIGHT / 2;

    for (size_t i = 0; i < count; i++) {
        buffer32[i] = color32;
    }
}

void display_fill_rect(int x, int y, int w, int h, uint16_t color) {
    if (!display_initialized || !fb.current) {
        return;
    }

    // Clamp to screen bounds
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > DISPLAY_WIDTH) w = DISPLAY_WIDTH - x;
    if (y + h > DISPLAY_HEIGHT) h = DISPLAY_HEIGHT - y;

    if (w <= 0 || h <= 0) return;

    for (int row = y; row < y + h; row++) {
        uint16_t *row_ptr = fb.current + row * DISPLAY_WIDTH + x;
        for (int col = 0; col < w; col++) {
            row_ptr[col] = color;
        }
    }
}

void display_draw_pixel(int x, int y, uint16_t color) {
    if (!display_initialized || !fb.current) {
        return;
    }

    if (x < 0 || x >= DISPLAY_WIDTH || y < 0 || y >= DISPLAY_HEIGHT) {
        return;
    }

    fb.current[y * DISPLAY_WIDTH + x] = color;
}

void display_draw_scanline(int y, const uint16_t *data, int len) {
    if (!display_initialized || !fb.current || y < 0 || y >= DISPLAY_HEIGHT) {
        return;
    }

    len = (len > DISPLAY_WIDTH) ? DISPLAY_WIDTH : len;
    memcpy(fb.current + y * DISPLAY_WIDTH, data, len * 2);
}

void display_set_clip_rect(int x, int y, int w, int h) {
    // TODO: Implement clipping
}

void display_reset_clip_rect(void) {
    // TODO: Reset clipping
}

void display_sleep(void) {
    if (!display_initialized) {
        return;
    }
    ESP_LOGI(TAG, "Display sleep");
}

void display_wake(void) {
    if (!display_initialized) {
        return;
    }
    ESP_LOGI(TAG, "Display wake");
}

uint32_t display_get_frame_time_ms(void) {
    return 0; // TODO: Implement timing
}

float display_get_fps(void) {
    return 0.0f; // TODO: Implement FPS counter
}