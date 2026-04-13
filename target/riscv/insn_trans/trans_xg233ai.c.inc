

/*
 * Xg233ai Custom Instruction Translation
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

static bool trans_dma(DisasContext *ctx, arg_r *a)
{
    TCGv dest = get_gpr(ctx, a->rd, EXT_NONE);
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv src2 = get_gpr(ctx, a->rs2, EXT_NONE);
    gen_helper_dma(tcg_env, dest, src1, src2);
    return true;
}

static bool trans_gemm(DisasContext *ctx, arg_r *a)
{
    TCGv dest = get_gpr(ctx, a->rd, EXT_NONE);
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv src2 = get_gpr(ctx, a->rs2, EXT_NONE);
    gen_helper_gemm(tcg_env, dest, src1, src2);
    return true;
}

static bool trans_sort(DisasContext *ctx, arg_r *a)
{
    TCGv dest = get_gpr(ctx, a->rd, EXT_NONE);
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv src2 = get_gpr(ctx, a->rs2, EXT_NONE);
    gen_helper_sort(tcg_env, dest, src1, src2);
    return true;
}

static bool trans_crush(DisasContext *ctx, arg_r *a)
{
    TCGv dest = get_gpr(ctx, a->rd, EXT_NONE);
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv src2 = get_gpr(ctx, a->rs2, EXT_NONE);
    gen_helper_crush(tcg_env, dest, src1, src2);
    return true;
}

static bool trans_expand(DisasContext *ctx, arg_r *a)
{
    TCGv dest = get_gpr(ctx, a->rd, EXT_NONE);
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv src2 = get_gpr(ctx, a->rs2, EXT_NONE);
    gen_helper_expand(tcg_env, dest, src1, src2);
    return true;
}

static bool trans_vdot(DisasContext *ctx, arg_r *a)
{
    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv src2 = get_gpr(ctx, a->rs2, EXT_NONE);
    gen_helper_vdot(dest, tcg_env, src1, src2);
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}

static bool trans_vrelu(DisasContext *ctx, arg_r *a)
{
    TCGv dest = get_gpr(ctx, a->rd, EXT_NONE);
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv src2 = get_gpr(ctx, a->rs2, EXT_NONE);
    gen_helper_vrelu(tcg_env, dest, src1, src2);
    return true;
}

static bool trans_vscale(DisasContext *ctx, arg_r *a)
{
    TCGv dest = get_gpr(ctx, a->rd, EXT_NONE);
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv src2 = get_gpr(ctx, a->rs2, EXT_NONE);
    gen_helper_vscale(tcg_env, dest, src1, src2);
    return true;
}

static bool trans_vmax(DisasContext *ctx, arg_r *a)
{
    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv src2 = get_gpr(ctx, a->rs2, EXT_NONE);
    gen_helper_vmax(dest,tcg_env, src1, src2);
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}

static bool trans_vadd(DisasContext *ctx, arg_r *a)
{
    TCGv dest = get_gpr(ctx, a->rd, EXT_NONE);
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv src2 = get_gpr(ctx, a->rs2, EXT_NONE);
    gen_helper_vadd(tcg_env, dest, src1, src2);
    return true;
}