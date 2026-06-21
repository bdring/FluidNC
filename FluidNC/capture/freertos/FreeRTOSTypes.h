#pragma once

#include <mutex>

#define portMAX_DELAY (TickType_t)0xffffffffUL

using portBASE_TYPE = int;
using UBaseType_t   = unsigned int;
using BaseType_t    = portBASE_TYPE;
using TickType_t    = uint32_t;

typedef void (*TaskFunction_t)(void*);
typedef void* TaskHandle_t;
