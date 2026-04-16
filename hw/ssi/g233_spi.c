#include "qemu/osdep.h"
#include "hw/core/irq.h"
#include "hw/core/sysbus.h"
#include "qemu/log.h"

#include "qemu/timer.h"
#include <stdint.h>

#define TYPE_G233_SPI "g233-spi"
#define G233_SPI(obj) OBJECT_CHECK(G233SPIState, (obj), TYPE_G233_SPI)
#define FLASH_CMD_JEDEC_ID  0x9F


#define SPI_SR_RXNE     (1u << 0)
#define SPI_SR_TXE      (1u << 1)


typedef struct G233SPIState {
    SysBusDevice parent_obj;
    MemoryRegion mmio;

    uint32_t SPI_CR1;
    uint32_t SPI_CR2;
    uint32_t SPI_SR;
    uint32_t SPI_DR;
    uint32_t cmd;
    uint32_t rx_idx ;
    uint8_t rx_buf;
    uint8_t flash[2][4 * 1024 * 1024];  // CS0=2MB, CS1=4MB
    int cs;  // 当前选中的片选



    qemu_irq irq;
} G233SPIState;



static uint64_t g233_spi_read(void *opaque, hwaddr addr, unsigned size) {
    G233SPIState *s = opaque;
    switch (addr) {
        case 0x00:
            return s->SPI_CR1;
        case 0x04:
            return s->SPI_CR2;
        case 0x08:
            return s->SPI_SR;
        case 0x0C:
            s->SPI_SR &= ~0x1;
            return s->SPI_DR;
    }

    return 0;
}

static void g233_spi_write(void *opaque, hwaddr addr, uint64_t val,
                           unsigned size) {
    G233SPIState *s = opaque;
    switch (addr) {
        case 0x00:
            s->SPI_CR1 = val;
            break;
        case 0x04:
            s->SPI_CR2 = val;
            s->cs = val & 0x3;
            s->cmd = 0;  // 切换CS时重置命令
            s->rx_idx = 0;
            break;


        case 0x08:
            s->SPI_SR = val;
            break;
        case 0x0C:
            if (s->cmd == 0) {
            s->cmd = val & 0xFF;
            s->rx_idx = 0;
            s->rx_buf = 0x00;
            } else {
                if (s->cmd == 0x9F) {
                    uint8_t jedec[2][3] = {
                        {0xEF, 0x30, 0x15},  // CS0
                        {0xEF, 0x30, 0x16},  // CS1
                    };
                    s->rx_buf = jedec[s->cs][s->rx_idx++];
                    if (s->rx_idx >= 3) s->rx_idx = 0;
                }
            }
            s->SPI_DR = s->rx_buf;  // 把回传数据写进 DR
            s->SPI_SR |= SPI_SR_TXE | SPI_SR_RXNE;
            break;
            }
        
    
}





static const MemoryRegionOps g233_spi_ops = {
    .read = g233_spi_read,
    .write = g233_spi_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};


static void g233_spi_realize(DeviceState *dev, Error **errp) {
    G233SPIState *s = G233_SPI(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    memory_region_init_io(&s->mmio, OBJECT(s), &g233_spi_ops, s, TYPE_G233_SPI,
                          0x1000);
    sysbus_init_mmio(sbd, &s->mmio);
    sysbus_init_irq(sbd, &s->irq);
    s->SPI_SR = 0x00000002;

    
}


static void g233_wdt_reset(DeviceState *dev) {
    G233SPIState *s = G233_SPI(dev);
    s->SPI_CR1 = 0x00000000;
    s->SPI_CR2 = 0x00000000;
    s->SPI_SR = 0x00000002;
    s->SPI_DR = 0x00000000;
    s->rx_idx = 0;
    s->cmd = 0;
    

}


static void g233_wdt_class_init(ObjectClass *klass, const void *data) {
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->realize = g233_spi_realize;
    device_class_set_legacy_reset(dc, g233_wdt_reset);
}


static const TypeInfo g233_spi_info = {
    .name = TYPE_G233_SPI,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(G233SPIState),
    .class_init = g233_wdt_class_init,
};

static void g233_spi_register_types(void) {
    type_register_static(&g233_spi_info);
}

type_init(g233_spi_register_types)

