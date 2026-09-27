#include "control_ipc.h"
#include <string.h>
#include <stddef.h>

/* ABI guards: both cores must agree on these sizes. */
_Static_assert(sizeof(ipc_packet_t)==124,"IPC ABI size");
_Static_assert(IPC_WEB_WORDS==5 && IPC_STATUS_WORDS==12 && IPC_AI_WORDS==20,"IPC payload layout");
_Static_assert(IPC_AI_WORDS<=IPC_MAX_WORDS && IPC_STATUS_WORDS<=IPC_MAX_WORDS,"IPC payload fits");
/* The CRC covers every word before the crc field. */
#define IPC_CRC_WORDS (offsetof(ipc_packet_t,crc)/sizeof(uint32_t))

/* ---- locking -------------------------------------------------------------- */
static int lock(ipc_endpoint_t *e) {
    if(!e || !e->ram || !e->sync.try_lock || !e->sync.unlock || !e->sync.barrier) return IPC_ERROR;
    if(e->sync.try_lock(e->sync.ctx)) return IPC_BUSY;
    e->sync.barrier(e->sync.ctx); return IPC_OK;
}
static void unlock(ipc_endpoint_t *e) { e->sync.barrier(e->sync.ctx);e->sync.unlock(e->sync.ctx); }
static int shared_valid(const ipc_endpoint_t *e) {
    return e->ram->magic==IPC_MAGIC && e->ram->version==IPC_VERSION;
}

uint32_t ipc_crc(const ipc_packet_t *p) {
    uint32_t words[IPC_CRC_WORDS],c=0xFFFFFFFFU;
    unsigned i,j,k;
    memcpy(words,p,sizeof(words));
    for(i=0;i<IPC_CRC_WORDS;++i) for(j=0;j<4;++j) {
        c^=(words[i]>>(8*j))&255U;
        for(k=0;k<8;++k) c=(c>>1)^((0U-(c&1U))&0xEDB88320U);
    }
    return ~c;
}

/* ---- shared RAM ----------------------------------------------------------- */
int ipc_control_init(ipc_endpoint_t *e,uint32_t session,uint32_t now) {
    unsigned i;
    if(!session || lock(e)) return IPC_ERROR;
    e->ram->magic=0;
    e->ram->version=IPC_VERSION;e->ram->session=session;e->ram->control_ms=now;
    for(i=0;i<IPC_SLOTS;++i) e->ram->slots[i].pending=0;
    e->sync.barrier(e->sync.ctx);e->ram->magic=IPC_MAGIC;unlock(e);return IPC_OK;
}
int ipc_clock(ipc_endpoint_t *e,uint32_t *session,uint32_t *now) {
    if(!session||!now||lock(e)) return IPC_ERROR;
    if(!shared_valid(e)) {unlock(e);return IPC_ERROR;}
    *session=e->ram->session;*now=e->ram->control_ms;unlock(e);return IPC_OK;
}
int ipc_set_clock(ipc_endpoint_t *e,uint32_t now) {
    if(lock(e)) return IPC_ERROR;
    e->ram->control_ms=now;unlock(e);return IPC_OK;
}
int ipc_write(ipc_endpoint_t *e,const ipc_packet_t *p) {
    int rc;
    if(!p||p->type>=IPC_SLOTS||p->version!=IPC_VERSION||p->words>IPC_MAX_WORDS||p->crc!=ipc_crc(p))return IPC_ERROR;
    rc=lock(e);if(rc)return rc;
    if(!shared_valid(e)||e->ram->session!=p->session) {unlock(e);return IPC_ERROR;}
    if(e->ram->slots[p->type].pending) {unlock(e);return IPC_BUSY;}
    e->ram->slots[p->type].packet=*p;
    e->sync.barrier(e->sync.ctx);e->ram->slots[p->type].pending=1;unlock(e);return IPC_OK;
}
int ipc_read(ipc_endpoint_t *e,unsigned slot,ipc_packet_t *p) {
    uint32_t session;
    if(!p||slot>=IPC_SLOTS||lock(e))return IPC_ERROR;
    if(!shared_valid(e)){unlock(e);return IPC_ERROR;}
    if(!e->ram->slots[slot].pending){unlock(e);return IPC_EMPTY;}
    *p=e->ram->slots[slot].packet;session=e->ram->session;
    e->ram->slots[slot].pending=0;unlock(e);
    return p->version==IPC_VERSION && p->session==session && p->type==slot &&
        p->words<=IPC_MAX_WORDS && p->crc==ipc_crc(p) ? IPC_OK : IPC_ERROR;
}

/* ---- common packet header ------------------------------------------------- */
static void header(ipc_packet_t *p,uint32_t type,uint32_t session,uint32_t seq,uint32_t stamp,uint32_t words) {
    memset(p,0,sizeof(*p));
    p->version=IPC_VERSION;p->type=type;p->session=session;p->seq=seq;p->stamp=stamp;p->words=words;
}
static int header_ok(const ipc_packet_t *p,uint32_t words) {
    return p->version==IPC_VERSION && p->words==words && p->crc==ipc_crc(p);
}

