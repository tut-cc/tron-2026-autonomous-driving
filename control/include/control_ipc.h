#ifndef CONTROL_IPC_H
#define CONTROL_IPC_H
#include "vehicle_control.h"
#ifdef __cplusplus
extern "C" {
#endif
/* ---- Shared-RAM mailbox ABI (M85 <-> M33) ---------------------------------
 * 32-bit words only; same little endian ABI on both RA8P1 cores. No pointers.
 * Every numeric value below is part of the wire format: changing one requires
 * rebuilding BOTH cores together and bumping IPC_VERSION. */

/* Slot numbers.  Each slot holds at most one pending packet (latest-value).
 * IPC_STOP is reserved for Web STOP/ESTOP so it can never queue behind DRIVE. */
enum { IPC_AI = 0, IPC_WEB = 1, IPC_HEARTBEAT = 2, IPC_STATUS = 3, IPC_STOP = 4, IPC_SLOTS = 5 };
#define IPC_MAGIC 0x544B4331UL
#define IPC_VERSION 1U
#define IPC_MAX_WORDS 24U
/* AI floats are sent as signed fixed point with this scale (1.0 -> 10000). */
#define IPC_FIXED_SCALE 10000.0F

/* Payload layout of each packet type (index into ipc_packet_t.data). */
enum {                                  /* IPC_WEB and IPC_STOP */
    IPC_WEB_ACTION, IPC_WEB_MODE, IPC_WEB_DEADMAN, IPC_WEB_LINEAR, IPC_WEB_STEERING,
    IPC_WEB_WORDS                       /* = 5 */
};
enum {                                  /* IPC_STATUS */
    IPC_STATUS_STATE, IPC_STATUS_REASON, IPC_STATUS_MODE, IPC_STATUS_ARMED,
    IPC_STATUS_TOF_MM, IPC_STATUS_TOF_VALID, IPC_STATUS_AI_SEQ, IPC_STATUS_WEB_SEQ,
    IPC_STATUS_CONTROL_MS, IPC_STATUS_MAX_GAP_MS, IPC_STATUS_LEFT, IPC_STATUS_RIGHT,
    IPC_STATUS_WORDS                    /* = 12 */
};
enum {                                  /* IPC_AI; obstacles follow, 4 words each */
    IPC_AI_PATH_VALID, IPC_AI_OBSTACLE_VALID, IPC_AI_OBSTACLE_COUNT, IPC_AI_PROCESSING_MS,
    IPC_AI_LATERAL_ERROR, IPC_AI_HEADING_ERROR, IPC_AI_PATH_WIDTH, IPC_AI_PATH_CONFIDENCE,
    IPC_AI_OBSTACLES,                   /* first obstacle word */
    IPC_AI_WORDS_PER_OBSTACLE = 4,
    IPC_AI_WORDS = IPC_AI_OBSTACLES + IPC_AI_WORDS_PER_OBSTACLE * AI_CONTROL_MAX_OBSTACLES /* = 20 */
};
#define IPC_HEARTBEAT_WORDS 0U          /* heartbeat carries only seq + stamp */

typedef struct { uint32_t version,type,session,seq,stamp,words,data[IPC_MAX_WORDS],crc; } ipc_packet_t;
typedef struct { uint32_t pending; ipc_packet_t packet; } ipc_slot_t;
typedef struct {
    uint32_t magic,version,session,control_ms;
    ipc_slot_t slots[IPC_SLOTS];
} ipc_shared_t;
typedef struct {
    void *ctx;
    int (*try_lock)(void *); /* zero=locked; nonblocking hardware semaphore */
    void (*unlock)(void *);
    void (*barrier)(void *);
} ipc_sync_t;
typedef struct { volatile ipc_shared_t *ram; ipc_sync_t sync; } ipc_endpoint_t;

/* Return codes of every ipc_* function that returns int (values are stable;
 * callers may keep testing `== 0` / non-zero). */
typedef enum {
    IPC_OK    =  0,
    IPC_EMPTY =  1, /* ipc_read: slot has no pending packet                    */
    IPC_ERROR = -1, /* bad argument / value, bad CRC or session, not initialized */
    IPC_BUSY  = -2  /* ipc_write: slot still pending or HSEM contended         */
} ipc_result_t;

/* RAM must be non-cacheable, shared by both cores, reserved in BOTH linkers.
 * Only M33 initializes, before M85 producers start. Session must be unique
 * per coordinated boot (retained boot counter or board RNG supplied by BSP).
 */
int ipc_control_init(ipc_endpoint_t *,uint32_t session,uint32_t now);
int ipc_clock(ipc_endpoint_t *,uint32_t *session,uint32_t *now);
int ipc_set_clock(ipc_endpoint_t *,uint32_t now);
/* M85 calls write from a task, never from an ISR. Full slot returns IPC_BUSY;
 * retry ONLY while original stamp is still valid, never re-stamp old data. */
int ipc_write(ipc_endpoint_t *,const ipc_packet_t *);
int ipc_read(ipc_endpoint_t *,unsigned slot,ipc_packet_t *);
uint32_t ipc_crc(const ipc_packet_t *);

/* Packet encoders / decoders: IPC_OK, or IPC_ERROR for a value outside the
 * documented range or a malformed packet (type, word count, CRC). */
int ipc_pack_ai(ipc_packet_t *,uint32_t session,const ai_perception_result_t *);
int ipc_unpack_ai(const ipc_packet_t *,ai_perception_result_t *);
/* STOP/ESTOP are packed for the reserved IPC_STOP slot, others for IPC_WEB. */
int ipc_pack_web(ipc_packet_t *,uint32_t session,const vc_web_t *);
int ipc_unpack_web(const ipc_packet_t *,vc_web_t *);
void ipc_pack_heartbeat(ipc_packet_t *,uint32_t session,uint32_t seq,uint32_t stamp);
void ipc_pack_status(ipc_packet_t *,uint32_t session,uint32_t seq,const vc_status_t *);
int ipc_unpack_status(const ipc_packet_t *,vc_status_t *);
#ifdef __cplusplus
}
#endif
#endif
