/**
 * @file    lcd_panel.c
 * @brief   ST7789 ÃÂ¤ÃÂ¸ÃÂ»ÃÂ¥ÃÂ±ÃÂ/ÃÂ¨ÃÂÃÂÃÂ¥ÃÂÃÂ/ÃÂ¦ÃÂ ÃÂ¡ÃÂ¥ÃÂÃÂ UIÃÂ¯ÃÂ¼ÃÂÃÂ¦ÃÂÃÂÃÂ©ÃÂÃÂÃÂ¥ÃÂ±ÃÂÃÂ©ÃÂÃÂ¨ÃÂ¥ÃÂÃÂ·ÃÂ¦ÃÂÃÂ°ÃÂ¯ÃÂ¼ÃÂ
 */

#include "lcd_panel.h"

#include "board.h"
#include "log.h"
#include "rc_lcd_cfg.h"

#include "bsp_spi.h"
#include "bsp_systick.h"
#include "lcd.h"
#include "st7789.h"

#include "driverlib/gpio.h"

#include <stdio.h>
#include <string.h>

#define HOME_BG           RC_LCD_COLOR_BG
#define HOME_FG           RC_LCD_COLOR_FG
#define CROSS_SIZE        44
#define CROSS_HALF        (CROSS_SIZE / 2)
#define J1_CX             60
#define J1_CY             78
#define J2_CX             180
#define J2_CY             78
/** æææ¾ç¤ºæ­»åºï¼cmd ååå°äºæ­¤å¼ä¸å·æ°ç¹ */
#define HOME_CMD_DEADBAND 25
/** åæéæå±åç´ å°ºå¯¸ï¼å«è¾¹æ¡ï¼ */
#define CROSS_PX          ((uint16_t)(CROSS_HALF * 2U + 1U))
#define DOT_RADIUS        2U
/* bat_percent==0xFF means unknown; same as BATTERY_PERCENT_UNKNOWN */
#define HOME_BAT_UNKNOWN  0xFFU
#define HOME_BAT_WARN_PCT 20U

static st7789_t s_lcd;
static bool_t s_ready;
static int16_t s_dot_j1x = 0;
static int16_t s_dot_j1y = 0;
static int16_t s_dot_j2x = 0;
static int16_t s_dot_j2y = 0;
static bool_t s_home_cache_valid;
static int16_t s_home_j1x;
static int16_t s_home_j1y;
static int16_t s_home_j2x;
static int16_t s_home_j2y;
static bool_t s_home_j1_btn;
static bool_t s_home_j2_btn;
static uint8_t s_home_bat_pct;
static bool_t s_home_link_up;
static int16_t s_home_drive_t;
static int16_t s_home_drive_s;
static int16_t s_cal_dot_j1x;
static int16_t s_cal_dot_j1y;
static int16_t s_cal_dot_j2x;
static int16_t s_cal_dot_j2y;
static char s_cal_status[40];
static char s_cal_coords[40];

typedef struct {
    uint16_t cx;
    uint16_t cy;
} lcd_stick_layer_t;

static const lcd_stick_layer_t s_stick_j1 = { J1_CX, J1_CY };
static const lcd_stick_layer_t s_stick_j2 = { J2_CX, J2_CY };

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

static int16_t clamp_cmd(int16_t v)
{
    if (v > 1000) {
        return 1000;
    }
    if (v < -1000) {
        return -1000;
    }
    return v;
}

static bool_t cmd_delta_ge(int16_t a, int16_t b, int16_t thresh)
{
    int16_t d = (int16_t)(a - b);

    if (d < 0) {
        d = (int16_t)(-d);
    }
    return (d >= thresh) ? TRUE : FALSE;
}

static void draw_crosshair_frame(uint16_t cx, uint16_t cy)
{
    lcd_draw_line(&s_lcd, (uint16_t)(cx - CROSS_HALF), cy, (uint16_t)(cx + CROSS_HALF), cy,
                  RC_LCD_COLOR_MUTED);
    lcd_draw_line(&s_lcd, cx, (uint16_t)(cy - CROSS_HALF), cx, (uint16_t)(cy + CROSS_HALF),
                  RC_LCD_COLOR_MUTED);
    lcd_draw_rectangle(&s_lcd, (uint16_t)(cx - CROSS_HALF), (uint16_t)(cy - CROSS_HALF),
                       (uint16_t)(cx + CROSS_HALF), (uint16_t)(cy + CROSS_HALF), RC_LCD_COLOR_CROSS);
}

