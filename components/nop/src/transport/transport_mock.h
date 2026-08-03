/**
 * @file transport_mock.h  (internal/test)
 * @brief In-memory transport backend implementing nop_transport_if. Bytes
 *        written via send() can be read back via recv() (loopback FIFO). Used to
 *        prove the injection contract and drive transport-level tests on host.
 */
#ifndef NOP_TRANSPORT_MOCK_H
#define NOP_TRANSPORT_MOCK_H

#include "nop_sdk/nop_transport.h"

typedef struct transport_mock transport_mock_t;

transport_mock_t *transport_mock_create(void);
void              transport_mock_destroy(transport_mock_t *mock);

/** Fill @p out_interface with the nop_transport_if vtable bound to this mock. */
void transport_mock_iface(transport_mock_t *mock, nop_transport_if *out_interface);

#endif /* NOP_TRANSPORT_MOCK_H */
