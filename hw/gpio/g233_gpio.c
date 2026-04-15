#include "qemu/osdep.h"
#include "hw/core/irq.h"
#include "hw/core/sysbus.h"
#include "qemu/log.h"


#define TYPE_G233_GPIO "g233-gpio"
#define G233_GPIO(obj) OBJECT_CHECK(G233GPIOState, (obj), TYPE_G233_GPIO)

typedef struct G233GPIOState {
    SysBusDevice parent_obj;
    MemoryRegion mmio;

    uint32_t dir;
    uint32_t out;
    uint32_t ie;     //中断使能
    uint32_t is;     //中断
    uint32_t trig;  //触发方式
    uint32_t pol;   //极性

    qemu_irq irq;
} G233GPIOState;

static uint64_t g233_gpio_read(void *opaque, hwaddr addr, unsigned size) {
    G233GPIOState *s = opaque;
    switch (addr) {
    case 0x00:
        return s->dir;
    case 0x04:
        return s->out;
    case 0x08:
        return s->out & s->dir;
    case 0x0C:
        return s->ie;
    case 0x10:
        return s->is;
    case 0x14:
        return s->trig;
    case 0x18:
        return s->pol;
    default:
        return 0;
    }
}

static void g233_gpio_write(void *opaque, hwaddr addr, uint64_t val,
                            unsigned size) {
    G233GPIOState *s = opaque;
    switch (addr) {
    case 0x00:
        s->dir = val;
        break;
    case 0x04: {
        uint32_t old_out = s->out;
        s->out = val;
        for (int n = 0; n < 32; n++) {
            uint32_t ie_n = (s->ie >> n) & 1;
            uint32_t trig_n = (s->trig >> n) & 1;
            uint32_t pol_n = (s->pol >> n) & 1;
            uint32_t old_bit = (old_out >> n) & 1;
            uint32_t new_bit = (val >> n) & 1;

            if (!ie_n)
                continue; // IE 未使能，跳过

            if (trig_n == 0) { // 边沿触发
                if (pol_n == 1 && old_bit == 0 && new_bit == 1)
                    s->is |= (1u << n);
                if (pol_n == 0 && old_bit == 1 && new_bit == 0)
                    s->is |= (1u << n);
            } else { // 电平触发
                if (pol_n == 1 && new_bit == 1)
                    s->is |= (1u << n);
                else if (pol_n == 0 && new_bit == 0)
                    s->is |= (1u << n);
                else
                    s->is &= ~(1u << n); // 电平消失，清零
            }
        }
        qemu_set_irq(s->irq, !!(s->is & s->ie));
        break;
    }

    case 0x08:
        break;
    case 0x0C:
        s->ie = val;
        break;
    case 0x10:
        s->is &= ~val;
        break;
    case 0x14:
        s->trig = val;
        break;
    case 0x18:
        s->pol = val;
        break;
    }
}

static const MemoryRegionOps g233_gpio_ops = {
    .read = g233_gpio_read,
    .write = g233_gpio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};

static void g233_gpio_reset(DeviceState *dev) {
    G233GPIOState *s = G233_GPIO(dev);
    s->dir = s->out = s->ie = s->is = s->trig = s->pol = 0;
}

static void g233_gpio_realize(DeviceState *dev, Error **errp) {
    G233GPIOState *s = G233_GPIO(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    memory_region_init_io(&s->mmio, OBJECT(s), &g233_gpio_ops, s,
                          TYPE_G233_GPIO, 0x1000);
    sysbus_init_mmio(sbd, &s->mmio);
    sysbus_init_irq(sbd, &s->irq);
}

static void g233_gpio_class_init(ObjectClass *klass, const void *data) {
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->realize = g233_gpio_realize;
    device_class_set_legacy_reset(dc, g233_gpio_reset);
}

static const TypeInfo g233_gpio_info = {
    .name = TYPE_G233_GPIO,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(G233GPIOState),
    .class_init = g233_gpio_class_init,
};

static void g233_gpio_register_types(void) {
    type_register_static(&g233_gpio_info);
}

type_init(g233_gpio_register_types)
