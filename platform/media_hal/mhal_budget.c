/***************************************************************************************
 *  mhal_budget.c — 硬解像素吞吐预算准入。见 mhal_budget.h。
 ***************************************************************************************/
#include "mhal_budget.h"
#include <pthread.h>

/* 数据手册硬上限：1920*1080*360 = 746,496,000 px/s */
#define MHAL_BUDGET_DEFAULT_TOTAL (1920.0 * 1080.0 * 360.0)

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static double g_total = MHAL_BUDGET_DEFAULT_TOTAL;
static double g_used  = 0.0;

void mhal_budget_set_total(double mpix_per_s)
{
    pthread_mutex_lock(&g_lock);
    g_total = (mpix_per_s > 0) ? mpix_per_s : MHAL_BUDGET_DEFAULT_TOTAL;
    pthread_mutex_unlock(&g_lock);
}

double mhal_budget_total(void)
{
    pthread_mutex_lock(&g_lock);
    double v = g_total;
    pthread_mutex_unlock(&g_lock);
    return v;
}

double mhal_budget_used(void)
{
    pthread_mutex_lock(&g_lock);
    double v = g_used;
    pthread_mutex_unlock(&g_lock);
    return v;
}

double mhal_budget_cost(int w, int h, int fps)
{
    if (w <= 0 || h <= 0) return 0.0;
    if (fps <= 0) fps = 25;
    return (double)w * (double)h * (double)fps;
}

int mhal_budget_try_reserve(int w, int h, int fps)
{
    double cost = mhal_budget_cost(w, h, fps);
    pthread_mutex_lock(&g_lock);
    /* 未知开销(0)一律放行(占 0)；否则须留在预算内 */
    if (cost > 0 && g_used + cost > g_total) {
        pthread_mutex_unlock(&g_lock);
        return -1;                       /* 超预算：拒绝这一路 */
    }
    g_used += cost;
    pthread_mutex_unlock(&g_lock);
    return 0;
}

void mhal_budget_release(int w, int h, int fps)
{
    double cost = mhal_budget_cost(w, h, fps);
    pthread_mutex_lock(&g_lock);
    g_used -= cost;
    if (g_used < 0) g_used = 0;           /* 防御：不欠账 */
    pthread_mutex_unlock(&g_lock);
}
