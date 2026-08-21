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

    /* ★ 必须忽略 SIGPIPE:作为 8089 HTTP 服务端,向"对端已关闭"的 socket 写响应会收到 SIGPIPE,
     * 默认动作是终止进程(rc=141=128+13)。真机现象:向导页"检查版本更新"走 HTTPS(SSL 慢/失败重试),
     * GUI 客户端超时先关连接,server 再写响应 → SIGPIPE → nvr_app 崩 → 看门狗连带重启 GUI。
     * SIG_IGN 后写失败改为返回 EPIPE(由各 write 调用方处理),进程不再被打死。 */
    signal(SIGPIPE, SIG_IGN);
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
