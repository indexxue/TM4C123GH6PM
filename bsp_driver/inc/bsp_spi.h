/**
 * @file bsp_spi.h
 * @brief TM4C123 SPI 薄封装（硬件为 SSI）：主机收发 + 从机固定帧
 */

#ifndef BSP_DRIVER_SPI_H
#define BSP_DRIVER_SPI_H

#include "bsp_gpio.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Camera 协议固定帧长（全双工 32B） */
#define BSP_SPI_SLAVE_FRAME_LEN 32U

typedef enum {
    BSP_SPI_MODE_0 = 0,
    BSP_SPI_MODE_1,
    BSP_SPI_MODE_2,
    BSP_SPI_MODE_3,
} bsp_spi_mode_t;

typedef enum {
    BSP_SPI_ROLE_MASTER = 0,
    BSP_SPI_ROLE_SLAVE,
} bsp_spi_role_t;

typedef struct {
    uint32_t base;
    uint32_t clock_hz;
    bsp_spi_mode_t mode;
    uint8_t data_bits;
    bsp_spi_role_t role;
} bsp_spi_config_t;

typedef struct {
    const bsp_gpio_pin_t *cs_pin;
    bool cs_active_low;
} bsp_spi_cs_t;

/**
 * 从机一帧收齐后回调（可能在 ISR 上下文，须短且 FromISR 安全）。
 * @param rx 刚完成的 32B RX 缓冲（在下次帧完成前有效）
 * @param user 用户指针
 */
typedef void (*bsp_spi_slave_frame_cb_t)(const uint8_t *rx, void *user);

bool bsp_spi_init(const bsp_spi_config_t *cfg);
void bsp_spi_cs_set(const bsp_spi_cs_t *cs, bool selected);
bool bsp_spi_transfer(uint32_t base, uint8_t tx, uint8_t *rx);
bool bsp_spi_write(uint32_t base, const uint8_t *data, size_t len);
bool bsp_spi_read(uint32_t base, uint8_t *data, size_t len);
bool bsp_spi_transceive(uint32_t base, const bsp_spi_cs_t *cs,
                        const uint8_t *tx, uint8_t *rx, size_t len);

/**
 * 启动从机 32B 帧交换：ISR 维护 FIFO；须在 CS 前由 load_tx / initial_tx 装好应答。
 * @param initial_tx 首拍 TX（32B）；NULL 则填 0
 */
bool bsp_spi_slave_start(uint32_t base, const uint8_t *initial_tx,
                         bsp_spi_slave_frame_cb_t cb, void *user);

/** 装载下一拍 TX（拷贝 32B）；帧间隙生效，未更新则重复上一拍 */
bool bsp_spi_slave_load_tx(uint32_t base, const uint8_t *tx);

/** 拷贝最近一帧完整 RX；无新帧返回 false */
bool bsp_spi_slave_take_rx(uint32_t base, uint8_t *rx);

void bsp_spi_slave_stop(uint32_t base);

typedef struct {
    uint32_t isr_hits;
    uint32_t rx_bytes;
    uint32_t frames_done;
    uint32_t rxor_hits;
    uint32_t tx_underrun;
    uint32_t tx_puts;
    uint8_t rx_idx;
    uint8_t tx_idx;
} bsp_spi_slave_diag_t;

/** 联调诊断：是否进过 ISR / 收到过字节 / 是否 RX 溢出复位 */
bool bsp_spi_slave_get_diag(uint32_t base, bsp_spi_slave_diag_t *out);

#endif /* BSP_DRIVER_SPI_H */