/** ÃÂ©ÃÂÃÂÃÂ¦ÃÂÃÂÃÂ¥ÃÂ±ÃÂÃÂ¯ÃÂ¼ÃÂÃÂ¨ÃÂ®ÃÂ¡ÃÂ§ÃÂ®ÃÂÃÂ¥ÃÂÃÂÃÂ¦ÃÂÃÂÃÂ¥ÃÂÃÂºÃÂ¥ÃÂÃÂÃÂ¥ÃÂÃÂÃÂ§ÃÂ´ÃÂ ÃÂ¨ÃÂÃÂ²ÃÂ¯ÃÂ¼ÃÂÃÂ¨ÃÂÃÂÃÂ¦ÃÂÃÂ¯ / ÃÂ¥ÃÂÃÂÃÂ¥ÃÂ­ÃÂÃÂ§ÃÂºÃÂ¿ / ÃÂ¦ÃÂÃÂ¹ÃÂ¦ÃÂ¡ÃÂÃÂ¯ÃÂ¼ÃÂ */
static uint16_t crosshair_pixel_color(uint16_t cx, uint16_t cy, uint16_t x, uint16_t y)
{
    uint16_t left = (uint16_t)(cx - CROSS_HALF);
    uint16_t right = (uint16_t)(cx + CROSS_HALF);
    uint16_t top = (uint16_t)(cy - CROSS_HALF);
    uint16_t bottom = (uint16_t)(cy + CROSS_HALF);

    if ((y == cy) && (x >= left) && (x <= right)) {
        return RC_LCD_COLOR_MUTED;
    }
    if ((x == cx) && (y >= top) && (y <= bottom)) {
        return RC_LCD_COLOR_MUTED;
    }
    if ((x == left) || (x == right) || (y == top) || (y == bottom)) {
        return RC_LCD_COLOR_CROSS;
    }
    return HOME_BG;
}

/** ÃÂ¤ÃÂ»ÃÂÃÂ©ÃÂÃÂÃÂ¦ÃÂÃÂÃÂ¥ÃÂ±ÃÂÃÂ¦ÃÂÃÂ¢ÃÂ¥ÃÂ¤ÃÂÃÂ¦ÃÂÃÂ§ÃÂ§ÃÂÃÂ¹ÃÂ¥ÃÂÃÂºÃÂ¥ÃÂÃÂÃÂ¯ÃÂ¼ÃÂÃÂ¦ÃÂÃÂÃÂ¥ÃÂÃÂ ÃÂ¤ÃÂ½ÃÂÃÂ©ÃÂÃÂÃÂ§ÃÂ»ÃÂÃÂ¥ÃÂÃÂÃÂ§ÃÂ´ÃÂ ÃÂ¯ÃÂ¼ÃÂÃÂ¤ÃÂ¸ÃÂÃÂ§ÃÂÃÂ¨ÃÂ¨ÃÂÃÂÃÂ¦ÃÂÃÂ¯ÃÂ¨ÃÂÃÂ²ÃÂ¥ÃÂÃÂÃÂ¨ÃÂ¦ÃÂÃÂ§ÃÂÃÂÃÂ¯ÃÂ¼ÃÂ */
static void restore_dot_patch(uint16_t cx, uint16_t cy, uint16_t px, uint16_t py)
{
    uint16_t sx = (uint16_t)(px - DOT_RADIUS);
    uint16_t sy = (uint16_t)(py - DOT_RADIUS);
    uint16_t patch = (uint16_t)(DOT_RADIUS * 2U + 1U);
    uint16_t patch_px = patch * patch;
    uint16_t rowbuf[25];
    uint16_t y;
    uint16_t x;

    if (patch_px > (uint16_t)(sizeof(rowbuf) / sizeof(rowbuf[0]))) {
        return;
    }
    for (y = 0U; y < patch; y++) {
        for (x = 0U; x < patch; x++) {
            rowbuf[(uint32_t)y * patch + x] = crosshair_pixel_color(cx, cy, (uint16_t)(sx + x),
                                                                    (uint16_t)(sy + y));
        }
    }
    if (st7789_set_window(&s_lcd, sx, sy, (uint16_t)(sx + patch - 1U),
                          (uint16_t)(sy + patch - 1U)) != ST7789_OK) {
        return;
    }
    for (y = 0U; y < patch; y++) {
        if (st7789_write_pixels(&s_lcd, &rowbuf[(uint32_t)y * patch], patch) != ST7789_OK) {
            break;
        }
    }
    st7789_end_write(&s_lcd);
}

