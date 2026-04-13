/*
 * Xg233ai Custom Instruction Helpers
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/helper-proto.h"
#include "accel/tcg/cpu-ldst.h"


void helper_dma(CPURISCVState *env, target_ulong rd,
                target_ulong rs1, target_ulong rs2)
{
    /* rd = dst address, rs1 = src address, rs2 = grain */
    int n;
    switch (rs2) {
    case 0: n = 8;  break;
    case 1: n = 16; break;
    case 2: n = 32; break;
    default: return;
    }

    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            /* src[i][j] → dst[j][i] */
            uint32_t val = cpu_ldl_data(env, rs1 + (i * n + j) * 4);
            cpu_stl_data(env, rd + (j * n + i) * 4, val);
        }
    }
}

void helper_gemm(CPURISCVState *env, target_ulong rd,
                 target_ulong rs1, target_ulong rs2)
{
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            /* src[i][j] → dst[j][i] */
            uint32_t sum = 0;
            for(int k=0;k<4;k++)
            {
                uint32_t val1 = cpu_ldl_data(env, rs1 + (i * 4 + k) * 4);
                uint32_t val2 = cpu_ldl_data(env, rs2 + (k * 4 + j) * 4);
                sum+=val1*val2;
            }
            cpu_stl_data(env, rd + (i * 4 + j) * 4, sum);
        }
        
    }
}

void helper_sort(CPURISCVState *env, target_ulong rd,
                 target_ulong rs1, target_ulong rs2)
{
    uint32_t f;
    if(rs2>rd)
        f = rd;
    else
        f = rs2;
    for(int i=0;i<f;i++)
    {
        for(int j=0;j<f-i-1;j++)
        {
            uint32_t val = cpu_ldl_data(env, rs1 + (j)* 4);
            uint32_t valcmp = cpu_ldl_data(env, rs1 + (j+1)* 4);
            if(val>valcmp)
            {
                cpu_stl_data(env, rs1 +(j+1)*4, val);
                cpu_stl_data(env, rs1 +j*4, valcmp);
            }
        }
    }
}

/*
 */
void helper_crush(CPURISCVState *env, target_ulong rd,
                  target_ulong rs1, target_ulong rs2)
{
    int n = rs2/2;
    for(int i=0;i<n;i++)
    {
        uint8_t a= cpu_ldub_data(env,rs1+i*2);
        uint8_t b= cpu_ldub_data(env,rs1+i*2+1);
        a = a&0x0F;
        b = b&0x0F;
        cpu_stb_data(env,rd+i*1,a|b<<4);
    }
    if(rs2%2)
    {
        uint8_t c= cpu_ldub_data(env,rs1+n*2);
        c = c&0x0F;
        cpu_stb_data(env,rd+n*1,c);
    }

}

void helper_expand(CPURISCVState *env, target_ulong rd,
                   target_ulong rs1, target_ulong rs2)
{
    for(int i=0;i<rs2;i++)
    {
        uint8_t a= cpu_ldub_data(env,rs1+i);
        cpu_stb_data(env,rd+i*2,a&0x0F);
        cpu_stb_data(env,rd+i*2+1,(a>>4)&0x0F);
    }


}

target_ulong  helper_vdot(CPURISCVState *env,target_ulong rs1, target_ulong rs2)
{
    int sum=0;
    for(int i=0;i<16;i++)
    {
        int32_t a = cpu_ldl_data(env, rs1 + (i)* 4);
        int32_t b = cpu_ldl_data(env, rs2 + (i)* 4);
        sum+=a*b;
    }
    return sum;


}

void helper_vrelu(CPURISCVState *env, target_ulong rd,
                  target_ulong rs1, target_ulong rs2)
{
    for(int i=0;i<rs2;i++)
    {
        int32_t a = cpu_ldl_data(env, rs1 + (i)* 4);
        if(a>0)
            cpu_stl_data(env, rd + i * 4, a);
        else
            cpu_stl_data(env, rd + i * 4, 0); 
    }
}

void helper_vscale(CPURISCVState *env, target_ulong rd,
                   target_ulong rs1, target_ulong rs2)
{
    for(int i=0;i<16;i++)
    {
        int32_t a = cpu_ldl_data(env, rs1 + (i)* 4);

        cpu_stl_data(env, rd + i * 4, (int32_t)((int64_t)a*rs2));
    }
}

target_ulong helper_vmax(CPURISCVState *env,target_ulong rs1, target_ulong rs2)
{
    int32_t max = cpu_ldl_data(env, rs1);
    for(int i=1;i<rs2;i++)
    {
        int32_t a = cpu_ldl_data(env, rs1 + (i)* 4);
        if(a>max)
        {
            max = a;
        }
    }
    return max;
}

void helper_vadd(CPURISCVState *env, target_ulong rd,
                 target_ulong rs1, target_ulong rs2)
{
    for(int i=0;i<16;i++)
    {
        uint32_t a = cpu_ldl_data(env, rs1 + (i)* 4);
        uint32_t b = cpu_ldl_data(env, rs2 + (i)* 4);
        cpu_stl_data(env, rd + (i) * 4, a+b);
    }
}


