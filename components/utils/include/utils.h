#ifndef _UTILS_H_
#define _UTILS_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Memory utilities
void *malloc_dma(size_t size);
void free_dma(void *ptr);

// Timing utilities
uint32_t get_time_ms(void);
void delay_ms(uint32_t ms);

// CRC utilities
uint16_t crc16(const uint8_t *data, size_t length);
uint32_t crc32(const uint8_t *data, size_t length);

// String utilities
int str_to_int(const char *str);
float str_to_float(const char *str);

// Math utilities
uint32_t next_power_of_two(uint32_t x);
int clamp(int value, int min, int max);
float clampf(float value, float min, float max);

// Byte swapping utilities
uint16_t swap16(uint16_t x);
uint32_t swap32(uint32_t x);

#endif // _UTILS_H_