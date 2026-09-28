#include <tk/tkernel.h>
#include <tm/tmonitor.h>

#include "lwip/api.h"
#include "lwip/init.h"
#include "lwip/tcpip.h"
#include "lwip/dhcp.h"
#include "netif/etharp.h"
#include "netif/tknetif.h"
#include "http/http_server.h"
#include <hal_net.h>
#include "interface/controller_if.h"
#include "dualcore_board.h"
#include "mipi_csi.h"
#include "frame_stream.h"
#include "m85_gateway_runtime.h"

struct netif tknetif;

/* ---- Network address setting ------------------------------------------
 * NET_USE_STATIC_IP=1 : fixed IP (phone gets its IP from the router's DHCP
 *                       and opens http://NET_STATIC_IP/ ).
 * NET_USE_STATIC_IP=0 : previous behaviour (DHCP client).
 * Default: WRH-733GBK router mode (192.168.2.1/24). .200 is kept away from
 * the low addresses the router's DHCP normally hands out.
 * ----------------------------------------------------------------------- */
#ifndef NET_USE_STATIC_IP
#define NET_USE_STATIC_IP   1
#endif
#define NET_STATIC_IP       192, 168, 2, 200
#define NET_STATIC_NETMASK  255, 255, 255, 0
#define NET_STATIC_GATEWAY  192, 168, 2, 1
/* expand the address list before IP4_ADDR() counts its arguments */
#define NET_IP4_ADDR(dst, addr)  IP4_ADDR(dst, addr)

LOCAL void task_lwip(INT stacd, void *exinf);  // task execution function
LOCAL ID    tskid_lwip;            // Task ID number
LOCAL T_CTSK ctsk_lwip = {             // Task creation information
    .itskpri    = 10,
    .stksz      = 4096,
    .task       = task_lwip,
    .tskatr     = TA_HLNG | TA_RNG3,
};
LOCAL void task_camera(INT stacd, void *exinf);
LOCAL void task_video(INT stacd, void *exinf);
LOCAL void task_usb_stream(INT stacd, void *exinf);
LOCAL ID tskid_camera, tskid_video, tskid_usb_stream;
LOCAL T_CTSK ctsk_camera = {
    .itskpri = 12,
    .stksz = 8192,
    .task = task_camera,
    .tskatr = TA_HLNG | TA_RNG3,
};
LOCAL T_CTSK ctsk_video = {
    .itskpri = 20,
    .stksz = 4096,
    .task = task_video,
    .tskatr = TA_HLNG | TA_RNG3,
};
LOCAL T_CTSK ctsk_usb_stream = {
    .itskpri = 22,
    .stksz = 4096,
    .task = task_usb_stream,
    .tskatr = TA_HLNG | TA_RNG3,
};

static void task_camera(INT stacd, void *exinf)
{
    (void) stacd;
    (void) exinf;
    mipi_csi_ep_entry();
}

static void task_video(INT stacd, void *exinf)
{
    (void) stacd;
    (void) exinf;
    for (;;) {
        (void) frame_stream_video_task_poll();
        tk_dly_tsk(5);
    }
}

static void task_usb_stream(INT stacd, void *exinf)
{
    (void) stacd;
    (void) exinf;
    for (;;) {
        (void) frame_stream_usb_task_poll();
        tk_dly_tsk(5);
    }
}

static void tcpip_init_done(void *arg)
{
	tk_wup_tsk((ID) arg);
}

