/* Mock transport loopback: prove the nop_transport_if injection contract. */
#include "transport/transport_mock.h"
#include "test_util.h"

int main(void)
{
    transport_mock_t *m = transport_mock_create();
    nop_transport_if  io;
    const char       *msg = "{\"func\":\"ping\"}";
    uint8_t           buf[64];
    int               n;

    CHECK(m != NULL);
    transport_mock_iface(m, &io);
    CHECK(io.send && io.recv && io.ctx == m);

    n = io.send(io.ctx, (const uint8_t *)msg, strlen(msg), 100);
    CHECK_EQ_INT(n, (int)strlen(msg));

    n = io.recv(io.ctx, buf, sizeof(buf), 100);
    CHECK_EQ_INT(n, (int)strlen(msg));
    buf[n] = '\0';
    CHECK_STR((char *)buf, msg);

    /* nothing left -> 0 (timeout) */
    n = io.recv(io.ctx, buf, sizeof(buf), 100);
    CHECK_EQ_INT(n, 0);

    transport_mock_destroy(m);
    TEST_RETURN();
}
