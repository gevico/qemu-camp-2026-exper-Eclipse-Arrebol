#include "qemu/osdep.h"
#include "hw/core/irq.h"
#include "hw/core/sysbus.h"
#include "qemu/log.h"
#include "qemu/timer.h"
#include <stdint.h>

#define TYPE_G233_SPI "g233-spi"
#define G233_SPI(obj) OBJECT_CHECK(G233SPIState, (obj), TYPE_G233_SPI)

#define FLASH_CMD_JEDEC_ID       0x9F
#define FLASH_CMD_READ_STATUS    0x05
#define FLASH_CMD_SECTOR_ERASE   0x20
#define FLASH_CMD_READ_DATA      0x03
#define FLASH_CMD_WRITE_ENABLE   0x06
#define FLASH_CMD_PAGE_PROGRAM   0x02

#define SPI_SR_RXNE     (1u << 0)
#define SPI_SR_TXE      (1u << 1)
#define SPI_SR_OVERRUN  (1u << 4)

#define SPI_CR1_ERRIE   (1u << 5)
#define SPI_CR1_RXNEIE  (1u << 6)
#define SPI_CR1_TXEIE   (1u << 7)

#define FLASH_SR_BUSY   0x01

/* PP 暂存 buffer，最多一个 page */
#define PP_BUF_MAX      256

/* 虚拟时间阈值：两次 DR 写入间隔超过此值视为事务结束
 * qtest_clock_step(100000) = 100us，阈值设为 50us 足够 */
#define XFER_GAP_NS     50000

typedef struct G233SPIState {
    SysBusDevice parent_obj;
    MemoryRegion mmio;

    uint32_t SPI_CR1;
    uint32_t SPI_CR2;
    uint32_t SPI_SR;
    uint32_t SPI_DR;

    uint32_t cmd;
    uint32_t rx_idx;
    uint8_t  rx_buf;
    uint8_t  addr[3];
    uint32_t send_addr;

    /* PP 延迟 commit 暂存 */
    uint8_t  pp_buf[PP_BUF_MAX];
    int      pp_len;
    uint32_t pp_base;       /* 本次 PP 的起始地址 */

    uint8_t  flash[2][4 * 1024 * 1024];
    uint8_t  flash_sr[2];
    int      cs;

    /* 上一次 DR 写入的虚拟时间戳 */
    int64_t  last_xfer_ns;

    qemu_irq irq;
} G233SPIState;

static void pp_commit(G233SPIState *s)
{
    /* 把暂存 buffer 刷入 flash */
    for (int i = 0; i < s->pp_len; i++) {
        uint32_t a = (s->pp_base & ~0xFFu)
                   | ((s->pp_base + i) & 0xFFu);  /* page 内回绕 */
        s->flash[s->cs][a] = s->pp_buf[i];
    }
    s->pp_len = 0;
}

static void pp_rollback(G233SPIState *s, int n)
{
    /* 回滚最后 n 个字节（它们其实是下一个命令的开头） */
    if (n > s->pp_len) n = s->pp_len;
    s->pp_len -= n;
}

static void end_xfer(G233SPIState *s)
{
    if (s->cmd == FLASH_CMD_PAGE_PROGRAM) {
        pp_commit(s);
        s->flash_sr[s->cs] &= ~FLASH_SR_BUSY;
    }
    s->cmd    = 0;
    s->rx_idx = 0;
    s->rx_buf = 0;
}

/* 根据当前 CR1 中断使能位和 SR 标志更新 IRQ 电平 */
static void g233_spi_update_irq(G233SPIState *s)
{
    int level = 0;

    if ((s->SPI_CR1 & SPI_CR1_TXEIE) && (s->SPI_SR & SPI_SR_TXE)) {
        level = 1;
    }
    if ((s->SPI_CR1 & SPI_CR1_RXNEIE) && (s->SPI_SR & SPI_SR_RXNE)) {
        level = 1;
    }
    if ((s->SPI_CR1 & SPI_CR1_ERRIE) && (s->SPI_SR & SPI_SR_OVERRUN)) {
        level = 1;
    }

    qemu_set_irq(s->irq, level);
}

