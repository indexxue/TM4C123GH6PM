/**
 * @file bsp_spi.c
 * @brief TM4C123 SSI（SPI）主机收发 + 从机固定 32B 帧（ISR 补 FIFO）
 */

#include "bsp_spi.h"

#include "bsp_bus_lock.h"
#include "bsp_systick.h"
#include "bsp_sysctl.h"

#include "bsp_config.h"

#include "driverlib/interrupt.h"
#include "driverlib/ssi.h"
#include "driverlib/sysctl.h"
#include "inc/hw_ints.h"
#include "inc/hw_memmap.h"

#include <string.h>

#define BSP_SPI_PERIPH_READY_US 100000U
#define BSP_SPI_OP_TIMEOUT_US   10000U
#define BSP_SPI_SLAVE_SLOTS     4U
/* 1MHz 下字节间隙约 8µs；空转阈值轮仍无 RX 则本拍 ISR 退出，等下次中断 */
#define BSP_SPI_SLAVE_IDLE_SPINS 8000U

typedef struct {
    uint32_t base;
    bool configured;
    bool running;
    uint8_t tx_cur[BSP_SPI_SLAVE_FRAME_LEN];
    uint8_t tx_next[BSP_SPI_SLAVE_FRAME_LEN];
    uint8_t rx_cur[BSP_SPI_SLAVE_FRAME_LEN];
    uint8_t rx_done[BSP_SPI_SLAVE_FRAME_LEN];
    volatile uint8_t tx_idx;
    volatile uint8_t rx_idx;
    volatile bool tx_next_pending;
    volatile bool rx_ready;
    volatile uint32_t isr_hits;
    volatile uint32_t rx_bytes;
    volatile uint32_t frames_done;
    volatile uint32_t rxor_hits;
    volatile uint32_t tx_underrun;
    volatile uint32_t tx_puts;
    bsp_spi_slave_frame_cb_t cb;
    void *user;
} spi_slave_slot_t;

static bsp_spi_role_t s_role_by_base[BSP_SPI_SLAVE_SLOTS];
static spi_slave_slot_t s_slave[BSP_SPI_SLAVE_SLOTS];

static uint32_t spi_periph_from_base(uint32_t base)
{
    switch (base) {
    case SSI0_BASE:
        return SYSCTL_PERIPH_SSI0;
    case SSI1_BASE:
        return SYSCTL_PERIPH_SSI1;
    case SSI2_BASE:
        return SYSCTL_PERIPH_SSI2;
    case SSI3_BASE:
        return SYSCTL_PERIPH_SSI3;
    default:
        return 0U;
    }
}

static int spi_slot_index(uint32_t base)
{
    switch (base) {
    case SSI0_BASE:
        return 0;
    case SSI1_BASE:
        return 1;
    case SSI2_BASE:
        return 2;
    case SSI3_BASE:
        return 3;
    default:
        return -1;
    }
}

static uint32_t spi_irq_from_base(uint32_t base)
{
    switch (base) {
    case SSI0_BASE:
        return INT_SSI0;
    case SSI1_BASE:
        return INT_SSI1;
    case SSI2_BASE:
        return INT_SSI2;
    case SSI3_BASE:
        return INT_SSI3;
    default:
        return 0U;
    }
}

static uint32_t spi_frame_mode(bsp_spi_mode_t mode)
{
    switch (mode) {
    case BSP_SPI_MODE_1:
        return SSI_FRF_MOTO_MODE_1;
    case BSP_SPI_MODE_2:
        return SSI_FRF_MOTO_MODE_2;
    case BSP_SPI_MODE_3:
        return SSI_FRF_MOTO_MODE_3;
    case BSP_SPI_MODE_0:
    default:
        return SSI_FRF_MOTO_MODE_0;
    }
}

static void spi_drain_rx(uint32_t base)
{
    uint32_t junk;

    while (SSIDataGetNonBlocking(base, &junk)) {
    }
}

