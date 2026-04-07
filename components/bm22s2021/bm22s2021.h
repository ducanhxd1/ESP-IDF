/**
 * @file bm22s2021.h
 * @defgroup bm22s2021 bm22s2021
 * @{
 *
 * ESP-IDF driver for BM22S2021-1 Smoke Detector Digital Sensor
 * (BEST MODULES CORP.)
 *
 * Communication: UART @ 9600 baud, 4-byte command / variable response
 * Status pin: GPIO digital input — HIGH = alarm, LOW = normal
 */

#if !defined(__BM22S2021__H__)
#define __BM22S2021__H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <driver/uart.h>
#include <driver/gpio.h>
#include <esp_err.h>

/* ------------------------------------------------------------------ */
/*  Constants                                                           */
/* ------------------------------------------------------------------ */

#define BM22S2021_UART_BAUD         9600
#define BM22S2021_CMD_LEN           4       /*!< All commands are 4 bytes */
#define BM22S2021_ACK_SHORT_LEN     8       /*!< Short ACK frame length   */
#define BM22S2021_INFO_PKG_FULL     41      /*!< Full  auto-TX packet     */
#define BM22S2021_INFO_PKG_SHORT    21      /*!< Short auto-TX packet     */
#define BM22S2021_UART_BUF_SIZE     256     /*!< UART ring-buffer size    */

/* Return codes (mirrors Arduino lib) */
#define BM22S2021_OK                0
#define BM22S2021_CHECK_ERROR       1
#define BM22S2021_TIMEOUT_ERROR     2

/* Register addresses */
#define BM22S2021_REG_T0A_TOP       0x08
#define BM22S2021_REG_T0A_BOT       0x09
#define BM22S2021_REG_T0B_TOP       0x0A
#define BM22S2021_REG_T0B_BOT       0x0B
#define BM22S2021_REG_T0A_THR_L     0x10
#define BM22S2021_REG_T0A_THR_H     0x11
#define BM22S2021_REG_T0B_THR_L     0x12
#define BM22S2021_REG_T0B_THR_H     0x13
#define BM22S2021_REG_DETECT_CYCLE  0x2D
#define BM22S2021_REG_AUTO_TX       0x2E
#define BM22S2021_REG_STATUS_PIN    0x2F

/* Auto-TX mode values */
#define BM22S2021_AUTOTX_FULL       0x80    /*!< 41-byte detailed packet  */
#define BM22S2021_AUTOTX_SHORT      0x81    /*!< 21-byte simple  packet   */
#define BM22S2021_AUTOTX_OFF        0x00    /*!< No auto output           */

/* STATUS pin active-level values */
#define BM22S2021_STATUS_ACTIVE_HIGH  0x80
#define BM22S2021_STATUS_ACTIVE_LOW   0x00

/* ------------------------------------------------------------------ */
/*  Device handle                                                       */
/* ------------------------------------------------------------------ */

/**
 * @brief Device configuration — fill before calling bm22s2021_init().
 */
typedef struct {
    uart_port_t uart_port;      /*!< UART port number (UART_NUM_0/1/2)   */
    gpio_num_t  tx_pin;         /*!< UART TX GPIO                         */
    gpio_num_t  rx_pin;         /*!< UART RX GPIO                         */
    gpio_num_t  status_pin;     /*!< STATUS (alarm) GPIO — input          */
} bm22s2021_config_t;

/**
 * @brief Internal device state — treat as opaque; do not touch directly.
 */
typedef struct {
    bm22s2021_config_t cfg;
    uint8_t auto_tx_mode;       /*!< Cached AUTO_TX register value        */
    uint8_t rec_buf[41];        /*!< Last successfully validated packet   */
} bm22s2021_dev_t;

/* ------------------------------------------------------------------ */
/*  API                                                                 */
/* ------------------------------------------------------------------ */

/**
 * @brief Initialise UART and STATUS GPIO.
 *
 * Call once after filling a bm22s2021_config_t.
 *
 * @param dev    Pointer to an uninitialised device struct (caller owns it).
 * @param config Hardware configuration.
 * @return `ESP_OK` on success.
 */
esp_err_t bm22s2021_init(bm22s2021_dev_t *dev, const bm22s2021_config_t *config);

/**
 * @brief Free UART driver and release resources.
 *
 * @param dev  Device handle.
 * @return `ESP_OK` on success.
 */
esp_err_t bm22s2021_deinit(bm22s2021_dev_t *dev);

/* -- Status pin ---------------------------------------------------- */

/**
 * @brief Read the STATUS (alarm) pin level.
 *
 * @param dev  Device handle.
 * @return 0 = LOW (normal), 1 = HIGH (alarm — depends on active-level setting).
 */
int bm22s2021_get_status_pin(bm22s2021_dev_t *dev);

/* -- Info & Identity ----------------------------------------------- */

/**
 * @brief Query firmware version (8421 BCD, 16-bit).
 *
 * @param dev  Device handle.
 * @param[out] fw_ver  Firmware version word; 0 on error.
 * @return `ESP_OK` on success.
 */
esp_err_t bm22s2021_get_fw_ver(bm22s2021_dev_t *dev, uint16_t *fw_ver);

/**
 * @brief Query production date (8421 BCD).
 *
 * @param dev    Device handle.
 * @param[out] year   BCD year byte.
 * @param[out] month  BCD month byte.
 * @param[out] day    BCD day byte.
 * @return `ESP_OK` on success.
 */