/* ---- value encodings ------------------------------------------------------ */
static int norm(float f,float lo) {return f>=lo && f<=1.0F;}
static uint32_t fixed(float f) {return (uint32_t)(int32_t)(f*IPC_FIXED_SCALE);}
static float real(uint32_t v) {
    /* Decode two's complement without implementation-defined unsigned cast. */
    return (v<=0x7FFFFFFFU ? (float)v : -(float)(~v+1U))/IPC_FIXED_SCALE;
}
/* Signed permille as a two's-complement word, restricted to +-VC_PERMILLE_MAX. */
static int decode_permille(uint32_t raw,int32_t *out) {
    if(raw<=(uint32_t)VC_PERMILLE_MAX) {*out=(int32_t)raw;return IPC_OK;}
    if(raw>=(uint32_t)-VC_PERMILLE_MAX) {*out=-(int32_t)(~raw+1U);return IPC_OK;}
    return IPC_ERROR;
}

/* ---- AI perception -------------------------------------------------------- */
int ipc_pack_ai(ipc_packet_t *p,uint32_t session,const ai_perception_result_t *a) {
    unsigned i,k=IPC_AI_OBSTACLES;
    if(!p||!a||a->interface_version!=AI_CONTROL_INTERFACE_VERSION||a->path_valid>1||a->obstacle_valid>1||
       a->obstacle_count>AI_CONTROL_MAX_OBSTACLES||
       !norm(a->lateral_error,-1)||!norm(a->heading_error,-1)||!norm(a->path_width,0)||!norm(a->path_confidence,0))return IPC_ERROR;
    header(p,IPC_AI,session,a->seq,a->capture_timestamp_ms,IPC_AI_WORDS);
    p->data[IPC_AI_PATH_VALID]=a->path_valid;
    p->data[IPC_AI_OBSTACLE_VALID]=a->obstacle_valid;
    p->data[IPC_AI_OBSTACLE_COUNT]=a->obstacle_count;
    p->data[IPC_AI_PROCESSING_MS]=a->processing_time_ms;
    p->data[IPC_AI_LATERAL_ERROR]=fixed(a->lateral_error);
    p->data[IPC_AI_HEADING_ERROR]=fixed(a->heading_error);
    p->data[IPC_AI_PATH_WIDTH]=fixed(a->path_width);
    p->data[IPC_AI_PATH_CONFIDENCE]=fixed(a->path_confidence);
    for(i=0;i<a->obstacle_count;++i) {
        const ai_obstacle_result_t *o=&a->obstacles[i];
        if(!norm(o->confidence,0)||!norm(o->corridor_overlap,0)||!norm(o->center_x,-1)||!norm(o->bbox_bottom,0))return IPC_ERROR;
        p->data[k++]=fixed(o->confidence);p->data[k++]=fixed(o->corridor_overlap);
        p->data[k++]=fixed(o->center_x);p->data[k++]=fixed(o->bbox_bottom);
    }
    p->crc=ipc_crc(p);return IPC_OK;
}
int ipc_unpack_ai(const ipc_packet_t *p,ai_perception_result_t *a) {
    unsigned i,k=IPC_AI_OBSTACLES;
    if(!p||!a||p->type!=IPC_AI||!header_ok(p,IPC_AI_WORDS)||
       p->data[IPC_AI_PATH_VALID]>1||p->data[IPC_AI_OBSTACLE_VALID]>1||
       p->data[IPC_AI_OBSTACLE_COUNT]>AI_CONTROL_MAX_OBSTACLES||p->data[IPC_AI_PROCESSING_MS]>65535)return IPC_ERROR;
    memset(a,0,sizeof(*a));a->interface_version=AI_CONTROL_INTERFACE_VERSION;a->seq=p->seq;a->capture_timestamp_ms=p->stamp;
    a->path_valid=(uint8_t)p->data[IPC_AI_PATH_VALID];
    a->obstacle_valid=(uint8_t)p->data[IPC_AI_OBSTACLE_VALID];
    a->obstacle_count=(uint8_t)p->data[IPC_AI_OBSTACLE_COUNT];
    a->processing_time_ms=(uint16_t)p->data[IPC_AI_PROCESSING_MS];
    a->lateral_error=real(p->data[IPC_AI_LATERAL_ERROR]);
    a->heading_error=real(p->data[IPC_AI_HEADING_ERROR]);
    a->path_width=real(p->data[IPC_AI_PATH_WIDTH]);
    a->path_confidence=real(p->data[IPC_AI_PATH_CONFIDENCE]);
    for(i=0;i<a->obstacle_count;++i) {
        a->obstacles[i].confidence=real(p->data[k++]);a->obstacles[i].corridor_overlap=real(p->data[k++]);
        a->obstacles[i].center_x=real(p->data[k++]);a->obstacles[i].bbox_bottom=real(p->data[k++]);
    }
    return IPC_OK; /* vc_ai performs all range/freshness checks on consumer. */
}

