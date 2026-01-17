#ifndef MODULE_FREERTOS_H
#define MODULE_FREERTOS_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <Arduino.h>

#if CONFIG_FREERTOS_UNICORE
static const BaseType_t app_cpu = 0;
#else
static const BaseType_t app_cpu = 1;
#endif

#endif
