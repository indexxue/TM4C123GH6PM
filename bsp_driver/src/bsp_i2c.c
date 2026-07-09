/**
 * @file bsp_i2c.c
 * @brief TM4C123 I2C 主机
 */

#include "bsp_i2c.h"

#include "bsp_bus_lock.h"
#include "bsp_systick.h"
#include "bsp_sysctl.h"

#include "bsp_config.h"

#include "driverlib/gpio.h"
#include "driverlib/i2c.h"
#include "driverlib/pin_map.h"
#include "driverlib/sysctl.h"
#include "inc/hw_memmap.h"

#define BSP_I2C_PERIPH_READY_US 100000U
#define BSP_I2C_OP_TIMEOUT_US   10000U
#define BSP_I2C_GPIO_PULSE_US     10U
#define BSP_I2C_MASTER_TIMEOUT    0xFFU
#define BSP_I2C0_SCL_PIN          GPIO_PIN_2
#define BSP_I2C0_SDA_PIN          GPIO_PIN_3
#define BSP_I2C0_PINS             (BSP_I2C0_SCL_PIN | BSP_I2C0_SDA_PIN)

static uint32_t i2c_periph_from_base(uint32_t base)
{
    switch (base) {
    case I2C0_BASE:
        return SYSCTL_PERIPH_I2C0;
    case I2C1_BASE:
        return SYSCTL_PERIPH_I2C1;
    case I2C2_BASE:
        return SYSCTL_PERIPH_I2C2;
    case I2C3_BASE:
        return SYSCTL_PERIPH_I2C3;
    case I2C4_BASE:
        return SYSCTL_PERIPH_I2C4;
    case I2C5_BASE:
        return SYSCTL_PERIPH_I2C5;
    case I2C6_BASE:
        return SYSCTL_PERIPH_I2C6;
    case I2C7_BASE:
        return SYSCTL_PERIPH_I2C7;
    case I2C8_BASE:
        return SYSCTL_PERIPH_I2C8;
    case I2C9_BASE:
        return SYSCTL_PERIPH_I2C9;
    default:
        return 0U;
    }
}

static bool i2c_wait_busy(uint32_t base, uint32_t timeout_us)
{
    bsp_timeout_t timeout;

    bsp_timeout_start_us(&timeout, timeout_us);
    while (I2CMasterBusy(base)) {
        if (bsp_timeout_expired(&timeout)) {
            return false;
        }
    }

    return true;
}

static void i2c_bus_recover(uint32_t base)
{
    I2CMasterControl(base, I2C_MASTER_CMD_BURST_SEND_ERROR_STOP);
    (void)I2CMasterErr(base);
}

static bool i2c_wait_idle(uint32_t base, uint32_t timeout_us)
{
    bsp_timeout_t timeout;

    bsp_timeout_start_us(&timeout, timeout_us);
    while (I2CMasterBusy(base)) {
        if (bsp_timeout_expired(&timeout)) {
            i2c_bus_recover(base);
            return false;
        }
    }

    if (I2CMasterErr(base) != I2C_MASTER_ERR_NONE) {
        i2c_bus_recover(base);
        return false;
    }

    return true;
}

static void i2c0_restore_pin_mux(void)
{
    GPIOPinConfigure(GPIO_PB2_I2C0SCL);
    GPIOPinTypeI2C(GPIO_PORTB_BASE, BSP_I2C0_SCL_PIN);
    GPIOPinConfigure(GPIO_PB3_I2C0SDA);
    GPIOPinTypeI2C(GPIO_PORTB_BASE, BSP_I2C0_SDA_PIN);
}

static void i2c_master_reinit(const bsp_i2c_config_t *cfg)
{
    if (cfg == NULL) {
        return;
    }

    I2CMasterInitExpClk(cfg->base, bsp_clock_get_hz(), cfg->clock_hz > 100000U);
    I2CMasterTimeoutSet(cfg->base, BSP_I2C_MASTER_TIMEOUT);
    I2CMasterEnable(cfg->base);
}