/* ---- Web command ---------------------------------------------------------- */
int ipc_pack_web(ipc_packet_t *p,uint32_t session,const vc_web_t *w) {
    if(!p||!w||w->action>VC_WEB_ESTOP||w->mode>VC_MODE_MANUAL||w->deadman>1||
       !vc_permille_ok(w->linear)||!vc_permille_ok(w->steering))return IPC_ERROR;
    header(p,vc_web_action_is_stop(w->action)?IPC_STOP:IPC_WEB,session,w->seq,w->timestamp_ms,IPC_WEB_WORDS);
    p->data[IPC_WEB_ACTION]=w->action;p->data[IPC_WEB_MODE]=w->mode;p->data[IPC_WEB_DEADMAN]=w->deadman;
    p->data[IPC_WEB_LINEAR]=(uint32_t)w->linear;p->data[IPC_WEB_STEERING]=(uint32_t)w->steering;
    p->crc=ipc_crc(p);return IPC_OK;
}
int ipc_unpack_web(const ipc_packet_t *p,vc_web_t *w) {
    int32_t linear,steering;
    if(!p||!w||(p->type!=IPC_WEB&&p->type!=IPC_STOP)||!header_ok(p,IPC_WEB_WORDS))return IPC_ERROR;
    if(decode_permille(p->data[IPC_WEB_LINEAR],&linear)||decode_permille(p->data[IPC_WEB_STEERING],&steering))return IPC_ERROR;
    w->seq=p->seq;w->timestamp_ms=p->stamp;
    w->action=p->data[IPC_WEB_ACTION];w->mode=p->data[IPC_WEB_MODE];w->deadman=p->data[IPC_WEB_DEADMAN];
    w->linear=linear;w->steering=steering;return IPC_OK;
}

/* ---- heartbeat and status ------------------------------------------------- */
void ipc_pack_heartbeat(ipc_packet_t *p,uint32_t session,uint32_t seq,uint32_t stamp) {
    header(p,IPC_HEARTBEAT,session,seq,stamp,IPC_HEARTBEAT_WORDS);p->crc=ipc_crc(p);
}
void ipc_pack_status(ipc_packet_t *p,uint32_t session,uint32_t seq,const vc_status_t *s) {
    header(p,IPC_STATUS,session,seq,s->control_ms,IPC_STATUS_WORDS);
    p->data[IPC_STATUS_STATE]=s->state;p->data[IPC_STATUS_REASON]=s->reason;
    p->data[IPC_STATUS_MODE]=s->mode;p->data[IPC_STATUS_ARMED]=s->armed;
    p->data[IPC_STATUS_TOF_MM]=s->tof_mm;p->data[IPC_STATUS_TOF_VALID]=s->tof_valid;
    p->data[IPC_STATUS_AI_SEQ]=s->ai_seq;p->data[IPC_STATUS_WEB_SEQ]=s->web_seq;
    p->data[IPC_STATUS_CONTROL_MS]=s->control_ms;p->data[IPC_STATUS_MAX_GAP_MS]=s->control_max_gap_ms;
    p->data[IPC_STATUS_LEFT]=(uint32_t)s->left_permille;p->data[IPC_STATUS_RIGHT]=(uint32_t)s->right_permille;
    p->crc=ipc_crc(p);
}
int ipc_unpack_status(const ipc_packet_t *p,vc_status_t *s) {
    if(!p||!s||p->type!=IPC_STATUS||!header_ok(p,IPC_STATUS_WORDS))return IPC_ERROR;
    /* Current profile produces forward-only commands. */
    if(p->data[IPC_STATUS_LEFT]>(uint32_t)VC_PERMILLE_MAX||p->data[IPC_STATUS_RIGHT]>(uint32_t)VC_PERMILLE_MAX)return IPC_ERROR;
    s->state=p->data[IPC_STATUS_STATE];s->reason=p->data[IPC_STATUS_REASON];
    s->mode=p->data[IPC_STATUS_MODE];s->armed=p->data[IPC_STATUS_ARMED];
    s->tof_mm=p->data[IPC_STATUS_TOF_MM];s->tof_valid=p->data[IPC_STATUS_TOF_VALID];
    s->ai_seq=p->data[IPC_STATUS_AI_SEQ];s->web_seq=p->data[IPC_STATUS_WEB_SEQ];
    s->control_ms=p->data[IPC_STATUS_CONTROL_MS];s->control_max_gap_ms=p->data[IPC_STATUS_MAX_GAP_MS];
    s->left_permille=(int32_t)p->data[IPC_STATUS_LEFT];s->right_permille=(int32_t)p->data[IPC_STATUS_RIGHT];
    return IPC_OK;
}
