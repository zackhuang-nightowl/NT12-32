/* test_mhal_budget.c — 解码预算准入单测（主机）
 * 注意：绝不把有副作用的调用放进 assert()（NDEBUG 下会被整体删除）。 */
#include "mhal_budget.h"
#include <stdio.h>
#include <stdlib.h>

#define CHECK(cond) do { if (!(cond)) { \
    printf("  FAIL: %s (line %d)\n", #cond, __LINE__); exit(1); } } while (0)

int main(void)
{
    printf("test_mhal_budget:\n");

    /* 默认预算 = 1920*1080*360 = 746,496,000 */
    double total = mhal_budget_total();
    CHECK(total == 1920.0 * 1080.0 * 360.0);
    CHECK(mhal_budget_used() == 0.0);

    /* 32 路子码流 640×360@20 = 147.5 Mpix/s，应全部放行(占 ~20%) */
    for (int i = 0; i < 32; i++) {
        int rc = mhal_budget_try_reserve(640, 360, 20);   /* 调用在 assert 外 */
        CHECK(rc == 0);
    }
    double used32 = mhal_budget_used();
    CHECK(used32 == 32.0 * 640 * 360 * 20);
    CHECK(used32 < total * 0.25);
    printf("  32x sub 640x360@20 = %.0f Mpix/s (%.0f%% 预算) 全放行\n",
           used32 / 1e6, used32 / total * 100);

    /* 释放全部，回零 */
    for (int i = 0; i < 32; i++) mhal_budget_release(640, 360, 20);
    CHECK(mhal_budget_used() == 0.0);

    /* 逐路加 1080p@20（每路 41.5 Mpix/s）：746.5/41.5 ≈ 18 路，第 19 路应被拒 */
    int admitted = 0;
    for (int i = 0; i < 40; i++) {
        if (mhal_budget_try_reserve(1920, 1080, 20) == 0) admitted++;
        else break;
    }
    printf("  1080p@20 逐路准入：%d 路后拒绝(每路 %.0f Mpix/s，已用 %.0f Mpix/s)\n",
           admitted, mhal_budget_cost(1920, 1080, 20) / 1e6, mhal_budget_used() / 1e6);
    CHECK(admitted == 18);                        /* 18*41.47M=746.5M 刚好；第 19 超 */

    /* 超限那一路不占预算 */
    double before = mhal_budget_used();
    int denied = mhal_budget_try_reserve(1920, 1080, 20);
    CHECK(denied == -1);
    CHECK(mhal_budget_used() == before);          /* 拒绝不改变占用 */

    /* 单宫格 4K 主码流@20 在空预算下可开 */
    for (int i = 0; i < 18; i++) mhal_budget_release(1920, 1080, 20);
    CHECK(mhal_budget_used() == 0.0);
    int rc4k = mhal_budget_try_reserve(3840, 2160, 20);
    CHECK(rc4k == 0);                             /* 166 Mpix/s < 746 */
    printf("  单宫格 4K 3840x2160@20 = %.0f Mpix/s 放行\n", mhal_budget_used() / 1e6);
    mhal_budget_release(3840, 2160, 20);

    /* 未知分辨率(0) → 放行且占 0 */
    int rcunk = mhal_budget_try_reserve(0, 0, 20);
    CHECK(rcunk == 0);
    CHECK(mhal_budget_used() == 0.0);

    printf("ALL PASS\n");
    return 0;
}