static uint64_t g233_spi_read(void *opaque, hwaddr addr, unsigned size)
{
    G233SPIState *s = opaque;
    switch (addr) {
    case 0x00: return s->SPI_CR1;
    case 0x04: return s->SPI_CR2;
    case 0x08: return s->SPI_SR;
    case 0x0C:
        s->SPI_SR &= ~SPI_SR_RXNE;
        g233_spi_update_irq(s);
        return s->SPI_DR;
    }
    return 0;
}

static void g233_spi_write(void *opaque, hwaddr addr, uint64_t val,
                           unsigned size)
{
    G233SPIState *s = opaque;

    switch (addr) {
    case 0x00:
        s->SPI_CR1 = val;
        g233_spi_update_irq(s);
        break;

    case 0x04:
        /* CR2 写入视为切 CS，强制结束当前事务 */
        s->SPI_CR2 = val;
        end_xfer(s);
        s->cs = val & 0x3;
        break;

    case 0x08:
        /* Write-1-to-clear：只有 OVERRUN 等错误标志可由软件清零，
         * TXE/RXNE 由硬件管理，忽略软件写入 */
        if (val & SPI_SR_OVERRUN) {
            s->SPI_SR &= ~SPI_SR_OVERRUN;
        }
        g233_spi_update_irq(s);
        break;

    case 0x0C: {
        /* 事务边界检测：距上次 DR 写入的虚拟时间跳跃 */
        int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        int64_t gap = now - s->last_xfer_ns;
        s->last_xfer_ns = now;

        if (gap > XFER_GAP_NS && s->cmd == FLASH_CMD_PAGE_PROGRAM) {
            /* 时间跳跃发生在 flash_wait_busy 的 clock_step。
             * 此时 PP 后面已经混入了 flash_read_status 的 2 个字节
             * (0x05, 0x00)，它们被暂存在 pp_buf 末尾，回滚掉。 */
            pp_rollback(s, 2);
            pp_commit(s);
            s->flash_sr[s->cs] &= ~FLASH_SR_BUSY;
            s->cmd    = 0;
            s->rx_idx = 0;
            s->rx_buf = 0;
            /* 当前写入的 val 实际上是 flash_wait_busy 下一轮的新 opcode
             * 但我们已错过它 —— 不过 flash_wait_busy 看到 BUSY=0 后
             * 就 break 了，所以这里继续把 val 当新命令处理 */
        }

        if (s->cmd == 0) {
            s->cmd    = val & 0xFF;
            s->rx_idx = 0;
            s->rx_buf = 0;

            switch (s->cmd) {
            case FLASH_CMD_WRITE_ENABLE:
                /* 单字节命令 */
                s->cmd = 0;
                break;
            case FLASH_CMD_PAGE_PROGRAM:
                s->pp_len = 0;
                s->flash_sr[s->cs] |= FLASH_SR_BUSY;
                break;
            }
        } else {
            switch (s->cmd) {
            case FLASH_CMD_JEDEC_ID: {
                static const uint8_t jedec[2][3] = {
                    { 0xEF, 0x30, 0x15 },
                    { 0xEF, 0x30, 0x16 },
                };
                s->rx_buf = jedec[s->cs][s->rx_idx];
                s->rx_idx++;
                if (s->rx_idx >= 3) {
                    s->rx_idx = 0;
                    s->cmd = 0;
                }
                break;
            }

            case FLASH_CMD_READ_STATUS:
                s->rx_buf = s->flash_sr[s->cs];
                s->cmd = 0;
                break;

            case FLASH_CMD_PAGE_PROGRAM:
                if (s->rx_idx < 3) {
                    s->addr[2 - s->rx_idx] = val & 0xFF;
                    s->rx_idx++;
                    if (s->rx_idx == 3) {
                        s->send_addr = (s->addr[2] << 16)
                                     | (s->addr[1] << 8)
                                     |  s->addr[0];
                        s->pp_base = s->send_addr;
                        s->pp_len  = 0;
                    }
                } else {
                    /* 写入暂存 buffer，不立刻写 flash */
                    if (s->pp_len < PP_BUF_MAX) {
                        s->pp_buf[s->pp_len++] = val & 0xFF;
                    }
                    s->send_addr = (s->send_addr & ~0xFFu)
                                 | ((s->send_addr + 1) & 0xFFu);
                    /* 返回 BUSY：这样如果这个字节其实是 flash_read_status
                     * 的 dummy，flash_wait_busy 会触发 clock_step */
                    s->rx_buf = s->flash_sr[s->cs];
                    /* 写满整 page → 自动结束 */
                    if (s->pp_len >= PP_BUF_MAX) {
                        pp_commit(s);
                        s->flash_sr[s->cs] &= ~FLASH_SR_BUSY;
                        s->cmd    = 0;
                        s->rx_idx = 0;
                    }
                }
                break;

            case FLASH_CMD_READ_DATA:
                if (s->rx_idx < 3) {
                    s->addr[2 - s->rx_idx] = val & 0xFF;
                    s->rx_idx++;
                    if (s->rx_idx == 3) {
                        s->send_addr = (s->addr[2] << 16)
                                     | (s->addr[1] << 8)
                                     |  s->addr[0];
                    }
                } else {
                    s->rx_buf = s->flash[s->cs][s->send_addr];
                    s->send_addr++;
                }
                break;

            case FLASH_CMD_SECTOR_ERASE:
                s->addr[2 - s->rx_idx] = val & 0xFF;
                s->rx_idx++;
                if (s->rx_idx >= 3) {
                    uint32_t a = (s->addr[2] << 16)
                               | (s->addr[1] << 8)
                               |  s->addr[0];
                    memset(&s->flash[s->cs][a & ~0xFFFu], 0xFF, 0x1000);
                    s->flash_sr[s->cs] &= ~FLASH_SR_BUSY;
                    s->cmd    = 0;
                    s->rx_idx = 0;
                }
                break;
            }
        }

        s->SPI_DR  = s->rx_buf;
        /* Overrun 检测：新数据到来前如果 RXNE 还是 1，说明软件没读走上一个，
         * 置 OVERRUN */
        if (s->SPI_SR & SPI_SR_RXNE) {
            s->SPI_SR |= SPI_SR_OVERRUN;
        }
        s->SPI_SR |= SPI_SR_TXE | SPI_SR_RXNE;
        g233_spi_update_irq(s);
        break;
    }
    }
}

