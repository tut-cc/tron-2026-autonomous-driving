/*
 * hal_net.c
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
 
#include <sys/machine.h>
#include <config_bsp/ra_fsp/config_bsp.h>

#ifdef MTKBSP_RAFSP
#if DEVCNF_USE_HAL_NET

#include <stdlib.h>

#include <tk/tkernel.h>
#include <tk/device.h>
#include <sys/queue.h>

#include <sysdepend/ra_fsp/cpu_status.h>
#include <mtkernel/kernel/knlinc/tstdlib.h>
#include <mtkernel/device/common/drvif/msdrvif.h>
#include <hal_data.h>
#include "hal_net_cnf.h"

#include <tm/tmonitor.h>

/* Spare RX buffer pool fed to the descriptor ring via rxBufferUpdate() in
 * zero-copy mode. Like hal_net_ether_buffers, these are kept in CACHED RAM
 * (see HAL_NET_CACHE_ALIGN note below) for fast CPU access; coherency with the
 * MAC DMA is handled by the invalidate-after-read() maintenance in the glue.
 * 32-byte aligned for safe cache-line maintenance. */
LOCAL __attribute__((section(".ram_nocache"), aligned(32)))
	uint8_t ether_rx_buffers[DEV_HAL_RBUF_NUM][1536];

#define UNUSED(x)		((void)(x))

#define ETHER_FLGPTN_TX_COMPLETE	(1U << 0)
#define ETHER_FLGPTN_TX_ABORTED		(1U << 1)

/* Transmit Complete. */
#define ETHER_ISR_EE_TC_MASK              (1U << 21U)

/* Frame Receive. */
#define ETHER_ISR_EE_FR_MASK              (1U << 18U)

#define PHY_RESET_PIN		(BSP_IO_PORT_07_PIN_08)

#define HAL_NET_ETH_BUFFER_SIZE     1536
/* TX descriptor ring depth per queue. Kept at the FSP default of 4 -
 * TX completions are processed quickly so a shallow ring is fine. */
#define HAL_NET_TX_DESC_PER_QUEUE   (3 + 1)
/* RX descriptor ring depth per queue. 32 fills num_rx_descriptors=64 with
 * 2 queues. Pushing to 48/queue (num_rx_descriptors=96) was measured to
 * give no improvement: CPU is the throughput limit at 100Mbps, not the
 * MAC ring depth, so the extra SRAM stays unused. */
#define HAL_NET_TX_QUEUE_NUM        2
#define HAL_NET_CACHE_ALIGN __attribute__((aligned(32)))
/* TX buffer ring for zero-copy transmit. In zero-copy mode R_RMAC_Write()
 * DMAs directly from the supplied buffer, so the source MUST NOT be reused
 * until its transmission completes. write_data() copies the frame into the
 * next slot and SCB_CleanDCache_by_Addr()s it before write() (cached buffer,
 * see HAL_NET_CACHE_ALIGN note above). HAL_NET_TX_BUF_NUM is chosen larger
 * than the maximum number of frames that can be in flight (TX descriptors =
 * HAL_NET_TX_DESC_PER_QUEUE * HAL_NET_TX_QUEUE_NUM), so round-robin reuse never
 * catches a slot that is still being DMA'd - no completion tracking or blocking
 * is required and the non-blocking pipelined-TX throughput is preserved. */
#define HAL_NET_TX_BUF_NUM   (HAL_NET_TX_DESC_PER_QUEUE * HAL_NET_TX_QUEUE_NUM * 2)
LOCAL uint8_t hal_net_tx_buffers[HAL_NET_TX_BUF_NUM][HAL_NET_ETH_BUFFER_SIZE] HAL_NET_CACHE_ALIGN __attribute__((section(".ram_nocache")));
LOCAL UW hal_net_tx_index = 0;

/*
 *	hal_net.c
 *	Net device driver (RA FSP)
*/

/*---------------------------------------------------------------------*/
/*Net Device driver Control block
 */