static void i2c_master_configure(uint32_t base, uint32_t clock_hz)
{
    I2CMasterInitExpClk(base, bsp_clock_get_hz(), clock_hz > 100000U);
    I2CMasterTimeoutSet(base, BSP_I2C_MASTER_TIMEOUT);
    I2CMasterEnable(base);
}

static bool i2c0_lines_need_gpio_recover(void)
{
    bool scl;
    bool sda;

    I2CMasterDisable(I2C0_BASE);
    GPIOPinTypeGPIOInput(GPIO_PORTB_BASE, BSP_I2C0_PINS);
    GPIOPadConfigSet(GPIO_PORTB_BASE, BSP_I2C0_PINS, GPIO_STRENGTH_2MA, GPIO_PIN_TYPE_STD_WPU);
    bsp_delay_us(20U);
    scl = (GPIOPinRead(GPIO_PORTB_BASE, BSP_I2C0_SCL_PIN) & BSP_I2C0_SCL_PIN) != 0U;
    sda = (GPIOPinRead(GPIO_PORTB_BASE, BSP_I2C0_SDA_PIN) & BSP_I2C0_SDA_PIN) != 0U;
    return (!scl || !sda);
}

static bool i2c0_hw_probe_once(uint8_t addr_7bit)
{
    uint32_t err;

    I2CMasterSlaveAddrSet(I2C0_BASE, addr_7bit, false);
    I2CMasterDataPut(I2C0_BASE, 0x00U);
    I2CMasterControl(I2C0_BASE, I2C_MASTER_CMD_SINGLE_SEND);
    if (!i2c_wait_busy(I2C0_BASE, BSP_I2C_OP_TIMEOUT_US)) {
        i2c_bus_recover(I2C0_BASE);
        (void)I2CMasterErr(I2C0_BASE);
        return false;
    }

    err = I2CMasterErr(I2C0_BASE);
    if (err != I2C_MASTER_ERR_NONE) {
        i2c_bus_recover(I2C0_BASE);
        (void)I2CMasterErr(I2C0_BASE);
        return false;
    }

    return true;
}

static void i2c0_gpio_bus_recover(const bsp_i2c_config_t *cfg, bool reinit_master)
{
    uint8_t i;

    GPIOPinTypeGPIOOutputOD(GPIO_PORTB_BASE, BSP_I2C0_PINS);

    GPIOPinWrite(GPIO_PORTB_BASE, BSP_I2C0_PINS, BSP_I2C0_PINS);
    bsp_delay_us(10U);

    for (i = 0U; i < 9U; i++) {
        GPIOPinWrite(GPIO_PORTB_BASE, BSP_I2C0_SCL_PIN, 0U);
        bsp_delay_us(BSP_I2C_GPIO_PULSE_US);
        GPIOPinWrite(GPIO_PORTB_BASE, BSP_I2C0_SCL_PIN, BSP_I2C0_SCL_PIN);
        bsp_delay_us(BSP_I2C_GPIO_PULSE_US);
    }

    GPIOPinWrite(GPIO_PORTB_BASE, BSP_I2C0_SDA_PIN, 0U);
    bsp_delay_us(BSP_I2C_GPIO_PULSE_US);
    GPIOPinWrite(GPIO_PORTB_BASE, BSP_I2C0_SCL_PIN, BSP_I2C0_SCL_PIN);
    bsp_delay_us(BSP_I2C_GPIO_PULSE_US);
    GPIOPinWrite(GPIO_PORTB_BASE, BSP_I2C0_PINS, BSP_I2C0_PINS);
    bsp_delay_us(10U);

    if (reinit_master && (cfg != NULL)) {
        i2c0_restore_pin_mux();
        i2c_master_reinit(cfg);
    }
}

