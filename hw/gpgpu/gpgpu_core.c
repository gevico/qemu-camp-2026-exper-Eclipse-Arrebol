/*
 * QEMU GPGPU - RISC-V SIMT Core Implementation
 *
 * Copyright (c) 2024-2025
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#include "gpgpu_core.h"
#include "gpgpu.h"
#include "qemu/log.h"
#include "qemu/osdep.h"
#include <stdint.h>

static inline float u32_to_float(uint32_t v) {
  union {
    uint32_t u;
    float f;
  } x = {.u = v};
  return x.f;
}

static inline uint32_t float_to_u32(float v) {
  union {
    uint32_t u;
    float f;
  } x = {.f = v};
  return x.u;
}

static uint8_t f32_to_e4m3(uint32_t f32) {
  uint32_t sign = (f32 >> 31) & 0x1;
  int32_t fp32_exp = (f32 >> 23) & 0xFF;
  uint32_t mant = (f32 >> 20) & 0x7; // 尾数高3位

  int32_t e4m3_exp = fp32_exp - 120; // rebias

  // 处理边界
  if (e4m3_exp <= 0)
    return (sign << 7); // 下溢，返回±0
  if (e4m3_exp > 15)
    e4m3_exp = 15; // 上溢，钳位到最大

  return (uint8_t)((sign << 7) | (e4m3_exp << 3) | mant);
}

static uint8_t f32_to_e5m2(uint32_t f32) {
  uint32_t sign = (f32 >> 31) & 0x1;
  int32_t fp32_exp = (f32 >> 23) & 0xFF;
  uint32_t mant = (f32 >> 21) & 0x3; // 尾数高3位

  int32_t e4m3_exp = fp32_exp - 112; // rebias

  // 处理边界
  if (e4m3_exp <= 0)
    return (sign << 7); // 下溢，返回±0
  if (e4m3_exp > 31)
    e4m3_exp = 31; // 上溢，钳位到最大

  return (uint8_t)((sign << 7) | (e4m3_exp << 2) | mant);
}

static uint8_t f32_to_e2m1(uint32_t f32) {
  uint32_t sign = (f32 >> 31) & 0x1;
  int32_t fp32_exp = (f32 >> 23) & 0xFF;
  uint32_t mant = (f32 >> 22) & 0x1; // 尾数高3位

  int32_t e4m3_exp = fp32_exp - 126; // rebias

  // 处理边界
  if (e4m3_exp <= 0)
    return (sign << 7); // 下溢，返回±0
  if (e4m3_exp > 3)
    e4m3_exp = 3; // 上溢，钳位到最大

  return (uint8_t)((sign << 7) | (e4m3_exp << 1) | mant);
}

static uint32_t e4m3_to_f32(uint8_t u8) {
  uint32_t sign = (u8 >> 7) & 0x1;
  int32_t fp8_exp = (u8 >> 3) & 0xF;
  uint32_t mant = u8 & 0x7;

  // 零值
  if (fp8_exp == 0 && mant == 0)
    return sign << 31;

  int32_t f32_exp = fp8_exp + 120; // rebias

  if (f32_exp > 255)
    f32_exp = 255; // 上溢钳位

  return (sign << 31) | ((uint32_t)f32_exp << 23) | (mant << 20);
}

static uint32_t e2m1_to_f32(uint8_t u8) {
  uint32_t sign = (u8 >> 3) & 0x1;
  int32_t fp8_exp = (u8 >> 1) & 0x3;
  uint32_t mant = u8 & 0x1;

  // 零值
  if (fp8_exp == 0 && mant == 0)
    return sign << 31;

  int32_t f32_exp = fp8_exp + 126; // rebias

  if (f32_exp > 255)
    f32_exp = 255; // 上溢钳位

  return (sign << 31) | ((uint32_t)f32_exp << 23) | (mant << 22);
}

static uint32_t e5m2_to_f32(uint8_t u8) {
  uint32_t sign = (u8 >> 7) & 0x1;
  int32_t fp8_exp = (u8 >> 2) & 0x1F;
  uint32_t mant = u8 & 0x3;

  // 零值
  if (fp8_exp == 0 && mant == 0)
    return sign << 31;

  int32_t f32_exp = fp8_exp + 112; // rebias

  if (f32_exp > 255)
    f32_exp = 255; // 上溢钳位

  return (sign << 31) | ((uint32_t)f32_exp << 23) | (mant << 21);
}

/* TODO: Implement warp initialization */
void gpgpu_core_init_warp(GPGPUWarp *warp, uint32_t pc, uint32_t thread_id_base,
                          const uint32_t block_id[3], uint32_t num_threads,
                          uint32_t warp_id, uint32_t block_id_linear) {
  for (int i = 0; i < GPGPU_WARP_SIZE; i++) {
    warp->lanes[i].pc = pc;
    warp->lanes[i].active = (i < num_threads);
    warp->lanes[i].mhartid =
        MHARTID_ENCODE(block_id_linear, warp_id, thread_id_base + i);
  }
  warp->warp_id = warp_id;
  warp->thread_id_base = thread_id_base;
  memcpy(warp->block_id, block_id, sizeof(warp->block_id));
  uint32_t mask = 0;
  for (int i = 0; i < num_threads; i++) {
    mask += 1 << i;
  }
  warp->active_mask = mask;
}

