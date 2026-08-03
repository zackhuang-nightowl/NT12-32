/**
 * @file test_server_request.c
 * @brief Outbound server-request / OTA: URL construction, and a full OTA
 *        download against a throwaway local HTTP server (status reaches
 *        DOWNLOADED and the firmware writer receives the body).
 */
#include "nop_sdk/nop_server_request.h"
#include "nop_sdk/nop_config.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define FIRMWARE_BODY "FIRMWARE-IMAGE-BYTES-v2"

static int fail(const char *m) { fprintf(stderr, "FAIL: %s\n", m); return 1; }

/* --- throwaway HTTP server: one request, replies 200 + FIRMWARE_BODY --- */
typedef struct { int listen_fd; int port; } test_server_t;

static void *server_thread(void *arg)
{
    test_server_t *s = (test_server_t *)arg;
    int conn = accept(s->listen_fd, NULL, NULL);
    if (conn >= 0) {
        char    buf[1024];
        char    reply[256];
        int     len;
        ssize_t io;
        io = read(conn, buf, sizeof(buf));    /* drain request */
        (void)io;
        len = snprintf(reply, sizeof(reply),
            "HTTP/1.1 200 OK\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n%s",
            strlen(FIRMWARE_BODY), FIRMWARE_BODY);
        io = write(conn, reply, (size_t)len);
        (void)io;
        close(conn);
    }
    close(s->listen_fd);
    return NULL;
}

/* --- firmware writer captures the downloaded bytes --- */
static char   g_written[256];
static size_t g_written_len;
static void writer(void *ctx, const unsigned char *data, size_t len, long long off)
{
    (void)ctx; (void)off;
    if (len < sizeof(g_written)) {
        memcpy(g_written, data, len);
        g_written[len] = '\0';
        g_written_len = len;
    }
}

int main(void)
{
    nop_config_ota_t   ota_cfg;
    char               url[256];
    test_server_t      server;
    struct sockaddr_in addr;
    socklen_t          addr_len = sizeof(addr);
    pthread_t          thread;
    nop_ota_t         *ota;
    int                i;
    nop_ota_status_t   status;

    /* --- 1) URL construction --- */
    memset(&ota_cfg, 0, sizeof(ota_cfg));
    strcpy(ota_cfg.url_base, "https://kota.example/ota");
    strcpy(ota_cfg.company,  "NightOwl");
    strcpy(ota_cfg.product,  "NVR");
    strcpy(ota_cfg.model,    "WNVR-BTWN8");
    if (nop_ota_build_url(&ota_cfg, url, sizeof(url)) != NOP_OK)
        return fail("build_url");
    if (strcmp(url, "https://kota.example/ota/NightOwl/NVR/WNVR-BTWN8") != 0)
        return fail("build_url value");

    /* --- 2) spin a local HTTP server --- */
    server.listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server.listen_fd < 0)
        return fail("socket");
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;     /* ephemeral */
    if (bind(server.listen_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0)
        return fail("bind");
    if (listen(server.listen_fd, 1) != 0)
        return fail("listen");
    getsockname(server.listen_fd, (struct sockaddr *)&addr, &addr_len);
    server.port = ntohs(addr.sin_port);
    if (pthread_create(&thread, NULL, server_thread, &server) != 0)
        return fail("server thread");

    /* --- 3) OTA download against it --- */
    ota = nop_ota_create(&ota_cfg);
    if (!ota)
        return fail("ota create");
    nop_ota_set_writer(ota, writer, NULL);
    snprintf(url, sizeof(url), "http://127.0.0.1:%d/firmware.bin", server.port);
    if (nop_ota_start(ota, url, 1) != NOP_OK)
        return fail("ota start");

    for (i = 0; i < 300; i++) {        /* up to ~3s */
        status = nop_ota_status_get(ota, NULL);
        if (status != NOP_OTA_DOWNLOADING)
            break;
        usleep(10000);
    }
    if (status != NOP_OTA_DOWNLOADED)
        return fail("status != DOWNLOADED");
    if (strcmp(g_written, FIRMWARE_BODY) != 0)
        return fail("writer body mismatch");

    nop_ota_destroy(ota);
    pthread_join(thread, NULL);
    printf("test_server_request: OK (port %d, %zu bytes)\n", server.port, g_written_len);
    return 0;
}