static bool spi_wait_idle(uint32_t base, uint32_t timeout_us)
{
    bsp_timeout_t timeout;

    bsp_timeout_start_us(&timeout, timeout_us);
    while (SSIBusy(base)) {
        if (bsp_timeout_expired(&timeout)) {
            return false;
        }
    }

    return true;
}

static bool spi_transfer_locked(uint32_t base, uint8_t tx, uint8_t *rx)
{
    if (spi_periph_from_base(base) == 0U) {
        return false;
    }

    if (!spi_wait_idle(base, BSP_SPI_OP_TIMEOUT_US)) {
        return false;
    }

    SSIDataPut(base, tx);
    if (!spi_wait_idle(base, BSP_SPI_OP_TIMEOUT_US)) {
        return false;
    }

    if (rx != NULL) {
        uint32_t raw;

        SSIDataGet(base, &raw);
        *rx = (uint8_t)raw;
    } else {
        uint32_t junk;

        SSIDataGet(base, &junk);
    }

    return true;
}

static bool spi_is_master(uint32_t base)
{
    int idx = spi_slot_index(base);

    if (idx < 0) {
        return false;
    }
    return s_role_by_base[idx] == BSP_SPI_ROLE_MASTER;
}

/**
 * 硬件 TX FIFO 深 8：一次最多写入 8B。软件帧 32B，靠传输中持续补仓。
 * @return 本轮写入 FIFO 的字节数
 */
static uint8_t slave_preload_tx(spi_slave_slot_t *slot)
{
    uint32_t word;
    uint8_t n = 0U;

    while (slot->tx_idx < BSP_SPI_SLAVE_FRAME_LEN) {
        word = slot->tx_cur[slot->tx_idx];
        if (!SSIDataPutNonBlocking(slot->base, word)) {
            break;
        }
        slot->tx_idx++;
        slot->tx_puts++;
        n++;
    }
    return n;
}

static void slave_apply_pending_tx(spi_slave_slot_t *slot)
{
    if (slot->tx_next_pending) {
        (void)memcpy(slot->tx_cur, slot->tx_next, BSP_SPI_SLAVE_FRAME_LEN);
        slot->tx_next_pending = false;
    }
}

/** CS 间隙：换入下一拍 32B 缓冲，清 RX 采样区（0x00，绝不用 0x5A 预填），填满 TX FIFO（≤8） */
static void slave_arm_next_frame(spi_slave_slot_t *slot)
{
    slave_apply_pending_tx(slot);
    slot->tx_idx = 0U;
    slot->rx_idx = 0U;
    /* 显式清零：若日志仍见 5A，必来自 SSI 采样，而非软件预填 */
    (void)memset(slot->rx_cur, 0, BSP_SPI_SLAVE_FRAME_LEN);
    (void)slave_preload_tx(slot);
    SSIIntEnable(slot->base, SSI_TXFF | SSI_RXFF | SSI_RXTO | SSI_RXOR);
}

static void slave_frame_complete(spi_slave_slot_t *slot)
{
    (void)memcpy(slot->rx_done, slot->rx_cur, BSP_SPI_SLAVE_FRAME_LEN);
    slot->rx_ready = true;
    slot->frames_done++;

    slave_arm_next_frame(slot);

    if (slot->cb != NULL) {
        slot->cb(slot->rx_done, slot->user);
    }
}

/**
 * 收齐本拍：每读 1B RX 立刻补 TX，并在 ISR 内自旋直到 32B 或主机关钟。
 * 避免「只进一次 ISR、FIFO 吐空」导致 ESP 只看到 5A 后全 00/FF。
 */
