#include "vehicle_control.h"
#include "producer_api.h"
#include "motor_output_drv8833.h"
#include "motor_build_profile.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#undef assert
#define assert(e) do { if(!(e)) {fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#e);exit(1);} } while(0)
static ai_perception_result_t ai(uint32_t seq,uint32_t t) {
    ai_perception_result_t a={0};a.interface_version=1;a.seq=seq;a.capture_timestamp_ms=t;
    a.path_valid=1;a.obstacle_valid=1;a.path_width=.5F;a.path_confidence=.9F;return a;
}
static void feed(vc_t *v,uint32_t seq,uint32_t now) {
    ai_perception_result_t a=ai(seq,now);
    tof_safety_result_t t={seq,now,500,1,0};
    assert(vc_ai(v,&a,now)==0);assert(vc_tof(v,&t,now)==0);vc_link(v,now);
}
static uint32_t wseq;
static int web(vc_t *v,uint32_t action,uint32_t mode,uint32_t deadman,int32_t lin,int32_t str,uint32_t now) {
    vc_web_t w={++wseq,now,action,mode,deadman,lin,str};return vc_web(v,&w,now);
}
/* Sensors healthy and AI path ready (3 good frames), MANUAL, stopped. */
static void boot(vc_t *v) {
    control_motor_output_t o;unsigned i;vc_init(v,0);wseq=0;
    for(i=1;i<=3;++i){feed(v,i,i*10);vc_step(v,i*10,&o);assert(!o.motor_enable);}
}
static void test_core(void) {
    vc_t v;control_motor_output_t o;ai_perception_result_t a;tof_safety_result_t t;unsigned i;uint32_t n;
    /* Boot: MANUAL, stopped, nothing moves without an operator input. */
    vc_init(&v,0);assert(v.status.mode==VC_MODE_MANUAL&&!v.status.armed);
    assert(vc_start(&v,0)!=VC_ARM_OK);vc_step(&v,10,&o);assert(!o.motor_enable);
    /* MANUAL: holding the D-pad starts and drives, forward-only, ramped. */
    boot(&v);n=v.start_count;
    assert(web(&v,VC_WEB_DRIVE,VC_MODE_MANUAL,0,0,0,30)==0&&!v.status.armed); /* neutral poll */
    assert(web(&v,VC_WEB_DRIVE,VC_MODE_MANUAL,1,1000,1000,40)==0);
    assert(v.status.armed&&v.status.state==VC_MANUAL&&v.start_count==n+1);
    for(i=0;i<15;++i){feed(&v,4+i,40+10*i);vc_step(&v,40+10*i,&o);}
    assert(o.motor_enable&&o.left_command>o.right_command&&o.left_command<=.6F);
    assert(web(&v,VC_WEB_DRIVE,VC_MODE_MANUAL,1,-1000,0,190)==0);
    for(i=0;i<40;++i){feed(&v,20+i,190+10*i);vc_step(&v,190+10*i,&o);}
    assert(o.left_command==0&&o.right_command==0);                     /* no reverse */
    /* Release stops; pressing again restarts (no extra arming step). */
    assert(web(&v,VC_WEB_DRIVE,VC_MODE_MANUAL,0,0,0,590)==0);assert(!v.status.armed);
    vc_step(&v,590,&o);assert(!o.motor_enable);
    assert(web(&v,VC_WEB_DRIVE,VC_MODE_MANUAL,1,1000,0,600)==0&&v.status.armed);
    vc_step(&v,600,&o);assert(o.motor_enable);
    /* MANUAL fail-safe: phone silent for VC_WEB_TIMEOUT_MS stops. */
    n=600+VC_WEB_TIMEOUT_MS+10;feed(&v,70,n);vc_step(&v,n,&o);
    assert(!o.motor_enable&&!v.status.armed&&v.status.reason==VC_WEB);
    /* ToF pre-stop (<= VC_TOF_PRESTOP_MM): ordinary STOP, not an emergency;
     * starting is refused until the distance is beyond the pre-stop. */
    boot(&v);assert(web(&v,VC_WEB_DRIVE,VC_MODE_MANUAL,1,1000,0,30)==0&&v.status.armed);
    t=(tof_safety_result_t){4,40,VC_TOF_PRESTOP_MM,1,0};assert(vc_tof(&v,&t,40)==0);vc_link(&v,40);vc_step(&v,40,&o);
    assert(!o.motor_enable&&v.status.state==VC_STOPPED&&v.status.reason==VC_TOF_PRESTOP);
    assert(web(&v,VC_WEB_DRIVE,VC_MODE_MANUAL,1,1000,0,45)==0&&!v.status.armed);
    assert(g_vc_start_result==VC_ARM_TOF_NOT_OK);
    /* Stopped: a near or invalid sample changes nothing (no MANUAL_ABORT
     * flicker); the start stays refused by tof_ok(). */
    t=(tof_safety_result_t){5,45,VC_TOF_STOP_MM,1,0};assert(vc_tof(&v,&t,45)==0);vc_link(&v,45);vc_step(&v,45,&o);
    assert(v.status.state==VC_STOPPED&&v.status.reason==VC_TOF_PRESTOP&&!v.status.armed);
    t=(tof_safety_result_t){6,46,0,0,0};assert(vc_tof(&v,&t,46)==0);vc_step(&v,46,&o);
    assert(v.status.state==VC_STOPPED&&v.status.reason==VC_TOF_PRESTOP&&!v.status.tof_valid);
    assert(vc_tof(&v,0,47)<0&&v.status.state==VC_STOPPED);
    /* Driving: ToF near (<= VC_TOF_STOP_MM) is an emergency; it clears itself
     * only beyond VC_TOF_CLEAR_MM (hysteresis), keeps showing its cause and
     * never restarts by itself. */
    boot(&v);assert(web(&v,VC_WEB_DRIVE,VC_MODE_MANUAL,1,1000,0,30)==0&&v.status.armed);
    t=(tof_safety_result_t){5,50,VC_TOF_STOP_MM,1,0};assert(vc_tof(&v,&t,50)==0);vc_link(&v,50);vc_step(&v,50,&o);
    assert(!o.motor_enable&&v.status.state==VC_EMERGENCY&&v.status.reason==VC_TOF_NEAR);
    t=(tof_safety_result_t){6,60,VC_TOF_PRESTOP_MM,1,0};vc_tof(&v,&t,60);vc_link(&v,60);vc_step(&v,60,&o);
    assert(v.status.state==VC_EMERGENCY);                                 /* hysteresis */
    assert(web(&v,VC_WEB_DRIVE,VC_MODE_MANUAL,1,1000,0,60)==0&&!v.status.armed);
    t=(tof_safety_result_t){7,70,VC_TOF_CLEAR_MM,1,0};vc_tof(&v,&t,70);vc_link(&v,70);vc_step(&v,70,&o);
    assert(v.status.state==VC_STOPPED&&!v.status.armed&&!o.motor_enable); /* cleared, not moving */
    assert(v.status.reason==VC_TOF_NEAR);                                 /* cause still shown */
    assert(web(&v,VC_WEB_DRIVE,VC_MODE_MANUAL,0,0,0,70)==0);
    assert(web(&v,VC_WEB_DRIVE,VC_MODE_MANUAL,1,1000,0,80)==0&&v.status.armed);
    /* ESTOP stops and stays latched until a board reset, even when every
     * sensor is healthy; a Web STOP/reset cannot clear it either. */
    assert(web(&v,VC_WEB_ESTOP,VC_MODE_MANUAL,0,0,0,90)==0&&v.status.state==VC_EMERGENCY);
    assert(v.status.reason==VC_ESTOP);
    {
        vc_web_t queued_before_stop={wseq-1U,90U,VC_WEB_DRIVE,VC_MODE_MANUAL,1U,1000,0};
        assert(v.status.web_seq==wseq);
        assert(vc_web(&v,&queued_before_stop,90U)<0); /* no stale re-arm */
    }
    for(i=0;i<20;++i){feed(&v,8+i,90+10*i);vc_step(&v,90+10*i,&o);}
    assert(v.status.state==VC_EMERGENCY&&v.status.reason==VC_ESTOP&&!o.motor_enable);
    assert(vc_clear_emergency(&v,280)==VC_ARM_EMERGENCY);
    assert(web(&v,VC_WEB_STOP,VC_MODE_MANUAL,0,0,0,280)==0&&v.status.reason==VC_ESTOP);
    assert(web(&v,VC_WEB_DRIVE,VC_MODE_MANUAL,1,1000,0,280)==0&&!v.status.armed);
    /* AUTO button starts autonomous driving and ignores the phone afterwards. */
    boot(&v);assert(web(&v,VC_WEB_MODE,VC_MODE_AUTO,0,0,0,30)==0);
    assert(v.status.armed&&v.status.mode==VC_MODE_AUTO&&v.status.state==VC_AUTO);
    for(i=0;i<300;++i){feed(&v,4+i,40+10*i);vc_step(&v,40+10*i,&o);} /* 3s without Web */
    assert(v.status.armed&&o.motor_enable&&o.left_command>0);
    /* AUTO: AI path lost -> TOR (output 0, still AUTO, driver asked to take over). */
    a=ai(400,3040);a.path_valid=0;a.obstacle_valid=0;a.path_confidence=0;a.path_width=0;
    assert(vc_ai(&v,&a,3040)==0);t=(tof_safety_result_t){400,3040,500,1,0};vc_tof(&v,&t,3040);vc_link(&v,3040);
    vc_step(&v,3040,&o);
    assert(!o.motor_enable&&!v.status.armed&&v.status.state==VC_TOR&&v.status.mode==VC_MODE_AUTO&&v.status.reason==VC_TOR_REQUEST);
    /* AUTO: MANUAL button stops and returns to MANUAL. */
    boot(&v);assert(web(&v,VC_WEB_MODE,VC_MODE_AUTO,0,0,0,30)==0&&v.status.armed);
    assert(web(&v,VC_WEB_MODE,VC_MODE_MANUAL,0,0,0,40)==0);
    assert(!v.status.armed&&v.status.mode==VC_MODE_MANUAL);
    /* AUTO: ToF pre-stop -> ordinary stop; ToF near -> emergency; link loss
     * reported as VC_LINK. */
    boot(&v);assert(web(&v,VC_WEB_MODE,VC_MODE_AUTO,0,0,0,30)==0);
    t=(tof_safety_result_t){4,40,VC_TOF_PRESTOP_MM,1,0};vc_tof(&v,&t,40);
    assert(!v.status.armed&&v.status.state==VC_STOPPED&&v.status.reason==VC_TOF_PRESTOP&&v.status.mode==VC_MODE_MANUAL);
    boot(&v);assert(web(&v,VC_WEB_MODE,VC_MODE_AUTO,0,0,0,30)==0);
    t=(tof_safety_result_t){4,40,VC_TOF_STOP_MM,1,0};vc_tof(&v,&t,40);
    assert(!v.status.armed&&v.status.state==VC_EMERGENCY&&v.status.reason==VC_TOF_NEAR&&v.status.mode==VC_MODE_MANUAL);
    boot(&v);assert(web(&v,VC_WEB_MODE,VC_MODE_AUTO,0,0,0,30)==0);
    t=(tof_safety_result_t){4,400,500,1,0};vc_tof(&v,&t,400);vc_step(&v,400,&o);
    assert(!o.motor_enable&&v.status.state==VC_EMERGENCY&&v.status.reason==VC_LINK);
    boot(&v);assert(web(&v,VC_WEB_MODE,VC_MODE_AUTO,0,0,0,30)==0);
    vc_link(&v,200);vc_step(&v,200,&o);assert(!o.motor_enable&&v.status.reason==VC_TOF); /* ToF stale */
    /* AUTO: malformed AI frame stops; last good frame must not allow restart. */
    boot(&v);assert(web(&v,VC_WEB_MODE,VC_MODE_AUTO,0,0,0,30)==0);
    a=ai(4,40);a.lateral_error=NAN;assert(vc_ai(&v,&a,40)<0);assert(!v.status.armed);
    assert(web(&v,VC_WEB_MODE,VC_MODE_AUTO,0,0,0,40)==0&&!v.status.armed&&v.status.mode==VC_MODE_MANUAL);
    /* AUTO refused while the path follower is not ready; vc_start is a pure check. */
    vc_init(&v,0);wseq=0;feed(&v,1,10);   /* no vc_step yet: 0 good frames */
    {uint8_t before=v.path.good_frame_count;feed(&v,2,20);
     assert(web(&v,VC_WEB_MODE,VC_MODE_AUTO,0,0,0,20)==0);
     assert(!v.status.armed&&v.status.mode==VC_MODE_MANUAL);
     assert(v.path.good_frame_count==before&&g_vc_start_result==VC_ARM_PATH_NOT_READY);
     assert(v.status.reason==VC_AUTO_REFUSED_PATH);
     /* The refusal remains visible while AUTO_PENDING sends neutral polls. */
     assert(web(&v,VC_WEB_DRIVE,VC_MODE_AUTO,0,0,0,30)==0);
     assert(v.status.reason==VC_AUTO_REFUSED_PATH);
     /* Once the UI synchronizes back to MANUAL, the event is consumed. */
     assert(web(&v,VC_WEB_DRIVE,VC_MODE_MANUAL,0,0,0,40)==0);
     assert(v.status.reason==VC_OK);}
    /* The refusal names the missing prerequisite: no ToF yet, then no M85 link. */
    vc_init(&v,0);wseq=0;vc_link(&v,10);
    assert(web(&v,VC_WEB_MODE,VC_MODE_AUTO,0,0,0,10)==0);
    assert(!v.status.armed&&v.status.state==VC_STOPPED&&g_vc_start_result==VC_ARM_TOF_NOT_OK);
    assert(v.status.reason==VC_AUTO_REFUSED_TOF&&vc_reason_is_auto_refusal(v.status.reason));
    vc_init(&v,0);wseq=0;t=(tof_safety_result_t){1,10,500,1,0};assert(vc_tof(&v,&t,10)==0);
    assert(web(&v,VC_WEB_MODE,VC_MODE_AUTO,0,0,0,10)==0);
    assert(!v.status.armed&&v.status.state==VC_STOPPED&&g_vc_start_result==VC_ARM_LINK_STALE);
    assert(v.status.reason==VC_AUTO_REFUSED_LINK);
    /* Running stops are not refusals. */
    assert(!vc_reason_is_auto_refusal(VC_TOF)&&!vc_reason_is_auto_refusal(VC_LINK)&&!vc_reason_is_auto_refusal(VC_AI));
    /* ToF acquisition failure invalidates the old reading (emergency while driving). */
    boot(&v);assert(vc_tof(&v,0,40)<0);assert(!v.status.tof_valid&&v.status.state==VC_STOPPED);
    boot(&v);assert(web(&v,VC_WEB_DRIVE,VC_MODE_MANUAL,1,1000,0,30)==0&&v.status.armed);
    assert(vc_tof(&v,0,40)<0);assert(!v.status.tof_valid&&v.status.state==VC_EMERGENCY&&v.status.reason==VC_TOF);
    /* Releasing the D-pad is a normal idle, not an abort. */
    boot(&v);assert(web(&v,VC_WEB_DRIVE,VC_MODE_MANUAL,1,1000,0,30)==0&&v.status.armed);
    assert(web(&v,VC_WEB_DRIVE,VC_MODE_MANUAL,0,0,0,40)==0&&!v.status.armed&&v.status.reason==VC_OK);
    assert(web(&v,VC_WEB_STOP,VC_MODE_MANUAL,0,0,0,50)==0&&v.status.reason==VC_OPERATOR);
    vc_init(&v,0xfffffff0U);a=ai(0xffffffffU,0xfffffff0U);assert(vc_ai(&v,&a,0xfffffff0U)==0);
    a=ai(0,5);assert(vc_ai(&v,&a,5)==0); /* seq and clock wrap */
    puts("PASS control: MANUAL boot, D-pad drive/release, no-reverse, web fail-safe, ToF 100mm stop / 50mm emergency + hysteresis, ESTOP latched until reset, AUTO start/stop, wrap");
}
static void auto_running(vc_t *v, uint32_t *now, uint32_t *seq);
/* Person/car detections are logged by the M85 only. Detection results, even
 * when unstable or unavailable, do not affect M33 mode or motor output. */
