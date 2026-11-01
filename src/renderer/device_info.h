#pragma once
#include <stdbool.h>
#include <stdint.h>
#define AX_DEVICE_NAME_CAPACITY 256
typedef struct { uint32_t index; bool compatible; char name[AX_DEVICE_NAME_CAPACITY]; } AxDeviceInfo;
