#ifndef _DISPLAY_H_
#define _DISPLAY_H_

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#define DISPLAY_WIDTH  720
#define DISPLAY_HEIGHT 720
#define DISPLAY_BPP    16  // RGB565
#define DISPLAY_BUFFER_SIZE (DISPLAY_WIDTH * DISPLAY_HEIGHT * 2)

// Display initialization structure
typedef struct {
    int width;
    int height;
    int refresh_rate;
    bool use_dma;
} display_config_t;

// Frame buffer structure
typedef struct {
    uint16_t *buffer1;
    uint16_t *buffer2;
    uint16_t *current;
    uint8_t *line_buffer;  // For Mode-7 rendering
    size_t buffer_size;
} frame_buffer_t;

// Public API
esp_err_t display_init(const display_config_t *config);
void display_deinit(void);

// Frame buffer management
uint16_t* display_get_frame_buffer(void);
void display_swap_buffers(void);
void display_flush(void);

// Drawing primitives
void display_clear(uint16_t color);
void display_fill_rect(int x, int y, int w, int h, uint16_t color);
void display_draw_pixel(int x, int y, uint16_t color);

// Mode-7 specific functions
void display_draw_scanline(int y, const uint16_t *data, int len);
void display_set_clip_rect(int x, int y, int w, int h);
void display_reset_clip_rect(void);

// Power management
void display_sleep(void);
void display_wake(void);

// Performance monitoring
uint32_t display_get_frame_time_ms(void);
float display_get_fps(void);

#endif // _DISPLAY_H_