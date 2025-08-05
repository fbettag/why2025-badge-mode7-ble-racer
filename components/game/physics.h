#ifndef _PHYSICS_H_
#define _PHYSICS_H_

#include "include/math.h"
#include "stdint.h"
#include "esp_err.h"

// Physics constants
#define PHYSICS_MAX_CARS 2
#define PHYSICS_GRAVITY FLOAT_TO_FIXED16(9.8f)  // 9.8 m/s^2 in fixed-point
#define PHYSICS_FRICTION_COEFFICIENT FLOAT_TO_FIXED16(0.85f)  // 0.85 in fixed-point
#define PHYSICS_DRAG_COEFFICIENT FLOAT_TO_FIXED16(0.15f)  // 0.15 in fixed-point
#define PHYSICS_MAX_SPEED FLOAT_TO_FIXED16(20.0f)  // 20 m/s max speed
#define PHYSICS_ACCELERATION FLOAT_TO_FIXED16(1.5f)  // 1.5 m/s^2 acceleration
#define PHYSICS_BRAKING_FORCE FLOAT_TO_FIXED16(3.0f)  // 3.0 m/s^2 braking
#define PHYSICS_TURN_RADIUS FLOAT_TO_FIXED16(5.0f)  // 5.0m minimum turn radius
#define PHYSICS_COLLISION_ELASTICITY FLOAT_TO_FIXED16(0.75f)  // 0.75 bounce factor

// Collision detection constants
#define PHYSICS_TRACK_WIDTH FLOAT_TO_FIXED16(8.0f)  // 8.0m track width
#define PHYSICS_WALL_DISTANCE FLOAT_TO_FIXED16(4.0f)  // 4.0m from center to wall
#define PHYSICS_CHECKPOINT_RADIUS FLOAT_TO_FIXED16(1.0f)  // 1.0m checkpoint radius

// Physics structures
typedef struct {
    vec2_t position;      // World position (fixed-point 16.16)
    vec2_t velocity;      // Velocity vector (m/s)
    vec2_t acceleration;  // Acceleration vector (m/s^2)
    fixed16_t heading;       // Car heading angle (radians, fixed-point)
    fixed16_t angular_vel;   // Angular velocity (rad/s)
    fixed16_t speed;         // Magnitude of velocity
    fixed16_t mass;          // Car mass (kg)
    fixed16_t drag;          // Drag coefficient
    fixed16_t friction;      // Friction coefficient
} car_physics_t;

typedef struct {
    vec2_t position;      // Checkpoint position
    fixed16_t radius;        // Checkpoint radius
    bool passed;          // Whether checkpoint has been passed
    uint8_t index;        // Checkpoint index
} checkpoint_t;

typedef struct {
    car_physics_t cars[PHYSICS_MAX_CARS];
    checkpoint_t checkpoints[16];  // Up to 16 checkpoints
    uint8_t checkpoint_count;
    uint8_t current_checkpoint[PHYSICS_MAX_CARS];
    uint32_t race_time[PHYSICS_MAX_CARS];
    bool race_finished[PHYSICS_MAX_CARS];
    fixed16_t track_length;
} physics_world_t;

// Physics functions
esp_err_t physics_init(void);
void physics_deinit(void);
void physics_update(physics_world_t *world, float delta_time);
void physics_reset_car(car_physics_t *car, vec2_t position, fixed16_t heading);

// Car physics functions
void physics_apply_force(car_physics_t *car, vec2_t force);
void physics_apply_torque(car_physics_t *car, fixed16_t torque);
void physics_handle_input(car_physics_t *car, float throttle, float brake, float steering, float delta_time);

// Collision detection
bool physics_check_track_collision(vec2_t position, vec2_t *normal, fixed16_t *penetration);
bool physics_check_car_collision(car_physics_t *car1, car_physics_t *car2);
bool physics_check_checkpoint_collision(car_physics_t *car, checkpoint_t *checkpoint);

// Ray casting for AI and collision detection
bool physics_ray_cast(vec2_t origin, vec2_t direction, fixed16_t max_distance, vec2_t *hit_point, fixed16_t *distance);

// Utility functions
fixed16_t physics_get_distance_to_wall(vec2_t position, fixed16_t heading);
vec2_t physics_get_closest_point_on_track(vec2_t position);
bool physics_is_position_valid(vec2_t position);

// Race management
void physics_start_race(physics_world_t *world);
void physics_reset_race(physics_world_t *world);
bool physics_check_race_finished(physics_world_t *world, uint8_t car_index);

#endif // _PHYSICS_H_