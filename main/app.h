#ifndef _APP_H_
#define _APP_H_

#include "esp_err.h"

esp_err_t app_init(void);
void app_run(void);
void app_deinit(void);

#endif // _APP_H_