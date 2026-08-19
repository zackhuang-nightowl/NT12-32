/***************************************************************************************
 *  nvr_gui_notify.c — 向导/解锁状态(进程内)。App notify 后 GUI 经 longPolling 切页。
 ***************************************************************************************/
#include "nvr_gui_notify.h"
#include <pthread.h>

static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static int g_setup_status;
static int g_setup_has;
static int g_setup_gen;
static int g_ui_unlocked;

void nvr_gui_set_setup_status(int status)
{
    pthread_mutex_lock(&g_mu);
    g_setup_status = status;
    g_setup_has = 1;
    g_setup_gen++;
    if (status == 2 || status == 3)
        g_ui_unlocked = 1;
    pthread_mutex_unlock(&g_mu);
}

int nvr_gui_get_setup_status(int *has)
{
    int v, h;
    pthread_mutex_lock(&g_mu);
    v = g_setup_status;
    h = g_setup_has;
    pthread_mutex_unlock(&g_mu);
    if (has) *has = h;
    return v;
}

int nvr_gui_setup_gen(void)
{
    int g;
    pthread_mutex_lock(&g_mu);
    g = g_setup_gen;
    pthread_mutex_unlock(&g_mu);
    return g;
}

void nvr_gui_set_ui_unlocked(int on)
{
    pthread_mutex_lock(&g_mu);
    g_ui_unlocked = on ? 1 : 0;
    pthread_mutex_unlock(&g_mu);
}

int nvr_gui_ui_unlocked(void)
{
    int v;
    pthread_mutex_lock(&g_mu);
    v = g_ui_unlocked;
    pthread_mutex_unlock(&g_mu);
    return v;
}
