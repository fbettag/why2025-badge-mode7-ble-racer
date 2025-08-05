#include "physics.h"
#include "esp_log.h"
#include "string.h"

static const char *TAG = "physics";
static physics_world_t physics_world;
static bool physics_initialized = false;

// Internal helper functions
static void integrate_motion(car_physics_t *car, float delta_time);
static void apply_friction(car_physics_t *car, float delta_time);
static void resolve_collisions(physics_world_t *world);
static void update_checkpoint_progress(physics_world_t *world, uint8_t car_index);

esp_err_t physics_init(void) {
    if (physics_initialized) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing physics system");

    // Initialize physics world
    memset(&physics_world, 0, sizeof(physics_world_t));
    
    // Set up default car configurations
    for (int i = 0; i < PHYSICS_MAX_CARS; i++) {
        physics_world.cars[i].mass = INT_TO_FIXED16(1000);  // 1000kg
        physics_world.cars[i].drag = PHYSICS_DRAG_COEFFICIENT;
        physics_world.cars[i].friction = PHYSICS_FRICTION_COEFFICIENT;
    }

    physics_initialized = true;
    ESP_LOGI(TAG, "Physics system initialized");
    return ESP_OK;
}

void physics_deinit(void) {
    if (!physics_initialized) {
        return;
    }

    physics_initialized = false;
    ESP_LOGI(TAG, "Physics system deinitialized");
}

void physics_update(physics_world_t *world, float delta_time) {
    if (!world || delta_time <= 0.0f || !world->cars) {
        return;
    }

    // Update each car
    for (int i = 0; i < PHYSICS_MAX_CARS; i++) {
        if (world->race_finished[i]) {
            continue;
        }

        car_physics_t *car = &world->cars[i];
        
        // Integrate motion
        integrate_motion(car, delta_time);
        
        // Apply friction and drag
        apply_friction(car, delta_time);
        
        // Update checkpoint progress
        update_checkpoint_progress(world, i);
        
        // Update race time
        world->race_time[i] += (uint32_t)(delta_time * 1000);
    }

    // Resolve collisions between cars and track
    resolve_collisions(world);
}

void physics_reset_car(car_physics_t *car, vec2_t position, fixed16_t heading) {
    if (!car) {
        ESP_LOGW(TAG, "Attempted to reset NULL car");
        return;
    }

    car->position = position;
    car->velocity = (vec2_t){0, 0};
    car->acceleration = (vec2_t){0, 0};
    car->heading = heading;
    car->angular_vel = 0;
    car->speed = 0;
}

void physics_apply_force(car_physics_t *car, vec2_t force) {
    if (!car) {
        ESP_LOGW(TAG, "Attempted to apply force to NULL car");
        return;
    }

    vec2_t acceleration = vec2_scale(force, fixed_div(FIXED16_ONE, car->mass));
    car->acceleration = vec2_add(car->acceleration, acceleration);
}

void physics_apply_torque(car_physics_t *car, fixed16_t torque) {
    if (!car) {
        ESP_LOGW(TAG, "Attempted to apply torque to NULL car");
        return;
    }

    // Convert torque to angular acceleration: α = τ / I
    // Assuming I = m * r^2, with r = 1m for simplicity
    car->angular_vel += fixed_div(torque, car->mass);
}

