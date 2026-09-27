/*
 * phy.h
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

#ifndef _DEV_NET_PHY_H_
#define _DEV_NET_PHY_H_

/*
 *	PHY chips definition (STM32N6570-DK) - RTL8211F
 */

#include <stdint.h>
#include <tm/tmonitor.h>		/* tm_printf prototype (link speed log) */

#define PHY_ADDRESS		(0U)		/* Default; auto-detected in PHY_Init */
#define PHY_ADDRESS_SCAN_MAX	(7U)		/* Scan addresses 0..7 */
#define RTL8211F_PHY_ID		(0x001CU)	/* Realtek OUI in PHYI1R */

/* Detected PHY address (set by PHY_Init) */
static uint32_t s_phy_addr = PHY_ADDRESS;

/* Standard IEEE 802.3 registers */
#define RTL8211F_BCR     ((uint16_t)0x0000U)
#define RTL8211F_BSR     ((uint16_t)0x0001U)
#define RTL8211F_PHYI1R  ((uint16_t)0x0002U)
#define RTL8211F_PHYI2R  ((uint16_t)0x0003U)
#define RTL8211F_ANAR    ((uint16_t)0x0004U)
#define RTL8211F_ANLPAR  ((uint16_t)0x0005U)
#define RTL8211F_ANER    ((uint16_t)0x0006U)
#define RTL8211F_ANNPTR  ((uint16_t)0x0007U)
#define RTL8211F_ANNPRR  ((uint16_t)0x0008U)
#define RTL8211F_GBCR    ((uint16_t)0x0009U)
#define RTL8211F_GBSR    ((uint16_t)0x000AU)
#define RTL8211F_MACR    ((uint16_t)0x000DU)
#define RTL8211F_MAADR   ((uint16_t)0x000EU)

/* RTL8211F specific registers (page 0) */
#define RTL8211F_PHYSR   ((uint16_t)0x001AU)
#define RTL8211F_INER    ((uint16_t)0x0012U)
#define RTL8211F_INSR    ((uint16_t)0x0013U)
#define RTL8211F_PAGSEL  ((uint16_t)0x001FU)

/* RTL8211F PHYSR (PHY Specific Status Register, page 0 reg 0x1A) bits
 * Per RTL8211F-CG datasheet and Linux phy-realtek.c:
 *   bits [5:4]: speed  00=10M, 01=100M, 10=1000M
 *   bit  [3]:   duplex 0=half, 1=full
 *   bit  [2]:   link   0=down, 1=up  */
#define RTL8211F_PHYSR_SPEED_MASK    ((uint16_t)0x0030U)  /* bits [5:4] */
#define RTL8211F_PHYSR_SPEED_10M     ((uint16_t)0x0000U)
#define RTL8211F_PHYSR_SPEED_100M    ((uint16_t)0x0010U)
#define RTL8211F_PHYSR_SPEED_1000M   ((uint16_t)0x0020U)
#define RTL8211F_PHYSR_DUPLEX_FD     ((uint16_t)0x0008U)  /* bit [3]: 1=FD */
#define RTL8211F_PHYSR_LINK          ((uint16_t)0x0004U)  /* bit [2]: 1=link up */

/* RTL8211F RGMII delay registers (page 0x0d08) */
#define RTL8211F_RGMII_PAGE      ((uint16_t)0x0d08U)
#define RTL8211F_TX_DELAY_REG    ((uint16_t)0x0011U)
#define RTL8211F_RX_DELAY_REG    ((uint16_t)0x0015U)
#define RTL8211F_TX_DELAY_EN     ((uint16_t)0x0100U)
#define RTL8211F_RX_DELAY_EN     ((uint16_t)0x0008U)


