/**
 * @file    oled_panel.c
 * @brief   SSD1306 OLED 公共模块（I2C0 @ 0x3C）
 *
 * 写屏后执行 bus recover，避免长时间 soft-I2C 事务后 SCL/SDA 卡死影响 IMU/磁力计。
 */

#include "oled_panel.h"

#include "board.h"
#include "bsp_i2c.h"
#include "bsp_systick.h"
#include "log.h"
#include "oled.h"

#include <stddef.h>
#include <stdio.h>

#define OLED_PANEL_FONT_SIZE 16u

static bool_t s_ready;

static void oled_panel_bus_prepare(void)
{
    (void)bsp_i2c_init(&BOARD_I2C_CFG);
}

static void oled_panel_bus_release(void)
{
    bsp_i2c_bus_release_idle_high(BOARD_I2C_CFG.base);
    (void)bsp_i2c_init(&BOARD_I2C_CFG);
}

static int oled_panel_i2c_write(uint8_t addr, const uint8_t *data, uint16_t len)
{
    if ((data == NULL) || (len < 2u)) {
        return -1;
    }

    return bsp_i2c_write(BOARD_I2C_CFG.base, addr, data[0], &data[1], (size_t)(len - 1u)) ? 0 : -1;
}

static void oled_panel_delay_ms(uint32_t ms)
{
    bsp_delay_ms(ms);
}

static void oled_panel_format_time(char *buf, size_t buf_len, uint32_t elapsed_ms)
{
    uint32_t sec;
    uint32_t ms;

    if ((buf == NULL) || (buf_len == 0u)) {
        return;
    }

    sec = elapsed_ms / 1000u;
    ms = elapsed_ms % 1000u;
    (void)snprintf(buf, buf_len, "%lu.%03lu s", (unsigned long)sec, (unsigned long)ms);
}

status_t oled_panel_init(void)
{
    if (s_ready != FALSE) {
        return STATUS_OK;
    }

    oled_panel_bus_prepare();

    if (OLED_Register(oled_panel_i2c_write,
                      oled_panel_delay_ms,
                      OLED_PANEL_I2C_ADDR_DEFAULT,
                      OLED_PANEL_WIDTH,
                      OLED_PANEL_HEIGHT) != 0) {
        LOG_WARN("oled: register fail");
        oled_panel_bus_release();
        return STATUS_FAIL;
    }

    /* 仅初始化 + 清屏一次，不在控制环刷屏 */
    OLED_Init();
    OLED_ClearGram();
    OLED_Refresh();
    s_ready = TRUE;

    oled_panel_bus_release();

    LOG_INFO("oled: ready addr=0x%02X %ux%u",
             (unsigned)OLED_PANEL_I2C_ADDR_DEFAULT,
             (unsigned)OLED_PANEL_WIDTH,
             (unsigned)OLED_PANEL_HEIGHT);
    return STATUS_OK;
}

bool_t oled_panel_is_ready(void)
{
    return s_ready;
}

void oled_panel_show_lf_result(uint32_t elapsed_ms)
{
    char time_buf[16];

    if (s_ready == FALSE) {
        return;
    }

    oled_panel_format_time(time_buf, sizeof(time_buf), elapsed_ms);

    oled_panel_bus_prepare();
    OLED_ClearGram();
    OLED_ShowString(0u, 0u, (uint8_t *)"DONE", OLED_PANEL_FONT_SIZE);
    OLED_ShowString(0u, 24u, (uint8_t *)time_buf, OLED_PANEL_FONT_SIZE);
    OLED_Refresh();
    oled_panel_bus_release();
}