void physics_handle_input(car_physics_t *car, float throttle, float brake, float steering, float delta_time) {
    if (!car) {
        ESP_LOGW(TAG, "Attempted to handle input for NULL car");
        return;
    }
    
    // Clamp input values to valid ranges
    throttle = (throttle < 0.0f) ? 0.0f : (throttle > 1.0f) ? 1.0f : throttle;
    brake = (brake < 0.0f) ? 0.0f : (brake > 1.0f) ? 1.0f : brake;
    steering = (steering < -1.0f) ? -1.0f : (steering > 1.0f) ? 1.0f : steering;

    // Convert float inputs to fixed-point
    fixed16_t fp_throttle = FLOAT_TO_FIXED16(throttle);
    fixed16_t fp_brake = FLOAT_TO_FIXED16(brake);
    fixed16_t fp_steering = FLOAT_TO_FIXED16(steering);

    // Calculate engine force
    fixed16_t engine_force = fixed_mul(PHYSICS_ACCELERATION, fp_throttle);
    
    // Calculate braking force
    fixed16_t brake_force = fixed_mul(PHYSICS_BRAKING_FORCE, fp_brake);
    
    // Calculate steering based on speed (faster = less steering)
    // Add small epsilon to avoid division by zero
    fixed16_t speed_factor = fixed_div(PHYSICS_MAX_SPEED, car->speed + PHYSICS_MAX_SPEED + FIXED16_ONE);
    fixed16_t steering_angle = fixed_mul(fixed_mul(PHYSICS_TURN_RADIUS, fp_steering), speed_factor);

    // Apply forces in car's local coordinate system
    vec2_t forward = (vec2_t){fixed_cos(car->heading), fixed_sin(car->heading)};
    vec2_t engine_force_vec = vec2_scale(forward, engine_force);
    vec2_t brake_force_vec = vec2_scale(forward, -brake_force);
    
    // Apply forces
    physics_apply_force(car, engine_force_vec);
    physics_apply_force(car, brake_force_vec);
    
    // Apply steering torque
    physics_apply_torque(car, steering_angle);
}

bool physics_check_track_collision(vec2_t position, vec2_t *normal, fixed16_t *penetration) {
    // Simple circular track collision detection
    fixed16_t distance_from_center = vec2_length(position);
    
    if (distance_from_center > PHYSICS_WALL_DISTANCE) {
        if (normal && distance_from_center != 0) {
            *normal = vec2_scale(position, fixed_div(FIXED16_ONE, distance_from_center));
        }
        if (penetration) {
            *penetration = distance_from_center - PHYSICS_WALL_DISTANCE;
        }
        return true;
    }
    
    if (distance_from_center < -PHYSICS_WALL_DISTANCE) {
        if (normal && distance_from_center != 0) {
            *normal = vec2_scale(position, -fixed_div(FIXED16_ONE, distance_from_center));
        }
        if (penetration) {
            *penetration = -PHYSICS_WALL_DISTANCE - distance_from_center;
        }
        return true;
    }
    
    return false;
}

bool physics_check_car_collision(car_physics_t *car1, car_physics_t *car2) {
    if (!car1 || !car2) return false;

    // Simple bounding circle collision
    vec2_t delta = vec2_sub(car1->position, car2->position);
    fixed16_t distance_squared = vec2_dot(delta, delta);
    fixed16_t min_distance = INT_TO_FIXED16(100);  // 1.0m radius per car
    fixed16_t min_distance_squared = fixed_mul(min_distance, min_distance);
    
    return distance_squared < min_distance_squared;
}

bool physics_check_checkpoint_collision(car_physics_t *car, checkpoint_t *checkpoint) {
    if (!car || !checkpoint || checkpoint->passed) return false;

    vec2_t delta = vec2_sub(car->position, checkpoint->position);
    fixed16_t distance_squared = vec2_dot(delta, delta);
    fixed16_t radius_squared = fixed_mul(checkpoint->radius, checkpoint->radius);
    
    return distance_squared < radius_squared;
}

