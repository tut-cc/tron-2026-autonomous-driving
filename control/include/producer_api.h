#ifndef PRODUCER_API_H
#define PRODUCER_API_H
#include "control_ipc.h"
#ifdef __cplusplus
extern "C" {
#endif
/* M85-side context. One owner task OR caller serializes accesses. */
typedef struct {ipc_endpoint_t *endpoint;uint32_t session,hb_seq;} producer_t;
int producer_attach(producer_t *,ipc_endpoint_t *);
int producer_control_clock(producer_t *,uint32_t *now);
int producer_send_ai(producer_t *,const ai_perception_result_t *);
int producer_send_web(producer_t *,const vc_web_t *);
int producer_heartbeat(producer_t *);
int producer_get_status(producer_t *,vc_status_t *);
#ifdef __cplusplus
}
#endif
#endif
