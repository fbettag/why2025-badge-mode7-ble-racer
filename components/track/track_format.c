#include "track_format.h"
#include "esp_log.h"
#include "esp_crc.h"
#include "utils.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "track_format";

// Default tracks data
static const uint8_t default_track_tilemap[] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0,
    0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0,
    0, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 0,
    0, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 0,
    0, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 0,
    0, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 0,
    0, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 0,
    0, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 0,
    0, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 0,
    0, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 0,
    0, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 0,
    0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0,
    0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static const int8_t default_track_heightmap[] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0,
    0, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 0,
    0, 1, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 2, 1, 0,
    0, 1, 2, 3, 4, 4, 4, 4, 4, 4, 4, 4, 3, 2, 1, 0,
    0, 1, 2, 3, 4, 5, 5, 5, 5, 5, 5, 4, 3, 2, 1, 0,
    0, 1, 2, 3, 4, 5, 6, 6, 6, 6, 5, 4, 3, 2, 1, 0,
    0, 1, 2, 3, 4, 5, 6, 7, 7, 6, 5, 4, 3, 2, 1, 0,
    0, 1, 2, 3, 4, 5, 6, 7, 7, 6, 5, 4, 3, 2, 1, 0,
    0, 1, 2, 3, 4, 5, 6, 6, 6, 6, 5, 4, 3, 2, 1, 0,
    0, 1, 2, 3, 4, 5, 5, 5, 5, 5, 5, 4, 3, 2, 1, 0,
    0, 1, 2, 3, 4, 4, 4, 4, 4, 4, 4, 4, 3, 2, 1, 0,
    0, 1, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 2, 1, 0,
    0, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 0,
    0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

// Create a default track file
esp_err_t track_create_default(const char *filename)
{
    if (!filename) {
        return ESP_ERR_INVALID_ARG;
    }

    char filepath[64];
    snprintf(filepath, sizeof(filepath), "/tracks/%s", filename);

    FILE *file = fopen(filepath, "wb");
    if (!file) {
        ESP_LOGE(TAG, "Failed to create track file: %s", filename);
        return ESP_FAIL;
    }

    track_header_t header = {0};
    header.magic = TRACK_MAGIC_HEADER;
    header.version = TRACK_VERSION;
    strncpy(header.name, "Default Circuit", sizeof(header.name) - 1);
    header.width = 16;
    header.height = 16;
    header.tile_size = 32;
    header.checkpoint_count = 4;
    header.lap_count = 3;
    header.track_length = 2048;

    // Calculate offsets
    uint32_t current_offset = sizeof(header);
    
    header.tilemap_offset = current_offset;
    header.tilemap_size = sizeof(default_track_tilemap);
    current_offset += header.tilemap_size;

    header.heightmap_offset = current_offset;
    header.heightmap_size = sizeof(default_track_heightmap);
    current_offset += header.heightmap_size;

    header.checkpoint_offset = current_offset;
    header.checkpoint_size = sizeof(track_checkpoint_t) * header.checkpoint_count;
    current_offset += header.checkpoint_size;

    header.collision_offset = current_offset;
    header.collision_size = 0;  // No collision data for default track
    current_offset += header.collision_size;

    header.thumbnail_offset = current_offset;
    header.thumbnail_size = 0;  // No thumbnail for default track

    // Create checkpoints (simple rectangular circuit)
    track_checkpoint_t checkpoints[4] = {
        { .x = 128, .y = 128, .radius = 64, .index = 0, .type = 1, .order = 0 },  // Start/finish
        { .x = 384, .y = 128, .radius = 48, .index = 1, .type = 0, .order = 1 },
        { .x = 384, .y = 384, .radius = 48, .index = 2, .type = 0, .order = 2 },
        { .x = 128, .y = 384, .radius = 48, .index = 3, .type = 0, .order = 3 }
    };

    // Calculate checksum
    header.checksum = crc32((uint8_t *)&header, sizeof(header) - sizeof(uint32_t));

    // Write header
    fwrite(&header, 1, sizeof(header), file);

    // Write tilemap
    fwrite(default_track_tilemap, 1, sizeof(default_track_tilemap), file);

    // Write heightmap
    fwrite(default_track_heightmap, 1, sizeof(default_track_heightmap), file);

    // Write checkpoints
    fwrite(checkpoints, 1, sizeof(checkpoints), file);

    fclose(file);

    ESP_LOGI(TAG, "Default track created: %s", filename);
    return ESP_OK;
}