/* TODO: Implement warp execution (RV32I + RV32F interpreter) */
int gpgpu_core_exec_warp(GPGPUState *s, GPGPUWarp *warp, uint32_t max_cycles) {
  uint32_t cycles = 0;

  while (warp->active_mask && cycles < max_cycles) {
    cycles++;

    // 取指
    uint32_t first = __builtin_ctz(warp->active_mask);
    uint32_t insn = *(uint32_t *)(s->vram_ptr + warp->lanes[first].pc);

    // 对每个活跃 lane 执行
    for (int i = 0; i < GPGPU_WARP_SIZE; i++) {
      if (!(warp->active_mask & (1u << i)))
        continue;

      GPGPULane *lane = &warp->lanes[i];

      // 译码
      uint32_t opcode = insn & 0x7F;
      uint32_t rd = (insn >> 7) & 0x1F;
      uint32_t funct3 = (insn >> 12) & 0x7;
      uint32_t rs1 = (insn >> 15) & 0x1F;
      uint32_t rs2 = (insn >> 20) & 0x1F;
      uint32_t funct7 = (insn >> 25) & 0x7F;
      int32_t imm_i = (int32_t)insn >> 20;
      int32_t imm_u = (int32_t)(insn & 0xFFFFF000);
      int32_t imm_s =
          ((int32_t)(insn & 0xFE000000) >> 20) | ((insn >> 7) & 0x1F);

      // 执行
      switch (opcode) {

      case 0x73:                  // csrrs / ebreak
        if (insn == 0x00100073) { // ebreak
          warp->active_mask &= ~(1u << i);
          continue;
        }
        if (funct3 == 0x2) { // csrrs
          uint32_t csr = (insn >> 20) & 0xFFF;
          if (csr == CSR_MHARTID) {
            lane->gpr[rd] = lane->mhartid;
          }
        }
        break;

      case 0x13: // I-type 整数运算
        switch (funct3) {
        case 0x7: // andi
          lane->gpr[rd] = lane->gpr[rs1] & imm_i;
          break;
        case 0x0: // addi
          lane->gpr[rd] = lane->gpr[rs1] + imm_i;
          break;
        case 0x1: // slli
          lane->gpr[rd] = lane->gpr[rs1] << rs2;
          break;
        }
        break;

      case 0x37: // lui
        lane->gpr[rd] = imm_u;
        break;

      case 0x33:                               // R-type
        if (funct3 == 0x0 && funct7 == 0x00) { // add
          lane->gpr[rd] = lane->gpr[rs1] + lane->gpr[rs2];
        }
        break;

      case 0x23:             // S-type store
        if (funct3 == 0x2) { // sw
          // TODO: addr = rs1 + imm_s, 写 vram
          uint32_t addr = lane->gpr[rs1] + imm_s;
          fprintf(stderr, "sw: rs1=x%d(%u) rs2=x%d(%u) addr=0x%x\n", rs1,
                  lane->gpr[rs1], rs2, lane->gpr[rs2], addr);
          *(uint32_t *)(s->vram_ptr + addr) = lane->gpr[rs2];
        }
        break;

      case 0x53:
        fprintf(stderr, "FP insn: funct7=0x%02x rd=%d rs1=%d rs2=%d\n", funct7,
                rd, rs1, rs2);
        if (funct7 == 0x68) {
          float result = (float)(int32_t)lane->gpr[rs1];
          lane->fpr[rd] = float_to_u32(result);
        }
        if (funct7 == 0x60) {
          float val = u32_to_float(lane->fpr[rs1]);
          lane->gpr[rd] = (int32_t)val;
        }
        if (funct7 == 0x00) {
          float result =
              u32_to_float(lane->fpr[rs1]) + u32_to_float(lane->fpr[rs2]);
          lane->fpr[rd] = float_to_u32(result);
        }
        if (funct7 == 0x08) {
          float result =
              u32_to_float(lane->fpr[rs1]) * u32_to_float(lane->fpr[rs2]);
          lane->fpr[rd] = float_to_u32(result);
        }
        if (funct7 == 0x22) {
          if (rs2 == 1) {
            lane->fpr[rd] = lane->fpr[rs1] >> 16;
          }
          if (rs2 == 0) { // fcvt.s.bf16
            lane->fpr[rd] = lane->fpr[rs1] << 16;
          }
        }
        if (funct7 == 0x24) {
          if (rs2 == 1) {
            lane->fpr[rd] = (uint32_t)f32_to_e4m3(lane->fpr[rs1]);
          }
          if (rs2 == 0) { // fcvt.s.bf16
            lane->fpr[rd] = e4m3_to_f32((uint8_t)lane->fpr[rs1]);
          }
          if (rs2 == 3) { // fcvt.e5m2.s
            lane->fpr[rd] = (uint32_t)f32_to_e5m2(lane->fpr[rs1]);
          }
          if (rs2 == 2) { // fcvt.s.e5m2
            lane->fpr[rd] = e5m2_to_f32((uint8_t)lane->fpr[rs1]);
          }
        }
        if (funct7 == 0x26) {
          if (rs2 == 1) { // fcvt.e2m1.s
            lane->fpr[rd] = (uint32_t)f32_to_e2m1(lane->fpr[rs1]);
          }
          if (rs2 == 0) { // fcvt.s.e2m1
            lane->fpr[rd] = e2m1_to_f32((uint8_t)lane->fpr[rs1]);
          }
        }

        break;
      }

      lane->pc += 4;
    }
  }

  return 0;
}
/* TODO: Implement kernel dispatch and execution */
int gpgpu_core_exec_kernel(GPGPUState *s) {

  uint32_t block_size =
      s->kernel.block_dim[0] * s->kernel.block_dim[1] * s->kernel.block_dim[2];
  uint32_t num_warps = (block_size + GPGPU_WARP_SIZE - 1) / GPGPU_WARP_SIZE;

  for (uint32_t bz = 0; bz < s->kernel.grid_dim[2]; bz++) {
    for (uint32_t by = 0; by < s->kernel.grid_dim[1]; by++) {
      for (uint32_t bx = 0; bx < s->kernel.grid_dim[0]; bx++) {
        uint32_t block_id[3] = {bx, by, bz};
        uint32_t block_id_linear =
            bz * s->kernel.grid_dim[1] * s->kernel.grid_dim[0] +
            by * s->kernel.grid_dim[0] + bx;
        for (uint32_t warp_id = 0; warp_id < num_warps; warp_id++) {
          uint32_t thread_id_base = warp_id * GPGPU_WARP_SIZE;
          uint32_t remaining = block_size - thread_id_base;
          uint32_t num_threads =
              (remaining < GPGPU_WARP_SIZE) ? remaining : GPGPU_WARP_SIZE;
          GPGPUWarp warp;
          gpgpu_core_init_warp(&warp, (uint32_t)s->kernel.kernel_addr,
                               thread_id_base, block_id, num_threads, warp_id,
                               block_id_linear);
          gpgpu_core_exec_warp(s, &warp, 1000);
        }
        // 这里遍历 warp
      }
    }
  }
  return 0;
}
