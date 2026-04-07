/*
 * ESP-IDF component for BM22S2021-1 Smoke Detector (BEST MODULES CORP.)
 * Ported from the official Arduino library v1.0.2.
 *
 * Protocol summary
 * ----------------
 * All commands: 4 bytes  [CMD, DATA_H, DATA_L, CHK]
 * Short ACK   : 8 bytes  [0xAA, 0x08, 0x11, 0x01, CMD_ECHO, 0x00, DATA, CHK]
 * Info packet : 41 bytes [0xAA, 0x29, 0x11, 0x01, 0xAC, ... , CHK]
 *
 * Checksum    : two's complement of the sum of all preceding bytes
 *               checksum = (~sum_of_bytes) + 1   (8-bit arithmetic)
 */

#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <esp_err.h>
#include <esp_log.h>
#include <driver/uart.h>
#include <driver/gpio.h>

#include "bm22s2021.h"

/* ------------------------------------------------------------------ */
/*  Private                                                             */
/* ------------------------------------------------------------------ */

static const char *TAG = "bm22s2021";

/* Timeout waiting for a single UART byte, in FreeRTOS ticks */
#define BYTE_TIMEOUT_TICKS  pdMS_TO_TICKS(50)

/* How long to wait for the module to process a command (ms) */
#define CMD_DELAY_MS        50
#define RESTORE_DELAY_MS    350
#define CALIBRATE_STEP_MS   1000

/* ------------------------------------------------------------------ */
/*  Internal helpers                                                    */
/* ------------------------------------------------------------------ */

/**
 * @brief Compute 8-bit two's-complement checksum of `len` bytes.
 */
static uint8_t checksum_calc(const uint8_t *data, size_t len)
{
    uint8_t sum = 0;
    for (size_t i = 0; i < len; i++)
    {
        sum += data[i];
    }
    return (uint8_t)((~sum) + 1);
}

// /**
//  * @brief Flush the UART receive buffer.
//  */
// static void uart_flush(uart_port_t port)
// {
//     uart_flush_input(port);
// }

/**
 * @brief Write `len` bytes to the module.
 */
static void bm22s2021_write_bytes(bm22s2021_dev_t *dev,
                                   const uint8_t *buf,
                                   size_t len)
{
    uart_flush(dev->cfg.uart_port);
    uart_write_bytes(dev->cfg.uart_port, (const char *)buf, len);
}

/**
 * @brief Read exactly `rlen` bytes from UART with per-byte timeout.
 *
 * @return BM22S2021_OK, BM22S2021_TIMEOUT_ERROR, or BM22S2021_CHECK_ERROR.
 */
static uint8_t bm22s2021_read_bytes(bm22s2021_dev_t *dev,
                                     uint8_t *rbuf,
                                     size_t rlen,
                                     uint32_t timeout_ms)
{
    TickType_t ticks = pdMS_TO_TICKS(timeout_ms);

    for (size_t i = 0; i < rlen; i++)
    {
        int ret = uart_read_bytes(dev->cfg.uart_port,
                                  &rbuf[i], 1,
                                  ticks);
        if (ret <= 0)
        {
            ESP_LOGW(TAG, "read_bytes: timeout at byte %d", (int)i);
            return BM22S2021_TIMEOUT_ERROR;
        }
    }

    /* Validate checksum */
    uint8_t chk = checksum_calc(rbuf, rlen - 1);
    if (chk != rbuf[rlen - 1])
    {
        ESP_LOGW(TAG, "read_bytes: checksum error (got 0x%02X, want 0x%02X)",
                 rbuf[rlen - 1], chk);
        return BM22S2021_CHECK_ERROR;
    }

    return BM22S2021_OK;
}

/**
 * @brief Send a 4-byte command and read a short (8-byte) ACK.
 *
 * @param cmd        4-byte command buffer (caller fills first 3; CHK auto-appended).
 * @param ack        8-byte buffer for the ACK frame.
 * @param echo_byte  Expected CMD echo byte in ack[4].
 * @return ESP_OK or ESP_FAIL.
 */
