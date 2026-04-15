#include "qemu/osdep.h"
#include "hw/core/irq.h"
#include "hw/core/sysbus.h"
#include "qemu/log.h"

#include "qemu/timer.h"

#define TYPE_G233_WDT "g233-wdt"
#define G233_WDT(obj) OBJECT_CHECK(G233WDTState, (obj), TYPE_G233_WDT)

#define WDT_KEY_FEED    0x5A5A5A5A
#define WDT_KEY_LOCK    0x1ACCE551

typedef struct G233WDTState {
    SysBusDevice parent_obj;
    MemoryRegion mmio;

    uint32_t WDT_CTRL;
    uint32_t WDT_LOAD;
    uint32_t WDT_VAL;
    uint32_t WDT_SR;
    uint32_t WDT_KEY;

    QEMUTimer *timer;


    qemu_irq irq;
} G233WDTState;

static void wdt_timer_cb(void *opaque) {
    G233WDTState *s = opaque;

    // 重新 arm，1ns 后再触发
    if (!(s->WDT_CTRL & 0x1)) return;
    if (s->WDT_VAL > 0) s->WDT_VAL--;
    if (s->WDT_VAL == 0) {
        s->WDT_SR |= 0x1;  // TIMEOUT
        if (s->WDT_CTRL & 0x2)  // INTEN
            qemu_set_irq(s->irq, 1);
        return;  // 停止，不再 arm
    }
    timer_mod(s->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 1000000);
}


static uint64_t g233_pwm_read(void *opaque, hwaddr addr, unsigned size) {
    G233WDTState *s = opaque;
    switch (addr) {
        case 0x00:
            return s->WDT_CTRL;
        case 0x04:
            return s->WDT_LOAD;

        case 0x08:
            return s->WDT_VAL;
        case 0x0C:
            return s->WDT_KEY;
            
        case 0x10:
            return s->WDT_SR;
            

    }

    return 0;
}

static void g233_pwm_write(void *opaque, hwaddr addr, uint64_t val,
                           unsigned size) {
    G233WDTState *s = opaque;
    switch (addr) {
        case 0x00:

            if(s->WDT_KEY != WDT_KEY_LOCK)
            {
                if(val&0x01)
                {
                    timer_mod(s->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 1000000);
                }
                s->WDT_CTRL = val;
            }
                
            break;
        case 0x04:
            s->WDT_LOAD = val;
            s->WDT_VAL = val;
            break;
        case 0x0C:
            if(val == WDT_KEY_FEED)
                s->WDT_VAL = s->WDT_LOAD;
            s->WDT_KEY = val;
            
            break;
        case 0x10:
            s->WDT_SR &= ~(val & 0x1);
            break;
    
    }
    
}





static const MemoryRegionOps g233_wdt_ops = {
    .read = g233_pwm_read,
    .write = g233_pwm_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};


static void g233_wdt_realize(DeviceState *dev, Error **errp) {
    G233WDTState *s = G233_WDT(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    memory_region_init_io(&s->mmio, OBJECT(s), &g233_wdt_ops, s, TYPE_G233_WDT,
                          0x1000);
    sysbus_init_mmio(sbd, &s->mmio);
    sysbus_init_irq(sbd, &s->irq);
    s->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, wdt_timer_cb, s);
    
}


static void g233_wdt_reset(DeviceState *dev) {
    G233WDTState *s = G233_WDT(dev);
    s->WDT_CTRL = 0x00000000;
    s->WDT_LOAD = 0x0000FFFF;
    s->WDT_VAL = 0x0000FFFF;
    s->WDT_SR = 0x00000000;
    if (s->timer) timer_del(s->timer);

}


static void g233_wdt_class_init(ObjectClass *klass, const void *data) {
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->realize = g233_wdt_realize;
    device_class_set_legacy_reset(dc, g233_wdt_reset);
}


static const TypeInfo g233_wdt_info = {
    .name = TYPE_G233_WDT,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(G233WDTState),
    .class_init = g233_wdt_class_init,
};

static void g233_wdt_register_types(void) {
    type_register_static(&g233_wdt_info);
}

type_init(g233_wdt_register_types)

