/**
 * @file    lcd_panel.c
 * @brief   ST7789 @ BOARD_SPI_CFG + PA3 CS / PF0 RST / PF1 DC / PF2 BL
 */

#include "lcd_panel.h"

#include "board.h"
#include "log.h"
#include "nvs.h"

#include "bsp_spi.h"
#include "bsp_systick.h"
#include "lcd.h"
#include "st7789.h"

#include "driverlib/gpio.h"

static st7789_t s_lcd;
static bool_t s_ready;

static void lcd_pin_cs(int high)
{
    GPIOPinWrite(GPIO_LCD_CS_PORT, GPIO_LCD_CS_MASK, high ? GPIO_LCD_CS_MASK : 0U);
}

static void lcd_pin_dc(int high)
{
    GPIOPinWrite(GPIO_LCD_DC_PORT, GPIO_LCD_DC_MASK, high ? GPIO_LCD_DC_MASK : 0U);
}

static void lcd_pin_rst(int high)
{
    GPIOPinWrite(GPIO_LCD_RST_PORT, GPIO_LCD_RST_MASK, high ? GPIO_LCD_RST_MASK : 0U);
}

static void lcd_pin_bl(int high)
{
    GPIOPinWrite(GPIO_LCD_BL_PORT, GPIO_LCD_BL_MASK, high ? GPIO_LCD_BL_MASK : 0U);
}

static void lcd_spi_tx(const uint8_t *data, uint16_t len)
{
    if ((data == NULL) || (len == 0U)) {
        return;
    }
    (void)bsp_spi_write(BOARD_SPI_CFG.base, data, (size_t)len);
}

static void lcd_delay_ms(uint32_t ms)
{
    bsp_delay_ms(ms);
}

static void lcd_gpio_prepare(void)
{
    GPIOPinTypeGPIOOutput(GPIO_LCD_CS_PORT, GPIO_LCD_CS_MASK);
    GPIOPinTypeGPIOOutput(GPIO_LCD_RST_PORT, GPIO_LCD_RST_MASK);
    GPIOPinTypeGPIOOutput(GPIO_LCD_DC_PORT, GPIO_LCD_DC_MASK);
    GPIOPinTypeGPIOOutput(GPIO_LCD_BL_PORT, GPIO_LCD_BL_MASK);
    lcd_pin_cs(1);
    lcd_pin_rst(1);
    lcd_pin_dc(0);
    lcd_pin_bl(0);
}

status_t lcd_panel_init(void)
{
    st7789_config_t cfg;

    if (s_ready != FALSE) {
        return STATUS_OK;
    }

    lcd_gpio_prepare();

    if (!bsp_spi_init(&BOARD_SPI_CFG)) {
        LOG_WARN("lcd: spi init fail");
        return STATUS_FAIL;
    }

    cfg.spi_tx = lcd_spi_tx;
    cfg.set_cs = lcd_pin_cs;
    cfg.set_dc = lcd_pin_dc;
    cfg.set_rst = lcd_pin_rst;
    cfg.set_bl = lcd_pin_bl;
    cfg.delay_ms = lcd_delay_ms;
    cfg.rotation = (uint8_t)ST7789_ROT_LANDSCAPE;

    if (st7789_register(&s_lcd, &cfg) != ST7789_OK) {
        LOG_WARN("lcd: st7789 register fail");
        return STATUS_FAIL;
    }

    s_ready = TRUE;
    LOG_INFO("lcd: st7789 %ux%u ready",
             (unsigned)st7789_display_width(&s_lcd),
             (unsigned)st7789_display_height(&s_lcd));
    return STATUS_OK;
}

bool_t lcd_panel_is_ready(void)
{
    return s_ready;
}

void lcd_panel_show_boot(void)
{
    uint16_t w;
    uint16_t h;

    if (s_ready == FALSE) {
        return;
    }

    w = st7789_display_width(&s_lcd);
    h = st7789_display_height(&s_lcd);

    lcd_fill_fast(&s_lcd, 0U, 0U, w, h, LCD_COLOR_DARKBLUE);
    lcd_show_string(&s_lcd, 8U, 12U, (const uint8_t *)"RC Controller",
                    LCD_COLOR_WHITE, LCD_COLOR_DARKBLUE, 16U, 0U);
    lcd_show_string(&s_lcd, 8U, 36U, (const uint8_t *)"v" FW_VERSION_STR,
                    LCD_COLOR_CYAN, LCD_COLOR_DARKBLUE, 16U, 0U);
    lcd_show_string(&s_lcd, 8U, 64U, (const uint8_t *)"T:",
                    LCD_COLOR_YELLOW, LCD_COLOR_DARKBLUE, 16U, 0U);
    lcd_show_string(&s_lcd, 8U, 84U, (const uint8_t *)"S:",
                    LCD_COLOR_YELLOW, LCD_COLOR_DARKBLUE, 16U, 0U);
    lcd_show_string(&s_lcd, 8U, 104U, (const uint8_t *)"BAT:",
                    LCD_COLOR_GREEN, LCD_COLOR_DARKBLUE, 16U, 0U);
    lcd_show_string(&s_lcd, 120U, 104U, (const uint8_t *)"BT:",
                    LCD_COLOR_GREEN, LCD_COLOR_DARKBLUE, 16U, 0U);
}

void lcd_panel_update_telemetry(int16_t throttle, int16_t steer, uint32_t bat_mv, bool_t link_up)
{
    uint16_t t_abs;
    uint16_t s_abs;

    if (s_ready == FALSE) {
        return;
    }

    t_abs = (uint16_t)((throttle < 0) ? -throttle : throttle);
    s_abs = (uint16_t)((steer < 0) ? -steer : steer);

    lcd_fill(&s_lcd, 32U, 64U, 120U, 80U, LCD_COLOR_DARKBLUE);
    lcd_fill(&s_lcd, 32U, 84U, 120U, 100U, LCD_COLOR_DARKBLUE);
    lcd_fill(&s_lcd, 56U, 104U, 112U, 120U, LCD_COLOR_DARKBLUE);
    lcd_fill(&s_lcd, 152U, 104U, 220U, 120U, LCD_COLOR_DARKBLUE);

    lcd_show_int_num(&s_lcd, 32U, 64U, t_abs, 4U, LCD_COLOR_WHITE, LCD_COLOR_DARKBLUE, 16U);
    lcd_show_int_num(&s_lcd, 32U, 84U, s_abs, 4U, LCD_COLOR_WHITE, LCD_COLOR_DARKBLUE, 16U);
    lcd_show_int_num(&s_lcd, 56U, 104U, (uint16_t)((bat_mv > 9999U) ? 9999U : bat_mv), 4U,
                    LCD_COLOR_WHITE, LCD_COLOR_DARKBLUE, 16U);

    if (link_up != FALSE) {
        lcd_show_string(&s_lcd, 152U, 104U, (const uint8_t *)"OK",
                        LCD_COLOR_WHITE, LCD_COLOR_DARKBLUE, 16U, 0U);
    } else {
        lcd_show_string(&s_lcd, 152U, 104U, (const uint8_t *)"--",
                        LCD_COLOR_GRAY, LCD_COLOR_DARKBLUE, 16U, 0U);
    }
}