bool bsp_i2c0_sample_idle_lines(bool *scl_high, bool *sda_high)
{
    bool scl;
    bool sda;

    GPIOPinTypeGPIOInput(GPIO_PORTB_BASE, BSP_I2C0_PINS);
    GPIOPadConfigSet(GPIO_PORTB_BASE, BSP_I2C0_PINS, GPIO_STRENGTH_2MA, GPIO_PIN_TYPE_STD_WPU);
    bsp_delay_us(50U);

    scl = (GPIOPinRead(GPIO_PORTB_BASE, BSP_I2C0_SCL_PIN) & BSP_I2C0_SCL_PIN) != 0U;
    sda = (GPIOPinRead(GPIO_PORTB_BASE, BSP_I2C0_SDA_PIN) & BSP_I2C0_SDA_PIN) != 0U;
    if (scl_high != NULL) {
        *scl_high = scl;
    }
    if (sda_high != NULL) {
        *sda_high = sda;
    }

    return true;
}

/* -------------------------------------------------------------------------- */
/* I2C0 GPIO 位操作扫描（PB2/PB3）— 规避硬件主机锁死                           */
/* -------------------------------------------------------------------------- */

static void i2c0_bb_pins_output_od(uint8_t pins)
{
    GPIOPinTypeGPIOOutputOD(GPIO_PORTB_BASE, pins);
}

static void i2c0_bb_sda_out(bool high)
{
    i2c0_bb_pins_output_od(BSP_I2C0_SDA_PIN);
    GPIOPinWrite(GPIO_PORTB_BASE, BSP_I2C0_SDA_PIN, high ? BSP_I2C0_SDA_PIN : 0U);
}

static void i2c0_bb_scl_out(bool high)
{
    i2c0_bb_pins_output_od(BSP_I2C0_SCL_PIN);
    GPIOPinWrite(GPIO_PORTB_BASE, BSP_I2C0_SCL_PIN, high ? BSP_I2C0_SCL_PIN : 0U);
}

static bool i2c0_bb_sda_read(void)
{
    bool level;

    GPIOPinTypeGPIOInput(GPIO_PORTB_BASE, BSP_I2C0_SDA_PIN);
    GPIOPadConfigSet(GPIO_PORTB_BASE, BSP_I2C0_SDA_PIN, GPIO_STRENGTH_2MA, GPIO_PIN_TYPE_STD_WPU);
    bsp_delay_us(2U);
    level = (GPIOPinRead(GPIO_PORTB_BASE, BSP_I2C0_SDA_PIN) & BSP_I2C0_SDA_PIN) != 0U;
    GPIOPinTypeGPIOOutputOD(GPIO_PORTB_BASE, BSP_I2C0_SDA_PIN);
    return level;
}

static void i2c0_bb_start(void)
{
    i2c0_bb_sda_out(true);
    i2c0_bb_scl_out(true);
    bsp_delay_us(BSP_I2C_GPIO_PULSE_US);
    i2c0_bb_sda_out(false);
    bsp_delay_us(BSP_I2C_GPIO_PULSE_US);
    i2c0_bb_scl_out(false);
    bsp_delay_us(BSP_I2C_GPIO_PULSE_US);
}

static void i2c0_bb_stop(void)
{
    i2c0_bb_sda_out(false);
    bsp_delay_us(BSP_I2C_GPIO_PULSE_US);
    i2c0_bb_scl_out(true);
    bsp_delay_us(BSP_I2C_GPIO_PULSE_US);
    i2c0_bb_sda_out(true);
    bsp_delay_us(BSP_I2C_GPIO_PULSE_US);
}

