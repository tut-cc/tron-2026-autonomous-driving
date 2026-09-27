#include "producer_api.h"
/* M85-side sender.  All packing lives in control_ipc.c; this file only adds
 * the boot-session binding and the heartbeat sequence. */
int producer_attach(producer_t *p,ipc_endpoint_t *e) {
    uint32_t now,session;
    if(!p||ipc_clock(e,&session,&now))return -1;
    *p=(producer_t){e,session,0};return 0;
}
int producer_control_clock(producer_t *p,uint32_t *now) {
    uint32_t session;
    if(!p||!now||ipc_clock(p->endpoint,&session,now)||session!=p->session)return -1;
    return 0;
}
int producer_send_ai(producer_t *p,const ai_perception_result_t *a) {
    ipc_packet_t packet;
    if(!p||ipc_pack_ai(&packet,p->session,a))return -1;
    return ipc_write(p->endpoint,&packet);
}
int producer_send_web(producer_t *p,const vc_web_t *w) {
    ipc_packet_t packet; /* STOP/ESTOP are packed for the reserved IPC_STOP slot. */
    if(!p||ipc_pack_web(&packet,p->session,w))return -1;
    return ipc_write(p->endpoint,&packet);
}
int producer_heartbeat(producer_t *p) {
    ipc_packet_t packet;uint32_t now;
    if(producer_control_clock(p,&now))return -1;
    ipc_pack_heartbeat(&packet,p->session,++p->hb_seq,now);
    return ipc_write(p->endpoint,&packet);
}
int producer_get_status(producer_t *p,vc_status_t *s) {
    ipc_packet_t q;int rc;
    if(!p||!s)return -1;
    rc=ipc_read(p->endpoint,IPC_STATUS,&q);if(rc)return rc;
    if(q.session!=p->session)return -1;
    return ipc_unpack_status(&q,s);
}