static void slave_pump(spi_slave_slot_t *slot)
{
    uint32_t word;
    uint32_t idle = 0U;

    while (slot->rx_idx < BSP_SPI_SLAVE_FRAME_LEN) {
        (void)slave_preload_tx(slot);

        if (SSIDataGetNonBlocking(slot->base, &word)) {
            slot->rx_cur[slot->rx_idx] = (uint8_t)word;
            slot->rx_idx++;
            slot->rx_bytes++;
            idle = 0U;

            /* 已收字节超过已排队 TX → 发生过 TX underrun */
            if (slot->rx_idx > slot->tx_idx) {
                slot->tx_underrun++;
            }

            (void)slave_preload_tx(slot);

            if (slot->rx_idx >= BSP_SPI_SLAVE_FRAME_LEN) {
                slave_frame_complete(slot);
                return;
            }
        } else {
            idle++;
            if (idle >= BSP_SPI_SLAVE_IDLE_SPINS) {
                break;
            }
        }
    }

    (void)slave_preload_tx(slot);
}

static void slave_isr(spi_slave_slot_t *slot)
{
    uint32_t status;

    if ((slot == NULL) || !slot->running) {
        return;
    }

    slot->isr_hits++;
    status = SSIIntStatus(slot->base, true);
    SSIIntClear(slot->base, status);

    if ((status & SSI_RXOR) != 0U) {
        slot->rxor_hits++;
        spi_drain_rx(slot->base);
        slave_arm_next_frame(slot);
        return;
    }

    /* 先补满 FIFO，再抽 RX（含 TXFF 触发时也泵，防止欠载） */
    (void)slave_preload_tx(slot);

    if ((status & (SSI_RXFF | SSI_RXTO | SSI_TXFF)) != 0U) {
        slave_pump(slot);
    }

    if (slot->tx_idx < BSP_SPI_SLAVE_FRAME_LEN) {
        SSIIntEnable(slot->base, SSI_TXFF);
    }
}

static void ssi0_slave_isr(void)
{
    slave_isr(&s_slave[0]);
}

static void ssi1_slave_isr(void)
{
    slave_isr(&s_slave[1]);
}

static void ssi2_slave_isr(void)
{
    slave_isr(&s_slave[2]);
}

static void ssi3_slave_isr(void)
{
    slave_isr(&s_slave[3]);
}

static void (*slave_isr_fn(uint32_t base))(void)
{
    switch (base) {
    case SSI0_BASE:
        return ssi0_slave_isr;
    case SSI1_BASE:
        return ssi1_slave_isr;
    case SSI2_BASE:
        return ssi2_slave_isr;
    case SSI3_BASE:
        return ssi3_slave_isr;
    default:
        return NULL;
    }
}

bool bsp_spi_init(const bsp_spi_config_t *cfg)
{
    uint32_t periph;
    uint8_t data_bits;
    int idx;
    uint32_t ssi_role;

    if ((cfg == NULL) || (cfg->base == 0U) || (cfg->clock_hz == 0U)) {
        return false;
    }

    idx = spi_slot_index(cfg->base);
    if (idx < 0) {
        return false;
    }

    data_bits = cfg->data_bits;
    if ((data_bits < 4U) || (data_bits > 16U)) {
        data_bits = 8U;
    }

    periph = spi_periph_from_base(cfg->base);
    if (periph == 0U) {
        return false;
    }

    SysCtlPeripheralEnable(periph);
    if (!bsp_periph_wait_ready(periph, BSP_SPI_PERIPH_READY_US)) {
        return false;
    }

    SSIDisable(cfg->base);
    ssi_role = (cfg->role == BSP_SPI_ROLE_SLAVE) ? SSI_MODE_SLAVE : SSI_MODE_MASTER;
    /* Freescale Mode0 = SPO=0 SPH=0；TivaWare 固定 MSB first；data_bits=8 */
    SSIConfigSetExpClk(cfg->base, bsp_clock_get_hz(), spi_frame_mode(cfg->mode),
                       ssi_role, cfg->clock_hz, data_bits);
    SSIEnable(cfg->base);
    spi_drain_rx(cfg->base);

    s_role_by_base[idx] = cfg->role;
    s_slave[idx].base = cfg->base;
    s_slave[idx].configured = true;
    s_slave[idx].running = false;
    return true;
}