bool physics_ray_cast(vec2_t origin, vec2_t direction, fixed16_t max_distance, vec2_t *hit_point, fixed16_t *distance) {
    // Simple ray-sphere intersection for track boundaries
    fixed16_t a = vec2_dot(direction, direction);
    if (a == 0) return false;
    
    fixed16_t b = fixed_mul(INT_TO_FIXED16(2), vec2_dot(origin, direction));
    fixed16_t c = vec2_dot(origin, origin) - fixed_mul(PHYSICS_WALL_DISTANCE, PHYSICS_WALL_DISTANCE);
    
    fixed16_t discriminant = fixed_mul(b, b) - fixed_mul(INT_TO_FIXED16(4), fixed_mul(a, c));
    
    if (discriminant < 0) return false;
    
    fixed16_t sqrt_disc = fixed_sqrt(discriminant);
    fixed16_t t1 = fixed_div(-b - sqrt_disc, fixed_mul(INT_TO_FIXED16(2), a));
    fixed16_t t2 = fixed_div(-b + sqrt_disc, fixed_mul(INT_TO_FIXED16(2), a));
    
    fixed16_t t = t1 < t2 ? t1 : t2;
    if (t < 0) t = t2;
    
    if (t < 0 || t > max_distance) return false;
    
    if (hit_point) {
        *hit_point = vec2_add(origin, vec2_scale(direction, t));
    }
    if (distance) {
        *distance = t;
    }
    
    return true;
}

fixed16_t physics_get_distance_to_wall(vec2_t position, fixed16_t heading) {
    vec2_t direction = (vec2_t){fixed_cos(heading), fixed_sin(heading)};
    vec2_t hit_point;
    fixed16_t distance;
    
    if (physics_ray_cast(position, direction, PHYSICS_WALL_DISTANCE, &hit_point, &distance)) {
        return distance;
    }
    
    return PHYSICS_WALL_DISTANCE;
}

vec2_t physics_get_closest_point_on_track(vec2_t position) {
    // Clamp position to track boundaries
    fixed16_t distance = vec2_length(position);
    
    if (distance > PHYSICS_WALL_DISTANCE) {
        return vec2_scale(vec2_scale(position, fixed_div(FIXED16_ONE, distance)), PHYSICS_WALL_DISTANCE);
    }
    
    if (distance < -PHYSICS_WALL_DISTANCE) {
        return vec2_scale(vec2_scale(position, fixed_div(FIXED16_ONE, distance)), -PHYSICS_WALL_DISTANCE);
    }
    
    return position;
}

bool physics_is_position_valid(vec2_t position) {
    fixed16_t distance = vec2_length(position);
    return distance <= PHYSICS_WALL_DISTANCE && distance >= -PHYSICS_WALL_DISTANCE;
}

void physics_start_race(physics_world_t *world) {
    if (!world) return;

    for (int i = 0; i < PHYSICS_MAX_CARS; i++) {
        world->race_time[i] = 0;
        world->race_finished[i] = false;
        world->current_checkpoint[i] = 0;
    }

    for (int i = 0; i < world->checkpoint_count; i++) {
        world->checkpoints[i].passed = false;
    }

    ESP_LOGI(TAG, "Race started");
}

void physics_reset_race(physics_world_t *world) {
    if (!world) return;

    physics_start_race(world);
    
    // Reset car positions to starting positions
    for (int i = 0; i < PHYSICS_MAX_CARS; i++) {
        vec2_t start_pos = {0, -i * 100};  // Staggered start
        physics_reset_car(&world->cars[i], start_pos, 0);
    }
}

bool physics_check_race_finished(physics_world_t *world, uint8_t car_index) {
    if (!world || car_index >= PHYSICS_MAX_CARS) return false;
    
    if (world->race_finished[car_index]) {
        return true;
    }
    
    // Check if all checkpoints passed
    bool all_checkpoints_passed = true;
    for (int i = 0; i < world->checkpoint_count; i++) {
        if (!world->checkpoints[i].passed) {
            all_checkpoints_passed = false;
            break;
        }
    }
    
    if (all_checkpoints_passed) {
        world->race_finished[car_index] = true;
        ESP_LOGI(TAG, "Car %d finished race in %d ms", car_index, world->race_time[car_index]);
        return true;
    }
    
    return false;
}

// Internal helper function implementations
static void integrate_motion(car_physics_t *car, float delta_time) {
    fixed16_t dt = FLOAT_TO_FIXED16(delta_time);
    
    // Integrate acceleration to velocity
    vec2_t velocity_change = vec2_scale(car->acceleration, dt);
    car->velocity = vec2_add(car->velocity, velocity_change);
    
    // Integrate velocity to position
    vec2_t position_change = vec2_scale(car->velocity, dt);
    car->position = vec2_add(car->position, position_change);
    
    // Integrate angular velocity to heading
    car->heading += fixed_mul(car->angular_vel, dt);
    
    // Update speed
    car->speed = vec2_length(car->velocity);
    
    // Reset acceleration for next frame
    car->acceleration = (vec2_t){0, 0};
}

