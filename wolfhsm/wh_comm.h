/*
 * wolfhsm/comm.h
 *
 * Library to provide client to server requests and server to client responses.
 * Fundamentally, communications are reliable, bidirectional, and packet-based
 * with a fixed MTU.  Packets are delivered in-order without any intrinsic
 * queuing nor OOB support.  Transports deliver complete packets up to the MTU
 * size and provide the number of bytes received as metadata.
 *
 * Note: Multibyte data will be passed in native order, which means clients and
 * servers must be the SAME endianess or will be required to translate data
 * elements in messages.
 *
 * Provided exmaple transports are:
 *  - shared memory (transport_shm)
 *  - UNIX domain socket (transport_unix)
 *  - TCP streaming socket (transport_tcp)
 *
 * All functions return an integer value with 0 meaning success and !=0 an error
 * enumerated either within wolfssl/wolfcrypt/error-crypt.h or in the function
 * comments.  Unless otherwise noted, all functions are non-blocking and each
 * may update the context state or perform other bookkeeping actions as
 * necessary.
 */

#ifndef WOLFHSM_WH_COMM_H_
#define WOLFHSM_WH_COMM_H_

#include <stdint.h>  /* For sized ints */

#include "wolfhsm/wh_transport.h"

/* Request/response packets are composed of a single fixed-length header
 * (whHeader) followed immediately by variable-length data between 0 and
 * DATA_LEN bytes.
 */
enum {
    WOLFHSM_COMM_HEADER_LEN = 8,
    WOLFHSM_COMM_DATA_LEN = 1280,
    WOLFHSM_COMM_MTU = (WOLFHSM_COMM_HEADER_LEN + WOLFHSM_COMM_DATA_LEN)
};

/* Support for endian and version differences */
/* Version is BCD to avoid conflict with endian marker */
#define WH_COMM_VERSION (0x01u)
#define WH_COMM_ENDIAN (0xA5u)

#define WH_COMM_MAGIC_ENDIAN_MASK 0xFF00u
#define WH_COMM_MAGIC_VERSION_MASK 0x00FFu

#define WH_COMM_MAGIC_NATIVE    ((WH_COMM_ENDIAN << 8) | WH_COMM_VERSION)
#define WH_COMM_MAGIC_SWAP      (WH_COMM_ENDIAN | (WH_COMM_VERSION << 8))

#define WH_COMM_FLAGS_SWAPTEST(_magic) \
    (_magic                 & WH_COMM_MAGIC_ENDIAN_MASK) ==  \
    (WH_COMM_MAGIC_NATIVE   & WH_COMM_MAGIC_ENDIAN_MASK)

/* Header for a packet, request or response. On-the-wire format */
typedef struct {
    uint16_t magic;     /* Endian marker with version */
    uint16_t type;      /* Type of packet.  Enumerated in message.h */
    uint16_t seq;       /* Sequence number. Incremented on request, copied for
                         * response. */
    uint16_t aux;       /* Session identifier for request or error indicator
                         * for response. */
} whHeader;
/* static_assert(sizeof_whHeader == WOLFHSM_COMM_HEADER_LEN,
                 "Size of whHeader doesn't match WOLFHSM_COMM_HEADER_LEN") */

static inline uint8_t wh_Translate8(uint16_t magic, uint8_t val)
{
    (void) magic;
    return val;
}

static inline uint16_t wh_Translate16(uint16_t magic, uint16_t val)
{
    return WH_COMM_FLAGS_SWAPTEST(magic) ? val :
            (val >> 8) | (val << 8);
}

static inline uint32_t wh_Translate32(uint16_t magic, uint32_t val)
{
    return WH_COMM_FLAGS_SWAPTEST(magic) ? val :
            ((val & 0xFF000000ul) >> 24) |
            ((val & 0xFF0000ul) >> 8) |
            ((val & 0xFF00ul) >> 8) |
            ((val & 0xFFul) << 24);
}