static void obstacle_frame(vc_t *v,uint32_t seq,uint32_t now,uint16_t tof_mm,
                           float conf,float overlap,float bottom) {
    ai_perception_result_t a=ai(seq,now);tof_safety_result_t t={seq,now,tof_mm,1,0};
    a.obstacle_count=1;
    a.obstacles[0]=(ai_obstacle_result_t){.confidence=conf,.corridor_overlap=overlap,
        .center_x=0.0F,.bbox_bottom=bottom};
    assert(vc_ai(v,&a,now)==0&&vc_tof(v,&t,now)==0);vc_link(v,now);
}
static void test_obstacle_alarm_only(void) {
    vc_t v;control_motor_output_t o;uint32_t now,seq;unsigned i;float baseline_speed_scale;
    /* A person detection is advisory and leaves AUTO output unchanged. */
    auto_running(&v,&now,&seq);
    now+=10;feed(&v,++seq,now);vc_step(&v,now,&o);
    baseline_speed_scale=o.speed_scale;
    for(i=0;i<5;++i){
        ai_perception_result_t a;
        tof_safety_result_t t;
        now+=10;a=ai(++seq,now);a.obstacle_count=1;
        a.obstacles[0]=(ai_obstacle_result_t){.confidence=.95F,.corridor_overlap=.9F,
            .center_x=0.0F,.bbox_bottom=.6F};
        t=(tof_safety_result_t){seq,now,500,1,0};
        assert(vc_ai(&v,&a,now)==0&&vc_tof(&v,&t,now)==0);vc_link(&v,now);
        vc_step(&v,now,&o);
        assert(o.motor_enable&&v.status.armed&&v.status.state==VC_AUTO);
        assert(o.speed_scale==baseline_speed_scale);
    }
    assert(o.motor_enable&&v.status.armed&&v.status.state==VC_AUTO);
    /* Near ToF and image-bottom detections still don't change mode or disarm. */
    for(i=0;i<8;++i){
        now+=10;
        obstacle_frame(&v,++seq,now,200,.95F,.9F,(i&1U)? .96F:.60F);
        vc_step(&v,now,&o);
        assert(o.motor_enable&&v.status.armed&&v.status.state==VC_AUTO&&
               v.status.mode==VC_MODE_AUTO);
    }
    /* Losing/reacquiring the detection also cannot cause mode churn. */
    for(i=0;i<4;++i){
        ai_perception_result_t a;
        tof_safety_result_t t;
        now+=10;a=ai(++seq,now);t=(tof_safety_result_t){seq,now,500,1,0};
        if(i&1U){a.obstacle_count=1;a.obstacles[0]=(ai_obstacle_result_t){
            .confidence=.95F,.corridor_overlap=.9F,.center_x=0.0F,.bbox_bottom=.96F};}
        assert(vc_ai(&v,&a,now)==0&&vc_tof(&v,&t,now)==0);vc_link(&v,now);
        vc_step(&v,now,&o);
        assert(o.motor_enable&&v.status.armed&&v.status.state==VC_AUTO);
    }
    /* A transiently unavailable obstacle detector is advisory too: preserve
     * a valid road path and keep the current AUTO mode. */
    for(i=0;i<4;++i){
        ai_perception_result_t a;
        tof_safety_result_t t;
        now+=10;a=ai(++seq,now);a.obstacle_valid=(uint8_t)(i&1U);
        t=(tof_safety_result_t){seq,now,500,1,0};
        assert(vc_ai(&v,&a,now)==0&&vc_tof(&v,&t,now)==0);vc_link(&v,now);
        vc_step(&v,now,&o);
        assert(o.motor_enable&&v.status.armed&&v.status.state==VC_AUTO);
    }
    /* Non-corridor / low-confidence detections remain non-blocking. */
    auto_running(&v,&now,&seq);
    for(i=0;i<3;++i){now+=10;obstacle_frame(&v,++seq,now,200,.95F,.2F,.9F);vc_step(&v,now,&o);}
    assert(o.motor_enable&&v.status.state==VC_AUTO);
    for(i=0;i<3;++i){now+=10;obstacle_frame(&v,++seq,now,200,.5F,.9F,.9F);vc_step(&v,now,&o);}
    assert(o.motor_enable&&v.status.state==VC_AUTO);
    /* AUTO can start while the independent ToF is clear even with an alarm. */
    boot(&v);obstacle_frame(&v,4,30,200,.95F,.9F,.6F);
    assert(web(&v,VC_WEB_MODE,VC_MODE_AUTO,0,0,0,30)==0&&v.status.armed);
    assert(v.status.mode==VC_MODE_AUTO&&v.status.state==VC_AUTO);
    printf("PASS obstacle: alarm only, no motor/mode response to near/flickering detections, independent ToF safety\n");
}
/* Wheel-balance trim: a car that drifts left keeps seeing the road to the
 * right; AUTO learns the correction (bounded), MANUAL ▲ applies it, and
 * MANUAL driving never changes it. */