static bool i2c0_bb_write_byte(uint8_t byte)
{
    uint8_t i;

    for (i = 0U; i < 8U; i++) {
        i2c0_bb_sda_out((byte & 0x80U) != 0U);
        byte <<= 1;
        bsp_delay_us(BSP_I2C_GPIO_PULSE_US);
        i2c0_bb_scl_out(true);
        bsp_delay_us(BSP_I2C_GPIO_PULSE_US);
        i2c0_bb_scl_out(false);
    }

    i2c0_bb_sda_out(true);
    bsp_delay_us(BSP_I2C_GPIO_PULSE_US);
    i2c0_bb_scl_out(true);
    bsp_delay_us(BSP_I2C_GPIO_PULSE_US);
    {
        bool ack = !i2c0_bb_sda_read();
        i2c0_bb_scl_out(false);
        bsp_delay_us(BSP_I2C_GPIO_PULSE_US);
        return ack;
    }
}

static bool i2c0_bb_read_byte(uint8_t *byte, bool ack)
{
    uint8_t i;
    uint8_t value = 0u;

    if (byte == NULL) {
        return false;
    }

    GPIOPinTypeGPIOInput(GPIO_PORTB_BASE, BSP_I2C0_SDA_PIN);
    GPIOPadConfigSet(GPIO_PORTB_BASE, BSP_I2C0_SDA_PIN, GPIO_STRENGTH_2MA, GPIO_PIN_TYPE_STD_WPU);

    for (i = 0U; i < 8U; i++) {
        i2c0_bb_scl_out(false);
        bsp_delay_us(BSP_I2C_GPIO_PULSE_US);
        i2c0_bb_scl_out(true);
        bsp_delay_us(BSP_I2C_GPIO_PULSE_US);
        value <<= 1;
        if ((GPIOPinRead(GPIO_PORTB_BASE, BSP_I2C0_SDA_PIN) & BSP_I2C0_SDA_PIN) != 0U) {
            value |= 1u;
        }
    }

    i2c0_bb_scl_out(false);
    i2c0_bb_pins_output_od(BSP_I2C0_SDA_PIN);
    i2c0_bb_sda_out(ack ? false : true);
    bsp_delay_us(BSP_I2C_GPIO_PULSE_US);
    i2c0_bb_scl_out(true);
    bsp_delay_us(BSP_I2C_GPIO_PULSE_US);
    i2c0_bb_scl_out(false);
    bsp_delay_us(BSP_I2C_GPIO_PULSE_US);
    i2c0_bb_sda_out(true);

    *byte = value;
    return true;
}

static bool i2c0_bb_write_locked(uint8_t addr_7bit, uint8_t reg, const uint8_t *data, size_t len)
{
    size_t i;

    i2c0_bb_pins_output_od(BSP_I2C0_PINS);
    GPIOPinWrite(GPIO_PORTB_BASE, BSP_I2C0_PINS, BSP_I2C0_PINS);
    bsp_delay_us(10U);

    i2c0_bb_start();
    if (!i2c0_bb_write_byte((uint8_t)(addr_7bit << 1))) {
        i2c0_bb_stop();
        return false;
    }
    if (!i2c0_bb_write_byte(reg)) {
        i2c0_bb_stop();
        return false;
    }

    for (i = 0U; i < len; i++) {
        if (!i2c0_bb_write_byte(data[i])) {
            i2c0_bb_stop();
            return false;
        }
    }

    i2c0_bb_stop();
    return true;
}

static bool i2c0_bb_read_locked(uint8_t addr_7bit, uint8_t reg, uint8_t *data, size_t len)
{
    size_t i;

    if ((data == NULL) || (len == 0U)) {
        return false;
    }

    i2c0_bb_pins_output_od(BSP_I2C0_PINS);
    GPIOPinWrite(GPIO_PORTB_BASE, BSP_I2C0_PINS, BSP_I2C0_PINS);
    bsp_delay_us(10U);

    i2c0_bb_start();
    if (!i2c0_bb_write_byte((uint8_t)(addr_7bit << 1))) {
        i2c0_bb_stop();
        return false;
    }
    if (!i2c0_bb_write_byte(reg)) {
        i2c0_bb_stop();
        return false;
    }

    i2c0_bb_stop();
    i2c0_bb_start();
    if (!i2c0_bb_write_byte((uint8_t)((addr_7bit << 1) | 1u))) {
        i2c0_bb_stop();
        return false;
    }

    for (i = 0U; i < len; i++) {
        if (!i2c0_bb_read_byte(&data[i], i < (len - 1U))) {
            i2c0_bb_stop();
            return false;
        }
    }

    i2c0_bb_stop();
    return true;
}

