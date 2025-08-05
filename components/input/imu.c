#include "imu.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "math.h"

static const char *TAG = "imu";

// MPU6050 I2C address and registers
#define MPU6050_ADDR         0x68
#define MPU6050_WHO_AM_I     0x75
#define MPU6050_PWR_MGMT_1   0x6B
#define MPU6050_ACCEL_XOUT_H 0x3B
#define MPU6050_GYRO_XOUT_H  0x43
#define MPU6050_CONFIG       0x1A
#define MPU6050_GYRO_CONFIG  0x1B
#define MPU6050_ACCEL_CONFIG 0x1C

// I2C configuration
#define I2C_PORT             I2C_NUM_0
#define I2C_SDA_PIN          8
#define I2C_SCL_PIN          9
#define I2C_FREQ_HZ          400000

static bool imu_initialized = false;
static float accel_offsets[3] = {0};
static float gyro_offsets[3] = {0};
static float tilt_calibration = 0.0f;

static esp_err_t i2c_write_byte(uint8_t reg, uint8_t data) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (MPU6050_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_write_byte(cmd, data, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_PORT, cmd, 1000 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(cmd);
    return ret;
}

static esp_err_t i2c_read_bytes(uint8_t reg, uint8_t *data, size_t len) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (MPU6050_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (MPU6050_ADDR << 1) | I2C_MASTER_READ, true);
    i2c_master_read(cmd, data, len, I2C_MASTER_LAST_NACK);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_PORT, cmd, 1000 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(cmd);
    return ret;
}

esp_err_t imu_init(void) {
    if (imu_initialized) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing IMU");

    // Configure I2C
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_SDA_PIN,
        .scl_io_num = I2C_SCL_PIN,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_param_config(I2C_PORT, &conf));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_PORT, conf.mode, 0, 0, 0));

    // Check WHO_AM_I
    uint8_t who_am_i = 0;
    ESP_ERROR_CHECK(i2c_read_bytes(MPU6050_WHO_AM_I, &who_am_i, 1));
    if (who_am_i != 0x68) {
        ESP_LOGE(TAG, "Unexpected WHO_AM_I: 0x%02X", who_am_i);
        return ESP_FAIL;
    }

    // Initialize MPU6050
    ESP_ERROR_CHECK(i2c_write_byte(MPU6050_PWR_MGMT_1, 0x00)); // Wake up
    vTaskDelay(pdMS_TO_TICKS(100));
    
    ESP_ERROR_CHECK(i2c_write_byte(MPU6050_CONFIG, 0x01)); // DLPF = 1
    ESP_ERROR_CHECK(i2c_write_byte(MPU6050_GYRO_CONFIG, 0x08)); // ±500 dps
    ESP_ERROR_CHECK(i2c_write_byte(MPU6050_ACCEL_CONFIG, 0x08)); // ±4g

    imu_initialized = true;
    ESP_LOGI(TAG, "IMU initialized");
    return ESP_OK;
}

void imu_deinit(void) {
    if (!imu_initialized) {
        return;
    }

    i2c_driver_delete(I2C_PORT);
    imu_initialized = false;
}

void imu_update(void) {
    if (!imu_initialized) {
        return;
    }

    uint8_t data[14];
    ESP_ERROR_CHECK(i2c_read_bytes(MPU6050_ACCEL_XOUT_H, data, 14));

    // Convert raw data to g units
    float accel_x = (int16_t)((data[0] << 8) | data[1]) / 8192.0f;
    float accel_y = (int16_t)((data[2] << 8) | data[3]) / 8192.0f;
    float accel_z = (int16_t)((data[4] << 8) | data[5]) / 8192.0f;

    float gyro_x = (int16_t)((data[8] << 8) | data[9]) / 65.5f;
    float gyro_y = (int16_t)((data[10] << 8) | data[11]) / 65.5f;
    float gyro_z = (int16_t)((data[12] << 8) | data[13]) / 65.5f;

    // Apply calibration offsets
    accel_x -= accel_offsets[0];
    accel_y -= accel_offsets[1];
    accel_z -= accel_offsets[2];
    
    gyro_x -= gyro_offsets[0];
    gyro_y -= gyro_offsets[1];
    gyro_z -= gyro_offsets[2];

    // Calculate tilt angle for steering
    float tilt_angle = atan2(accel_y, sqrt(accel_x * accel_x + accel_z * accel_z)) * 180.0f / M_PI;
    tilt_angle -= tilt_calibration;
}

void imu_get_data(float *accel_x, float *accel_y, float *accel_z,
                  float *gyro_x, float *gyro_y, float *gyro_z) {
    if (!imu_initialized) {
        if (accel_x) *accel_x = 0;
        if (accel_y) *accel_y = 0;
        if (accel_z) *accel_z = 0;
        if (gyro_x) *gyro_x = 0;
        if (gyro_y) *gyro_y = 0;
        if (gyro_z) *gyro_z = 0;
        return;
    }

    // Return latest data
    if (accel_x) *accel_x = 0; // TODO: Store actual values
    if (accel_y) *accel_y = 0;
    if (accel_z) *accel_z = 0;
    if (gyro_x) *gyro_x = 0;
    if (gyro_y) *gyro_y = 0;
    if (gyro_z) *gyro_z = 0;
}

void imu_calibrate(void) {
    if (!imu_initialized) {
        return;
    }

    ESP_LOGI(TAG, "Calibrating IMU...");
    
    float accel_sum[3] = {0};
    float gyro_sum[3] = {0};
    const int samples = 100;
    
    for (int i = 0; i < samples; i++) {
        uint8_t data[14];
        ESP_ERROR_CHECK(i2c_read_bytes(MPU6050_ACCEL_XOUT_H, data, 14));
        
        accel_sum[0] += (int16_t)((data[0] << 8) | data[1]) / 8192.0f;
        accel_sum[1] += (int16_t)((data[2] << 8) | data[3]) / 8192.0f;
        accel_sum[2] += (int16_t)((data[4] << 8) | data[5]) / 8192.0f;
        
        gyro_sum[0] += (int16_t)((data[8] << 8) | data[9]) / 65.5f;
        gyro_sum[1] += (int16_t)((data[10] << 8) | data[11]) / 65.5f;
        gyro_sum[2] += (int16_t)((data[12] << 8) | data[13]) / 65.5f;
        
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    // Calculate averages
    for (int i = 0; i < 3; i++) {
        accel_offsets[i] = accel_sum[i] / samples;
        gyro_offsets[i] = gyro_sum[i] / samples;
    }
    
    // Set Z-axis offset to 1g (gravity)
    accel_offsets[2] -= 1.0f;
    
    ESP_LOGI(TAG, "IMU calibration complete");
}

void imu_reset_calibration(void) {
    memset(accel_offsets, 0, sizeof(accel_offsets));
    memset(gyro_offsets, 0, sizeof(gyro_offsets));
    tilt_calibration = 0.0f;
    ESP_LOGI(TAG, "IMU calibration reset");
}