static void test_steering_trim(void) {
    vc_t v;control_motor_output_t o;ai_perception_result_t a;tof_safety_result_t t;uint32_t now,seq;unsigned i;
    auto_running(&v,&now,&seq);
    assert(v.path.steering_trim==0.0F);
    for(i=0;i<60;++i){now+=10;a=ai(++seq,now);a.lateral_error=.05F;   /* inside the deadband */
        t=(tof_safety_result_t){seq,now,500,1,0};
        assert(vc_ai(&v,&a,now)==0&&vc_tof(&v,&t,now)==0);vc_link(&v,now);vc_step(&v,now,&o);}
    assert(v.path.steering_trim>0.0F&&v.path.steering_trim<=0.10F+1e-6F);
    assert(g_vc_steering_trim_permille>0);
    assert(o.left_command>o.right_command);        /* corrects although inside the deadband */
    for(i=0;i<400;++i){now+=10;a=ai(++seq,now);a.lateral_error=.9F;a.heading_error=.1F;
        t=(tof_safety_result_t){seq,now,500,1,0};
        assert(vc_ai(&v,&a,now)==0&&vc_tof(&v,&t,now)==0);vc_link(&v,now);vc_step(&v,now,&o);}
    assert(v.path.steering_trim<=0.10F+1e-6F);       /* bounded */
    /* Kept across a stop; MANUAL straight ▲ now drives left > right. */
    assert(web(&v,VC_WEB_MODE,VC_MODE_MANUAL,0,0,0,now)==0);
    {float learned=v.path.steering_trim;
    assert(web(&v,VC_WEB_DRIVE,VC_MODE_MANUAL,1,1000,0,now)==0&&v.status.armed);
    for(i=0;i<30;++i){now+=10;a=ai(++seq,now);a.lateral_error=-.9F;t=(tof_safety_result_t){seq,now,500,1,0};
        assert(vc_ai(&v,&a,now)==0&&vc_tof(&v,&t,now)==0);vc_link(&v,now);
        assert(web(&v,VC_WEB_DRIVE,VC_MODE_MANUAL,1,1000,0,now)==0);vc_step(&v,now,&o);}
    assert(o.motor_enable&&o.left_command>o.right_command);
    assert(v.path.steering_trim==learned);}          /* MANUAL does not learn */
    /* A curve (large heading) is not learned as imbalance. */
    auto_running(&v,&now,&seq);
    for(i=0;i<60;++i){now+=10;a=ai(++seq,now);a.lateral_error=.3F;a.heading_error=.6F;
        t=(tof_safety_result_t){seq,now,500,1,0};
        assert(vc_ai(&v,&a,now)==0&&vc_tof(&v,&t,now)==0);vc_link(&v,now);vc_step(&v,now,&o);}
    assert(v.path.steering_trim==0.0F);
    puts("PASS steering trim: learned in AUTO on straight road only, bounded, kept across stop, applied to MANUAL forward");
}
static int held;
static int sem_take(void *x){(void)x;if(held)return -1;held=1;return 0;}
static void sem_give(void *x){(void)x;held=0;}
static void barrier(void *x){(void)x;}
static void test_ipc(void) {
    ipc_shared_t shared={0};ipc_endpoint_t e={&shared,{0,sem_take,sem_give,barrier}};
    ipc_packet_t packet;producer_t p;ai_perception_result_t a=ai(1,10),b;
    vc_web_t w={1,10,VC_WEB_DRIVE,1,1,1000,-200},decoded;
    vc_status_t status={0},got;
    assert(ipc_control_init(&e,123,10)==0);assert(producer_attach(&p,&e)==0);
    assert(producer_send_ai(&p,&a)==0);assert(producer_send_ai(&p,&a)==-2);
    assert(ipc_read(&e,IPC_AI,&packet)==0);assert(ipc_unpack_ai(&packet,&b)==0);assert(b.seq==1&&b.path_confidence>.89F);
    assert(producer_send_web(&p,&w)==0);
    w.action=VC_WEB_ESTOP;assert(producer_send_web(&p,&w)==0); /* reserved stop slot */
    assert(ipc_read(&e,IPC_STOP,&packet)==0&&ipc_unpack_web(&packet,&decoded)==0&&decoded.action==VC_WEB_ESTOP);
    assert(ipc_read(&e,IPC_WEB,&packet)==0);assert(ipc_unpack_web(&packet,&decoded)==0);assert(decoded.steering==-200);
    packet.crc^=1;assert(ipc_write(&e,&packet)<0);
    a.lateral_error=NAN;assert(producer_send_ai(&p,&a)<0);
    assert(producer_heartbeat(&p)==0);shared.slots[IPC_HEARTBEAT].packet.data[0]^=1;
    assert(ipc_read(&e,IPC_HEARTBEAT,&packet)<0);
    status.left_permille=450;status.control_ms=20;ipc_pack_status(&packet,123,1,&status);
    assert(ipc_write(&e,&packet)==0&&producer_get_status(&p,&got)==0&&got.left_permille==450);
    held=1;assert(producer_heartbeat(&p)<0);held=0;
    assert(ipc_control_init(&e,124,0)==0);assert(producer_heartbeat(&p)<0);
    a=ai(2,0);assert(producer_send_ai(&p,&a)<0); /* stale boot session */
    puts("PASS IPC: roundtrip, CRC, saturation, stop slot, contention, reboot session");
}
static unsigned last[4];
static int write_mock(void *x,unsigned a,unsigned b,unsigned c,unsigned d){(void)x;last[0]=a;last[1]=b;last[2]=c;last[3]=d;return 0;}
static void test_inversion_path(int invert_left, int invert_right) {
    motor_output_drv8833_t d;
    motor_drv8833_config_t c={.context=0,.write_pins=write_mock,.duty_limit=.6F,
                              .invert_left=invert_left,.invert_right=invert_right};
    control_motor_output_t out={.motor_enable=1,.left_command=.25F,.right_command=.25F};
    assert(motor_output_drv8833_init(&d,&c)==0);assert(motor_output_drv8833_arm(&d)==0);
    assert(motor_output_drv8833_apply(&d,&out)==0);
    motor_output_drv8833_tick_100us(&d);
    assert(last[0]==(invert_left?0U:1U)&&last[1]==(invert_left?1U:0U));
    assert(last[2]==(invert_right?0U:1U)&&last[3]==(invert_right?1U:0U));
}
/* MOTOR_SWAP_SIDES: the vehicle's left wheel is on BIN, the right on AIN.
 * A left turn (right wheel forward only) must then drive AIN only. */