void bsp_i2c0_gpio_scan_begin(void)
{
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOB);
    (void)bsp_periph_wait_ready(SYSCTL_PERIPH_GPIOB, BSP_I2C_PERIPH_READY_US);
    GPIOPinTypeGPIOOutputOD(GPIO_PORTB_BASE, BSP_I2C0_PINS);
    GPIOPinWrite(GPIO_PORTB_BASE, BSP_I2C0_PINS, BSP_I2C0_PINS);
    bsp_delay_us(10U);
}

bool bsp_i2c0_gpio_probe(uint8_t addr_7bit)
{
    bool ack;

    i2c0_bb_start();
    ack = i2c0_bb_write_byte((uint8_t)(addr_7bit << 1));
    i2c0_bb_stop();
    return ack;
}

void bsp_i2c0_gpio_scan_end_idle_high(void)
{
    GPIOPinWrite(GPIO_PORTB_BASE, BSP_I2C0_PINS, BSP_I2C0_PINS);
    bsp_delay_us(10U);
}

/* -------------------------------------------------------------------------- */
/* I2C0 硬件主机扫描（TI 推荐：DataPut + SINGLE_SEND + ERROR_STOP）            */
/* -------------------------------------------------------------------------- */

void bsp_i2c0_hw_scan_begin(const bsp_i2c_config_t *cfg)
{
    if (cfg == NULL) {
        return;
    }

    if (i2c0_lines_need_gpio_recover()) {
        i2c0_gpio_bus_recover(cfg, false);
    }

    i2c0_restore_pin_mux();
    I2CMasterDisable(I2C0_BASE);
    /* 扫描阶段用 100 kHz，减少 NACK 时状态机异常 */
    I2CMasterInitExpClk(I2C0_BASE, bsp_clock_get_hz(), false);
    I2CMasterTimeoutSet(I2C0_BASE, BSP_I2C_MASTER_TIMEOUT);
    I2CMasterEnable(I2C0_BASE);
    bsp_delay_us(50U);
}

bool bsp_i2c0_hw_probe(uint8_t addr_7bit)
{
    return i2c0_hw_probe_once(addr_7bit);
}

void bsp_i2c0_hw_scan_end(const bsp_i2c_config_t *cfg)
{
    if (I2CMasterBusy(I2C0_BASE)) {
        i2c_bus_recover(I2C0_BASE);
        (void)i2c_wait_busy(I2C0_BASE, BSP_I2C_OP_TIMEOUT_US);
    }
    (void)I2CMasterErr(I2C0_BASE);

    if (cfg != NULL) {
        I2CMasterDisable(I2C0_BASE);
        i2c_master_configure(cfg->base, cfg->clock_hz);
    }
}

static bool i2c_put_byte(uint32_t base, uint8_t value, bool finish)
{
    I2CMasterDataPut(base, value);
    I2CMasterControl(base, finish ? I2C_MASTER_CMD_BURST_SEND_FINISH : I2C_MASTER_CMD_BURST_SEND_CONT);
    return i2c_wait_idle(base, BSP_I2C_OP_TIMEOUT_US);
}

static bool i2c_write_locked(uint32_t base, uint8_t addr_7bit, uint8_t reg, const uint8_t *data, size_t len)
{
    size_t i;

    I2CMasterSlaveAddrSet(base, addr_7bit, false);
    I2CMasterControl(base, I2C_MASTER_CMD_BURST_SEND_START);
    if (!i2c_wait_idle(base, BSP_I2C_OP_TIMEOUT_US)) {
        return false;
    }

    if (!i2c_put_byte(base, reg, len == 0U)) {
        return false;
    }

    for (i = 0U; i < len; i++) {
        if (!i2c_put_byte(base, data[i], i == (len - 1U))) {
            return false;
        }
    }

    return true;
}

