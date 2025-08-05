#ifndef _KEYBOARD_H_
#define _KEYBOARD_H_

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "input.h"

esp_err_t keyboard_init(void);
void keyboard_deinit(void);
void keyboard_update(void);
bool keyboard_is_key_pressed(key_code_t key);

#endif // _KEYBOARD_H_