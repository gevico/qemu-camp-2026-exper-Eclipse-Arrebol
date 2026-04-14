/*
 * QEMU GPGPU - RISC-V SIMT Core Implementation
 *
 * Copyright (c) 2024-2025
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#include "qemu/osdep.h"
#include "qemu/log.h"
#include "gpgpu.h"
#include "gpgpu_core.h"




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
          *(uint32_t *)(s->vram_ptr + addr) = lane->gpr[rs2];
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