void bsp_spi_cs_set(const bsp_spi_cs_t *cs, bool selected)
{
    bool level;

    if ((cs == NULL) || (cs->cs_pin == NULL)) {
        return;
    }

    level = cs->cs_active_low ? !selected : selected;
    bsp_gpio_write(cs->cs_pin, level);
}

bool bsp_spi_transfer(uint32_t base, uint8_t tx, uint8_t *rx)
{
    bool ok;

    if (!spi_is_master(base)) {
        return false;
    }

    if (!bsp_bus_lock_spi(base, 0U)) {
        return false;
    }

    ok = spi_transfer_locked(base, tx, rx);
    bsp_bus_unlock_spi(base);
    return ok;
}

bool bsp_spi_write(uint32_t base, const uint8_t *data, size_t len)
{
    return bsp_spi_transceive(base, NULL, data, NULL, len);
}

bool bsp_spi_read(uint32_t base, uint8_t *data, size_t len)
{
    return bsp_spi_transceive(base, NULL, NULL, data, len);
}

bool bsp_spi_transceive(uint32_t base, const bsp_spi_cs_t *cs,
                        const uint8_t *tx, uint8_t *rx, size_t len)
{
    size_t i;
    bool ok = true;

    if (!spi_is_master(base)) {
        return false;
    }

    if (((tx == NULL) && (rx == NULL)) || (len == 0U)) {
        return false;
    }

    if (!bsp_bus_lock_spi(base, 0U)) {
        return false;
    }

    bsp_spi_cs_set(cs, true);
    for (i = 0U; i < len; i++) {
        uint8_t out = (tx != NULL) ? tx[i] : 0xFFU;
        uint8_t in;

        if (!spi_transfer_locked(base, out, &in)) {
            ok = false;
            break;
        }

        if (rx != NULL) {
            rx[i] = in;
        }
    }
    bsp_spi_cs_set(cs, false);
    bsp_bus_unlock_spi(base);
    return ok;
}

bool bsp_spi_slave_start(uint32_t base, const uint8_t *initial_tx,
                         bsp_spi_slave_frame_cb_t cb, void *user)
{
    int idx;
    spi_slave_slot_t *slot;
    void (*handler)(void);
    uint32_t irqn;

    idx = spi_slot_index(base);
    if (idx < 0) {
        return false;
    }
    if (s_role_by_base[idx] != BSP_SPI_ROLE_SLAVE) {
        return false;
    }

    slot = &s_slave[idx];
    if (!slot->configured) {
        return false;
    }

    handler = slave_isr_fn(base);
    irqn = spi_irq_from_base(base);
    if ((handler == NULL) || (irqn == 0U)) {
        return false;
    }

    SSIIntDisable(base, SSI_TXFF | SSI_RXFF | SSI_RXTO | SSI_RXOR);
    SSIIntClear(base, SSI_TXFF | SSI_RXFF | SSI_RXTO | SSI_RXOR);

    /* 关外设清 FIFO，再装首拍，避免残留导致只吐少量字节 */
    SSIDisable(base);
    spi_drain_rx(base);
    SSIEnable(base);
    spi_drain_rx(base);

    if (initial_tx != NULL) {
        (void)memcpy(slot->tx_cur, initial_tx, BSP_SPI_SLAVE_FRAME_LEN);
    } else {
        (void)memset(slot->tx_cur, 0, BSP_SPI_SLAVE_FRAME_LEN);
    }
    (void)memset(slot->tx_next, 0, BSP_SPI_SLAVE_FRAME_LEN);
    (void)memset(slot->rx_cur, 0, BSP_SPI_SLAVE_FRAME_LEN);
    (void)memset(slot->rx_done, 0, BSP_SPI_SLAVE_FRAME_LEN);
    slot->tx_idx = 0U;
    slot->rx_idx = 0U;
    slot->tx_next_pending = false;
    slot->rx_ready = false;
    slot->isr_hits = 0U;
    slot->rx_bytes = 0U;
    slot->frames_done = 0U;
    slot->rxor_hits = 0U;
    slot->tx_underrun = 0U;
    slot->tx_puts = 0U;
    slot->cb = cb;
    slot->user = user;
    slot->running = true;

    (void)slave_preload_tx(slot);

    SSIIntRegister(base, handler);
    /* 高于 FreeRTOS syscall 屏蔽线，保证补 FIFO 不被临界区拖死（ISR 不调 RTOS API） */
    IntPrioritySet(irqn, 0x40U);
    IntEnable(irqn);
    SSIIntEnable(base, SSI_TXFF | SSI_RXFF | SSI_RXTO | SSI_RXOR);
    return true;
}