static const MemoryRegionOps g233_spi_ops = {
    .read = g233_spi_read,
    .write = g233_spi_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};

static void g233_spi_realize(DeviceState *dev, Error **errp)
{
    G233SPIState *s = G233_SPI(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    memory_region_init_io(&s->mmio, OBJECT(s), &g233_spi_ops, s,
                          TYPE_G233_SPI, 0x1000);
    sysbus_init_mmio(sbd, &s->mmio);
    sysbus_init_irq(sbd, &s->irq);

    s->SPI_SR = SPI_SR_TXE;
    memset(s->flash, 0xFF, sizeof(s->flash));
}

static void g233_spi_reset(DeviceState *dev)
{
    G233SPIState *s = G233_SPI(dev);
    s->SPI_CR1     = 0;
    s->SPI_CR2     = 0;
    s->SPI_SR      = SPI_SR_TXE;
    s->SPI_DR      = 0;
    s->cmd         = 0;
    s->rx_idx      = 0;
    s->rx_buf      = 0;
    s->pp_len      = 0;
    s->flash_sr[0] = 0;
    s->flash_sr[1] = 0;
    s->last_xfer_ns = 0;
    qemu_set_irq(s->irq, 0);
}

static void g233_spi_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->realize = g233_spi_realize;
    device_class_set_legacy_reset(dc, g233_spi_reset);
}

static const TypeInfo g233_spi_info = {
    .name          = TYPE_G233_SPI,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(G233SPIState),
    .class_init    = g233_spi_class_init,
};

static void g233_spi_register_types(void)
{
    type_register_static(&g233_spi_info);
}

type_init(g233_spi_register_types)