typedef struct {
	ID			devid;		// Device ID
	UINT			omode;		// Open mode
	UW			unit;		// Unit no
	BOOL			initialized;	// Is device initialized.
	ID			evtmbfid;	// MBF ID for event notification
	ID			rxmbfid;	// MBF ID for RX event notification
	QUEUE 			freerxbufq;	// Free RX buffer Queue
	ID			flgid;		// Event flag ID
	BOOL			linkstatus;	// Link status	
} T_HAL_NET_DCB;

/* Interrupt detection flag */
LOCAL const T_CFLG	id_flg	= {
			.flgatr		= TA_TFIFO | TA_WMUL,
			.iflgptn	= 0,
};

LOCAL void phy_gpio_reset(void)
{
	g_ioport.p_api->pinWrite(g_ioport.p_ctrl, PHY_RESET_PIN, BSP_IO_LEVEL_LOW);
	R_BSP_SoftwareDelay(10, BSP_DELAY_UNITS_MILLISECONDS);
	g_ioport.p_api->pinWrite(g_ioport.p_ctrl, PHY_RESET_PIN, BSP_IO_LEVEL_HIGH);
	R_BSP_SoftwareDelay(300, BSP_DELAY_UNITS_MILLISECONDS);
}


#if TK_SUPPORT_MEMLIB
LOCAL T_HAL_NET_DCB	*dev_net_cb[DEV_HAL_NET_UNITNM] = {0};
#define		get_dcb_ptr(unit)	(dev_net_cb[unit])
#else
LOCAL T_HAL_NET_DCB	dev_net_cb[DEV_HAL_NET_UNITNM] = {0};
#define		get_dcb_ptr(unit)	(&dev_net_cb[unit])
#endif

#define netdrv_check_param(req, type)	(((req)->size < (W) sizeof(type)) ? E_PAR : E_OK)

/*---------------------------------------------------------------------*/
/* Attribute data control
 */
LOCAL ER read_atr(T_HAL_NET_DCB *p_dcb, T_DEVREQ *req)
{
	ER ercd;
	
	switch(req->start) {
	case DN_NETEVENT:
		ercd = netdrv_check_param( req, ID );
		if( ercd == E_OK ) {
			*((ID *)req->buf) = p_dcb->rxmbfid;
		}
		break;
	case DN_NETADDR:
		ercd = netdrv_check_param( req, NetAddr );
		if( ercd == E_OK ) {
			memcpy(req->buf, g_ether0.p_cfg->p_mac_address, sizeof(NetAddr));
		}
		break;
	case DN_NETRXBUFSZ:
	case DN_NETDEVINFO:
	case DN_NETRESET:
	case DN_NETSTINFO:
	case DN_NETCSTINFO:
	case DN_NETWLANCONFIG:
	case DN_NETWLANSTINFO:
	case DN_NETWLANCSTINFO:
		return E_NOSPT;
	default:
		return E_PAR;
	}
	return ercd;
}

LOCAL ER write_atr(T_HAL_NET_DCB *p_dcb, T_DEVREQ *req)
{
	ER ercd;
	
	switch( req->start ) {
	case DN_NETEVENT:
		ercd = netdrv_check_param( req, ID );
		if( ercd == E_OK ) {
			p_dcb->rxmbfid = *((ID *)req->buf);
		}
		break;

	case DN_NETRXBUF:
		ercd = netdrv_check_param( req, void* );
		if( ercd == E_OK ) {
			/* Disable the ether interrupt to achieve synchronization. */
			DisableInt((UINT) g_ether0.p_cfg->irq);
			QueInsert(*((QUEUE**)req->buf), &p_dcb->freerxbufq);
			EnableInt((UINT) g_ether0.p_cfg->irq, (INT) g_ether0.p_cfg->interrupt_priority);
		}
		break;
	case DN_NETRXBUFSZ:
		ercd = netdrv_check_param( req, NetRxBufSz );
		/* Can't set RX buffer size to r_ether module. Always return E_OK. */
		return E_OK;
	case DN_NETRESET:
	case DN_SET_MCAST_LIST:
	case DN_SET_ALL_MCAST:
	case DN_NETWLANCONFIG:
		/* NOT SUPPORTED */
		return E_NOSPT;
	default:
		
		return E_PAR;
	}
	
	return ercd;
}