bool bsp_spi_slave_load_tx(uint32_t base, const uint8_t *tx)
{
    int idx;
    spi_slave_slot_t *slot;
    bool ints_were_disabled;

    if (tx == NULL) {
        return false;
    }

    idx = spi_slot_index(base);
    if (idx < 0) {
        return false;
    }

    slot = &s_slave[idx];
    if (!slot->running) {
        return false;
    }

    /* 仅挂起下一拍 TX；在 frame_complete 时换入并预装 FIFO，避免打断进行中的交换 */
    ints_were_disabled = IntMasterDisable();
    (void)memcpy(slot->tx_next, tx, BSP_SPI_SLAVE_FRAME_LEN);
    slot->tx_next_pending = true;
    if (!ints_were_disabled) {
        IntMasterEnable();
    }
    return true;
}

bool bsp_spi_slave_take_rx(uint32_t base, uint8_t *rx)
{
    int idx;
    spi_slave_slot_t *slot;
    bool ints_were_disabled;
    bool ready;

    if (rx == NULL) {
        return false;
    }

    idx = spi_slot_index(base);
    if (idx < 0) {
        return false;
    }

    slot = &s_slave[idx];
    ints_were_disabled = IntMasterDisable();
    ready = slot->rx_ready;
    if (ready) {
        (void)memcpy(rx, slot->rx_done, BSP_SPI_SLAVE_FRAME_LEN);
        slot->rx_ready = false;
    }
    if (!ints_were_disabled) {
        IntMasterEnable();
    }
    return ready;
}

void bsp_spi_slave_stop(uint32_t base)
{
    int idx;
    spi_slave_slot_t *slot;
    uint32_t irqn;

    idx = spi_slot_index(base);
    if (idx < 0) {
        return;
    }

    slot = &s_slave[idx];
    if (!slot->running) {
        return;
    }

    SSIIntDisable(base, SSI_TXFF | SSI_RXFF | SSI_RXTO | SSI_RXOR);
    irqn = spi_irq_from_base(base);
    if (irqn != 0U) {
        IntDisable(irqn);
    }
    SSIIntUnregister(base);
    slot->running = false;
    slot->cb = NULL;
    slot->user = NULL;
}

bool bsp_spi_slave_get_diag(uint32_t base, bsp_spi_slave_diag_t *out)
{
    int idx;
    spi_slave_slot_t *slot;
    bool ints_were_disabled;

    if (out == NULL) {
        return false;
    }

    idx = spi_slot_index(base);
    if (idx < 0) {
        return false;
    }

    slot = &s_slave[idx];
    ints_were_disabled = IntMasterDisable();
    out->isr_hits = slot->isr_hits;
    out->rx_bytes = slot->rx_bytes;
    out->frames_done = slot->frames_done;
    out->rxor_hits = slot->rxor_hits;
    out->tx_underrun = slot->tx_underrun;
    out->tx_puts = slot->tx_puts;
    out->rx_idx = slot->rx_idx;
    out->tx_idx = slot->tx_idx;
    if (!ints_were_disabled) {
        IntMasterEnable();
    }
    return slot->running;
}