static void apply_friction(car_physics_t *car, float delta_time) {
    fixed16_t dt = FLOAT_TO_FIXED16(delta_time);
    
    // Apply drag force: F = -v * drag_coefficient
    vec2_t drag_force = vec2_scale(car->velocity, -car->drag);
    physics_apply_force(car, drag_force);
    
    // Apply friction force: F = -v * friction_coefficient * mass
    vec2_t friction_force = vec2_scale(car->velocity, -fixed_mul(car->friction, car->mass));
    physics_apply_force(car, friction_force);
    
    // Apply angular friction
    car->angular_vel = fixed_mul(car->angular_vel, FIXED16_ONE - fixed_mul(FIXED16_ONE / 10, dt));
}

static void resolve_collisions(physics_world_t *world) {
    if (!world) return;

    // Resolve car-track collisions
    for (int i = 0; i < PHYSICS_MAX_CARS; i++) {
        car_physics_t *car = &world->cars[i];
        
        vec2_t normal;
        fixed16_t penetration;
        
        if (physics_check_track_collision(car->position, &normal, &penetration)) {
            // Resolve penetration
            vec2_t correction = vec2_scale(normal, penetration);
            car->position = vec2_add(car->position, correction);
            
            // Reflect velocity with elasticity
            fixed16_t normal_vel = vec2_dot(car->velocity, normal);
            if (normal_vel < 0) {
                vec2_t reflected = vec2_sub(car->velocity, 
                                          vec2_scale(normal, fixed_mul(FIXED16_ONE * 2, normal_vel)));
                car->velocity = vec2_scale(reflected, PHYSICS_COLLISION_ELASTICITY);
            }
        }
    }

    // Resolve car-car collisions
    for (int i = 0; i < PHYSICS_MAX_CARS; i++) {
        for (int j = i + 1; j < PHYSICS_MAX_CARS; j++) {
            if (physics_check_car_collision(&world->cars[i], &world->cars[j])) {
                // Simple collision response - swap velocities
                vec2_t temp = world->cars[i].velocity;
                world->cars[i].velocity = world->cars[j].velocity;
                world->cars[j].velocity = temp;
                
                // Separate cars
                vec2_t delta = vec2_sub(world->cars[i].position, world->cars[j].position);
                vec2_t direction = vec2_scale(delta, fixed_div(FIXED16_ONE, vec2_length(delta)));
                
                world->cars[i].position = vec2_add(world->cars[i].position, 
                                                  vec2_scale(direction, INT_TO_FIXED16(50)));
                world->cars[j].position = vec2_sub(world->cars[j].position, 
                                                  vec2_scale(direction, INT_TO_FIXED16(50)));
            }
        }
    }
}

static void update_checkpoint_progress(physics_world_t *world, uint8_t car_index) {
    if (!world || car_index >= PHYSICS_MAX_CARS) return;

    car_physics_t *car = &world->cars[car_index];
    uint8_t current = world->current_checkpoint[car_index];
    
    if (current >= world->checkpoint_count) {
        return;
    }
    
    checkpoint_t *checkpoint = &world->checkpoints[current];
    
    if (physics_check_checkpoint_collision(car, checkpoint)) {
        checkpoint->passed = true;
        world->current_checkpoint[car_index]++;
        
        ESP_LOGI(TAG, "Car %d passed checkpoint %d", car_index, current);
        
        // Reset checkpoint for next lap
        if (world->current_checkpoint[car_index] >= world->checkpoint_count) {
            world->current_checkpoint[car_index] = 0;
            for (int i = 0; i < world->checkpoint_count; i++) {
                world->checkpoints[i].passed = false;
            }
        }
    }
}