static bool i2c_read_locked(uint32_t base, uint8_t addr_7bit, uint8_t reg, uint8_t *data, size_t len)
{
    size_t i;

    if ((data == NULL) || (len == 0U)) {
        return false;
    }

    I2CMasterSlaveAddrSet(base, addr_7bit, false);
    I2CMasterControl(base, I2C_MASTER_CMD_BURST_SEND_START);
    if (!i2c_wait_idle(base, BSP_I2C_OP_TIMEOUT_US)) {
        return false;
    }

    I2CMasterDataPut(base, reg);
    I2CMasterControl(base, I2C_MASTER_CMD_BURST_SEND_FINISH);
    if (!i2c_wait_idle(base, BSP_I2C_OP_TIMEOUT_US)) {
        return false;
    }

    I2CMasterSlaveAddrSet(base, addr_7bit, true);
    if (len == 1U) {
        I2CMasterControl(base, I2C_MASTER_CMD_SINGLE_RECEIVE);
        if (!i2c_wait_idle(base, BSP_I2C_OP_TIMEOUT_US)) {
            return false;
        }
        data[0] = (uint8_t)I2CMasterDataGet(base);
        I2CMasterControl(base, I2C_MASTER_CMD_SINGLE_SEND);
        return i2c_wait_idle(base, BSP_I2C_OP_TIMEOUT_US);
    }

    I2CMasterControl(base, I2C_MASTER_CMD_BURST_RECEIVE_START);
    if (!i2c_wait_idle(base, BSP_I2C_OP_TIMEOUT_US)) {
        return false;
    }

    for (i = 0U; i < len; i++) {
        if (i == (len - 1U)) {
            I2CMasterControl(base, I2C_MASTER_CMD_BURST_RECEIVE_FINISH);
        } else {
            I2CMasterControl(base, I2C_MASTER_CMD_BURST_RECEIVE_CONT);
        }
        if (!i2c_wait_idle(base, BSP_I2C_OP_TIMEOUT_US)) {
            return false;
        }
        data[i] = (uint8_t)I2CMasterDataGet(base);
    }

    return true;
}

bool bsp_i2c_init(const bsp_i2c_config_t *cfg)
{
    uint32_t periph;

    if ((cfg == NULL) || (cfg->base == 0U) || (cfg->clock_hz == 0U)) {
        return false;
    }

    if (cfg->base == I2C0_BASE) {
        bsp_i2c0_gpio_scan_begin();
        return true;
    }

    periph = i2c_periph_from_base(cfg->base);
    if (periph == 0U) {
        return false;
    }

    SysCtlPeripheralEnable(periph);
    if (!bsp_periph_wait_ready(periph, BSP_I2C_PERIPH_READY_US)) {
        return false;
    }

    I2CMasterInitExpClk(cfg->base, bsp_clock_get_hz(), cfg->clock_hz > 100000U);
    I2CMasterTimeoutSet(cfg->base, BSP_I2C_MASTER_TIMEOUT);
    I2CMasterEnable(cfg->base);
    return true;
}

bool bsp_i2c_probe(uint32_t base, uint8_t addr_7bit)
{
    if (base == I2C0_BASE) {
        return bsp_i2c0_gpio_probe(addr_7bit);
    }

    bool ok;

    if (!bsp_bus_lock_i2c(base, 0U)) {
        return false;
    }

    I2CMasterSlaveAddrSet(base, addr_7bit, false);
    I2CMasterDataPut(base, 0x00U);
    I2CMasterControl(base, I2C_MASTER_CMD_SINGLE_SEND);
    if (!i2c_wait_busy(base, BSP_I2C_OP_TIMEOUT_US)) {
        i2c_bus_recover(base);
        bsp_bus_unlock_i2c(base);
        return false;
    }

    ok = (I2CMasterErr(base) == I2C_MASTER_ERR_NONE);
    if (!ok) {
        i2c_bus_recover(base);
        (void)I2CMasterErr(base);
    }

    bsp_delay_us(20U);
    bsp_bus_unlock_i2c(base);
    return ok;
}

