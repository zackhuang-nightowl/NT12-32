/***************************************************************************************
 *  mhal_budget.h — 硬解像素吞吐预算准入（纯 C，可主机单测）
 *
 *  NA51090/NT98633 SDK 认证：纯解码 1920×1080@480fps ≡ 3840×2160@120fps ≈ 995 Mpix/s。
 *  每开一个解码器按其**实际分辨率×帧率**占用预算；总和不得超预算。
 *  新增一路若会超限 → 不予解码（拒绝最新那一路），由调用方在错误处上报。
 *  单实例最大帧 8192×8192；本层只管吞吐总量，帧尺寸上限在 mhal_vdec 校验。
 ***************************************************************************************/
#ifndef MHAL_BUDGET_H
#define MHAL_BUDGET_H

#ifdef __cplusplus
extern "C" {
#endif

/* 默认总预算 = 1920*1080*480 = 995,328,000 px/s。可按真机实测覆盖。 */
void   mhal_budget_set_total(double mpix_per_s);
double mhal_budget_total(void);
double mhal_budget_used(void);                    /* 当前已占用 px/s */

/* 一路的开销 = w*h*fps（px/s）。w/h/fps ≤0 视为未知 → 返回 0。 */
double mhal_budget_cost(int w, int h, int fps);

/* 尝试预留一路：不超总预算则占用并返回 0；会超返回 -1（不占用）。fps≤0 默认 25。 */
int    mhal_budget_try_reserve(int w, int h, int fps);

/* 释放一路（关闭解码器时调，参数须与 reserve 时一致）。 */
void   mhal_budget_release(int w, int h, int fps);

#ifdef __cplusplus
}
#endif
#endif /* MHAL_BUDGET_H */
