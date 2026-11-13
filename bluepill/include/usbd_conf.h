#ifndef USBD_CONF_H
#define USBD_CONF_H

#include <stdint.h>
#include <string.h>
#include "stm32f1xx_hal.h"

#define USBD_MAX_NUM_INTERFACES     1U
#define USBD_MAX_NUM_CONFIGURATION  1U
#define USBD_MAX_STR_DESC_SIZ       512U
#define USBD_DEBUG_LEVEL            0U
#define USBD_SELF_POWERED           1U
#define DEVICE_FS                   0

#define USBD_malloc  USBD_static_malloc
#define USBD_free    USBD_static_free
#define USBD_memset  memset
#define USBD_memcpy  memcpy
#define USBD_Delay   HAL_Delay

#define USBD_UsrLog(...) do { } while (0)
#define USBD_ErrLog(...) do { } while (0)
#define USBD_DbgLog(...) do { } while (0)

void *USBD_static_malloc(uint32_t size);
void USBD_static_free(void *p);

#endif
