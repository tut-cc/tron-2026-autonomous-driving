#include "http_server.h"
#include "../interface/controller_if.h"
#include "control_api.h"
#include "lwip/apps/httpd.h"
#include "lwip/tcpip.h"
static void start_on_core(void *arg)
{
    (void)arg;
    httpd_init();
}
bool http_server_start(void)
{
    if (!control_if_init())
        return false;
    control_api_init();
    /* Raw lwIP API must run in tcpip_thread, not the DHCP application task. */
    return tcpip_callback(start_on_core, NULL) == ERR_OK;
}