/* BSR (Basic Status Register) bits - IEEE 802.3 */
#define RTL8211F_BSR_100BASE_TX_FD    ((uint16_t)0x4000U)
#define RTL8211F_BSR_100BASE_TX_HD    ((uint16_t)0x2000U)
#define RTL8211F_BSR_10BASE_T_FD      ((uint16_t)0x1000U)
#define RTL8211F_BSR_10BASE_T_HD      ((uint16_t)0x0800U)
#define RTL8211F_BSR_100BASE_T2_FD    ((uint16_t)0x0400U)
#define RTL8211F_BSR_100BASE_T2_HD    ((uint16_t)0x0200U)
#define RTL8211F_BSR_EXTENDED_STATUS  ((uint16_t)0x0100U)
#define RTL8211F_BSR_AUTONEGO_CPLT    ((uint16_t)0x0020U)
#define RTL8211F_BSR_REMOTE_FAULT     ((uint16_t)0x0010U)
#define RTL8211F_BSR_AUTONEGO_ABILITY ((uint16_t)0x0008U)
#define RTL8211F_BSR_LINK_STATUS      ((uint16_t)0x0004U)
#define RTL8211F_BSR_JABBER_DETECT    ((uint16_t)0x0002U)
#define RTL8211F_BSR_EXTENDED_CAP     ((uint16_t)0x0001U)

/* BCR (Basic Control Register) bits - IEEE 802.3 */
#define RTL8211F_BCR_SOFT_RESET         ((uint16_t)0x8000U)
#define RTL8211F_BCR_LOOPBACK           ((uint16_t)0x4000U)
#define RTL8211F_BCR_SPEED_SELECT       ((uint16_t)0x2000U)
#define RTL8211F_BCR_AUTONEGO_EN        ((uint16_t)0x1000U)
#define RTL8211F_BCR_POWER_DOWN         ((uint16_t)0x0800U)
#define RTL8211F_BCR_ISOLATE            ((uint16_t)0x0400U)
#define RTL8211F_BCR_RESTART_AUTONEGO   ((uint16_t)0x0200U)
#define RTL8211F_BCR_DUPLEX_MODE        ((uint16_t)0x0100U)
#define RTL8211F_BCR_SPEED_SELECT_MSB   ((uint16_t)0x0040U)

/* ANLPAR (Auto-Negotiation Link Partner Ability) bits - IEEE 802.3 */
#define RTL8211F_ANLPAR_100BTX_FD    ((uint16_t)0x0100U)
#define RTL8211F_ANLPAR_100BTX_HD    ((uint16_t)0x0080U)
#define RTL8211F_ANLPAR_10BT_FD      ((uint16_t)0x0040U)
#define RTL8211F_ANLPAR_10BT_HD      ((uint16_t)0x0020U)

/* GBCR (1000BASE-T Control Register) bits - IEEE 802.3 */
#define RTL8211F_GBCR_1000BTX_FD     ((uint16_t)0x0200U)
#define RTL8211F_GBCR_1000BTX_HD     ((uint16_t)0x0100U)

/* GBSR (1000BASE-T Status Register) bits - IEEE 802.3 */
#define RTL8211F_GBSR_1000BTX_FD     ((uint16_t)0x0800U)
#define RTL8211F_GBSR_1000BTX_HD     ((uint16_t)0x0400U)

/* Linker and ETH descriptor symbols used to build MPU/cache ranges at runtime. */
extern uint8_t _sbss;
extern uint8_t _ebss;
extern ETH_DMADescTypeDef DMARxDscrTab[ETH_DMA_RX_CH_CNT][ETH_RX_DESC_CNT];
extern ETH_DMADescTypeDef DMATxDscrTab[ETH_DMA_TX_CH_CNT][ETH_TX_DESC_CNT];


