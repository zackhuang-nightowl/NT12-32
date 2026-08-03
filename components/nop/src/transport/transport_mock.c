#include "transport/transport_mock.h"
#include "base/nop_mem.h"

#include <string.h>

struct transport_mock {
    uint8_t *buffer;
    size_t   capacity;
    size_t   read_cursor;
    size_t   write_cursor;
};

static int mock_send(void *context, const uint8_t *buffer, size_t length, uint32_t timeout_ms)
{
    transport_mock_t *mock = (transport_mock_t *)context;
    (void)timeout_ms;
    if (!mock || !buffer)
        return -1;
    if (mock->write_cursor + length > mock->capacity) {
        size_t   new_capacity = (mock->write_cursor + length) * 2;
        uint8_t *new_buffer = (uint8_t *)nop_realloc(mock->buffer, new_capacity);
        if (!new_buffer)
            return -1;
        mock->buffer = new_buffer;
        mock->capacity = new_capacity;
    }
    memcpy(mock->buffer + mock->write_cursor, buffer, length);
    mock->write_cursor += length;
    return (int)length;
}

static int mock_recv(void *context, uint8_t *buffer, size_t length, uint32_t timeout_ms)
{
    transport_mock_t *mock = (transport_mock_t *)context;
    size_t available, copy_length;
    (void)timeout_ms;
    if (!mock || !buffer)
        return -1;
    available = mock->write_cursor - mock->read_cursor;
    copy_length = (length < available) ? length : available;
    if (copy_length == 0)
        return 0;   /* nothing buffered => "timeout" */
    memcpy(buffer, mock->buffer + mock->read_cursor, copy_length);
    mock->read_cursor += copy_length;
    return (int)copy_length;
}

transport_mock_t *transport_mock_create(void)
{
    transport_mock_t *mock = (transport_mock_t *)nop_calloc(1, sizeof(*mock));
    if (!mock)
        return NULL;
    mock->capacity = 256;
    mock->buffer = (uint8_t *)nop_malloc(mock->capacity);
    if (!mock->buffer) {
        nop_free(mock);
        return NULL;
    }
    return mock;
}

void transport_mock_destroy(transport_mock_t *mock)
{
    if (!mock)
        return;
    nop_free(mock->buffer);
    nop_free(mock);
}

void transport_mock_iface(transport_mock_t *mock, nop_transport_if *out_interface)
{
    if (!out_interface)
        return;
    out_interface->send = mock_send;
    out_interface->recv = mock_recv;
    out_interface->ctx  = mock;
}