/*---------------------------------------------------------------------*/
/* Device-specific data control
 */

/* HAL Callback functions */
LOCAL void HAL_Net_Callback(ether_callback_args_t * p_args)
{
	T_HAL_NET_DCB	*p_dcb;
	fsp_err_t	err;
	NetEvent	event;
	uint32_t 	length;
	void 		*pbuf;
	ER		ercd;

	ENTER_TASK_INDEPENDENT

	p_dcb = (T_HAL_NET_DCB*)p_args->p_context;

	switch(p_args->event) {
		case ETHER_EVENT_LINK_ON:
			tm_printf((const UB *)"Ether link up\n");
			p_dcb->linkstatus = TRUE;
			break;
		case ETHER_EVENT_LINK_OFF:
			tm_printf((const UB *)"Ether link down\n");
			p_dcb->linkstatus = FALSE;
			break;
#if (ETHER_CFG_KEEP_INTERRUPT_EVENT_BACKWORD_COMPATIBILITY)
		case ETHER_EVENT_INTERRUPT:
			if( ETHER_ISR_EE_TC_MASK == (p_args->status_eesr & ETHER_ISR_EE_TC_MASK) ) {
				tk_set_flg(p_dcb->flgid, ETHER_FLGPTN_TX_COMPLETE);
			}
			
			if( ETHER_ISR_EE_FR_MASK == (p_args->status_eesr & ETHER_ISR_EE_FR_MASK) ) {
				if( ETHER_ZEROCOPY_ENABLE == g_ether0.p_cfg->zerocopy ) {
					do {
						/* Zero-copy: read() returns a pointer to the driver-owned
						 * DMA buffer in event.buf (no data copy). */
						length = (uint32_t) g_ether0.p_cfg->ether_buffer_size;
						err = g_ether0.p_api->read(g_ether0.p_ctrl, &event.buf, &length);
						if( err == FSP_SUCCESS ) {
							/* Cached buffer: DMA has just written the frame, so
							 * invalidate the cache lines before the CPU (rx_task)
							 * reads it, otherwise stale cached data is seen. */
							SCB_InvalidateDCache_by_Addr(event.buf, (int32_t) length);
							/* Take a spare buffer to refill the descriptor ring. */
							pbuf = (void *) QueRemoveNext( &p_dcb->freerxbufq );
							if( pbuf != NULL ) {
								event.len = (UH) length;
								ercd = tk_snd_mbf( p_dcb->rxmbfid, &event, sizeof( NetEvent ), TMO_POL );

								if(ercd >= E_OK) {
									g_ether0.p_api->rxBufferUpdate(g_ether0.p_ctrl, pbuf);
									continue;
								}
								else {
									QueInsert((QUEUE*)pbuf, &p_dcb->freerxbufq);
								}
							}
						}

						if( err != FSP_ERR_ETHER_ERROR_NO_DATA ) {
							/* Could not hand the buffer to lwIP (no spare / mbf
							 * full): release it straight back to the ring. */
							g_ether0.p_api->bufferRelease(g_ether0.p_ctrl);
						}
					} while(FSP_ERR_ETHER_ERROR_NO_DATA != err);
				}
				else {
					do {
						/* Copy mode: read() copies the frame into the supplied
						 * buffer. event.buf MUST point at a real buffer. */
						pbuf = (void *) QueRemoveNext( &p_dcb->freerxbufq );
						if( pbuf == NULL ) {
							break;
						}
						event.buf = pbuf;
						length = (uint32_t) g_ether0.p_cfg->ether_buffer_size;
						err = g_ether0.p_api->read(g_ether0.p_ctrl, event.buf, &length);
						if( err == FSP_SUCCESS ) {
							event.len = (UH) length;
							ercd = tk_snd_mbf( p_dcb->rxmbfid, &event, sizeof( NetEvent ), TMO_POL );

							if(ercd >= E_OK) {
								continue;
							}
							else {
								QueInsert((QUEUE*)pbuf, &p_dcb->freerxbufq);
							}
						}
						else {
							QueInsert((QUEUE*)pbuf, &p_dcb->freerxbufq);
						}
					} while(FSP_ERR_ETHER_ERROR_NO_DATA != err);
				}
			}
			break;
#else
		case ETHER_EVENT_TX_COMPLETE:
			tk_set_flg(p_dcb->flgid, ETHER_FLGPTN_TX_COMPLETE);
			break;
		case ETHER_EVENT_TX_ABORTED:
			tk_set_flg(p_dcb->flgid, ETHER_FLGPTN_TX_ABORTED);
			break;
		case ETHER_EVENT_RX_COMPLETE:
			if( ETHER_ZEROCOPY_ENABLE == g_ether0.p_cfg->zerocopy ) {
				do {
					/* Zero-copy: read() returns a pointer to the driver-owned
					 * DMA buffer in event.buf (no data copy). */
					length = (uint32_t) g_ether0.p_cfg->ether_buffer_size;
					err = g_ether0.p_api->read(g_ether0.p_ctrl, &event.buf, &length);
					if( err == FSP_SUCCESS ) {
						/* Cached buffer: DMA has just written the frame, so
						 * invalidate the cache lines before the CPU (rx_task)
						 * reads it, otherwise stale cached data is seen. */
						SCB_InvalidateDCache_by_Addr(event.buf, (int32_t) length);
						/* Take a spare buffer to refill the descriptor ring. */
						pbuf = (void *) QueRemoveNext( &p_dcb->freerxbufq );
						if( pbuf != NULL ) {
							event.len = (UH) length;
							ercd = tk_snd_mbf( p_dcb->rxmbfid, &event, sizeof( NetEvent ), TMO_POL );

							if(ercd >= E_OK) {
								g_ether0.p_api->rxBufferUpdate(g_ether0.p_ctrl, pbuf);
								continue;
							}
							else {
								QueInsert((QUEUE*)pbuf, &p_dcb->freerxbufq);
							}
						}
					}

					if( err != FSP_ERR_ETHER_ERROR_NO_DATA ) {
						/* Could not hand the buffer to lwIP (no spare / mbf
						 * full): release it straight back to the ring. */
						g_ether0.p_api->bufferRelease(g_ether0.p_ctrl);
					}
				} while(FSP_ERR_ETHER_ERROR_NO_DATA != err);
			}
			else {
				do {
					/* Copy mode: read() copies the frame into the supplied
					 * buffer. event.buf MUST point at a real buffer. */
					pbuf = (void *) QueRemoveNext( &p_dcb->freerxbufq );
					if( pbuf == NULL ) {
						break;
					}
					event.buf = pbuf;
					length = (uint32_t) g_ether0.p_cfg->ether_buffer_size;
					err = g_ether0.p_api->read(g_ether0.p_ctrl, event.buf, &length);
					if( err == FSP_SUCCESS ) {
						event.len = (UH) length;
						ercd = tk_snd_mbf( p_dcb->rxmbfid, &event, sizeof( NetEvent ), TMO_POL );

						if(ercd >= E_OK) {
							continue;
						}
						else {
							QueInsert((QUEUE*)pbuf, &p_dcb->freerxbufq);
						}
					}
					else {
						QueInsert((QUEUE*)pbuf, &p_dcb->freerxbufq);
					}
				} while(FSP_ERR_ETHER_ERROR_NO_DATA != err);
			}
			break;
		case ETHER_EVENT_ERR_GLOBAL:
			break;
		case ETHER_EVENT_RX_MESSAGE_LOST:
#endif
		default:
			break;
	}

	LEAVE_TASK_INDEPENDENT
}

