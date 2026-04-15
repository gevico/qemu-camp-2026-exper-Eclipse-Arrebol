#include "qemu/osdep.h"
#include "hw/core/irq.h"
#include "hw/core/sysbus.h"
#include "qemu/log.h"


#include "qemu/timer.h"

#define TYPE_G233_PWM "g233-pwm"
#define G233_PWM(obj) OBJECT_CHECK(G233PWMState, (obj), TYPE_G233_PWM)
typedef struct G233PWMState G233PWMState;

typedef struct {
    G233PWMState *s;
    int n;
} PWMChannel;

typedef struct G233PWMState {
    SysBusDevice parent_obj;
    MemoryRegion mmio;

    uint32_t PWM_CHn_CTRL[4];
    uint32_t PWM_CHn_PERIOD[4];
    uint32_t PWM_CHn_DUTY[4];
    uint32_t PWM_CHn_CNT[4];
    uint32_t PWM_GLB;
    QEMUTimer *timer[4];
    PWMChannel ch[4];

    qemu_irq irq;
} G233PWMState;

static void pwm_timer_cb(void *opaque) {
    PWMChannel *ch = opaque;
    G233PWMState *s = ch->s;
    int n = ch->n;
    // 现在知道是第 n 个通道了
    if (!(s->PWM_CHn_CTRL[n] & 0x1))
        return;

    s->PWM_CHn_CNT[n]++;

    if (s->PWM_CHn_CNT[n] >= s->PWM_CHn_PERIOD[n]) {
        s->PWM_CHn_CNT[n] = 0;
        s->PWM_GLB |= (1u << (4 + n)); // DONE 置1
    }

    // 重新 arm，1ns 后再触发
    timer_mod(s->timer[n], qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 1);
}

static uint64_t g233_pwm_read(void *opaque, hwaddr addr, unsigned size) {
    G233PWMState *s = opaque;
    if (addr == 0x00) {
        return s->PWM_GLB;
    } else if (addr >= 0x10) {
        int n = (addr - 0x10) / 0x10;
        int reg = (addr - 0x10) % 0x10;
        if (n >= 4)
            return 0;
        switch (reg) {
        case 0x00:
            return s->PWM_CHn_CTRL[n];
        case 0x04:
            return s->PWM_CHn_PERIOD[n];
        case 0x08:
            return s->PWM_CHn_DUTY[n];
        case 0x0C:
            return s->PWM_CHn_CNT[n];
        default:
            return 0;
        }
    }
    return 0;
}

static void g233_pwm_write(void *opaque, hwaddr addr, uint64_t val,
                           unsigned size) {
    G233PWMState *s = opaque;
    if (addr == 0x00) {
        s->PWM_GLB &= ~(val & 0xF0);
    } else if (addr >= 0x10) {
        int n = (addr - 0x10) / 0x10;
        int reg = (addr - 0x10) % 0x10;
        if (n >= 4)
            return;
        switch (reg) {
        case 0x00:
            s->PWM_CHn_CTRL[n] = val;
            if (val & 0x1) {
                s->PWM_GLB |= (1u << n);
                timer_mod(s->timer[n],
                          qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 1);
            } else {
                s->PWM_GLB &= ~(1u << n);
                timer_del(s->timer[n]);
            }
            break;
        case 0x04:
            s->PWM_CHn_PERIOD[n] = val;
            break;
        case 0x08:
            s->PWM_CHn_DUTY[n] = val;
            break;
        default:
            return;
        }
    }
}

static const MemoryRegionOps g233_pwm_ops = {
    .read = g233_pwm_read,
    .write = g233_pwm_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};

static void g233_pwm_reset(DeviceState *dev) {
    G233PWMState *s = G233_PWM(dev);
    for (int i = 0; i < 4; i++) {
        if (s->timer[i]) timer_del(s->timer[i]); // 加这行
        s->PWM_CHn_CNT[i] = 0;
        s->PWM_CHn_CTRL[i] = 0;
        s->PWM_CHn_DUTY[i] = 0;
        s->PWM_CHn_PERIOD[i] = 0;
    }
    s->PWM_GLB = 0;
}

static void g233_pwm_realize(DeviceState *dev, Error **errp) {
    G233PWMState *s = G233_PWM(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    memory_region_init_io(&s->mmio, OBJECT(s), &g233_pwm_ops, s, TYPE_G233_PWM,
                          0x1000);
    sysbus_init_mmio(sbd, &s->mmio);
    sysbus_init_irq(sbd, &s->irq);
    for (int i = 0; i < 4; i++) {
        s->ch[i].s = s;
        s->ch[i].n = i;
        s->timer[i] = timer_new_ns(QEMU_CLOCK_VIRTUAL, pwm_timer_cb, &s->ch[i]);
    }
}

static void g233_pwm_class_init(ObjectClass *klass, const void *data) {
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->realize = g233_pwm_realize;
    device_class_set_legacy_reset(dc, g233_pwm_reset);
}

static const TypeInfo g233_pwm_info = {
    .name = TYPE_G233_PWM,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(G233PWMState),
    .class_init = g233_pwm_class_init,
};

static void g233_pwm_register_types(void) {
    type_register_static(&g233_pwm_info);
}

type_init(g233_pwm_register_types)