void task_lwip(INT stacd, void *exinf)
{
    ip_addr_t ipaddr, netmask, gw;
    W mscnt, link_stat, dhcp_stat = 0;
    W link_cnt;

    link_cnt = 100;
    do{
        link_cnt --;
        link_stat = hal_net_get_link_status( 0 );
        tk_dly_tsk(100);
    } while(link_stat < E_OK && link_cnt > 0);

    if (link_stat == E_OK) {
        tm_printf((UB *) "Ether link up cast %d.%d S.\n", (100 - link_cnt) / 10,  (100 - link_cnt) % 10);
    }

    tcpip_init(tcpip_init_done, (void *) tk_get_tid());
    tk_slp_tsk(TMO_FEVR);

    mscnt = 0;
#if NET_USE_STATIC_IP
    (void) mscnt;
    NET_IP4_ADDR(&gw, NET_STATIC_GATEWAY);
    NET_IP4_ADDR(&ipaddr, NET_STATIC_IP);
    NET_IP4_ADDR(&netmask, NET_STATIC_NETMASK);

    netif_add(&tknetif, &ipaddr, &netmask, &gw, NULL, tknetif_init, tcpip_input);
    netif_set_default(&tknetif);
    netif_set_up(&tknetif);
    tm_printf((UB *) "Static IP:\n");
    tm_printf((UB *) "        IP Address: %d.%d.%d.%d\n",
        ip4_addr1(&tknetif.ip_addr), ip4_addr2(&tknetif.ip_addr),
        ip4_addr3(&tknetif.ip_addr), ip4_addr4(&tknetif.ip_addr));
    tm_printf((UB *) "       Subnet Mask: %d.%d.%d.%d\n",
        ip4_addr1(&tknetif.netmask), ip4_addr2(&tknetif.netmask),
        ip4_addr3(&tknetif.netmask), ip4_addr4(&tknetif.netmask));
    tm_printf((UB *) "   Default Gateway: %d.%d.%d.%d\n",
        ip4_addr1(&tknetif.gw), ip4_addr2(&tknetif.gw),
        ip4_addr3(&tknetif.gw), ip4_addr4(&tknetif.gw));
    dhcp_stat |= 0x01;
#else
    IP4_ADDR(&gw, 0, 0, 0, 0);
    IP4_ADDR(&ipaddr, 0, 0, 0, 0);
    IP4_ADDR(&netmask, 0, 0, 0, 0);

    netif_add(&tknetif, &ipaddr, &netmask, &gw, NULL, tknetif_init, tcpip_input);
    netif_set_default(&tknetif);
    netif_set_up(&tknetif);
    tm_printf((UB *) "dhcp_start(&tknetif)\n");
    dhcp_start(&tknetif);

    while((dhcp_stat & 0x01) == 0){
        tk_dly_tsk(DHCP_FINE_TIMER_MSECS);
        if(tknetif.ip_addr.addr && (tknetif.ip_addr.addr != ipaddr.addr)){
            tm_printf((UB *) "Neta DHCP result:\n");
            tm_printf((UB *) "        IP Address: %d.%d.%d.%d\n",
            ip4_addr1(&tknetif.ip_addr),
            ip4_addr2(&tknetif.ip_addr),
            ip4_addr3(&tknetif.ip_addr),
            ip4_addr4(&tknetif.ip_addr));
            ipaddr.addr = tknetif.ip_addr.addr;
            tm_printf((UB *) "       Subnet Mask: %d.%d.%d.%d\n",
            ip4_addr1(&tknetif.netmask),
            ip4_addr2(&tknetif.netmask),
            ip4_addr3(&tknetif.netmask),
            ip4_addr4(&tknetif.netmask));
            tm_printf((UB *) "   Default Gateway: %d.%d.%d.%d\n",
            ip4_addr1(&tknetif.gw),
            ip4_addr2(&tknetif.gw),
            ip4_addr3(&tknetif.gw),
            ip4_addr4(&tknetif.gw));
            dhcp_stat |= 0x01;

            break;
        }
        mscnt += DHCP_FINE_TIMER_MSECS;
        if (mscnt >= DHCP_COARSE_TIMER_SECS*1000) {
            tm_printf((UB *) "Neta DHCP timeout.\n");
            break;
        }
    }
#endif /* NET_USE_STATIC_IP */

    /* Initialize httpserver */
    if(dhcp_stat & 0x01){
        tm_printf((UB *) "httpd_init() Start.\n");
        if (!http_server_start()) { tm_printf((UB *) "HTTP server initialization failed.\n"); }
    }

    do{
        tk_dly_tsk(1000);
    } while(1);
}

/* usermain関数 */
EXPORT  INT usermain( void )
{
    if (!control_if_init()) {
        tm_printf((UB *) "Control Interface initialization failed.\n");
        return -1;
    }
    dc_primary_start();
    /* Start the network before waiting for M33 IPC READY. Runtime init yields
     * while retrying, so lwIP can serve the UI and report control unavailable. */
    tskid_lwip = tk_cre_tsk(&ctsk_lwip);
    if (tskid_lwip <= 0 || tk_sta_tsk(tskid_lwip, 0) < E_OK) {
        tm_printf((UB *) "M85 lwIP task start failed.\n");
        return -1;
    }
    while (!m85_gateway_runtime_init()) {
        tm_printf((UB *) "Waiting for M33 IPC READY...\n");
        tk_dly_tsk(100);
    }
    if (!m85_gateway_task_start()) {
        tm_printf((UB *) "M85 IPC gateway task initialization failed.\n");
        return -1;
    }
    tskid_camera = tk_cre_tsk(&ctsk_camera);
    tskid_video = tk_cre_tsk(&ctsk_video);
    tskid_usb_stream = tk_cre_tsk(&ctsk_usb_stream);
    if (tskid_camera <= 0 || tskid_video <= 0 || tskid_usb_stream <= 0) {
        tm_printf((UB *) "M85 camera/video/USB task initialization failed.\n");
        return -1;
    }
    if (tk_sta_tsk(tskid_camera, 0) < E_OK) {
        tm_printf((UB *) "M85 camera task start failed.\n");
        return -1;
    }
    if (tk_sta_tsk(tskid_video, 0) < E_OK) {
        tm_printf((UB *) "M85 video task start failed.\n");
        return -1;
    }
    if (tk_sta_tsk(tskid_usb_stream, 0) < E_OK) {
        tm_printf((UB *) "M85 USB stream task start failed.\n");
        return -1;
    }
    tk_slp_tsk(TMO_FEVR);

    return 0;
}


