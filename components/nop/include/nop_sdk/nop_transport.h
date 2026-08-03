/**
 * @file nop_transport.h
 * @brief Transport injection contract (AWS coreMQTT style).
 *
 * The protocol core (nop/) does no networking. Every byte source/sink — the
 * 8089 HTTP server, a TUTK P2P tunnel, or the in-memory mock — implements this
 * interface and is injected. This keeps nop/ link-free of sockets/TLS and fully
 * testable on the host.
 */
#ifndef NOP_SDK_TRANSPORT_H
#define NOP_SDK_TRANSPORT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Byte-stream transport. @c send / @c recv return the number of bytes
 * transferred, 0 on timeout, or a negative value on error. @c ctx is opaque
 * backend state owned by the backend.
 */
typedef struct nop_transport_if {
    int (*send)(void *ctx, const uint8_t *buf, size_t len, uint32_t timeout_ms);
    int (*recv)(void *ctx, uint8_t *buf, size_t len, uint32_t timeout_ms);
    void *ctx;
} nop_transport_if;

#ifdef __cplusplus
}
#endif

#endif /* NOP_SDK_TRANSPORT_H */
