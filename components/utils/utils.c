#include "utils.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void *malloc_dma(size_t size)
{
    return heap_caps_malloc(size, MALLOC_CAP_DMA);
}

void free_dma(void *ptr)
{
    heap_caps_free(ptr);
}

uint32_t get_time_ms(void)
{
    return esp_timer_get_time() / 1000;
}

void delay_ms(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

uint16_t crc16(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xFFFF;
    
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 0x0001) {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc = crc >> 1;
            }
        }
    }
    
    return crc;
}

uint32_t crc32(const uint8_t *data, size_t length)
{
    uint32_t crc = 0xFFFFFFFF;
    
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 0x00000001) {
                crc = (crc >> 1) ^ 0xEDB88320;
            } else {
                crc = crc >> 1;
            }
        }
    }
    
    return ~crc;
}

int str_to_int(const char *str)
{
    int result = 0;
    int sign = 1;
    
    if (*str == '-') {
        sign = -1;
        str++;
    }
    
    while (*str >= '0' && *str <= '9') {
        result = result * 10 + (*str - '0');
        str++;
    }
    
    return result * sign;
}

float str_to_float(const char *str)
{
    float result = 0.0f;
    float decimal = 0.0f;
    float divisor = 1.0f;
    int sign = 1;
    
    if (*str == '-') {
        sign = -1;
        str++;
    }
    
    // Parse integer part
    while (*str >= '0' && *str <= '9') {
        result = result * 10.0f + (*str - '0');
        str++;
    }
    
    // Parse decimal part
    if (*str == '.') {
        str++;
        while (*str >= '0' && *str <= '9') {
            decimal = decimal * 10.0f + (*str - '0');
            divisor *= 10.0f;
            str++;
        }
        result += decimal / divisor;
    }
    
    return result * sign;
}

uint32_t next_power_of_two(uint32_t x)
{
    x--;
    x |= x >> 1;
    x |= x >> 2;
    x |= x >> 4;
    x |= x >> 8;
    x |= x >> 16;
    return x + 1;
}

int clamp(int value, int min, int max)
{
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

float clampf(float value, float min, float max)
{
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

uint16_t swap16(uint16_t x)
{
    return (x << 8) | (x >> 8);
}

uint32_t swap32(uint32_t x)
{
    return ((x << 24) & 0xFF000000) |
           ((x << 8)  & 0x00FF0000) |
           ((x >> 8)  & 0x0000FF00) |
           ((x >> 24) & 0x000000FF);
}