LOCAL ER read_data(T_HAL_NET_DCB *p_dcb, T_DEVREQ *req)
{
	UNUSED(p_dcb);
	UNUSED(req);
	return E_NOSPT;
}

LOCAL ER write_data(T_HAL_NET_DCB *p_dcb, T_DEVREQ *req)
{
	fsp_err_t	fsp_err;

	if( req->size < 0 ) {
		return E_PAR;
	}
	if( req->size == 0 ) {
		return ((g_ether0.p_cfg->ether_buffer_size > ETH_MAX_FRAME_LENGTH) ? ETH_MAX_FRAME_LENGTH : (ER) g_ether0.p_cfg->ether_buffer_size);
	}
	else {
		if( p_dcb->linkstatus != TRUE ) {
			/* Check link status */
			fsp_err = g_ether0.p_api->linkProcess(g_ether0.p_ctrl);
			if( p_dcb-> linkstatus != TRUE ) {
				return E_NOMDA;
			}
		}

		if( (UW) req->size > g_ether0.p_cfg->ether_buffer_size ) {
			return E_PAR;
		}

		/* Zero-copy TX: copy the assembled frame into the next slot so the
		 * (cached) lwIP output_buf can be reused immediately. The slot is not
		 * reused until it wraps around HAL_NET_TX_BUF_NUM writes later, by which
		 * time its previous transmission has long completed. */
		uint8_t *txbuf = hal_net_tx_buffers[hal_net_tx_index];
		memcpy(txbuf, req->buf, (size_t) req->size);

		/* Cached buffer: flush the CPU's writes to RAM before the DMA reads
		 * them, otherwise the MAC transmits stale data. */
		SCB_CleanDCache_by_Addr(txbuf, (int32_t) req->size);

		/* FSP RMAC write() is non-blocking: it enqueues the frame into the
		 * TX descriptor ring and returns. We previously did tk_wai_flg() on
		 * ETHER_FLGPTN_TX_COMPLETE which serialised every TX to one packet
		 * per system-tick wake-up (~10ms granularity), capping TCP ACK rate
		 * and dropping iperf throughput to ~1Mbps. Return immediately so
		 * lwIP can pipeline TX. */
		fsp_err = g_ether0.p_api->write(g_ether0.p_ctrl, txbuf, (uint32_t) req->size);
		if( fsp_err != FSP_SUCCESS ) {
			return E_IO;
		}
		hal_net_tx_index = (hal_net_tx_index + 1) % HAL_NET_TX_BUF_NUM;
		return E_OK;
	}
}