void bsp_i2c_bus_release(uint32_t base, const bsp_i2c_config_t *cfg)
{
    if (base == I2C0_BASE) {
        i2c0_gpio_bus_recover(NULL, false);
        return;
    }

    uint32_t periph = i2c_periph_from_base(base);

    if (I2CMasterBusy(base)) {
        i2c_bus_recover(base);
        (void)i2c_wait_busy(base, BSP_I2C_OP_TIMEOUT_US);
    }
    (void)I2CMasterErr(base);
    I2CMasterDisable(base);

    if (periph != 0U) {
        SysCtlPeripheralReset(periph);
        SysCtlPeripheralEnable(periph);
        (void)bsp_periph_wait_ready(periph, BSP_I2C_PERIPH_READY_US);
    }

    if (cfg != NULL) {
        i2c_master_reinit(cfg);
    }
}

void bsp_i2c_bus_release_idle_high(uint32_t base)
{
    if (base == I2C0_BASE) {
        i2c0_gpio_bus_recover(NULL, false);
        return;
    }

    uint32_t periph = i2c_periph_from_base(base);

    if (I2CMasterBusy(base)) {
        i2c_bus_recover(base);
        (void)i2c_wait_busy(base, BSP_I2C_OP_TIMEOUT_US);
    }
    (void)I2CMasterErr(base);
    I2CMasterDisable(base);

    if (periph != 0U) {
        SysCtlPeripheralReset(periph);
        SysCtlPeripheralEnable(periph);
        (void)bsp_periph_wait_ready(periph, BSP_I2C_PERIPH_READY_US);
    }
}

bool bsp_i2c_write(uint32_t base, uint8_t addr_7bit, uint8_t reg, const uint8_t *data, size_t len)
{
    bool ok;

    if ((data == NULL) && (len > 0U)) {
        return false;
    }

    if (base == I2C0_BASE) {
        if (!bsp_bus_lock_i2c(base, 0U)) {
            return false;
        }

        ok = i2c0_bb_write_locked(addr_7bit, reg, data, len);
        bsp_bus_unlock_i2c(base);
        return ok;
    }

    if (!bsp_bus_lock_i2c(base, 0U)) {
        return false;
    }

    ok = i2c_write_locked(base, addr_7bit, reg, data, len);
    bsp_bus_unlock_i2c(base);
    return ok;
}

bool bsp_i2c_read(uint32_t base, uint8_t addr_7bit, uint8_t reg, uint8_t *data, size_t len)
{
    bool ok;

    if (base == I2C0_BASE) {
        if (!bsp_bus_lock_i2c(base, 0U)) {
            return false;
        }

        ok = i2c0_bb_read_locked(addr_7bit, reg, data, len);
        bsp_bus_unlock_i2c(base);
        return ok;
    }

    if (!bsp_bus_lock_i2c(base, 0U)) {
        return false;
    }

    ok = i2c_read_locked(base, addr_7bit, reg, data, len);
    bsp_bus_unlock_i2c(base);
    return ok;
}

bool bsp_i2c_write_byte(uint32_t base, uint8_t addr_7bit, uint8_t reg, uint8_t value)
{
    return bsp_i2c_write(base, addr_7bit, reg, &value, 1U);
}

bool bsp_i2c_read_byte(uint32_t base, uint8_t addr_7bit, uint8_t reg, uint8_t *value)
{
    return bsp_i2c_read(base, addr_7bit, reg, value, 1U);
}
