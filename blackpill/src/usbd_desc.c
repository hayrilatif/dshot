#include <stdio.h>
#include "usbd_core.h"
#include "usbd_desc.h"
#include "usbd_conf.h"
#include "board.h"

#define USBD_VID          0x0483
#define USBD_PID          0x5740
#define USBD_LANGID       0x0409
#define USBD_MANUFACTURER "dshot-tools"

static uint8_t *dev_desc_cb(USBD_SpeedTypeDef speed, uint16_t *length);
static uint8_t *langid_cb(USBD_SpeedTypeDef speed, uint16_t *length);
static uint8_t *mfr_cb(USBD_SpeedTypeDef speed, uint16_t *length);
static uint8_t *product_cb(USBD_SpeedTypeDef speed, uint16_t *length);
static uint8_t *serial_cb(USBD_SpeedTypeDef speed, uint16_t *length);
static uint8_t *config_cb(USBD_SpeedTypeDef speed, uint16_t *length);
static uint8_t *iface_cb(USBD_SpeedTypeDef speed, uint16_t *length);

USBD_DescriptorsTypeDef FS_Desc = {
    dev_desc_cb, langid_cb, mfr_cb, product_cb, serial_cb, config_cb, iface_cb,
};

__ALIGN_BEGIN static uint8_t dev_desc[USB_LEN_DEV_DESC] __ALIGN_END = {
    0x12, USB_DESC_TYPE_DEVICE, 0x00, 0x02,
    0x02, 0x02, 0x00, USB_MAX_EP0_SIZE,
    LOBYTE(USBD_VID), HIBYTE(USBD_VID), LOBYTE(USBD_PID), HIBYTE(USBD_PID),
    0x00, 0x02, USBD_IDX_MFC_STR, USBD_IDX_PRODUCT_STR, USBD_IDX_SERIAL_STR,
    USBD_MAX_NUM_CONFIGURATION,
};

__ALIGN_BEGIN static uint8_t langid_desc[USB_LEN_LANGID_STR_DESC] __ALIGN_END = {
    USB_LEN_LANGID_STR_DESC, USB_DESC_TYPE_STRING, LOBYTE(USBD_LANGID), HIBYTE(USBD_LANGID),
};

__ALIGN_BEGIN static uint8_t str_desc[USBD_MAX_STR_DESC_SIZ] __ALIGN_END;

static uint8_t *string_desc(const char *s, uint16_t *length)
{
    USBD_GetString((uint8_t *)s, str_desc, length);
    return str_desc;
}

static uint8_t *dev_desc_cb(USBD_SpeedTypeDef speed, uint16_t *length)
{
    (void)speed;
    *length = sizeof dev_desc;
    return dev_desc;
}

static uint8_t *langid_cb(USBD_SpeedTypeDef speed, uint16_t *length)
{
    (void)speed;
    *length = sizeof langid_desc;
    return langid_desc;
}

static uint8_t *mfr_cb(USBD_SpeedTypeDef speed, uint16_t *length)
{
    (void)speed;
    return string_desc(USBD_MANUFACTURER, length);
}

static uint8_t *product_cb(USBD_SpeedTypeDef speed, uint16_t *length)
{
    (void)speed;
    return string_desc(BOARD_USB_PRODUCT, length);
}

static uint8_t *serial_cb(USBD_SpeedTypeDef speed, uint16_t *length)
{
    (void)speed;
    char sn[25];
    snprintf(sn, sizeof sn, "%08lX%08lX%08lX", (unsigned long)HAL_GetUIDw0(), (unsigned long)HAL_GetUIDw1(),
             (unsigned long)HAL_GetUIDw2());
    return string_desc(sn, length);
}

static uint8_t *config_cb(USBD_SpeedTypeDef speed, uint16_t *length)
{
    (void)speed;
    return string_desc("CDC Config", length);
}

static uint8_t *iface_cb(USBD_SpeedTypeDef speed, uint16_t *length)
{
    (void)speed;
    return string_desc("CDC Interface", length);
}