BOOL PHY_GetLinkStatus(ETH_HandleTypeDef *heth)
{
	uint32_t reg, physr = 0;
	HAL_StatusTypeDef sts;
	uint32_t duplex, speed;
	ETH_MACConfigTypeDef MACConf = {0};
	static int pre_start_done = 0;

	sts = HAL_ETH_ReadPHYRegister(heth, s_phy_addr, RTL8211F_BSR, &reg);

	if( (sts != HAL_OK) || (reg == 0xFFFFU) || ((reg & RTL8211F_BSR_LINK_STATUS) == 0) ) {
		return FALSE;
	}

	/* Read PHYSR for actual negotiated speed/duplex. Ensure page 0. */
	HAL_ETH_WritePHYRegister(heth, s_phy_addr, RTL8211F_PAGSEL, 0x0000U);
	HAL_ETH_ReadPHYRegister(heth, s_phy_addr, RTL8211F_PHYSR, &physr);

	if( (physr & RTL8211F_PHYSR_SPEED_MASK) == RTL8211F_PHYSR_SPEED_1000M ) {
		speed = ETH_SPEED_1000M;
	} else if( (physr & RTL8211F_PHYSR_SPEED_MASK) == RTL8211F_PHYSR_SPEED_100M ) {
		speed = ETH_SPEED_100M;
	} else {
		speed = ETH_SPEED_10M;
	}
	duplex = ((physr & RTL8211F_PHYSR_DUPLEX_FD) != 0) ? ETH_FULLDUPLEX_MODE : ETH_HALFDUPLEX_MODE;

	if( !pre_start_done ) {
		/* ETH_SPEED_10M == ETH_SPEED_1000M == 0; PS bit distinguishes them. */
		HAL_ETH_GetMACConfig(heth, &MACConf);
		MACConf.DuplexMode      = duplex;
		MACConf.Speed           = speed;
		MACConf.PortSelect      = ((physr & RTL8211F_PHYSR_SPEED_MASK) != RTL8211F_PHYSR_SPEED_1000M)
		                          ? ENABLE : DISABLE;
		/* Have the MAC verify IPv4 header + TCP/UDP/ICMP checksums in HW so
		 * lwIP can skip the SW verification (CHECKSUM_CHECK_* = 0). Bad-CRC
		 * frames are flagged in the descriptor and dropped in our RX path. */
		MACConf.ChecksumOffload = ENABLE;
		(void)HAL_ETH_SetMACConfig(heth, &MACConf);
		heth->Instance->MACPHYCSR |= ETH_MACPHYCSR_TC;

		tm_printf((UB *)"PHY link: speed=%s duplex=%s\n",
		          ((physr & RTL8211F_PHYSR_SPEED_MASK) == RTL8211F_PHYSR_SPEED_1000M) ? "1G" :
		          ((physr & RTL8211F_PHYSR_SPEED_MASK) == RTL8211F_PHYSR_SPEED_100M)  ? "100M" : "10M",
		          (duplex == ETH_FULLDUPLEX_MODE) ? "FD" : "HD");

		pre_start_done = 1;
		return FALSE;
	}

	heth->Instance->MACPHYCSR |= ETH_MACPHYCSR_TC;

	return TRUE;
}