// Convert ASCII track format to binary
esp_err_t track_convert_ascii(const char *ascii_filename, const char *binary_filename)
{
    // This would parse ASCII track format and convert to binary
    // For now, just return success
    return ESP_OK;
}

// Validate track format
bool track_format_validate(const track_header_t *header)
{
    if (!header) return false;
    if (header->magic != TRACK_MAGIC_HEADER) return false;
    if (header->version != TRACK_VERSION) return false;
    if (header->width == 0 || header->width > 1024) return false;
    if (header->height == 0 || header->height > 1024) return false;
    if (header->tile_size == 0 || header->tile_size > 256) return false;
    if (header->checkpoint_count > TRACK_MAX_CHECKPOINTS) return false;
    if (header->lap_count == 0 || header->lap_count > TRACK_MAX_LAPS) return false;
    
    return true;
}

// Get tile properties
void track_get_tile_properties(uint8_t tile_type, track_tile_properties_t *props)
{
    static const track_tile_properties_t tile_properties[TILE_COUNT] = {
        [TILE_ROAD_ASPHALT] = { .speed_multiplier = 1.0f, .friction = 0.95f, .is_drivable = true },
        [TILE_ROAD_DIRT] = { .speed_multiplier = 0.8f, .friction = 0.85f, .is_drivable = true },
        [TILE_ROAD_GRASS] = { .speed_multiplier = 0.6f, .friction = 0.75f, .is_drivable = true },
        [TILE_ROAD_SAND] = { .speed_multiplier = 0.4f, .friction = 0.65f, .is_drivable = true },
        [TILE_ROAD_ICE] = { .speed_multiplier = 0.9f, .friction = 0.3f, .is_drivable = true },
        [TILE_CHECKPOINT_START] = { .speed_multiplier = 1.0f, .friction = 0.95f, .is_drivable = true },
        [TILE_CHECKPOINT] = { .speed_multiplier = 1.0f, .friction = 0.95f, .is_drivable = true },
        [TILE_FINISH_LINE] = { .speed_multiplier = 1.0f, .friction = 0.95f, .is_drivable = true },
        [TILE_WALL_CONCRETE] = { .speed_multiplier = 0.0f, .friction = 1.0f, .is_drivable = false },
        [TILE_WALL_BARRIER] = { .speed_multiplier = 0.0f, .friction = 1.0f, .is_drivable = false },
        [TILE_WALL_FENCE] = { .speed_multiplier = 0.0f, .friction = 1.0f, .is_drivable = false },
        [TILE_WALL_TREES] = { .speed_multiplier = 0.0f, .friction = 1.0f, .is_drivable = false },
        [TILE_OBSTACLE_CRATE] = { .speed_multiplier = 0.0f, .friction = 1.0f, .is_drivable = false },
        [TILE_OBSTACLE_CONE] = { .speed_multiplier = 0.0f, .friction = 1.0f, .is_drivable = false },
        [TILE_BOOST_PAD] = { .speed_multiplier = 1.5f, .friction = 0.95f, .is_drivable = true },
        [TILE_JUMP_PAD] = { .speed_multiplier = 1.2f, .friction = 0.95f, .is_drivable = true },
        [TILE_WATER] = { .speed_multiplier = 0.2f, .friction = 0.5f, .is_drivable = false },
        [TILE_OFFROAD] = { .speed_multiplier = 0.5f, .friction = 0.7f, .is_drivable = true }
    };
    
    if (tile_type < TILE_COUNT) {
        *props = tile_properties[tile_type];
    } else {
        *props = tile_properties[TILE_OFFROAD];
    }
}