static inline uint64_t wh_Translate64(uint16_t magic, uint64_t val)
{
    return WH_COMM_FLAGS_SWAPTEST(magic) ? val :
            ((val & 0xFF00000000000000ull) >> 56) |
            ((val & 0xFF000000000000ull) >> 40) |
            ((val & 0xFF0000000000ull) >> 24) |
            ((val & 0xFF00000000ull) >> 8)|
            ((val & 0xFF000000ull) << 8) |
            ((val & 0xFF0000ull) << 24 ) |
            ((val & 0xFF00ull) << 40) |
            ((val & 0xFFull) << 56);
}


/** Client types */

typedef struct {
    const whTransportClientCb* transport_cb;
    void* transport_context;
    const void* transport_config;
    uint32_t client_id;
} whCommClientConfig;

/* Context structure for a client.  Note the client context will track the
 * request sequence number and provide a buffer for at least 1 packet.
 */
typedef struct {
    void* transport_context;
    const whTransportClientCb* transport_cb;
    uint16_t reqid;
    uint16_t seq;
    uint16_t size;
    uint8_t packet[WOLFHSM_COMM_MTU];
    whHeader* hdr;
    uint8_t* data;
    uint32_t client_id;
    uint32_t server_id;
    int initialized;
} whCommClient;


/* Reset the state of the client context and begin the connection to a server
 * using the config data specified.  On success, the Status of the context will
 * be greater than UNINITIALIZED, depending on the transport specifics.
 */
int wh_CommClient_Init(whCommClient* context, const whCommClientConfig* config);

/* If a request buffer is available, send a new request to the server.  The
 * transport will update the sequence number on success.
 */
int wh_CommClient_SendRequest(whCommClient* context,
        uint16_t magic, uint16_t type, uint16_t* out_seq,
        uint16_t data_size, const void* data);

/* If a response packet has been buffered, get the header and copy the data out
 * of the buffer.
 */
int wh_CommClient_RecvResponse(whCommClient* context,
        uint16_t* out_magic, uint16_t* out_type, uint16_t* out_seq,
        uint16_t* out_size, void* data);

/* Inform the server that no further communications are necessary and any
 * unfinished requests can be ignored.
 */
int wh_CommClient_Cleanup(whCommClient* context);

/** Server types */
typedef struct {
    void* transport_context;
    const whTransportServerCb* transport_cb;
    const void* transport_config;
    uint32_t server_id;
} whCommServerConfig;

/* Context structure for a server.  Note the client context will track the
 * request sequence number and provide a buffer for at least 1 request packet.
 */
typedef struct {

    void* transport_context;
    const whTransportServerCb* transport_cb;
    uint16_t reqid;
    uint8_t packet[WOLFHSM_COMM_MTU];
    whHeader* hdr;
    uint8_t* data;
    uint32_t client_id;
    uint32_t server_id;
    int initialized;
} whCommServer;

/* Reset the state of the server context and begin the connection to a client
 * using the config data specified.  On success, the Status of the context will
 * be greater than UNINITIALIZED, depending on the transport specifics.
 */
int wh_CommServer_Init(whCommServer* context, const whCommServerConfig* config);

/* If a request packet has been buffered, get the header and copy the data out
 * of the buffer.
 */
int wh_CommServer_RecvRequest(whCommServer* context,
        uint16_t* out_magic, uint16_t* out_type, uint16_t* out_seq,
        uint16_t* out_size, void* buffer);

/* Upon completion of the request, send the response packet using the same seq
 * as the incoming request.  Note that overriding the seq number should only be
 * used for asynchronous notifications, such as keep-alive or close.
 */
int wh_CommServer_SendResponse(whCommServer* context,
        uint16_t magic, uint16_t type, uint16_t seq,
        uint16_t data_size, const void* data);

int wh_CommServer_Cleanup(whCommServer* context);

#endif /* WOLFHSM_WH_COMM_H_ */
