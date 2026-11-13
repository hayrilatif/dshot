#include "usbd_cdc.h"
#include "usbd_desc.h"
#include "cdc_io.h"
#include "ring.h"

USBD_HandleTypeDef hUsbDeviceFS;

static uint8_t rx_packet[CDC_DATA_FS_MAX_PACKET_SIZE];
static uint8_t tx_packet[CDC_DATA_FS_MAX_PACKET_SIZE];
static uint8_t rx_mem[512];
static uint8_t tx_mem[4096];
static ring_t rx_ring;
static ring_t tx_ring;
static volatile bool dtr;
static uint8_t line_coding[7] = {0x00, 0xC2, 0x01, 0x00, 0x00, 0x00, 0x08}; /* 115200 8N1 */

static int8_t if_init(void)
{
    USBD_CDC_SetTxBuffer(&hUsbDeviceFS, tx_packet, 0);
    USBD_CDC_SetRxBuffer(&hUsbDeviceFS, rx_packet);
    return USBD_OK;
}

static int8_t if_deinit(void)
{
    dtr = false;
    return USBD_OK;
}

static int8_t if_control(uint8_t cmd, uint8_t *pbuf, uint16_t length)
{
    switch (cmd) {
    case CDC_SET_LINE_CODING:
        if (length >= sizeof line_coding) {
            memcpy(line_coding, pbuf, sizeof line_coding);
        }
        break;
    case CDC_GET_LINE_CODING:
        memcpy(pbuf, line_coding, sizeof line_coding);
        break;
    case CDC_SET_CONTROL_LINE_STATE: {
        const USBD_SetupReqTypedef *req = (const USBD_SetupReqTypedef *)pbuf;
        dtr = (req->wValue & 0x0001U) != 0U;
        break;
    }
    default:
        break;
    }
    return USBD_OK;
}

static int8_t if_receive(uint8_t *buf, uint32_t *len)
{
    ring_write(&rx_ring, buf, (uint16_t)*len);
    USBD_CDC_SetRxBuffer(&hUsbDeviceFS, rx_packet);
    USBD_CDC_ReceivePacket(&hUsbDeviceFS);
    return USBD_OK;
}

static USBD_CDC_ItfTypeDef cdc_fops = {if_init, if_deinit, if_control, if_receive};

void cdc_init(void)
{
    ring_init(&rx_ring, rx_mem, sizeof rx_mem);
    ring_init(&tx_ring, tx_mem, sizeof tx_mem);
    USBD_Init(&hUsbDeviceFS, &FS_Desc, DEVICE_FS);
    USBD_RegisterClass(&hUsbDeviceFS, &USBD_CDC);
    USBD_CDC_RegisterInterface(&hUsbDeviceFS, &cdc_fops);
    USBD_Start(&hUsbDeviceFS);
}

uint16_t cdc_read(uint8_t *buf, uint16_t max)
{
    return ring_read(&rx_ring, buf, max);
}

uint16_t cdc_write(const char *data, uint16_t len)
{
    return ring_write(&tx_ring, (const uint8_t *)data, len);
}

uint16_t cdc_write_wait(const char *data, uint16_t len, uint32_t timeout_ms)
{
    uint16_t done = 0;
    uint32_t start = HAL_GetTick();
    for (;;) {
        done = (uint16_t)(done + ring_write(&tx_ring, (const uint8_t *)data + done, (uint16_t)(len - done)));
        if (done == len || HAL_GetTick() - start >= timeout_ms) {
            return done;
        }
        cdc_flush();
    }
}

bool cdc_dtr(void)
{
    return dtr && hUsbDeviceFS.dev_state == USBD_STATE_CONFIGURED;
}

void cdc_flush(void)
{
    if (!cdc_dtr()) {
        /* Dinleyen yok: eski veriyi biriktirme */
        tx_ring.tail = tx_ring.head;
        return;
    }
    USBD_CDC_HandleTypeDef *h = (USBD_CDC_HandleTypeDef *)hUsbDeviceFS.pClassData;
    if (h == NULL || h->TxState != 0U) {
        return;
    }
    /* 64'ten küçük paket: ZLP gerekmez */
    uint16_t n = ring_read(&tx_ring, tx_packet, CDC_DATA_FS_MAX_PACKET_SIZE - 1U);
    if (n == 0U) {
        return;
    }
    USBD_CDC_SetTxBuffer(&hUsbDeviceFS, tx_packet, n);
    USBD_CDC_TransmitPacket(&hUsbDeviceFS);
}
