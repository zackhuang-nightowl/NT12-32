/***************************************************************************************
 *  nvr_tutk_cgi.c — ODC TUTK agent(AVAPIs_Server_CLI) 的 cgi。
 *
 *  契约见 APP_client_Agent.md / command_agent 开发包 src/dvr_cgi.c：
 *    agent 收到命令后 popen 本程序,本程序把应答打印到 stdout。
 *    argv:  cgi -s|-f <func> [-a <json>]
 *
 *  -s 启动引导(getIotcAuthKey 等):直接读 /User 身份,不依赖 6061 是否已起来。
 *  -s 写入 / 其它 -s / 全部 -f:POST http://127.0.0.1:6061/APPJsonCmd → nvr_cmd_dispatch。
 *  电池机 sleepPacket / 推送 getNotificationSetting:本机 200/501,NVR 不做休眠。
 ***************************************************************************************/
#include "nvr_identity.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#define AGENT_CMD_HOST "127.0.0.1"
#define AGENT_CMD_PORT 6061
#define HTTP_TIMEOUT_S 8
#define RESP_CAP       (256u * 1024u)

static void out_status(int code)
{
    printf("{\"statusCode\":%d}", code);
}

static void out_value(const char *v)
{
    printf("{\"statusCode\":200,\"content\":{\"value\":\"%s\"}}", v ? v : "");
}

static const char *find_args_json(int argc, char **argv)
{
    int i;
    for (i = 3; i < argc - 1; i++) {
        if (strcmp(argv[i], "-a") == 0 && argv[i + 1] && argv[i + 1][0])
            return argv[i + 1];
    }
    for (i = 3; i < argc; i++) {
        if (argv[i] && argv[i][0] == '{')
            return argv[i];
    }
    return NULL;
}

/* 最小 HTTP/1.1 POST,把应答 body 写到 out(不含 HTTP 头)。成功 0。 */
static int post_6061(const char *body, char *out, size_t cap)
{
    int fd, n, hdr_len;
    size_t blen, sent, have;
    char hdr[256];
    char *buf, *sep;
    struct sockaddr_in addr;
    struct timeval tv;

    if (!body || !out || cap < 32) return -1;
    out[0] = 0;
    blen = strlen(body);

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    tv.tv_sec = HTTP_TIMEOUT_S;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(AGENT_CMD_PORT);
    if (inet_pton(AF_INET, AGENT_CMD_HOST, &addr.sin_addr) != 1) {
        close(fd); return -1;
    }
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd); return -1;
    }

    hdr_len = snprintf(hdr, sizeof(hdr),
                       "POST /APPJsonCmd HTTP/1.1\r\n"
                       "Host: %s:%d\r\n"
                       "Content-Type: application/json\r\n"
                       "Content-Length: %zu\r\n"
                       "Connection: close\r\n"
                       "\r\n",
                       AGENT_CMD_HOST, AGENT_CMD_PORT, blen);
    if (hdr_len <= 0 || hdr_len >= (int)sizeof(hdr)) { close(fd); return -1; }

    sent = 0;
    while (sent < (size_t)hdr_len) {
        n = (int)write(fd, hdr + sent, (size_t)hdr_len - sent);
        if (n <= 0) { close(fd); return -1; }
        sent += (size_t)n;
    }
    sent = 0;
    while (sent < blen) {
        n = (int)write(fd, body + sent, blen - sent);
        if (n <= 0) { close(fd); return -1; }
        sent += (size_t)n;
    }

    buf = (char *)malloc(cap);
    if (!buf) { close(fd); return -1; }
    have = 0;
    for (;;) {
        if (have + 1 >= cap) break;
        n = (int)read(fd, buf + have, cap - 1 - have);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        have += (size_t)n;
    }
    close(fd);
    buf[have] = 0;

    sep = strstr(buf, "\r\n\r\n");
    if (sep) {
        sep += 4;
        snprintf(out, cap, "%s", sep);
    } else {
        snprintf(out, cap, "%s", buf);
    }
    free(buf);
    return out[0] ? 0 : -1;
}

static void forward_func(const char *func, const char *args_json)
{
    char req[8192];
    char *resp;

    if (!func || !func[0]) { out_status(400); return; }
    if (args_json && args_json[0] == '{' && strstr(args_json, "\"func\"")) {
        snprintf(req, sizeof(req), "%s", args_json);
    } else {
        snprintf(req, sizeof(req), "{\"func\":\"%s\",\"args\":%s}",
                 func, (args_json && args_json[0]) ? args_json : "{}");
    }

    resp = (char *)malloc(RESP_CAP);
    if (!resp) { out_status(400); return; }
    if (post_6061(req, resp, RESP_CAP) != 0 || !resp[0]) {
        free(resp);
        out_status(400);
        return;
    }
    fputs(resp, stdout);
    free(resp);
}

static int handle_dash_s(const char *func, const char *args_json)
{
    char buf[128];

    if (!func) return -1;

    if (strcmp(func, "getIotcAuthKey") == 0) {
        nvr_identity_get_tutk_creds(buf, sizeof(buf), NULL, 0);
        out_value(buf[0] ? buf : NVR_IDENTITY_DEF_IOTCKEY);
        return 0;
    }
    if (strcmp(func, "getAvPassword") == 0) {
        nvr_identity_get_tutk_creds(NULL, 0, buf, sizeof(buf));
        out_value(buf[0] ? buf : NVR_IDENTITY_DEF_AVKEY);
        return 0;
    }
    if (strcmp(func, "getAvAccount") == 0) {
        nvr_identity_get_av_account(buf, sizeof(buf));
        out_value(buf[0] ? buf : NVR_IDENTITY_DEF_AVACCOUNT);
        return 0;
    }
    if (strcmp(func, "getIotcUID") == 0) {
        nvr_identity_get_uid(buf, sizeof(buf));
        out_value(buf);
        return 0;
    }
    if (strcmp(func, "getNotificationSetting") == 0) {
        out_status(501);   /* NVR 不走 agent 内置推送配置 */
        return 0;
    }
    if (strcmp(func, "notifyLoginSuccess") == 0 ||
        strcmp(func, "notifySessionCount") == 0 ||
        strcmp(func, "sleepPacket") == 0) {
        out_status(200);
        return 0;
    }

    /* 写入与其余 -s → 6061(nvr_cmd_dispatch 落 /User) */
    forward_func(func, args_json);
    return 0;
}

int main(int argc, char **argv)
{
    const char *mode, *func, *args;

    if (argc < 3) {
        out_status(400);
        return 0;
    }
    mode = argv[1];
    func = argv[2];
    args = find_args_json(argc, argv);

    if (strcmp(mode, "-s") == 0) {
        handle_dash_s(func, args);
        return 0;
    }
    if (strcmp(mode, "-f") == 0) {
        forward_func(func, args);
        return 0;
    }
    out_status(400);
    return 0;
}