static esp_err_t bm22s2021_cmd_short(bm22s2021_dev_t *dev,
                                      uint8_t cmd[4],
                                      uint8_t ack[8],
                                      uint8_t echo_byte)
{
    uart_flush(dev->cfg.uart_port);
    bm22s2021_write_bytes(dev, cmd, BM22S2021_CMD_LEN);
    vTaskDelay(pdMS_TO_TICKS(CMD_DELAY_MS));

    uint8_t ret = bm22s2021_read_bytes(dev, ack,
                                        BM22S2021_ACK_SHORT_LEN,
                                        CMD_DELAY_MS);
    if (ret != BM22S2021_OK)
    {
        return ESP_FAIL;
    }
    if (ack[4] != echo_byte)
    {
        ESP_LOGW(TAG, "cmd_short: echo mismatch (0x%02X != 0x%02X)",
                 ack[4], echo_byte);
        return ESP_FAIL;
    }
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/*  Initialisation                                                      */
/* ------------------------------------------------------------------ */

esp_err_t bm22s2021_init(bm22s2021_dev_t *dev, const bm22s2021_config_t *config)
{
    if (!dev || !config)
    {
        return ESP_ERR_INVALID_ARG;
    }

    memset(dev, 0, sizeof(*dev));
    dev->cfg          = *config;
    dev->auto_tx_mode = BM22S2021_AUTOTX_FULL; // default after power-on

    /* Configure UART */
    uart_config_t uart_cfg = {
        .baud_rate  = BM22S2021_UART_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t err;

    err = uart_param_config(config->uart_port, &uart_cfg);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "uart_param_config failed: %s", esp_err_to_name(err));
        return err;
    }

    err = uart_set_pin(config->uart_port,
                       config->tx_pin,
                       config->rx_pin,
                       UART_PIN_NO_CHANGE,
                       UART_PIN_NO_CHANGE);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "uart_set_pin failed: %s", esp_err_to_name(err));
        return err;
    }

    err = uart_driver_install(config->uart_port,
                              BM22S2021_UART_BUF_SIZE * 2,
                              0, 0, NULL, 0);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "uart_driver_install failed: %s", esp_err_to_name(err));
        return err;
    }

    /* Configure STATUS GPIO as input */
    gpio_config_t io_cfg = {
        .pin_bit_mask = (1ULL << config->status_pin),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    err = gpio_config(&io_cfg);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "gpio_config failed: %s", esp_err_to_name(err));
        uart_driver_delete(config->uart_port);
        return err;
    }

    ESP_LOGI(TAG, "init OK — UART%d tx=%d rx=%d status=%d",
             config->uart_port,
             config->tx_pin,
             config->rx_pin,
             config->status_pin);

    return ESP_OK;
}

esp_err_t bm22s2021_deinit(bm22s2021_dev_t *dev)
{
    if (!dev)
    {
        return ESP_ERR_INVALID_ARG;
    }
    return uart_driver_delete(dev->cfg.uart_port);
}

/* ------------------------------------------------------------------ */
/*  Status pin                                                          */
/* ------------------------------------------------------------------ */

int bm22s2021_get_status_pin(bm22s2021_dev_t *dev)
{
    return gpio_get_level(dev->cfg.status_pin);
}

/* ------------------------------------------------------------------ */
/*  Firmware version / production date                                  */
/* ------------------------------------------------------------------ */

esp_err_t bm22s2021_get_fw_ver(bm22s2021_dev_t *dev, uint16_t *fw_ver)
{
    if (!dev || !fw_ver)
    {
        return ESP_ERR_INVALID_ARG;
    }

    uart_flush(dev->cfg.uart_port);
    uint8_t cmd[4] = {0xAD, 0x00, 0x00, 0x53};
    uint8_t ack[12];

    bm22s2021_write_bytes(dev, cmd, BM22S2021_CMD_LEN);
    vTaskDelay(pdMS_TO_TICKS(CMD_DELAY_MS));

    uint8_t ret = bm22s2021_read_bytes(dev, ack, 12, CMD_DELAY_MS);
    if (ret != BM22S2021_OK || ack[4] != 0xAD)
    {
        *fw_ver = 0;
        return ESP_FAIL;
    }

    *fw_ver = (uint16_t)((ack[6] << 8) | ack[7]);
    return ESP_OK;
}

