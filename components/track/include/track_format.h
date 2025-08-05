#ifndef _TRACK_FORMAT_H_
#define _TRACK_FORMAT_H_

#include <stdint.h>
#include <stdbool.h>
// Define track magic constants
#define TRACK_MAGIC 0x4D375452  // "M7TR" in little-endian

// Track file format constants
#define TRACK_VERSION 1
#define TRACK_MAX_NAME_LEN 32
#define TRACK_MAX_CHECKPOINTS 16
#define TRACK_MAX_LAPS 99
#define TRACK_TILE_SIZE 32
#define TRACK_HEIGHTMAP_SIZE 256

// Tile types for Mode-7 rendering
typedef enum {
    TILE_ROAD_ASPHALT = 0,
    TILE_ROAD_DIRT,
    TILE_ROAD_GRASS,
    TILE_ROAD_SAND,
    TILE_ROAD_ICE,
    TILE_CHECKPOINT_START,
    TILE_CHECKPOINT,
    TILE_FINISH_LINE,
    TILE_WALL_CONCRETE,
    TILE_WALL_BARRIER,
    TILE_WALL_FENCE,
    TILE_WALL_TREES,
    TILE_OBSTACLE_CRATE,
    TILE_OBSTACLE_CONE,
    TILE_BOOST_PAD,
    TILE_JUMP_PAD,
    TILE_WATER,
    TILE_OFFROAD,
    TILE_COUNT
} track_tile_type_t;

// Track metadata structure
typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t version;
    char name[TRACK_MAX_NAME_LEN];
    uint16_t width;
    uint16_t height;
    uint16_t tile_size;
    uint16_t checkpoint_count;
    uint16_t lap_count;
    uint32_t track_length;
    uint32_t thumbnail_offset;
    uint32_t thumbnail_size;
    uint32_t heightmap_offset;
    uint32_t heightmap_size;
    uint32_t tilemap_offset;
    uint32_t tilemap_size;
    uint32_t checkpoint_offset;
    uint32_t checkpoint_size;
    uint32_t collision_offset;
    uint32_t collision_size;
    uint32_t checksum;
} track_header_t;

// Checkpoint structure
typedef struct __attribute__((packed)) {
    int16_t x;
    int16_t y;
    uint16_t radius;
    uint8_t index;
    uint8_t type;  // 0=normal, 1=start/finish, 2=sector
    uint16_t order;
} track_checkpoint_t;

// Collision data structure
typedef struct __attribute__((packed)) {
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
    uint8_t collision_type;
    uint8_t material;
} track_collision_t;

// Heightmap data structure
typedef struct __attribute__((packed)) {
    int8_t height[TRACK_HEIGHTMAP_SIZE][TRACK_HEIGHTMAP_SIZE];
    uint8_t scale;  // Height scale factor (1 unit = scale * 0.1m)
} track_heightmap_t;

// Track data structure (in-memory representation)
typedef struct {
    char name[TRACK_MAX_NAME_LEN];
    uint16_t width;
    uint16_t height;
    uint16_t tile_size;
    uint16_t lap_count;
    uint16_t checkpoint_count;
    uint32_t track_length;
    
    // Tile data
    uint8_t *tilemap;           // 2D array of tile types
    int8_t *heightmap;          // 2D array of height values
    
    // Checkpoint data
    track_checkpoint_t checkpoints[TRACK_MAX_CHECKPOINTS];
    
    // Collision data
    track_collision_t *collision_data;
    uint16_t collision_count;
    
    // Render data
    uint16_t *texture_indices;  // Texture indices for each tile
    uint32_t texture_count;
    
    // Metadata
    uint8_t *thumbnail;         // RGB565 thumbnail data
    uint32_t thumbnail_size;
    
    // Runtime data
    bool loaded;
    uint32_t memory_usage;
} track_data_t;

// Track loader configuration
typedef struct {
    bool enable_cache;
    bool enable_compression;
    uint32_t max_memory_usage;
    uint8_t max_tracks_cached;
} track_loader_config_t;

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

// Track loader error codes
typedef enum {
    TRACK_OK = 0,
    TRACK_ERROR_FILE_NOT_FOUND,
    TRACK_ERROR_INVALID_FORMAT,
    TRACK_ERROR_VERSION_MISMATCH,
    TRACK_ERROR_CHECKSUM_MISMATCH,
    TRACK_ERROR_OUT_OF_MEMORY,
    TRACK_ERROR_TOO_MANY_CHECKPOINTS,
    TRACK_ERROR_INVALID_DIMENSIONS,
    TRACK_ERROR_IO_ERROR
} track_error_t;

// Tile properties for physics simulation
typedef struct {
    float speed_multiplier;
    float friction;
    bool is_drivable;
} track_tile_properties_t;

#endif // _TRACK_FORMAT_H_