static void cmd_to_pixel(uint16_t cx, uint16_t cy, int16_t x_cmd, int16_t y_cmd,
                         uint16_t *px, uint16_t *py)
{
    int16_t dx = (int16_t)(((int32_t)clamp_cmd(x_cmd) * (CROSS_HALF - 2)) / 1000);
    int16_t dy = (int16_t)((-(int32_t)clamp_cmd(y_cmd) * (CROSS_HALF - 2)) / 1000);

    *px = (uint16_t)((int16_t)cx + dx);
    *py = (uint16_t)((int16_t)cy + dy);
}

/** ÃÂ§ÃÂ»ÃÂÃÂ¥ÃÂÃÂ¶ÃÂ©ÃÂÃÂÃÂ¦ÃÂÃÂÃÂ¥ÃÂÃÂÃÂ¦ÃÂÃÂÃÂ¥ÃÂ±ÃÂÃÂ¥ÃÂÃÂ°ÃÂ¥ÃÂ±ÃÂÃÂ¥ÃÂ¹ÃÂÃÂ¯ÃÂ¼ÃÂHOME/CAL ÃÂ¥ÃÂÃÂÃÂ©ÃÂ¡ÃÂµÃÂ¦ÃÂÃÂ¶ÃÂ¨ÃÂ°ÃÂÃÂ§ÃÂÃÂ¨ÃÂ¤ÃÂ¸ÃÂÃÂ¦ÃÂ¬ÃÂ¡ÃÂ¯ÃÂ¼ÃÂ */
static void stick_layer_draw_static(const lcd_stick_layer_t *layer)
{
    draw_crosshair_frame(layer->cx, layer->cy);
}

/** ÃÂ¦ÃÂÃÂ¦ÃÂ©ÃÂÃÂ¤ÃÂ¦ÃÂÃÂ§ÃÂ§ÃÂÃÂ¹ÃÂ¯ÃÂ¼ÃÂÃÂ¦ÃÂÃÂÃÂ©ÃÂÃÂÃÂ¦ÃÂÃÂÃÂ¥ÃÂ±ÃÂÃÂ¥ÃÂÃÂ ÃÂ¤ÃÂ½ÃÂÃÂ¦ÃÂÃÂ¢ÃÂ¥ÃÂ¤ÃÂÃÂ¥ÃÂÃÂÃÂ§ÃÂ´ÃÂ  */
static void stick_layer_erase_dot(const lcd_stick_layer_t *layer, int16_t x_cmd, int16_t y_cmd)
{
    uint16_t px;
    uint16_t py;

    cmd_to_pixel(layer->cx, layer->cy, x_cmd, y_cmd, &px, &py);
    restore_dot_patch(layer->cx, layer->cy, px, py);
}

static void draw_dot(uint16_t cx, uint16_t cy, int16_t x_cmd, int16_t y_cmd, uint16_t color)
{
    uint16_t px;
    uint16_t py;

    cmd_to_pixel(cx, cy, x_cmd, y_cmd, &px, &py);
    lcd_fill(&s_lcd, (uint16_t)(px - DOT_RADIUS), (uint16_t)(py - DOT_RADIUS),
             (uint16_t)(px + DOT_RADIUS + 1U), (uint16_t)(py + DOT_RADIUS + 1U), color);
}