/*----------------------------------------------------------------------
 * mSDI I/F function
 */
/*
 * Open device
 */
LOCAL ER dev_net_openfn( ID devid, UINT omode, T_MSDI *p_msdi)
{
	UNUSED(devid);
	UNUSED(omode);
	UNUSED(p_msdi);
	/* Do Nothing. */
	return E_OK;
}

/*
 * Close Device
 */
LOCAL ER dev_net_closefn( ID devid, UINT option, T_MSDI *p_msdi)
{
	UNUSED(devid);
	UNUSED(option);
	UNUSED(p_msdi);
	/* Do Nothing. */
	return E_OK;
}

/*
 * Read Device
 */
LOCAL ER dev_net_readfn( T_DEVREQ *req, T_MSDI *p_msdi)
{
	T_HAL_NET_DCB	*p_dcb;
	ER		err;

	p_dcb = (T_HAL_NET_DCB*)(p_msdi->dmsdi.exinf);

	if(req->start >= 0) {
		err = read_data( p_dcb, req);	// Device specific data
	} else {
		err = read_atr( p_dcb, req);	// Device attribute data
	}
	return err;
}

/*
 * Write Device
 */
LOCAL ER dev_net_writefn( T_DEVREQ *req, T_MSDI *p_msdi)
{
	T_HAL_NET_DCB	*p_dcb;
	ER		err;

	p_dcb = (T_HAL_NET_DCB*)(p_msdi->dmsdi.exinf);

	if(req->start >= 0) {
		err = write_data( p_dcb, req);	// Device specific data
	} else {
		err = write_atr( p_dcb, req);	// Device attribute data
	}
	return err;
}

/*
 * Event Device
 */
LOCAL ER dev_net_eventfn( INT evttyp, void *evtinf, T_MSDI *p_msdi)
{
	UNUSED(evttyp);
	UNUSED(evtinf);
	UNUSED(p_msdi);
	/* Do Nothing. */
	return E_NOSPT;
}