void PHY_Init(ETH_HandleTypeDef *heth)
{
	uint32_t reg, addr;
	uint32_t wait_cnt;

	/* ---------------------------------------------------------------
	 * STM32N6570-DK Discovery board: MDC=PD1, MDIO=PD12 (both AF11).
	 * The NUCLEO-N657X0-Q project's HAL_ETH_MspInit() configured
	 * PG11=MDC, PF4=MDIO  Ewrong for the Discovery board hardware.
	 * Re-route the MAC MDC/MDIO signals to the correct Discovery pins.
	 * --------------------------------------------------------------- */
	{
		GPIO_InitTypeDef g = {0};
		g.Mode      = GPIO_MODE_AF_PP;
		g.Pull      = GPIO_NOPULL;
		g.Alternate = GPIO_AF11_ETH1;

		/* De-configure NUCLEO MDC/MDIO pins to avoid bus contention */
		HAL_GPIO_DeInit(GPIOG, GPIO_PIN_11);   /* was MDC on NUCLEO */
		HAL_GPIO_DeInit(GPIOF, GPIO_PIN_4);    /* was MDIO on NUCLEO */

		/* Enable GPIOD clock (not enabled by HAL_ETH_MspInit) */
		__HAL_RCC_GPIOD_CLK_ENABLE();

		/* Match RIF security attributes of the other ETH pins */
		HAL_GPIO_ConfigPinAttributes(GPIOD, GPIO_PIN_1 | GPIO_PIN_12,
		                             GPIO_PIN_SEC | GPIO_PIN_NPRIV);

		/* PD1 = ETH1_MDC */
		g.Pin   = GPIO_PIN_1;
		g.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
		HAL_GPIO_Init(GPIOD, &g);

		/* PD12 = ETH1_MDIO */
		g.Pin   = GPIO_PIN_12;
		g.Speed = GPIO_SPEED_FREQ_LOW;
		HAL_GPIO_Init(GPIOD, &g);

	}

	/* RTL8211F requires >=150ms after power-on before MDIO is accessible.
	 * On Discovery the PHY is always powered; allow extra settling time. */
	tk_dly_tsk(200);

	/* Mark ETH DMA descriptor region as non-cacheable.
	 * HAL_ETH_Start_IT (called after PHY_Init) writes OWN bits to the descriptors.
	 * Without this MPU setting those writes stay in D-cache and are invisible to
	 * the ETH DMA, which reads from RAM and sees OWN=0 on every descriptor. */
	{
		uintptr_t bss_start;
		uintptr_t bss_end;
		uintptr_t desc_start;
		uintptr_t desc_end;
		uintptr_t nc_start;
		uintptr_t nc_end;
		uintptr_t tx_start;
		uintptr_t tx_end;
		uintptr_t tmp;
		uintptr_t clean_start;
		uintptr_t clean_end;
		uintptr_t clean_len;
		uintptr_t inv_start;
		uintptr_t inv_len;
		MPU_Attributes_InitTypeDef attr = {0};
		MPU_Region_InitTypeDef    rgn  = {0};

		bss_start = (uintptr_t)&_sbss;
		bss_end   = (uintptr_t)&_ebss;

		desc_start = (uintptr_t)&DMARxDscrTab[0][0];
		desc_end   = desc_start + sizeof(DMARxDscrTab);
		nc_start   = (uintptr_t)&__snoncacheable;
		nc_end     = (uintptr_t)&__enoncacheable;
		tx_start   = (uintptr_t)&DMATxDscrTab[0][0];
		tx_end     = tx_start + sizeof(DMATxDscrTab);
		if (tx_start < desc_start) {
			tmp = desc_start;
			desc_start = tx_start;
			tx_start = tmp;
		}
		if (tx_end > desc_end) {
			desc_end = tx_end;
		}

		/* Flush D-Cache for ranges that will become non-cacheable.
		 * SCB_*Cache_by_Addr expects 32-byte aligned start and size. */
		clean_start = bss_start & ~(uintptr_t)31U;
		clean_end   = (bss_end + 31U) & ~(uintptr_t)31U;
		clean_len   = (clean_end > clean_start) ? (clean_end - clean_start) : 0U;
		if (clean_len != 0U) {
			SCB_CleanDCache_by_Addr((uint32_t *)clean_start, (int32_t)clean_len);
		}
		inv_start = clean_start;
		inv_len   = clean_len;
		if (inv_len != 0U) {
			SCB_InvalidateDCache_by_Addr((void *)inv_start, (int32_t)inv_len);
		}
		clean_start = desc_start & ~(uintptr_t)31U;
		clean_end   = (desc_end + 31U) & ~(uintptr_t)31U;
		clean_len   = (clean_end > clean_start) ? (clean_end - clean_start) : 0U;
		if (clean_len != 0U) {
			SCB_CleanDCache_by_Addr((uint32_t *)clean_start, (int32_t)clean_len);
			SCB_InvalidateDCache_by_Addr((void *)clean_start, (int32_t)clean_len);
		}
		clean_start = nc_start & ~(uintptr_t)31U;
		clean_end   = (nc_end + 31U) & ~(uintptr_t)31U;
		clean_len   = (clean_end > clean_start) ? (clean_end - clean_start) : 0U;
		if (clean_len != 0U) {
			SCB_CleanDCache_by_Addr((uint32_t *)clean_start, (int32_t)clean_len);
			SCB_InvalidateDCache_by_Addr((void *)clean_start, (int32_t)clean_len);
		}

		HAL_MPU_Disable();

		/* MAIR attribute slot 0 = Normal memory, inner+outer non-cacheable */
		attr.Number     = MPU_ATTRIBUTES_NUMBER0;
		attr.Attributes = INNER_OUTER(MPU_NOT_CACHEABLE);
		HAL_MPU_ConfigMemoryAttributes(&attr);

		/* Region 5: full BSS as non-cacheable. */
		rgn.Enable           = MPU_REGION_ENABLE;
		rgn.Number           = MPU_REGION_NUMBER5;
		rgn.AttributesIndex  = MPU_ATTRIBUTES_NUMBER0;
		rgn.BaseAddress      = (uint32_t)bss_start;
		rgn.LimitAddress     = (uint32_t)(bss_end - 1U);
		rgn.AccessPermission = MPU_REGION_ALL_RW;
		rgn.DisableExec      = MPU_INSTRUCTION_ACCESS_DISABLE;
		rgn.DisablePrivExec  = MPU_PRIV_INSTRUCTION_ACCESS_DISABLE;
		rgn.IsShareable      = MPU_ACCESS_OUTER_SHAREABLE;
		HAL_MPU_ConfigRegion(&rgn);

		/* Region 6: linker-managed noncacheable section (ETH RX/TX buffers). */
		rgn.Enable           = (nc_end > nc_start) ? MPU_REGION_ENABLE : MPU_REGION_DISABLE;
		rgn.Number           = MPU_REGION_NUMBER6;
		rgn.BaseAddress      = (uint32_t)((nc_end > nc_start) ? nc_start : 0U);
		rgn.LimitAddress     = (uint32_t)((nc_end > nc_start) ? (nc_end - 1U) : 31U);
		HAL_MPU_ConfigRegion(&rgn);
		rgn.Enable           = MPU_REGION_ENABLE;

		/* Region 7: ETH DMA descriptor area as non-cacheable. */
		rgn.Number           = MPU_REGION_NUMBER7;
		rgn.BaseAddress      = (uint32_t)desc_start;
		rgn.LimitAddress     = (uint32_t)(desc_end - 1U);
		HAL_MPU_ConfigRegion(&rgn);

		HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
	}

	/* Enable pull-up on PD12 (MDIO): pin12 = PUPDR bits[25:24] = 01.
	 * Without a pull-up the MDIO line floats after the MAC releases it,
	 * causing capacitive garbage reads instead of clean 0xFFFF or PHY data. */
	GPIOD->PUPDR = (GPIOD->PUPDR & ~(0x3U << 24U)) | (0x1U << 24U);

	/* Initialize MDIO Clock. */
	HAL_ETH_SetMDIOClockRange(heth);

	/* Warm up MDIO bus: dummy reads to flush any transient state. */
	{
		uint32_t dummy;
		for (uint32_t wi = 0; wi < 8; wi++) {
			HAL_ETH_ReadPHYRegister(heth, 0, RTL8211F_PHYI1R, &dummy);
		}
	}

	/* Scan PHY addresses 0-7 for RTL8211F (PHYI1R == 0x001C).
	 * Retry up to 5 times with 200ms intervals to handle slow PHY startup. */
	s_phy_addr = PHY_ADDRESS;
	{
		uint8_t found = 0;
		uint32_t scan_try;
		for (scan_try = 0; scan_try < 5U && !found; scan_try++) {
			if (scan_try > 0U) {
				tk_dly_tsk(200);
			}
			for( addr = 0; addr <= 7U; addr++ ) {
				reg = 0xDEADU;
				HAL_StatusTypeDef scan_sts = HAL_ETH_ReadPHYRegister(heth, addr, RTL8211F_PHYI1R, &reg);
				if( scan_sts == HAL_OK && reg == RTL8211F_PHY_ID ) {
					s_phy_addr = addr;
					found = 1;
					break;
				}
			}
		}
		if( !found ) {
			/* Fallback: accept first valid-looking response (not zero, not open-bus,
			 * bit15=0, and not floating-bus garbage with all lower 12 bits set). */
			for( addr = 0; addr <= 7U; addr++ ) {
				reg = 0xDEADU;
				if( HAL_ETH_ReadPHYRegister(heth, addr, RTL8211F_PHYI1R, &reg) == HAL_OK &&
				    reg != 0x0000U && reg != 0xFFFFU &&
				    (reg & 0x8000U) == 0U && (reg & 0x0FFFU) != 0x0FFFU ) {
					s_phy_addr = addr;
					found = 1;
					break;
				}
			}
		}
	}

	/* Software reset */
	HAL_ETH_WritePHYRegister(heth, s_phy_addr, RTL8211F_BCR, RTL8211F_BCR_SOFT_RESET);
	wait_cnt = 200;
	do {
		reg = 0U;
		HAL_ETH_ReadPHYRegister(heth, s_phy_addr, RTL8211F_BCR, &reg);
		wait_cnt--;
	} while( (reg & RTL8211F_BCR_SOFT_RESET) != 0 && wait_cnt > 0 );
	tk_dly_tsk(100);	/* Wait for PHY to stabilize after soft reset */

	/* Enable RGMII internal TX/RX delays (page 0x0d08) */
	HAL_ETH_WritePHYRegister(heth, s_phy_addr, RTL8211F_PAGSEL, RTL8211F_RGMII_PAGE);
	if( HAL_ETH_ReadPHYRegister(heth, s_phy_addr, RTL8211F_TX_DELAY_REG, &reg) == HAL_OK ) {
		reg |= RTL8211F_TX_DELAY_EN;
		HAL_ETH_WritePHYRegister(heth, s_phy_addr, RTL8211F_TX_DELAY_REG, reg);
	}
	if( HAL_ETH_ReadPHYRegister(heth, s_phy_addr, RTL8211F_RX_DELAY_REG, &reg) == HAL_OK ) {
		reg |= RTL8211F_RX_DELAY_EN;
		HAL_ETH_WritePHYRegister(heth, s_phy_addr, RTL8211F_RX_DELAY_REG, reg);
	}
	HAL_ETH_WritePHYRegister(heth, s_phy_addr, RTL8211F_PAGSEL, 0x0000U);

	/* After soft reset the RTL8211F defaults ANAR=0x01E1 (100M+10M) and
	 * GBCR=0x0E00 (1000M FD+HD). We let the PHY use default advertisement
	 * (including 1000M) so the switch picks the highest mutually supported speed.
	 * Explicit GBCR=0 + AN restart was causing repeated link-down events that
	 * put managed switches into error-disabled / STP discarding state. */

	/* Wait up to 120 seconds for link to establish. */
	for( wait_cnt = 0; wait_cnt < 1200; wait_cnt++ ) {
		tk_dly_tsk(100);
		reg = 0U;
		if( HAL_ETH_ReadPHYRegister(heth, s_phy_addr, RTL8211F_BSR, &reg) == HAL_OK &&
		    reg != 0xFFFFU && (reg & RTL8211F_BSR_LINK_STATUS) != 0 ) {
			/* Set TC=1: force MAC to use MACCR (FES/DM) for RGMII speed config
			 * instead of RGMII INBAND status. INBAND shows 10M (LNKSPEED=00)
			 * causing the MAC RX clock domain to run at 2.5MHz while the PHY
			 * sends 25MHz data, dropping every incoming frame. */
			heth->Instance->MACPHYCSR |= ETH_MACPHYCSR_TC;
			return;
		}
	}
}

#endif /* _DEV_NET_PHY_H_ */