static void test_swap_sides(void) {
    motor_output_drv8833_t d;
    motor_drv8833_config_t c={.context=0,.write_pins=write_mock,.duty_limit=.6F,.swap_sides=1};
    control_motor_output_t out={.motor_enable=1,.left_command=0,.right_command=.5F};
    unsigned i,ain=0,bin=0;
    assert(motor_output_drv8833_init(&d,&c)==0&&motor_output_drv8833_arm(&d)==0);
    assert(motor_output_drv8833_apply(&d,&out)==0);
    for(i=0;i<MOTOR_PWM_SLOTS;++i){motor_output_drv8833_tick_100us(&d);ain+=last[0]|last[1];bin+=last[2]|last[3];}
    assert(ain>0&&bin==0&&!last[1]);                    /* right wheel forward on AIN */
    out.left_command=.5F;out.right_command=0;assert(motor_output_drv8833_apply(&d,&out)==0);
    for(i=0,ain=bin=0;i<MOTOR_PWM_SLOTS;++i){motor_output_drv8833_tick_100us(&d);ain+=last[0]|last[1];bin+=last[2]|last[3];}
    assert(bin>0&&ain==0&&!last[3]);                    /* left wheel forward on BIN */
}
static void test_driver(void) {
    motor_output_drv8833_t d;
    motor_drv8833_config_t c={.context=0,.write_pins=write_mock,.duty_limit=.6F,
                              .invert_left=0,.invert_right=0};
    control_motor_output_t out={0};unsigned i,on=0;
    assert(MOTOR_DUTY_CAP_PERCENT==60U);
    test_inversion_path(0,1);test_inversion_path(1,0);test_swap_sides();
    assert(motor_output_drv8833_init(&d,&c)==0);assert(!d.armed);
    assert(motor_output_drv8833_arm(&d)==0);out.motor_enable=1;out.left_command=out.right_command=1;
    assert(motor_output_drv8833_apply(&d,&out)==0);
    for(i=0;i<20;++i){motor_output_drv8833_tick_100us(&d);on+=last[0];assert(!(last[0]&&last[1]));}
    assert(on==12);
    motor_output_drv8833_disarm(&d,MOTOR_STOP_COAST);
    assert(!last[0]&&!last[1]&&!last[2]&&!last[3]);
    assert(motor_output_drv8833_arm(&d)==0);
    motor_output_drv8833_disarm(&d,MOTOR_STOP_BRAKE);
    assert(last[0]&&last[1]&&last[2]&&last[3]);
    assert(motor_output_drv8833_arm(&d)==0);
    out.left_command=out.right_command=1;assert(motor_output_drv8833_apply(&d,&out)==0);
    motor_output_drv8833_tick_100us(&d);
    out.left_command=out.right_command=-1;assert(motor_output_drv8833_apply(&d,&out)==0);
    for(i=0;i<MOTOR_REVERSE_TICKS;++i){motor_output_drv8833_tick_100us(&d);assert(!last[0]&&!last[1]&&!last[2]&&!last[3]);}
    for(i=0;i<20;++i)motor_output_drv8833_tick_100us(&d);
    assert(last[1]||last[3]);
    for(i=0;i<1000;++i)motor_output_drv8833_tick_100us(&d);
    assert(d.fault&&!d.armed&&last[0]&&last[1]&&last[2]&&last[3]);
    assert(motor_output_drv8833_arm(&d)<0);
    /* Sub-slot resolution: 0.43 and 0.40 differ by less than one 5% slot but
     * their average on-time over 10 periods still differs (86 vs 80 slots). */
    {motor_drv8833_config_t f={.context=0,.write_pins=write_mock,.duty_limit=1.0F};
     control_motor_output_t w={.motor_enable=1,.left_command=.43F,.right_command=.40F};
     unsigned left_on=0,right_on=0;
     assert(motor_output_drv8833_init(&d,&f)==0&&motor_output_drv8833_arm(&d)==0);
     for(i=0;i<10U*MOTOR_PWM_SLOTS;++i){
         if(i%100U==0U)assert(motor_output_drv8833_apply(&d,&w)==0);   /* keep the watchdog fed */
         motor_output_drv8833_tick_100us(&d);left_on+=last[0];right_on+=last[2];}
     assert(left_on>=85U&&left_on<=86U&&right_on>=79U&&right_on<=80U);}
    puts("PASS driver: inversion paths, left/right swap, cap60%/ramp, brake/coast, no-reverse deadtime, watchdog/fault latch, sub-slot duty");
}
/* 2026-09-26 bench regression: M85 delivers AI frames every ~300ms, each
 * ~160ms old on arrival.  AUTO must still become ready; a real AI outage
 * longer than VC_AUTO_AI_TIMEOUT_MS must still stop AUTO. */
