/***************************************************************************************
 *  main.c — NVR 固件入口。加载 config/ → 起全链路 → 主循环，SIGINT/SIGTERM 优雅退出。
 ***************************************************************************************/
#include "nvr_app.h"
#include <stdio.h>
#include <signal.h>

static nvr_app_t *g_app = NULL;

static void on_signal(int sig) { (void)sig; if (g_app) nvr_app_request_exit(g_app); }

int main(int argc, char **argv)
{
    const char *cfg_dir = (argc > 1) ? argv[1] : "config";

    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);

    if (nvr_app_start(cfg_dir, &g_app) != 0) {
        fprintf(stderr, "NVR 启动失败\n");
        return 1;
    }
    nvr_app_run(g_app);       /* 阻塞至收到退出信号 */
    nvr_app_stop(g_app);
    printf("NVR 已退出\n");
    return 0;
}
