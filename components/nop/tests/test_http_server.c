/**
 * @file test_http_server.c
 * @brief Inbound NOP-over-HTTP: start the server, POST a NOP envelope over a
 *        real TCP socket, assert an HTTP 200 carrying statusCode 200.
 */
#include "nop_sdk/nop_app.h"
#include "nop_sdk/nop_http_server.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define TEST_PORT 18089

static int fail(const char *msg) { fprintf(stderr, "FAIL: %s\n", msg); return 1; }

int main(void)
{
    nop_app_config_t   cfg;
    nop_app_t         *app;
    nop_http_server_t *server;
    int                fd, port;
    struct sockaddr_in addr;
    const char        *body = "{\"func\":\"get_datetime\",\"args\":{}}";
    char               request[512], response[4096];
    int                request_len;
    ssize_t            total = 0, n;

    memset(&cfg, 0, sizeof(cfg));
    cfg.role = NOP_ROLE_IPC;
    cfg.auto_caps = 1;
    app = nop_app_create(&cfg);
    if (!app)
        return fail("app create");

    server = nop_http_server_start(TEST_PORT, app);
    if (!server)
        return fail("http server start");
    port = nop_http_server_port(server);

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return fail("socket");
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((uint16_t)port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0)
        return fail("connect");

    request_len = snprintf(request, sizeof(request),
        "POST /nop HTTP/1.1\r\nHost: 127.0.0.1\r\n"
        "Content-Type: application/json\r\nContent-Length: %zu\r\n"
        "Connection: close\r\n\r\n%s",
        strlen(body), body);
    if (write(fd, request, (size_t)request_len) != request_len)
        return fail("write");

    while (total < (ssize_t)sizeof(response) - 1 &&
           (n = read(fd, response + total, sizeof(response) - 1 - total)) > 0)
        total += n;
    response[total] = '\0';
    close(fd);
    nop_http_server_stop(server);
    nop_app_destroy(app);

    if (!strstr(response, "HTTP/1.1 200 OK"))
        return fail("no HTTP 200 status line");
    if (!strstr(response, "\"statusCode\":200"))
        return fail("no statusCode 200 in body");
    if (!strstr(response, "\"date\""))
        return fail("no date field in get_datetime content");

    printf("test_http_server: OK (port %d, response %zd bytes)\n", port, total);
    return 0;
}