static void test_slow_ai_period(void) {
    vc_t v;control_motor_output_t o;ai_perception_result_t a;tof_safety_result_t t;
    uint32_t now,seq=0,tseq=0,last=0;
    vc_init(&v,0);wseq=0;
    for(now=160;now<=2000;now+=10){
        t=(tof_safety_result_t){++tseq,now,500,1,0};assert(vc_tof(&v,&t,now)==0);vc_link(&v,now);
        if(now%300==160){a=ai(++seq,now-160);assert(vc_ai(&v,&a,now)==0);last=now;}
        vc_step(&v,now,&o);
    }
    assert(v.path.good_frame_count>=v.path.config.good_frames_to_auto);
    assert(web(&v,VC_WEB_MODE,VC_MODE_AUTO,0,0,0,now)==0);
    assert(v.status.armed&&v.status.mode==VC_MODE_AUTO&&g_vc_start_result==VC_ARM_OK);
    /* AI stops arriving: AUTO must stop once the timeout is exceeded. */
    for(;now<=last+VC_AUTO_AI_TIMEOUT_MS+50;now+=10){
        t=(tof_safety_result_t){++tseq,now,500,1,0};vc_tof(&v,&t,now);vc_link(&v,now);vc_step(&v,now,&o);
    }
    assert(!v.status.armed&&!o.motor_enable);
}
/* Path confidence band (ai_control_signals.h): below ENTER AUTO cannot start,
 * at ENTER it can, and once running it holds down to HOLD. */