/*----------------------------------------------------------------------
 * Device driver initialization and registration
 */
EXPORT ER dev_init_hal_net( UW unit )
{
	T_HAL_NET_DCB	*p_dcb;
	T_IDEV		idev;
	T_MSDI		*p_msdi;
	T_DMSDI		dmsdi;
	ER		err;
	INT		i;
	fsp_err_t 	fsp_err;

	if( unit >= DEV_HAL_NET_UNITNM) return E_PAR;

#if TK_SUPPORT_MEMLIB
	p_dcb = (T_HAL_NET_DCB*)Kmalloc(sizeof(T_HAL_NET_DCB));
	if( p_dcb == NULL) return E_NOMEM;
	dev_net_cb[unit]	= p_dcb;
#else
	p_dcb = &dev_net_cb[unit];
#endif

	p_dcb->flgid = tk_cre_flg(&id_flg);
	if(p_dcb->flgid <= E_OK) {
		err = (ER)p_dcb->flgid;
		goto err_1;
	}

	/* Device registration information */
	dmsdi.exinf	= p_dcb;
	dmsdi.drvatr	= 0;			/* Driver attributes */
	dmsdi.devatr	= TDK_UNDEF;		/* Device attributes */
	dmsdi.nsub	= 0;			/* Number of sub units */
	dmsdi.blksz	= 1;			/* Unique data block size (-1 = unknown) */
	dmsdi.openfn	= dev_net_openfn;
	dmsdi.closefn	= dev_net_closefn;
	dmsdi.readfn	= dev_net_readfn;
	dmsdi.writefn	= dev_net_writefn;
	dmsdi.eventfn	= dev_net_eventfn;
	
	knl_strcpy( (char*)dmsdi.devnm, DEVNAME_HAL_NET);
	i = knl_strlen(DEVNAME_HAL_NET);
	dmsdi.devnm[i] = (UB)('a' + unit);
	dmsdi.devnm[i+1] = 0;

	err = msdi_def_dev( &dmsdi, &idev, &p_msdi);
	if(err != E_OK) goto err_1;

	p_dcb->devid	= p_msdi->devid;
	p_dcb->unit	= unit;
	p_dcb->evtmbfid	= idev.evtmbfid;
	p_dcb->rxmbfid	= -1;
	p_dcb->linkstatus = FALSE;
	p_dcb->initialized = FALSE;
	
	/* Initialize the RX buffer. */
	QueInit( &p_dcb->freerxbufq );
	for( i = 0; i < DEV_HAL_RBUF_NUM; i++ ) {
		QueInsert((QUEUE*)ether_rx_buffers[i], &p_dcb->freerxbufq);
	}
	
	phy_gpio_reset();

	fsp_err = g_ether0.p_api->open(g_ether0.p_ctrl, g_ether0.p_cfg);
	if( fsp_err == FSP_SUCCESS ) {
		fsp_err = g_ether0.p_api->callbackSet(g_ether0.p_ctrl, 
						      HAL_Net_Callback, 
						      p_dcb,
						      NULL);
		if( fsp_err == FSP_SUCCESS ) {
			p_dcb->initialized = TRUE;
			
			/* Check link status */
			g_ether0.p_api->linkProcess(g_ether0.p_ctrl);
			return E_OK;
		}
	}
	err = E_SYS;

err_1:
#if TK_SUPPORT_MEMLIB
	Kfree(p_dcb);
#endif
	return err;
}

IMPORT ER hal_net_get_link_status( UW unit )
{
	T_HAL_NET_DCB	*p_dcb = get_dcb_ptr(unit);
	if( p_dcb == NULL || p_dcb->initialized == FALSE ) {
		return E_CTX;
	}
	else {
		if( p_dcb->linkstatus!= TRUE ) {
			/* Check link status */
			g_ether0.p_api->linkProcess(g_ether0.p_ctrl);
		}
		return p_dcb->linkstatus ? E_OK : E_NOMDA;
	}
}
	

#endif		/* DEVCNF_USE_HAL_NET */
#endif		/* MTKBSP_RAFSP */