esp_err_t bm22s2021_get_prod_date(bm22s2021_dev_t *dev,
                                   uint8_t *year,
                                   uint8_t *month,
                                   uint8_t *day)
{
    if (!dev || !year || !month || !day)
    {
        return ESP_ERR_INVALID_ARG;
    }

    uart_flush(dev->cfg.uart_port);
    uint8_t cmd[4] = {0xAD, 0x00, 0x00, 0x53};
    uint8_t ack[12];

    bm22s2021_write_bytes(dev, cmd, BM22S2021_CMD_LEN);
    vTaskDelay(pdMS_TO_TICKS(CMD_DELAY_MS));

    uint8_t ret = bm22s2021_read_bytes(dev, ack, 12, CMD_DELAY_MS);
    if (ret != BM22S2021_OK || ack[4] != 0xAD)
    {
        return ESP_FAIL;
    }

    *year  = ack[8];
    *month = ack[9];
    *day   = ack[10];
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/*  Poll-mode info packet                                               */
/* ------------------------------------------------------------------ */

esp_err_t bm22s2021_request_info_package(bm22s2021_dev_t *dev, uint8_t buf[41])
{
    if (!dev || !buf)
    {
        return ESP_ERR_INVALID_ARG;
    }

    uart_flush(dev->cfg.uart_port);
    uint8_t cmd[4] = {0xAC, 0x00, 0x00, 0x54};

    bm22s2021_write_bytes(dev, cmd, BM22S2021_CMD_LEN);
    vTaskDelay(pdMS_TO_TICKS(CMD_DELAY_MS));

    uint8_t ret = bm22s2021_read_bytes(dev, buf,
                                        BM22S2021_INFO_PKG_FULL,
                                        CMD_DELAY_MS);
    if (ret != BM22S2021_OK || buf[4] != 0xAC)
    {
        return ESP_FAIL;
    }
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/*  Auto-TX (interrupt-style) packet reception                          */
/* ------------------------------------------------------------------ */

bool bm22s2021_is_info_available(bm22s2021_dev_t *dev)
{
    uint8_t rec_len = (dev->auto_tx_mode == BM22S2021_AUTOTX_SHORT)
                      ? BM22S2021_INFO_PKG_SHORT
                      : BM22S2021_INFO_PKG_FULL;

    /* Expected 5-byte header */
    uint8_t header[5] = {0xAA, rec_len, 0x11, 0x01, 0xAC};
    uint8_t rec_buf[BM22S2021_INFO_PKG_FULL];

    /* Check if enough bytes are waiting */
    size_t available = 0;
    uart_get_buffered_data_len(dev->cfg.uart_port, &available);
    if (available < rec_len)
    {
        return false;
    }

    /* Try (up to 2 mismatches) to find a valid header + packet */
    uint8_t fail_cnt  = 0;
    uint8_t read_cnt  = 0;
    bool    is_header = false;
    bool    result    = false;

    while (fail_cnt < 2)
    {
        /* Search for the 5-byte header */
        is_header = false;
        for (uint8_t i = 0; i < 5; )
        {
            uint8_t byte_val = 0;
            int n = uart_read_bytes(dev->cfg.uart_port,
                                    &byte_val, 1,
                                    BYTE_TIMEOUT_TICKS);
            if (n <= 0)
            {
                break; // no more data
            }
            rec_buf[i] = byte_val;

            if (rec_buf[i] == header[i])
            {
                is_header = true;
                i++;
            }
            else if (rec_buf[i] != header[i] && i > 0)
            {
                is_header = false;
                fail_cnt++;
                break;
            }
            else // i == 0 and byte doesn't match
            {
                read_cnt++;
                if (read_cnt >= (available - 5))
                {
                    read_cnt = 0;
                    break;
                }
            }
        }

        if (!is_header)
        {
            continue;
        }

        /* Read remaining bytes */
        uint8_t chk_acc = 0;
        for (uint8_t i = 0; i < 5; i++)
        {
            chk_acc += rec_buf[i];
        }

        for (uint8_t i = 5; i < rec_len; i++)
        {
            uint8_t b = 0;
            uart_read_bytes(dev->cfg.uart_port, &b, 1, BYTE_TIMEOUT_TICKS);
            rec_buf[i] = b;
            chk_acc += b;
        }

        /* Two's-complement checksum check */
        chk_acc = chk_acc - rec_buf[rec_len - 1];
        chk_acc = (uint8_t)((~chk_acc) + 1);

        if (chk_acc == rec_buf[rec_len - 1])
        {
            memcpy(dev->rec_buf, rec_buf, rec_len);
            result = true;
            break;
        }
        else
        {
            fail_cnt++;
        }
    }

    uart_flush(dev->cfg.uart_port);
    return result;
}

void bm22s2021_read_info_package(bm22s2021_dev_t *dev, uint8_t buf[41])
{
    uint8_t rec_len = (dev->auto_tx_mode == BM22S2021_AUTOTX_SHORT)
                      ? BM22S2021_INFO_PKG_SHORT
                      : BM22S2021_INFO_PKG_FULL;
    memcpy(buf, dev->rec_buf, rec_len);
}

/* ------------------------------------------------------------------ */
/*  Register read / write                                               */
/* ------------------------------------------------------------------ */

esp_err_t bm22s2021_read_register(bm22s2021_dev_t *dev, uint8_t addr, uint8_t *data)
{
    if (!dev || !data)
    {
        return ESP_ERR_INVALID_ARG;
    }

    uart_flush(dev->cfg.uart_port);

    /* Build command with checksum */
    uint8_t cmd[4] = {0xD0, addr, 0x00, 0x00};
    uint16_t sum   = 0xD0 + addr + 0x00;
    cmd[3]         = (uint8_t)((~(sum & 0xFF)) + 1);

    uint8_t ack[8];
    esp_err_t err = bm22s2021_cmd_short(dev, cmd, ack, 0xD0);
    if (err != ESP_OK)
    {
        *data = 0;
        return ESP_FAIL;
    }

    *data = ack[6];
    return ESP_OK;
}

esp_err_t bm22s2021_write_register(bm22s2021_dev_t *dev, uint8_t addr, uint8_t val)
{
    if (!dev)
    {
        return ESP_ERR_INVALID_ARG;
    }

    uart_flush(dev->cfg.uart_port);

    uint8_t cmd[4]  = {0xE0, addr, val, 0x00};
    uint16_t sum    = 0xE0 + addr + val;
    cmd[3]          = (uint8_t)((~(sum & 0xFF)) + 1);

    /* Update cached auto_tx_mode when writing its register */
    if (addr == BM22S2021_REG_AUTO_TX)
    {
        dev->auto_tx_mode = val;
    }

    uint8_t ack[8];
    return bm22s2021_cmd_short(dev, cmd, ack, 0xE0);
}

esp_err_t bm22s2021_read_running_var(bm22s2021_dev_t *dev,
                                      uint8_t addr,
                                      uint8_t *data)
{
    if (!dev || !data)
    {
        return ESP_ERR_INVALID_ARG;
    }

    uart_flush(dev->cfg.uart_port);

    uint8_t cmd[4] = {0xD2, addr, 0x00, 0x00};
    uint16_t sum   = 0xD2 + addr + 0x00;
    cmd[3]         = (uint8_t)((~(sum & 0xFF)) + 1);

    bm22s2021_write_bytes(dev, cmd, BM22S2021_CMD_LEN);
    vTaskDelay(pdMS_TO_TICKS(CMD_DELAY_MS));

    uint8_t ack[8];
    uint8_t ret = bm22s2021_read_bytes(dev, ack, BM22S2021_ACK_SHORT_LEN, CMD_DELAY_MS);
    if (ret != BM22S2021_OK || ack[4] != 0xD2)
    {
        *data = 0;
        return ESP_FAIL;
    }

    *data = ack[6];
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/*  Configuration getters                                               */
/* ------------------------------------------------------------------ */

esp_err_t bm22s2021_get_auto_tx(bm22s2021_dev_t *dev, uint8_t *mode)
{
    return bm22s2021_read_register(dev, BM22S2021_REG_AUTO_TX, mode);
}

esp_err_t bm22s2021_get_status_pin_mode(bm22s2021_dev_t *dev, uint8_t *level)
{
    return bm22s2021_read_register(dev, BM22S2021_REG_STATUS_PIN, level);
}

esp_err_t bm22s2021_get_t0a_top_limit(bm22s2021_dev_t *dev, uint8_t *val)
{
    return bm22s2021_read_register(dev, BM22S2021_REG_T0A_TOP, val);
}

esp_err_t bm22s2021_get_t0a_bot_limit(bm22s2021_dev_t *dev, uint8_t *val)
{
    return bm22s2021_read_register(dev, BM22S2021_REG_T0A_BOT, val);
}

esp_err_t bm22s2021_get_t0b_top_limit(bm22s2021_dev_t *dev, uint8_t *val)
{
    return bm22s2021_read_register(dev, BM22S2021_REG_T0B_TOP, val);
}

esp_err_t bm22s2021_get_t0b_bot_limit(bm22s2021_dev_t *dev, uint8_t *val)
{
    return bm22s2021_read_register(dev, BM22S2021_REG_T0B_BOT, val);
}

esp_err_t bm22s2021_get_t0a_threshold(bm22s2021_dev_t *dev, uint16_t *thr)
{
    if (!dev || !thr)
    {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t lo, hi;
    esp_err_t err;

    err = bm22s2021_read_register(dev, BM22S2021_REG_T0A_THR_L, &lo);
    if (err != ESP_OK)
    {
        return err;
    }
    err = bm22s2021_read_register(dev, BM22S2021_REG_T0A_THR_H, &hi);
    if (err != ESP_OK)
    {
        return err;
    }

    *thr = (uint16_t)((hi << 8) | lo);
    return ESP_OK;
}

esp_err_t bm22s2021_get_t0b_threshold(bm22s2021_dev_t *dev, uint16_t *thr)
{
    if (!dev || !thr)
    {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t lo, hi;
    esp_err_t err;

    err = bm22s2021_read_register(dev, BM22S2021_REG_T0B_THR_L, &lo);
    if (err != ESP_OK)
    {
        return err;
    }
    err = bm22s2021_read_register(dev, BM22S2021_REG_T0B_THR_H, &hi);
    if (err != ESP_OK)
    {
        return err;
    }

    *thr = (uint16_t)((hi << 8) | lo);
    return ESP_OK;
}

esp_err_t bm22s2021_get_detect_cycle(bm22s2021_dev_t *dev, uint8_t *cycle)
{
    return bm22s2021_read_register(dev, BM22S2021_REG_DETECT_CYCLE, cycle);
}

/* ------------------------------------------------------------------ */
/*  Configuration setters                                               */
/* ------------------------------------------------------------------ */

esp_err_t bm22s2021_set_auto_tx(bm22s2021_dev_t *dev, uint8_t mode)
{
    esp_err_t err = bm22s2021_write_register(dev, BM22S2021_REG_AUTO_TX, mode);
    if (err == ESP_OK)
    {
        dev->auto_tx_mode = mode;
    }
    return err;
}

esp_err_t bm22s2021_set_status_pin_mode(bm22s2021_dev_t *dev, uint8_t level)
{
    return bm22s2021_write_register(dev, BM22S2021_REG_STATUS_PIN, level);
}

esp_err_t bm22s2021_set_t0a_calibrate_range(bm22s2021_dev_t *dev,
                                              uint8_t top,
                                              uint8_t bot)
{
    esp_err_t err = bm22s2021_write_register(dev, BM22S2021_REG_T0A_TOP, top);
    if (err != ESP_OK)
    {
        return err;
    }
    return bm22s2021_write_register(dev, BM22S2021_REG_T0A_BOT, bot);
}

esp_err_t bm22s2021_set_t0b_calibrate_range(bm22s2021_dev_t *dev,
                                              uint8_t top,
                                              uint8_t bot)
{
    esp_err_t err = bm22s2021_write_register(dev, BM22S2021_REG_T0B_TOP, top);
    if (err != ESP_OK)
    {
        return err;
    }
    return bm22s2021_write_register(dev, BM22S2021_REG_T0B_BOT, bot);
}

esp_err_t bm22s2021_set_t0a_threshold(bm22s2021_dev_t *dev, uint16_t value)
{
    esp_err_t err = bm22s2021_write_register(dev, BM22S2021_REG_T0A_THR_L,
                                              (uint8_t)(value & 0xFF));
    if (err != ESP_OK)
    {
        return err;
    }
    return bm22s2021_write_register(dev, BM22S2021_REG_T0A_THR_H,
                                    (uint8_t)(value >> 8));
}

esp_err_t bm22s2021_set_t0b_threshold(bm22s2021_dev_t *dev, uint16_t value)
{
    esp_err_t err = bm22s2021_write_register(dev, BM22S2021_REG_T0B_THR_L,
                                              (uint8_t)(value & 0xFF));
    if (err != ESP_OK)
    {
        return err;
    }
    return bm22s2021_write_register(dev, BM22S2021_REG_T0B_THR_H,
                                    (uint8_t)(value >> 8));
}

esp_err_t bm22s2021_set_detect_cycle(bm22s2021_dev_t *dev, uint8_t cycle)
{
    return bm22s2021_write_register(dev, BM22S2021_REG_DETECT_CYCLE, cycle);
}

/* ------------------------------------------------------------------ */
/*  Module commands                                                     */
/* ------------------------------------------------------------------ */

esp_err_t bm22s2021_calibrate(bm22s2021_dev_t *dev)
{
    if (!dev)
    {
        return ESP_ERR_INVALID_ARG;
    }

    uart_flush(dev->cfg.uart_port);
    uint8_t cmd[4] = {0xAB, 0x00, 0x00, 0x55};
    uint8_t ack[8];
    uint8_t ret = BM22S2021_TIMEOUT_ERROR;

    bm22s2021_write_bytes(dev, cmd, BM22S2021_CMD_LEN);
    vTaskDelay(pdMS_TO_TICKS(CMD_DELAY_MS));

    /* Calibration takes ~8 s — poll 9 times (last one = final result) */
    for (uint8_t c = 0; c < 9; c++)
    {
        ret = bm22s2021_read_bytes(dev, ack, BM22S2021_ACK_SHORT_LEN, CMD_DELAY_MS);
        if (c < 8)
        {
            vTaskDelay(pdMS_TO_TICKS(CALIBRATE_STEP_MS));
        }
    }

    if (ret == BM22S2021_OK && ack[6] == 0xA0)
    {
        ESP_LOGI(TAG, "calibration OK");
        return ESP_OK;
    }

    ESP_LOGW(TAG, "calibration failed (ret=%d ack[6]=0x%02X)", ret, ack[6]);
    return ESP_FAIL;
}

esp_err_t bm22s2021_reset(bm22s2021_dev_t *dev)
{
    if (!dev)
    {
        return ESP_ERR_INVALID_ARG;
    }

    uart_flush(dev->cfg.uart_port);
    uint8_t cmd[4] = {0xAF, 0x00, 0x00, 0x51};
    uint8_t ack[8];
    return bm22s2021_cmd_short(dev, cmd, ack, 0xAF);
}

esp_err_t bm22s2021_restore_default(bm22s2021_dev_t *dev)
{
    if (!dev)
    {
        return ESP_ERR_INVALID_ARG;
    }

    uart_flush(dev->cfg.uart_port);
    uint8_t cmd[4] = {0xA0, 0x00, 0x00, 0x60};
    uint8_t ack[8];

    bm22s2021_write_bytes(dev, cmd, BM22S2021_CMD_LEN);
    vTaskDelay(pdMS_TO_TICKS(RESTORE_DELAY_MS)); // module needs ~350 ms

    uint8_t ret = bm22s2021_read_bytes(dev, ack, BM22S2021_ACK_SHORT_LEN, CMD_DELAY_MS);
    if (ret != BM22S2021_OK || ack[4] != 0xA0)
    {
        return ESP_FAIL;
    }
    return ESP_OK;
}