static void feed_conf(vc_t *v,uint32_t seq,uint32_t now,float conf) {
    ai_perception_result_t a=ai(seq,now);tof_safety_result_t t={seq,now,500,1,0};
    a.path_confidence=conf;assert(vc_ai(v,&a,now)==0);assert(vc_tof(v,&t,now)==0);vc_link(v,now);
}
static void test_path_confidence_band(void) {
    vc_t v;control_motor_output_t o;uint32_t i,now=0,seq=0;
    const float enter=(float)AI_PATH_CONFIDENCE_ENTER_PER_MILLE/1000.0F;
    const float hold=(float)AI_PATH_CONFIDENCE_HOLD_PER_MILLE/1000.0F;
    assert(VC_AUTO_AI_TIMEOUT_MS==VC_AI_FRESH_MS&&VC_AI_FRESH_MS==AI_FRAME_MAX_AGE_MS);
    vc_init(&v,0);wseq=0;
    for(i=0;i<5;++i){now+=10;feed_conf(&v,++seq,now,enter-0.01F);vc_step(&v,now,&o);}
    assert(web(&v,VC_WEB_MODE,VC_MODE_AUTO,0,0,0,now)==0);
    assert(!v.status.armed&&v.status.reason==VC_AUTO_REFUSED_PATH);
    assert(web(&v,VC_WEB_DRIVE,VC_MODE_MANUAL,0,0,0,now)==0);
    for(i=0;i<5;++i){now+=10;feed_conf(&v,++seq,now,enter);vc_step(&v,now,&o);}
    assert(web(&v,VC_WEB_MODE,VC_MODE_AUTO,0,0,0,now)==0);
    assert(v.status.armed&&v.status.mode==VC_MODE_AUTO);
    for(i=0;i<5;++i){now+=10;feed_conf(&v,++seq,now,hold);vc_step(&v,now,&o);}
    assert(v.status.armed&&o.motor_enable);
    now+=10;feed_conf(&v,++seq,now,hold-0.01F);vc_step(&v,now,&o);
    assert(!v.status.armed);
}
/* TOR (take-over request): entry, take-over, timeout, hazards, no auto-resume. */
static void auto_running(vc_t *v, uint32_t *now, uint32_t *seq)
{
    control_motor_output_t o; unsigned i;
    boot(v); assert(web(v,VC_WEB_MODE,VC_MODE_AUTO,0,0,0,30)==0 && v->status.armed);
    *now=30; *seq=3;
    for(i=0;i<5;++i){*now+=10;feed(v,++*seq,*now);vc_step(v,*now,&o);}
    assert(v->status.state==VC_AUTO && o.motor_enable);
}
static void lose_path(vc_t *v, uint32_t now, uint32_t seq)
{
    ai_perception_result_t a=ai(seq,now); tof_safety_result_t t={seq,now,500,1,0};
    a.path_valid=0; a.path_confidence=0; assert(vc_ai(v,&a,now)==0); vc_tof(v,&t,now); vc_link(v,now);
}
static void keep_sensors(vc_t *v, uint32_t now, uint32_t seq, int path_ok)
{
    ai_perception_result_t a=ai(seq,now); tof_safety_result_t t={seq,now,500,1,0};
    if(!path_ok){a.path_valid=0;a.path_confidence=0;}
    assert(vc_ai(v,&a,now)==0); vc_tof(v,&t,now); vc_link(v,now);
}
static void test_tor(void)
{
    vc_t v; control_motor_output_t o; uint32_t now, seq, t0;
    /* 1. Entry: AUTO cannot continue -> TOR, output 0 in the same step. */
    auto_running(&v,&now,&seq); now+=10; lose_path(&v,now,++seq); vc_step(&v,now,&o);
    assert(v.status.state==VC_TOR && !v.status.armed && !o.motor_enable);
    assert(v.status.mode==VC_MODE_AUTO && v.status.reason==VC_TOR_REQUEST);
    t0=now;
    /* 2. Path comes back: TOR is kept (no silent resume of AUTO). */
    for(;now<t0+VC_TOR_TIMEOUT_MS-20;){now+=10;keep_sensors(&v,now,++seq,1);vc_step(&v,now,&o);
        assert(v.status.state==VC_TOR && !o.motor_enable);}
    /* AUTO button again does not leave TOR either. */
    assert(web(&v,VC_WEB_MODE,VC_MODE_AUTO,0,0,0,now)==0 && v.status.state==VC_TOR);
    /* 3. Take-over: MANUAL request -> MANUAL, stopped, D-pad drives again. */
    assert(web(&v,VC_WEB_MODE,VC_MODE_MANUAL,0,0,0,now)==0);
    assert(v.status.state==VC_STOPPED && v.status.mode==VC_MODE_MANUAL && !v.status.armed);
    assert(v.status.reason==VC_OK);                 /* take-over is not an abort */
    assert(web(&v,VC_WEB_DRIVE,VC_MODE_MANUAL,1,1000,0,now)==0 && v.status.armed && v.status.state==VC_MANUAL);
    /* 4. No answer: exactly at VC_TOR_TIMEOUT_MS -> safe stop, MANUAL, TOR_TIMEOUT. */
    auto_running(&v,&now,&seq); now+=10; lose_path(&v,now,++seq); vc_step(&v,now,&o); t0=now;
    for(;now+10<t0+VC_TOR_TIMEOUT_MS;){now+=10;keep_sensors(&v,now,++seq,0);vc_step(&v,now,&o);
        assert(v.status.state==VC_TOR);}
    now=t0+VC_TOR_TIMEOUT_MS; keep_sensors(&v,now,++seq,0); vc_step(&v,now,&o);
    assert(v.status.state==VC_STOPPED && v.status.mode==VC_MODE_MANUAL && v.status.reason==VC_TOR_TIMEOUT);
    assert(!v.status.armed && !o.motor_enable);
    /* ...stays stopped until the operator resets (Web STOP). */
    now+=10; keep_sensors(&v,now,++seq,1); vc_step(&v,now,&o);
    assert(v.status.reason==VC_TOR_TIMEOUT && !o.motor_enable);
    assert(web(&v,VC_WEB_STOP,VC_MODE_MANUAL,0,0,0,now)==0 && v.status.reason==VC_OPERATOR);
    /* 5. Hazard during TOR is still an emergency. */
    auto_running(&v,&now,&seq); now+=10; lose_path(&v,now,++seq); vc_step(&v,now,&o);
    { tof_safety_result_t t={++seq,now+10,VC_TOF_STOP_MM,1,0}; now+=10; vc_tof(&v,&t,now); }
    assert(v.status.state==VC_EMERGENCY && v.status.reason==VC_TOF_NEAR);
    /* 6. ESTOP during TOR. */
    auto_running(&v,&now,&seq); now+=10; lose_path(&v,now,++seq); vc_step(&v,now,&o);
    assert(web(&v,VC_WEB_ESTOP,VC_MODE_AUTO,0,0,0,now)==0 && v.status.state==VC_EMERGENCY);
    assert(v.status.reason==VC_ESTOP);
    /* 7. MANUAL driving never enters TOR (it is an AUTO-only state). */
    boot(&v); now=30; seq=3;
    assert(web(&v,VC_WEB_DRIVE,VC_MODE_MANUAL,1,1000,0,now)==0 && v.status.state==VC_MANUAL);
    now+=10; lose_path(&v,now,++seq); assert(web(&v,VC_WEB_DRIVE,VC_MODE_MANUAL,1,1000,0,now)==0); vc_step(&v,now,&o);
    assert(v.status.state==VC_MANUAL);
    printf("PASS TOR: entry on path loss (output 0), no auto-resume, take-over to MANUAL, %ums timeout safe stop + reset, ToF/ESTOP emergency during TOR, MANUAL never enters TOR\n", (unsigned)VC_TOR_TIMEOUT_MS);
}
/* Typed results: why an input or a start was rejected; STOP fence; status CRC. */
static void test_results(void) {
    vc_t v;control_motor_output_t o;ai_perception_result_t a;tof_safety_result_t t;vc_web_t w;
    ipc_packet_t packet;vc_status_t status={0},back;uint32_t now;
    boot(&v);now=30;
    /* AI: wrong version / NULL are INVALID, old capture is STALE, repeat seq is NOT_NEWER. */
    a=ai(4,now);a.interface_version=99;assert(vc_ai(&v,&a,now)==VC_INPUT_INVALID);
    assert(vc_ai(&v,0,now)==VC_INPUT_INVALID);
    a=ai(5,0);now=VC_AI_FRESH_MS+1;assert(vc_ai(&v,&a,now)==VC_INPUT_STALE);
    a=ai(6,now);assert(vc_ai(&v,&a,now)==VC_INPUT_ACCEPTED);
    a=ai(6,now);assert(vc_ai(&v,&a,now)==VC_INPUT_NOT_NEWER);
    a=ai(7,now);a.path_confidence=2;assert(vc_ai(&v,&a,now)==VC_INPUT_INVALID);
    /* ToF: stale and malformed samples are both emergencies while driving,
     * with distinct results (while stopped they are only recorded). */
    boot(&v);assert(web(&v,VC_WEB_DRIVE,VC_MODE_MANUAL,1,1000,0,30)==0&&v.status.armed);
    t=(tof_safety_result_t){9,0,500,1,0};
    assert(vc_tof(&v,&t,VC_TOF_FRESH_MS+50)==VC_INPUT_STALE&&v.status.state==VC_EMERGENCY);
    boot(&v);assert(web(&v,VC_WEB_DRIVE,VC_MODE_MANUAL,1,1000,0,30)==0&&v.status.armed);
    t=(tof_safety_result_t){9,30,500,2,0};
    assert(vc_tof(&v,&t,30)==VC_INPUT_INVALID&&v.status.state==VC_EMERGENCY);
    boot(&v);t=(tof_safety_result_t){9,30,500,2,0};
    assert(vc_tof(&v,&t,30)==VC_INPUT_INVALID&&v.status.state==VC_STOPPED);
    boot(&v);t=(tof_safety_result_t){3,30,500,1,0};assert(vc_tof(&v,&t,30)==VC_INPUT_NOT_NEWER);
    /* Web: out of range INVALID, old timestamp STALE, both stop MANUAL. */
    boot(&v);now=30;
    assert(web(&v,VC_WEB_DRIVE,VC_MODE_MANUAL,1,1001,0,now)==VC_INPUT_INVALID);
    w=(vc_web_t){++wseq,0,VC_WEB_DRIVE,VC_MODE_MANUAL,1,500,0};
    assert(vc_web(&v,&w,VC_WEB_TIMEOUT_MS+1)==VC_INPUT_STALE&&v.status.reason==VC_WEB);
    /* vc_start()/vc_clear_emergency() return the reason and mirror it. */
    boot(&v);assert(vc_start(&v,30)==VC_ARM_WEB_STALE&&g_vc_start_result==VC_ARM_WEB_STALE);
    boot(&v);t=(tof_safety_result_t){4,40,VC_TOF_PRESTOP_MM,1,0};vc_tof(&v,&t,40);vc_link(&v,40);
    assert(vc_clear_emergency(&v,40)==VC_ARM_TOF_NOT_OK&&g_vc_clear_result==VC_ARM_TOF_NOT_OK);
    assert(web(&v,VC_WEB_ESTOP,VC_MODE_MANUAL,0,0,0,40)==0);
    assert(vc_clear_emergency(&v,40)==VC_ARM_EMERGENCY&&g_vc_clear_result==VC_ARM_EMERGENCY);
    /* STOP fence: after vc_fence_web(seq) only a newer command can re-arm. */
    boot(&v);now=30;
    w=(vc_web_t){50,now,VC_WEB_DRIVE,VC_MODE_MANUAL,1,1000,0};
    vc_fence_web(&v,60,now);assert(v.status.web_seq==60&&!v.status.armed);
    assert(vc_web(&v,&w,now)==VC_INPUT_NOT_NEWER&&!v.status.armed);
    w.seq=61;assert(vc_web(&v,&w,now)==VC_INPUT_ACCEPTED&&v.status.armed);
    vc_step(&v,now,&o);assert(o.motor_enable);
    /* IPC: status decode checks the CRC like every other packet type. */
    status.state=VC_MANUAL;status.control_ms=5;ipc_pack_status(&packet,7,1,&status);
    assert(ipc_unpack_status(&packet,&back)==IPC_OK&&back.state==VC_MANUAL);
    packet.data[IPC_STATUS_TOF_MM]^=1U;assert(ipc_unpack_status(&packet,&back)==IPC_ERROR);
    puts("PASS results: input INVALID/STALE/NOT_NEWER, arm/clear result returned and mirrored, STOP fence, status CRC");
}
/* AUTO and MANUAL share one steering convention at the vehicle level
 * (positive = right: left wheel faster).  The contest car's crossed DRV8833
 * wiring is corrected once, in the driver (MOTOR_SWAP_SIDES), never here. */