esp_err_t bm22s2021_get_prod_date(bm22s2021_dev_t *dev,
                                   uint8_t *year,
                                   uint8_t *month,
                                   uint8_t *day);

/* -- Poll-mode data packet ----------------------------------------- */

/**
 * @brief Request a full info packet from the module (poll mode).
 *
 * Sends command 0xAC and blocks until the 41-byte response arrives.
 *
 * @param dev      Device handle.
 * @param[out] buf Buffer of at least 41 bytes to receive data.
 * @return `ESP_OK` on success.
 */
esp_err_t bm22s2021_request_info_package(bm22s2021_dev_t *dev, uint8_t buf[41]);

/* -- Auto-TX mode (interrupt-style) -------------------------------- */

/**
 * @brief Check whether a valid auto-TX packet is waiting in the UART buffer.
 *
 * If true, call bm22s2021_read_info_package() immediately to copy data out.
 *
 * @param dev  Device handle.
 * @return true if a valid, checksum-correct packet was found.
 */
bool bm22s2021_is_info_available(bm22s2021_dev_t *dev);

/**
 * @brief Copy the last validated auto-TX packet into caller's buffer.
 *
 * Must be called right after bm22s2021_is_info_available() returned true.
 *
 * @param dev      Device handle.
 * @param[out] buf Buffer of at least 41 bytes.
 */
void bm22s2021_read_info_package(bm22s2021_dev_t *dev, uint8_t buf[41]);

/* -- Register access ----------------------------------------------- */

/**
 * @brief Read one configuration register.
 *
 * @param dev       Device handle.
 * @param addr      Register address (BM22S2021_REG_*).
 * @param[out] data Register value.
 * @return `ESP_OK` on success.
 */
esp_err_t bm22s2021_read_register(bm22s2021_dev_t *dev, uint8_t addr, uint8_t *data);

/**
 * @brief Write one configuration register.
 *
 * @param dev   Device handle.
 * @param addr  Register address (BM22S2021_REG_*).
 * @param data  Value to write.
 * @return `ESP_OK` on success.
 */
esp_err_t bm22s2021_write_register(bm22s2021_dev_t *dev, uint8_t addr, uint8_t data);

/**
 * @brief Read one running-variable (live sensor value) register.
 *
 * @param dev       Device handle.
 * @param addr      Running-variable address (0x90 … 0xB2 range).
 * @param[out] data Register value.
 * @return `ESP_OK` on success.
 */
esp_err_t bm22s2021_read_running_var(bm22s2021_dev_t *dev, uint8_t addr, uint8_t *data);

/* -- Configuration getters ----------------------------------------- */

esp_err_t bm22s2021_get_auto_tx(bm22s2021_dev_t *dev, uint8_t *mode);
esp_err_t bm22s2021_get_status_pin_mode(bm22s2021_dev_t *dev, uint8_t *level);
esp_err_t bm22s2021_get_t0a_top_limit(bm22s2021_dev_t *dev, uint8_t *val);
esp_err_t bm22s2021_get_t0a_bot_limit(bm22s2021_dev_t *dev, uint8_t *val);
esp_err_t bm22s2021_get_t0b_top_limit(bm22s2021_dev_t *dev, uint8_t *val);
esp_err_t bm22s2021_get_t0b_bot_limit(bm22s2021_dev_t *dev, uint8_t *val);
esp_err_t bm22s2021_get_t0a_threshold(bm22s2021_dev_t *dev, uint16_t *thr);
esp_err_t bm22s2021_get_t0b_threshold(bm22s2021_dev_t *dev, uint16_t *thr);
esp_err_t bm22s2021_get_detect_cycle(bm22s2021_dev_t *dev, uint8_t *cycle);

/* -- Configuration setters ----------------------------------------- */

esp_err_t bm22s2021_set_auto_tx(bm22s2021_dev_t *dev, uint8_t mode);
esp_err_t bm22s2021_set_status_pin_mode(bm22s2021_dev_t *dev, uint8_t level);
esp_err_t bm22s2021_set_t0a_calibrate_range(bm22s2021_dev_t *dev, uint8_t top, uint8_t bot);
esp_err_t bm22s2021_set_t0b_calibrate_range(bm22s2021_dev_t *dev, uint8_t top, uint8_t bot);
esp_err_t bm22s2021_set_t0a_threshold(bm22s2021_dev_t *dev, uint16_t value);
esp_err_t bm22s2021_set_t0b_threshold(bm22s2021_dev_t *dev, uint16_t value);
esp_err_t bm22s2021_set_detect_cycle(bm22s2021_dev_t *dev, uint8_t cycle);

/* -- Module commands ----------------------------------------------- */

/**
 * @brief Trigger air calibration (~8 s blocking).
 *
 * @param dev  Device handle.
 * @return `ESP_OK` on success (0xA0 success byte received).
 */
esp_err_t bm22s2021_calibrate(bm22s2021_dev_t *dev);

/**
 * @brief Reset the sensor chip.
 *
 * @param dev  Device handle.
 * @return `ESP_OK` on success.
 */
esp_err_t bm22s2021_reset(bm22s2021_dev_t *dev);

/**
 * @brief Restore factory defaults.
 *
 * @param dev  Device handle.
 * @return `ESP_OK` on success.
 */
esp_err_t bm22s2021_restore_default(bm22s2021_dev_t *dev);

#ifdef __cplusplus
}
#endif

/**@}*/

#endif /* __BM22S2021__H__ */