/** ÃÂ¥ÃÂÃÂ¨ÃÂ¦ÃÂÃÂÃÂ¥ÃÂ±ÃÂÃÂ¯ÃÂ¼ÃÂÃÂ¥ÃÂÃÂÃÂ¦ÃÂÃÂ¢ÃÂ¥ÃÂ¤ÃÂÃÂ©ÃÂÃÂÃÂ¦ÃÂÃÂÃÂ¥ÃÂºÃÂÃÂ¥ÃÂÃÂ¾ÃÂ¥ÃÂÃÂÃÂ§ÃÂÃÂ»ÃÂ¦ÃÂÃÂ°ÃÂ§ÃÂÃÂ¹ */
static void stick_layer_update_dot(const lcd_stick_layer_t *layer,
                                   int16_t old_x, int16_t old_y,
                                   int16_t new_x, int16_t new_y,
                                   uint16_t color, bool_t moved)
{
    if (moved) {
        stick_layer_erase_dot(layer, old_x, old_y);
    }
    draw_dot(layer->cx, layer->cy, new_x, new_y, color);
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

    (void)memset(&cfg, 0, sizeof(cfg));
    cfg.spi_tx = lcd_spi_tx;
    cfg.set_cs = lcd_pin_cs;
    cfg.set_dc = lcd_pin_dc;
    cfg.set_rst = lcd_pin_rst;
    cfg.set_bl = lcd_pin_bl;
    cfg.delay_ms = lcd_delay_ms;
    cfg.rotation = (uint8_t)RC_LCD_ROTATION;
    cfg.bgr = (uint8_t)RC_LCD_BGR;

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

void lcd_panel_show_home(void)
{
    uint16_t w;
    uint16_t h;

    if (s_ready == FALSE) {
        return;
    }

    w = st7789_display_width(&s_lcd);
    h = st7789_display_height(&s_lcd);
    lcd_fill_fast(&s_lcd, 0U, 0U, w, h, HOME_BG);

    lcd_show_string(&s_lcd, 4U, 2U, (const uint8_t *)"RC", HOME_FG, HOME_BG, 16U, 0U);
    lcd_show_string(&s_lcd, 40U, 4U, (const uint8_t *)"BAT --%", RC_LCD_COLOR_OK, HOME_BG, 12U, 0U);
    lcd_show_string(&s_lcd, 170U, 4U, (const uint8_t *)"LINK --", RC_LCD_COLOR_OK, HOME_BG, 12U, 0U);

    lcd_show_string(&s_lcd, 44U, 28U, (const uint8_t *)"JS1", RC_LCD_COLOR_WARN, HOME_BG, 12U, 0U);
    lcd_show_string(&s_lcd, 164U, 28U, (const uint8_t *)"JS2 drv", RC_LCD_COLOR_WARN, HOME_BG, 12U, 0U);

    stick_layer_draw_static(&s_stick_j1);
    stick_layer_draw_static(&s_stick_j2);

    s_dot_j1x = 0;
    s_dot_j1y = 0;
    s_dot_j2x = 0;
    s_dot_j2y = 0;
    s_home_cache_valid = FALSE;
    draw_dot(J1_CX, J1_CY, 0, 0, RC_LCD_COLOR_ACCENT);
    draw_dot(J2_CX, J2_CY, 0, 0, RC_LCD_COLOR_ACCENT);

    lcd_show_string(&s_lcd, 4U, 120U, (const uint8_t *)"JS2:F/B/L/R  hold:menu", RC_LCD_COLOR_MUTED,
                    HOME_BG, 12U, 0U);
}

bool_t lcd_panel_update_home(int16_t j1x, int16_t j1y, int16_t j2x, int16_t j2y,
                             bool_t j1_btn, bool_t j2_btn,
                             uint8_t bat_percent, bool_t link_up,
                             int16_t drive_throttle, int16_t drive_steer)
{
    char buf[24];
    bool_t stick_dirty;
    bool_t btn_dirty;
    bool_t bat_dirty;
    bool_t link_dirty;
    bool_t drive_dirty;
    bool_t drew = FALSE;

    if (s_ready == FALSE) {
        return FALSE;
    }

    stick_dirty = !s_home_cache_valid ||
                  cmd_delta_ge(j1x, s_home_j1x, HOME_CMD_DEADBAND) ||
                  cmd_delta_ge(j1y, s_home_j1y, HOME_CMD_DEADBAND) ||
                  cmd_delta_ge(j2x, s_home_j2x, HOME_CMD_DEADBAND) ||
                  cmd_delta_ge(j2y, s_home_j2y, HOME_CMD_DEADBAND);
    btn_dirty = !s_home_cache_valid || (j1_btn != s_home_j1_btn) || (j2_btn != s_home_j2_btn);
    bat_dirty = !s_home_cache_valid || (bat_percent != s_home_bat_pct);
    link_dirty = !s_home_cache_valid || (link_up != s_home_link_up);
    drive_dirty = !s_home_cache_valid || (drive_throttle != s_home_drive_t) ||
                  (drive_steer != s_home_drive_s);

    if (!stick_dirty && !btn_dirty && !bat_dirty && !link_dirty && !drive_dirty) {
        return FALSE;
    }

    if (stick_dirty || btn_dirty) {
        bool_t j1_stick = !s_home_cache_valid ||
                          cmd_delta_ge(j1x, s_home_j1x, HOME_CMD_DEADBAND) ||
                          cmd_delta_ge(j1y, s_home_j1y, HOME_CMD_DEADBAND);
        bool_t j2_stick = !s_home_cache_valid ||
                          cmd_delta_ge(j2x, s_home_j2x, HOME_CMD_DEADBAND) ||
                          cmd_delta_ge(j2y, s_home_j2y, HOME_CMD_DEADBAND);
        uint16_t j1_color = j1_btn ? RC_LCD_COLOR_DOT_BTN : RC_LCD_COLOR_ACCENT;
        uint16_t j2_color = j2_btn ? RC_LCD_COLOR_DOT_BTN : RC_LCD_COLOR_ACCENT;

        if (j1_stick) {
            stick_layer_update_dot(&s_stick_j1, s_dot_j1x, s_dot_j1y, j1x, j1y, j1_color, TRUE);
            s_dot_j1x = j1x;
            s_dot_j1y = j1y;
        } else if (j1_btn != s_home_j1_btn) {
            stick_layer_update_dot(&s_stick_j1, j1x, j1y, j1x, j1y, j1_color, FALSE);
        }

        if (j2_stick) {
            stick_layer_update_dot(&s_stick_j2, s_dot_j2x, s_dot_j2y, j2x, j2y, j2_color, TRUE);
            s_dot_j2x = j2x;
            s_dot_j2y = j2y;
        } else if (j2_btn != s_home_j2_btn) {
            stick_layer_update_dot(&s_stick_j2, j2x, j2y, j2x, j2y, j2_color, FALSE);
        }

        drew = TRUE;
    }

    if (bat_dirty) {
        uint16_t bat_fc = RC_LCD_COLOR_OK;

        if (bat_percent == HOME_BAT_UNKNOWN) {
            (void)snprintf(buf, sizeof(buf), "BAT --%%");
            bat_fc = RC_LCD_COLOR_MUTED;
        } else {
            uint8_t pct = (bat_percent > 100U) ? 100U : bat_percent;
            (void)snprintf(buf, sizeof(buf), "BAT %3u%%", (unsigned)pct);
            if (pct <= HOME_BAT_WARN_PCT) {
                bat_fc = RC_LCD_COLOR_WARN;
            }
        }
        lcd_fill(&s_lcd, 40U, 4U, 120U, 16U, HOME_BG);
        lcd_show_string(&s_lcd, 40U, 4U, (const uint8_t *)buf, bat_fc, HOME_BG, 12U, 0U);
        s_home_bat_pct = bat_percent;
        drew = TRUE;
    }

    if (link_dirty) {
        lcd_fill(&s_lcd, 170U, 4U, 236U, 16U, HOME_BG);
        lcd_show_string(&s_lcd, 170U, 4U,
                        (const uint8_t *)(link_up ? "LINK OK" : "LINK --"),
                        link_up ? RC_LCD_COLOR_OK : RC_LCD_COLOR_MUTED, HOME_BG, 12U, 0U);
        s_home_link_up = link_up;
        drew = TRUE;
    }

    if (drive_dirty) {
        char f = (drive_throttle > 0) ? 'F' : '-';
        char b = (drive_throttle < 0) ? 'B' : '-';
        char l = (drive_steer < 0) ? 'L' : '-';
        char r = (drive_steer > 0) ? 'R' : '-';

        (void)snprintf(buf, sizeof(buf), "DRV %c%c%c%c", f, b, l, r);
        lcd_fill(&s_lcd, 4U, 106U, 88U, 118U, HOME_BG);
        lcd_show_string(&s_lcd, 4U, 106U, (const uint8_t *)buf,
                         (f != '-' || b != '-' || l != '-' || r != '-') ? RC_LCD_COLOR_ACCENT :
                                                                            RC_LCD_COLOR_MUTED,
                         HOME_BG, 12U, 0U);
        s_home_drive_t = drive_throttle;
        s_home_drive_s = drive_steer;
        drew = TRUE;
    }

    s_home_j1x = j1x;
    s_home_j1y = j1y;
    s_home_j2x = j2x;
    s_home_j2y = j2y;
    s_home_j1_btn = j1_btn;
    s_home_j2_btn = j2_btn;
    s_home_cache_valid = TRUE;
    return drew;
}

void lcd_panel_show_menu(const char *title,
                         const char *const lines[],
                         uint8_t line_count,
                         uint8_t cursor_vis,
                         const char *foot)
{
    uint16_t w;
    uint16_t h;
    uint8_t i;

    if (s_ready == FALSE) {
        return;
    }

    w = st7789_display_width(&s_lcd);
    h = st7789_display_height(&s_lcd);
    /* ååºéç»ï¼é¿åæ´å± fill_fast éªç */
    lcd_fill(&s_lcd, 0U, 0U, w, 20U, HOME_BG);
    lcd_fill(&s_lcd, 0U, 20U, w, 116U, HOME_BG);
    lcd_fill(&s_lcd, 0U, 116U, w, h, HOME_BG);

    lcd_show_string(&s_lcd, 4U, 2U,
                    (const uint8_t *)((title != NULL) ? title : "Menu"),
                    RC_LCD_COLOR_WARN, HOME_BG, 16U, 0U);

    for (i = 0U; i < line_count; i++) {
        uint16_t y = (uint16_t)(22U + (i * 20U));
        uint16_t fc = (i == cursor_vis) ? LCD_COLOR_BLACK : HOME_FG;
        uint16_t bc = (i == cursor_vis) ? RC_LCD_COLOR_ACCENT : HOME_BG;
        const char *text = (lines[i] != NULL) ? lines[i] : "";

        if (i == cursor_vis) {
            lcd_fill(&s_lcd, 2U, y, (uint16_t)(w - 2U), (uint16_t)(y + 18U), bc);
        }
        lcd_show_string(&s_lcd, 8U, (uint16_t)(y + 1U), (const uint8_t *)text, fc, bc, 16U, 0U);
    }

    if (foot != NULL) {
        lcd_show_string(&s_lcd, 4U, 118U, (const uint8_t *)foot, RC_LCD_COLOR_MUTED, HOME_BG, 12U, 0U);
    }
}

void lcd_panel_update_menu_row(uint8_t row, const char *text, bool_t highlighted)
{
    uint16_t w;
    uint16_t y;
    uint16_t fc;
    uint16_t bc;

    if ((s_ready == FALSE) || (row >= 4U)) {
        return;
    }

    w = st7789_display_width(&s_lcd);
    y = (uint16_t)(22U + ((uint16_t)row * 20U));
    fc = highlighted ? LCD_COLOR_BLACK : HOME_FG;
    bc = highlighted ? RC_LCD_COLOR_ACCENT : HOME_BG;

    lcd_fill(&s_lcd, 2U, y, (uint16_t)(w - 2U), (uint16_t)(y + 18U), bc);
    if ((text != NULL) && (text[0] != '\0')) {
        lcd_show_string(&s_lcd, 8U, (uint16_t)(y + 1U), (const uint8_t *)text, fc, bc, 16U, 0U);
    }
}

void lcd_panel_show_cal_frame(const char *title, const char *hint)
{
    uint16_t w;
    uint16_t h;

    if (s_ready == FALSE) {
        return;
    }

    w = st7789_display_width(&s_lcd);
    h = st7789_display_height(&s_lcd);
    lcd_fill_fast(&s_lcd, 0U, 0U, w, h, HOME_BG);

    lcd_show_string(&s_lcd, 4U, 2U,
                    (const uint8_t *)((title != NULL) ? title : "Calibrate"),
                    RC_LCD_COLOR_WARN, HOME_BG, 16U, 0U);
    if (hint != NULL) {
        lcd_show_string(&s_lcd, 4U, 22U, (const uint8_t *)hint, HOME_FG, HOME_BG, 12U, 0U);
    }

    stick_layer_draw_static(&s_stick_j1);
    stick_layer_draw_static(&s_stick_j2);

    s_cal_dot_j1x = 0;
    s_cal_dot_j1y = 0;
    s_cal_dot_j2x = 0;
    s_cal_dot_j2y = 0;
    s_cal_status[0] = '\0';
    s_cal_coords[0] = '\0';
    draw_dot(J1_CX, J1_CY, 0, 0, RC_LCD_COLOR_ACCENT);
    draw_dot(J2_CX, J2_CY, 0, 0, RC_LCD_COLOR_ACCENT);
}

bool_t lcd_panel_update_cal(int16_t j1x, int16_t j1y, int16_t j2x, int16_t j2y,
                            const char *status_line)
{
    char coords[40];
    bool_t stick_dirty;
    bool_t status_dirty;
    bool_t coords_dirty;
    bool_t drew = FALSE;
    const char *status = (status_line != NULL) ? status_line : "";

    if (s_ready == FALSE) {
        return FALSE;
    }

    (void)snprintf(coords, sizeof(coords), "J1 %d,%d  J2 %d,%d",
                   (int)j1x, (int)j1y, (int)j2x, (int)j2y);

    stick_dirty = cmd_delta_ge(j1x, s_cal_dot_j1x, HOME_CMD_DEADBAND) ||
                  cmd_delta_ge(j1y, s_cal_dot_j1y, HOME_CMD_DEADBAND) ||
                  cmd_delta_ge(j2x, s_cal_dot_j2x, HOME_CMD_DEADBAND) ||
                  cmd_delta_ge(j2y, s_cal_dot_j2y, HOME_CMD_DEADBAND);
    status_dirty = (strcmp(status, s_cal_status) != 0) ? TRUE : FALSE;
    coords_dirty = (strcmp(coords, s_cal_coords) != 0) ? TRUE : FALSE;

    if (!stick_dirty && !status_dirty && !coords_dirty) {
        return FALSE;
    }

    if (stick_dirty) {
        stick_layer_update_dot(&s_stick_j1, s_cal_dot_j1x, s_cal_dot_j1y, j1x, j1y,
                               RC_LCD_COLOR_ACCENT, TRUE);
        stick_layer_update_dot(&s_stick_j2, s_cal_dot_j2x, s_cal_dot_j2y, j2x, j2y,
                               RC_LCD_COLOR_ACCENT, TRUE);
        s_cal_dot_j1x = j1x;
        s_cal_dot_j1y = j1y;
        s_cal_dot_j2x = j2x;
        s_cal_dot_j2y = j2y;
        drew = TRUE;
    }

    if (coords_dirty) {
        lcd_fill(&s_lcd, 4U, 104U, 236U, 116U, HOME_BG);
        lcd_show_string(&s_lcd, 4U, 104U, (const uint8_t *)coords, RC_LCD_COLOR_CROSS, HOME_BG, 12U,
                        0U);
        (void)strncpy(s_cal_coords, coords, sizeof(s_cal_coords) - 1U);
        s_cal_coords[sizeof(s_cal_coords) - 1U] = '\0';
        drew = TRUE;
    }

    if (status_dirty) {
        lcd_fill(&s_lcd, 4U, 120U, 236U, 132U, HOME_BG);
        if (status[0] != '\0') {
            lcd_show_string(&s_lcd, 4U, 120U, (const uint8_t *)status, RC_LCD_COLOR_OK, HOME_BG,
                            12U, 0U);
        }
        (void)strncpy(s_cal_status, status, sizeof(s_cal_status) - 1U);
        s_cal_status[sizeof(s_cal_status) - 1U] = '\0';
        drew = TRUE;
    }

    return drew;
}