static void test_steering_sign(void) {
    vc_t v;control_motor_output_t o;ai_perception_result_t a;tof_safety_result_t t;unsigned i;
    boot(&v);
    assert(web(&v,VC_WEB_MODE,VC_MODE_AUTO,0,0,0,30)==0&&v.status.armed);
    v.left=v.right=0.30F;
    for(i=0;i<30;++i){
        uint32_t now=40U+10U*i;
        a=ai(4+i,now);a.lateral_error=0.30F;t=(tof_safety_result_t){4+i,now,500,1,0};
        assert(vc_ai(&v,&a,now)==0&&vc_tof(&v,&t,now)==0);vc_link(&v,now);vc_step(&v,now,&o);
    }
    assert(o.motor_enable&&o.left_command>o.right_command);      /* road to the right */
    boot(&v);
    assert(web(&v,VC_WEB_DRIVE,VC_MODE_MANUAL,1,1000,-1000,40)==0);
    for(i=0;i<15;++i){feed(&v,4+i,40+10*i);vc_step(&v,40+10*i,&o);}
    assert(o.motor_enable&&o.right_command>o.left_command);      /* left button */
    puts("PASS steering sign: AUTO right error and MANUAL left button use the vehicle convention");
}
int main(void){test_steering_sign();test_core();test_ipc();test_driver();test_slow_ai_period();test_path_confidence_band();test_tor();test_obstacle_alarm_only();test_steering_trim();test_results();return 0;}
