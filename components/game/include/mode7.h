#ifndef _MODE7_H_
#define _MODE7_H_

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#define TILE_SIZE 8
#define TILEMAP_WIDTH 128
#define TILEMAP_HEIGHT 128
#define TILESHEET_WIDTH 16
#define TILESHEET_HEIGHT 16
#define SCREEN_WIDTH 720
#define SCREEN_HEIGHT 720

// Fixed-point math
typedef int32_t fixed16_t;
#define FIXED16_ONE 65536
#define FIXED16_HALF 32768

// 16.16 fixed-point conversion
#define FLOAT_TO_FIXED16(f) ((fixed16_t)((f) * FIXED16_ONE))
#define FIXED16_TO_FLOAT(f) ((float)(f) / FIXED16_ONE)

// Mode-7 camera structure
typedef struct {
    fixed16_t x, y;           // World position
    fixed16_t z;              // Height above ground
    fixed16_t angle;          // Heading angle
    fixed16_t pitch;          // Camera pitch
    fixed16_t horizon;        // Horizon line
} mode7_camera_t;

// Tile structure
typedef struct {
    uint8_t tile_id;
    uint8_t flags;
} tile_t;

// Mode-7 context
typedef struct {
    mode7_camera_t camera;
    uint8_t *tilemap;
    uint8_t *tilesheet;
    uint16_t *palette;
    
    // Precomputed lookup tables
    int16_t *scale_lut;
    int16_t *angle_lut;
    
    // Rendering buffers
    uint16_t *frame_buffer;
    uint8_t *line_buffer;
    
    // Performance counters
    uint32_t frame_time_ms;
    uint32_t render_time_ms;
    
    // Configuration
    bool half_resolution;
    bool enable_sprites;
    uint8_t quality;
} mode7_context_t;

// Public API
esp_err_t mode7_init(mode7_context_t *ctx);
void mode7_deinit(mode7_context_t *ctx);
void mode7_set_camera(mode7_context_t *ctx, const mode7_camera_t *camera);
void mode7_render_frame(mode7_context_t *ctx);

// Asset loading
esp_err_t mode7_load_tilemap(mode7_context_t *ctx, const char *filename);
esp_err_t mode7_load_tilesheet(mode7_context_t *ctx, const char *filename);
esp_err_t mode7_load_palette(mode7_context_t *ctx, const char *filename);

// Camera utilities
void mode7_move_camera(mode7_context_t *ctx, fixed16_t dx, fixed16_t dy);
void mode7_rotate_camera(mode7_context_t *ctx, fixed16_t dangle);
void mode7_set_camera_height(mode7_context_t *ctx, fixed16_t height);

// Performance
uint32_t mode7_get_frame_time(mode7_context_t *ctx);
void mode7_set_quality(mode7_context_t *ctx, uint8_t quality);
void mode7_toggle_half_resolution(mode7_context_t *ctx, bool enable);

#endif // _MODE7_H_