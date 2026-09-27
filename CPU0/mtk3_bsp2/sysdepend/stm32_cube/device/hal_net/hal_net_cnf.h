/*
 * hal_net_cnf.h 
 * 
 * Copyright (c) 2026 UC Technology. All Rights Reserved.
 *
 * 本プログラムはサンプル・プログラムです。商用・非商用を問わず、
 * 自由に使用、改変、および再配布していただくことができます。
 * 本プログラムの使用によって生じた、いかなる損害やトラブルについても、
 * 作者および権利者は一切の責任を負いません。各自の責任においてご使用ください。
 *
 * This program is a sample program and is provided "as is". You are free
 * to use, modify, and redistribute it for any purpose.
 * In no event shall the authors or copyright holders be liable for any
 * claim, damages, or other liability arising from, out of, or in connection
 * with the use of this software. Use it at your own risk.
 */

/*
 *	Net device driver  (STM32)
 *		Device configuration file
 */
#ifndef	_DEV_HAL_NET_CNF_H_
#define	_DEV_HAL_NET_CNF_H_

#define DEVNAME_HAL_NET		"net"
#define DEV_HAL_NET_TMOUT	(2000)
/* HAL eats 2 * ETH_RX_DESC_CNT buffers at init (one set per channel).
 * With ETH_RX_DESC_CNT=16 (defined in stm32n6xx_hal_conf.h),
 * 32 consumed -> 128 leaves 96 headroom. */
#define DEV_HAL_RBUF_NUM	128

#define ETH_MAX_FRAME_LENGTH	(1514)


#define DEV_HAL_NET_UNITNM	(1)	// Number of Net units

/* 
 * This controller assumes that the maximum size of an Ethernet packet without
 * jumbo frame support can reach up to 1,536 bytes 
 */
#define ETHER_DRV_BUFF_SIZE		(1536U)

#define ETHER_DRV_BUFF_ALIGNMENT	(32U)

#define ETHER_DRV_MAX_RBUFF		(128U)

/* UTK lwIP options: */
/**
 * ETHER_DRV_NO_SETUP_RXBUF==1: Net device driver do not need RX buffer set.
 */
#define ETHER_DRV_NO_SETUP_RXBUF		1

/* Reserved space in the TX buffer. */
#define ETHER_DRV_TXBUF_RESERVED_SIZE		0

#endif	/* _DEV_HAL_NET_CNF_H_ */
