/*
 * Copyright (c) 2001-2003 Swedish Institute of Computer Science.
 * All rights reserved. 
 * 
 * Redistribution and use in source and binary forms, with or without modification, 
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED 
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF 
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT 
 * SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, 
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT 
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN 
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING 
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY 
 * OF SUCH DAMAGE.
 *
 * This file is part of the lwIP TCP/IP stack.
 * 
 * Author: Simon Goldschmidt
 *
 *----------------------------------------------------------------------
 *    Modifications: Porting to micro T-Kernel 2.0 /3.0
 *    Modified by UC Technology at 2026/6/25.
 *
 *    Copyright (c) 2011-2026 UC Technology. All Rights Reserved.
 *----------------------------------------------------------------------
 */
 
#ifndef __TKLWIP_OPTS_H__
#define __TKLWIP_OPTS_H__

/* Pull in MTKBSP_* board/system macros so per-board branches below resolve.
 * Required because lwIP includes lwipopts.h via opt.h, which may be processed
 * before any μT-Kernel header on a given translation unit. */
#include <sys/machine.h>

#define SYS_LIGHTWEIGHT_PROT            1
#define NO_SYS                          0

/**
 * LWIP_RAW==1: Enable application layer to hook into the IP layer itself.
 */
#define LWIP_RAW                        1

/**
 * LWIP_NETCONN==1: Enable Netconn API (require to use api_lib.c)
 */
#define LWIP_NETCONN                    1

/**
 * LWIP_SOCKET==1: Enable Socket API (require to use sockets.c)
 */
#define LWIP_SOCKET                     1

#if LWIP_SOCKET
#define LWIP_PROVIDE_ERRNO
#endif // LWIP_SOCKET

/**
 * LWIP_SO_SNDRCVTIMEO_NONSTANDARD==1: SO_RCVTIMEO/SO_SNDTIMEO take an int
 * (milliseconds, much like winsock does) instead of a struct timeval (default).
 */
#define LWIP_SO_SNDRCVTIMEO_NONSTANDARD 1

/**
 * LWIP_SO_SNDTIMEO==1: Enable send timeout for sockets/netconns and
 * SO_SNDTIMEO processing.
 */
#define LWIP_SO_SNDTIMEO                1

/**
 * LWIP_SO_RCVTIMEO==1: Enable receive timeout for sockets/netconns and
 * SO_RCVTIMEO processing.
 */
#define LWIP_SO_RCVTIMEO                1

/**
 * LWIP_IPV4==1: Enable IPv4
 */
#define LWIP_IPV4                       1

/**
 * LWIP_IPV6==1: Enable IPv6
 */
#define LWIP_IPV6                       0

/**
 * LWIP_DHCP==1: Enable DHCP module.
 */
#define LWIP_DHCP                       1

#define DHCP_DEBUG                      LWIP_DBG_OFF

#define LWIP_AUTOIP                     1

/**
 * LWIP_TCP==1: Turn on TCP.
 */
#define LWIP_TCP                        1

/**
 * LWIP_UDP==1: Turn on UDP.
 */
#define LWIP_UDP                        1

/**
 * LWIP_DNS==1: Turn on DNS module. UDP must be available for DNS
 * transport.
 */
#define LWIP_DNS                        1

/**
 * LWIP_ARP==1: Enable ARP functionality.
 */
#define LWIP_ARP                        1

/**
 * LWIP_ICMP==1: Enable ICMP module inside the IP stack.
 * Be careful, disable that make your product non-compliant to RFC1122
 */
#define LWIP_ICMP                       1

#define NO_STDCLIB                      1

#define ERRNO                           1

#define LWIP_IGMP			1

/**
 * PPP_SUPPORT==1: Enable PPP.
 */
#define PPP_SUPPORT                     0

/**
 * PPPOE_SUPPORT==1: Enable PPP Over Ethernet
 */
#define PPPOE_SUPPORT                   0

/**
 * PPPOS_SUPPORT==1: Enable PPP Over Serial
 */
#define PPPOS_SUPPORT                   0

#define PAP_SUPPORT                     1
#define CHAP_SUPPORT                    1
//#define MSCHAP_SUPPORT                  1

#define PPP_DEBUG                       LWIP_DBG_OFF

#define LWIP_SNMP                       0

#if LWIP_SNMP
#define MIB2_STATS                      1
#endif

/**
 * MEM_ALIGNMENT: should be set to the alignment of the CPU
 */
#define MEM_ALIGNMENT                   4

#define LWIP_DEBUG                      LWIP_DBG_OFF

#define TCPIP_DEBUG                     LWIP_DBG_OFF

/* µT-Kernel priorities: LOWER number = HIGHER priority.
 * Keep tcpip_thread ABOVE rx_task so lwIP's TCPIP mbox doesn't fill while
 * rx_task is busy. The ISR-to-rx_task mbox is sized below to absorb
 * bursts during tcpip processing. */
#define NETIF_THREAD_PRIO               11  /* above camera task (12) so RX is not starved */

/* Thread stack size per board.
 * STM32Cube (Cortex-M55 etc.) verified stable at 2048 during 45Mbps tuning.
 * RA FSP keeps the original 4096 as a safety margin until measured. */
#if defined(MTKBSP_STM32CUBE)
#define DEFAULT_THREAD_STACKSIZE        2048
#define TCPIP_THREAD_STACKSIZE          2048
#define SLIPIF_THREAD_STACKSIZE         2048
#define PPP_THREAD_STACKSIZE            2048
#else
#define DEFAULT_THREAD_STACKSIZE        4096
#define TCPIP_THREAD_STACKSIZE          4096
#define SLIPIF_THREAD_STACKSIZE         4096
#define PPP_THREAD_STACKSIZE            4096
#endif

#define DEFAULT_THREAD_PRIO             15
#define TCPIP_THREAD_PRIO               9
#define SLIPIF_THREAD_PRIO              NETIF_THREAD_PRIO
#define PPP_THREAD_PRIO                 NETIF_THREAD_PRIO

#define TCP_MSS                         1460

#define MEM_SIZE                        (64 * 1024)
/* TCP_SND_QUEUELEN must be >= 2 * (TCP_SND_BUF/TCP_MSS) per lwIP sanity check. */
#define TCP_SND_QUEUELEN                96
#define MEMP_NUM_TCP_SEG                TCP_SND_QUEUELEN
#define TCP_SND_BUF                     (44 * TCP_MSS)
#define TCP_WND                         (32 * TCP_MSS)

/* PBUF_POOL_SIZE * (PBUF_POOL_BUFSIZE - headers) must be >= TCP_WND.
 * TCP_WND = 44*1460 = 64240; PBUF_POOL_BUFSIZE default ~1538 usable.
 * On RA FSP measurements showed peak use of 44/48 with the deeper HAL_Net
 * RX queue (DEV_HAL_RBUF_NUM=32), which throttled TCP's advertised window
 * just shy of exhaustion. Bumping to 96 leaves headroom for bursts. */
#define MEMP_NUM_PBUF                   96
#define PBUF_POOL_SIZE                  96

/* HW checksum offload is only configured on STM32Cube targets:
 * TX HW offload is enabled by main.c. RX HW offload is enabled in phy.h
 * (MACConf.ChecksumOffload = ENABLE) and frames flagged with the descriptor
 * Error Summary bit are dropped in hal_net.c, so lwIP can skip both directions
 * of checksum work.
 * On other targets (e.g. RA FSP RMAC) the MAC does NOT compute IP/TCP/UDP
 * checksums, so lwIP must compute and verify them in software — leave these
 * defines off so lwIP defaults (=1) apply. */
#if defined(MTKBSP_STM32CUBE)
#define CHECKSUM_GEN_IP                 0
#define CHECKSUM_GEN_TCP                0
#define CHECKSUM_GEN_UDP                0
#define CHECKSUM_GEN_ICMP               0
#define CHECKSUM_CHECK_IP               0
#define CHECKSUM_CHECK_TCP              0
#define CHECKSUM_CHECK_UDP              0
#define CHECKSUM_CHECK_ICMP             0
#endif

/* Let lwIP coalesce the TX pbuf chain before calling low_level_output so the
 * driver's memcpy-into-output_buf loop runs once per packet. */
#define LWIP_NETIF_TX_SINGLE_PBUF       1

/* NOTE: LWIP_TCPIP_CORE_LOCKING was tried but regressed throughput (45 -> 36
 * Mbps) because LWIP_COMPAT_MUTEX=1 on the tk port implements the mutex as
 * a binary semaphore, and acquire/release is more expensive than the mbox
 * post-and-wait path the mbox-based default uses. */

/* NOTE: LWIP_TCP_SACK_OUT was measured to drop throughput from 39 -> 29 Mbps
 * because the sender responds to multiple SACK ranges with many small Fast
 * Retransmissions, and the per-ACK SACK-option processing on the receiver
 * eats CPU. Without SACK, the simpler Go-Back-N recovery is actually
 * faster on this MCU. Disabled. */

/* tknetif.c declares struct pbuf_ether { struct pbuf_custom cpbuf; ... }
 * unconditionally. LWIP_NETIF_TX_SINGLE_PBUF=1 turns off the default that
 * pulls in pbuf_custom, so request it explicitly. */
#define LWIP_SUPPORT_CUSTOM_PBUF        1

/* ---------- Statistics options ---------- */

#define LWIP_STATS              1
#define LWIP_STATS_DISPLAY      1

#if LWIP_STATS
#define LINK_STATS              1
#define IP_STATS                1
#define ICMP_STATS              1
#define IGMP_STATS              1
#define IPFRAG_STATS            1
#define UDP_STATS               1
#define TCP_STATS               1
#define MEM_STATS               1
#define MEMP_STATS              1
#define PBUF_STATS              1
#define SYS_STATS               1
#endif /* LWIP_STATS */

/* Minimal changes to opt.h required for etharp unit tests: */
#define ETHARP_SUPPORT_STATIC_ENTRIES   1

#define ETHER_DRV_MAX_RBUFF     (64)

/* HTTP application extensions; generated Flash files are included by fs.c. */
#define LWIP_HTTPD_SUPPORT_POST         1
#define LWIP_HTTPD_CUSTOM_FILES         1
#define LWIP_HTTPD_DYNAMIC_HEADERS      1
#define LWIP_HTTPD_FILE_EXTENSION       1
#define LWIP_HTTPD_SUPPORT_EXTSTATUS    1
#define HTTPD_FSDATA_FILE "../Application/web/fsdata.h"
/* Copy mutable custom-file data into TCP pbufs before a slot is reused. */
#define HTTP_IS_DATA_VOLATILE(hs) TCP_WRITE_FLAG_COPY

#endif /* __LWIPOPTS_H__ */


