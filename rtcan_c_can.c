/*
 * CAN bus driver for Bosch C_CAN controller, ported to Xenomai RTDM
 *
 *
 * Stephen J. Battazzo <stephen.j.battazzo@nasa.gov>, 
 * MEI Services/NASA Ames Research Center
 *
 * Borrowed original driver from:
 * 
 * Bhupesh Sharma <bhupesh.sharma@st.com>, ST Microelectronics
 * Borrowed heavily from the C_CAN driver originally written by:
 * Copyright (C) 2007
 * - Sascha Hauer, Marc Kleine-Budde, Pengutronix <s.hauer@pengutronix.de>
 * - Simon Kallweit, intefo AG <simon.kallweit@intefo.ch>
 *
 * TX and RX NAPI implementation has been removed and replaced with RT Socket CAN implementation.
 * RT Socket CAN implementation inspired by Flexcan RTDM port by Wolfgang Grandegger <wg@denx.de>
 *
 * Bosch C_CAN controller is compliant to CAN protocol version 2.0 part A and B.
 * Bosch C_CAN user manual can be obtained from:
 * http://www.semiconductors.bosch.de/media/en/pdf/ipmodules_1/c_can/
 * users_manual_c_can.pdf
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/if_arp.h>
#include <linux/if_ether.h>
#include <linux/list.h>
#include <linux/io.h>
#include <linux/pm_runtime.h>
#ifdef CONFIG_OF
#include <linux/of.h>
#include <linux/of_device.h>
#endif
#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/consumer.h>

#include <rtdm/rtdm_driver.h>

/* CAN device profile */
#include <rtdm/rtcan.h>
#include "rtcan_dev.h"
#include "rtcan_raw.h"
#include "rtcan_internal.h"

#define DEV_NAME	"rtcan%d"
#define DRV_NAME	"c_can"

//borrowed this from linux/can/dev.h for now:
#define CAN_MAX_DLC 8
#define get_can_dlc(i)          (min_t(__u8, (i), CAN_MAX_DLC))

enum reg {
	C_CAN_CTRL_REG = 0,
	C_CAN_CTRL_EX_REG,
	C_CAN_STS_REG,
	C_CAN_ERR_CNT_REG,
	C_CAN_BTR_REG,
	C_CAN_INT_REG,
	C_CAN_TEST_REG,
	C_CAN_BRPEXT_REG,
	C_CAN_IF1_COMREQ_REG,
	C_CAN_IF1_COMMSK_REG,
	C_CAN_IF1_MASK1_REG,
	C_CAN_IF1_MASK2_REG,
	C_CAN_IF1_ARB1_REG,
	C_CAN_IF1_ARB2_REG,
	C_CAN_IF1_MSGCTRL_REG,
	C_CAN_IF1_DATA1_REG,
	C_CAN_IF1_DATA2_REG,
	C_CAN_IF1_DATA3_REG,
	C_CAN_IF1_DATA4_REG,
	C_CAN_IF2_COMREQ_REG,
	C_CAN_IF2_COMMSK_REG,
	C_CAN_IF2_MASK1_REG,
	C_CAN_IF2_MASK2_REG,
	C_CAN_IF2_ARB1_REG,
	C_CAN_IF2_ARB2_REG,
	C_CAN_IF2_MSGCTRL_REG,
	C_CAN_IF2_DATA1_REG,
	C_CAN_IF2_DATA2_REG,
	C_CAN_IF2_DATA3_REG,
	C_CAN_IF2_DATA4_REG,
	C_CAN_TXRQST1_REG,
	C_CAN_TXRQST2_REG,
	C_CAN_NEWDAT1_REG,
	C_CAN_NEWDAT2_REG,
	C_CAN_INTPND1_REG,
	C_CAN_INTPND2_REG,
	C_CAN_MSGVAL1_REG,
	C_CAN_MSGVAL2_REG,
};

static u16 reg_map_c_can[] = {
	[C_CAN_CTRL_REG]	= 0x00,
	[C_CAN_STS_REG]		= 0x02,
	[C_CAN_ERR_CNT_REG]	= 0x04,
	[C_CAN_BTR_REG]		= 0x06,
	[C_CAN_INT_REG]		= 0x08,
	[C_CAN_TEST_REG]	= 0x0A,
	[C_CAN_BRPEXT_REG]	= 0x0C,
	[C_CAN_IF1_COMREQ_REG]	= 0x10,
	[C_CAN_IF1_COMMSK_REG]	= 0x12,
	[C_CAN_IF1_MASK1_REG]	= 0x14,
	[C_CAN_IF1_MASK2_REG]	= 0x16,
	[C_CAN_IF1_ARB1_REG]	= 0x18,
	[C_CAN_IF1_ARB2_REG]	= 0x1A,
	[C_CAN_IF1_MSGCTRL_REG]	= 0x1C,
	[C_CAN_IF1_DATA1_REG]	= 0x1E,
	[C_CAN_IF1_DATA2_REG]	= 0x20,
	[C_CAN_IF1_DATA3_REG]	= 0x22,
	[C_CAN_IF1_DATA4_REG]	= 0x24,
	[C_CAN_IF2_COMREQ_REG]	= 0x40,
	[C_CAN_IF2_COMMSK_REG]	= 0x42,
	[C_CAN_IF2_MASK1_REG]	= 0x44,
	[C_CAN_IF2_MASK2_REG]	= 0x46,
	[C_CAN_IF2_ARB1_REG]	= 0x48,
	[C_CAN_IF2_ARB2_REG]	= 0x4A,
	[C_CAN_IF2_MSGCTRL_REG]	= 0x4C,
	[C_CAN_IF2_DATA1_REG]	= 0x4E,
	[C_CAN_IF2_DATA2_REG]	= 0x50,
	[C_CAN_IF2_DATA3_REG]	= 0x52,
	[C_CAN_IF2_DATA4_REG]	= 0x54,
	[C_CAN_TXRQST1_REG]	= 0x80,
	[C_CAN_TXRQST2_REG]	= 0x82,
	[C_CAN_NEWDAT1_REG]	= 0x90,
	[C_CAN_NEWDAT2_REG]	= 0x92,
	[C_CAN_INTPND1_REG]	= 0xA0,
	[C_CAN_INTPND2_REG]	= 0xA2,
	[C_CAN_MSGVAL1_REG]	= 0xB0,
	[C_CAN_MSGVAL2_REG]	= 0xB2,
};

static u16 reg_map_d_can[] = {
	[C_CAN_CTRL_REG]	= 0x00,
	[C_CAN_CTRL_EX_REG]	= 0x02,
	[C_CAN_STS_REG]		= 0x04,
	[C_CAN_ERR_CNT_REG]	= 0x08,
	[C_CAN_BTR_REG]		= 0x0C,
	[C_CAN_BRPEXT_REG]	= 0x0E,
	[C_CAN_INT_REG]		= 0x10,
	[C_CAN_TEST_REG]	= 0x14,
	[C_CAN_TXRQST1_REG]	= 0x88,
	[C_CAN_TXRQST2_REG]	= 0x8A,
	[C_CAN_NEWDAT1_REG]	= 0x9C,
	[C_CAN_NEWDAT2_REG]	= 0x9E,
	[C_CAN_INTPND1_REG]	= 0xB0,
	[C_CAN_INTPND2_REG]	= 0xB2,
	[C_CAN_MSGVAL1_REG]	= 0xC4,
	[C_CAN_MSGVAL2_REG]	= 0xC6,
	[C_CAN_IF1_COMREQ_REG]	= 0x100,
	[C_CAN_IF1_COMMSK_REG]	= 0x102,
	[C_CAN_IF1_MASK1_REG]	= 0x104,
	[C_CAN_IF1_MASK2_REG]	= 0x106,
	[C_CAN_IF1_ARB1_REG]	= 0x108,
	[C_CAN_IF1_ARB2_REG]	= 0x10A,
	[C_CAN_IF1_MSGCTRL_REG]	= 0x10C,
	[C_CAN_IF1_DATA1_REG]	= 0x110,
	[C_CAN_IF1_DATA2_REG]	= 0x112,
	[C_CAN_IF1_DATA3_REG]	= 0x114,
	[C_CAN_IF1_DATA4_REG]	= 0x116,
	[C_CAN_IF2_COMREQ_REG]	= 0x120,
	[C_CAN_IF2_COMMSK_REG]	= 0x122,
	[C_CAN_IF2_MASK1_REG]	= 0x124,
	[C_CAN_IF2_MASK2_REG]	= 0x126,
	[C_CAN_IF2_ARB1_REG]	= 0x128,
	[C_CAN_IF2_ARB2_REG]	= 0x12A,
	[C_CAN_IF2_MSGCTRL_REG]	= 0x12C,
	[C_CAN_IF2_DATA1_REG]	= 0x130,
	[C_CAN_IF2_DATA2_REG]	= 0x132,
	[C_CAN_IF2_DATA3_REG]	= 0x134,
	[C_CAN_IF2_DATA4_REG]	= 0x136,
};

enum c_can_dev_id {
	BOSCH_C_CAN_PLATFORM,
	BOSCH_C_CAN,
	BOSCH_D_CAN,
};

/* c_can private data structure */
struct c_can_priv {
	struct rtcan_device *dev;

	int irq;
	
	struct device *device;
	
	struct can_bittime bit_time;
	int tx_object;
	int current_status;
	int last_status;
	u16 (*read_reg) (struct c_can_priv *priv, enum reg index);
	void (*write_reg) (struct c_can_priv *priv, enum reg index, u16 val);
	void __iomem *base;
	const u16 *regs;
	unsigned long irq_flags; /* for request_irq() */
	unsigned int tx_next;
	unsigned int tx_echo;
	void *priv;		/* for board-specific data */
	u16 irqstatus;
	enum c_can_dev_id type;
	u32 __iomem *raminit_ctrlreg;
	unsigned int instance;
	void (*raminit) (const struct c_can_priv *priv, bool enable);
};

struct rtcan_device *alloc_c_can_dev(void);

int register_c_candev(struct rtcan_device *dev);
void unregister_c_candev(struct rtcan_device *dev);
static int c_can_interrupt(rtdm_irq_t *irq_handle);

#ifdef CONFIG_PM
int c_can_power_up(struct rtcan_device *dev);
int c_can_power_down(struct rtcan_device *dev);
#endif

/* Number of interface registers */
#define IF_ENUM_REG_LEN		11
#define C_CAN_IFACE(reg, iface)	(C_CAN_IF1_##reg + (iface) * IF_ENUM_REG_LEN)

/* control extension register D_CAN specific */
#define CONTROL_EX_PDR		BIT(8)

/* control register */
#define CONTROL_TEST		BIT(7)
#define CONTROL_CCE		BIT(6)
#define CONTROL_DISABLE_AR	BIT(5)
#define CONTROL_ENABLE_AR	(0 << 5)
#define CONTROL_EIE		BIT(3)
#define CONTROL_SIE		BIT(2)
#define CONTROL_IE		BIT(1)
#define CONTROL_INIT		BIT(0)

/* test register */
#define TEST_RX			BIT(7)
#define TEST_TX1		BIT(6)
#define TEST_TX2		BIT(5)
#define TEST_LBACK		BIT(4)
#define TEST_SILENT		BIT(3)
#define TEST_BASIC		BIT(2)

/* status register */
#define STATUS_PDA		BIT(10)
#define STATUS_BOFF		BIT(7)
#define STATUS_EWARN		BIT(6)
#define STATUS_EPASS		BIT(5)
#define STATUS_RXOK		BIT(4)
#define STATUS_TXOK		BIT(3)

/* error counter register */
#define ERR_CNT_TEC_MASK	0xff
#define ERR_CNT_TEC_SHIFT	0
#define ERR_CNT_REC_SHIFT	8
#define ERR_CNT_REC_MASK	(0x7f << ERR_CNT_REC_SHIFT)
#define ERR_CNT_RP_SHIFT	15
#define ERR_CNT_RP_MASK		(0x1 << ERR_CNT_RP_SHIFT)

/* bit-timing register */
#define BTR_BRP_MASK		0x3f
#define BTR_BRP_SHIFT		0
#define BTR_SJW_SHIFT		6
#define BTR_SJW_MASK		(0x3 << BTR_SJW_SHIFT)
#define BTR_TSEG1_SHIFT		8
#define BTR_TSEG1_MASK		(0xf << BTR_TSEG1_SHIFT)
#define BTR_TSEG2_SHIFT		12
#define BTR_TSEG2_MASK		(0x7 << BTR_TSEG2_SHIFT)

/* brp extension register */
#define BRP_EXT_BRPE_MASK	0x0f
#define BRP_EXT_BRPE_SHIFT	0

/* IFx command request */
#define IF_COMR_BUSY		BIT(15)

/* IFx command mask */
#define IF_COMM_WR		BIT(7)
#define IF_COMM_MASK		BIT(6)
#define IF_COMM_ARB		BIT(5)
#define IF_COMM_CONTROL		BIT(4)
#define IF_COMM_CLR_INT_PND	BIT(3)
#define IF_COMM_TXRQST		BIT(2)
#define IF_COMM_DATAA		BIT(1)
#define IF_COMM_DATAB		BIT(0)
#define IF_COMM_ALL		(IF_COMM_MASK | IF_COMM_ARB | \
				IF_COMM_CONTROL | IF_COMM_TXRQST | \
				IF_COMM_DATAA | IF_COMM_DATAB)

/* IFx arbitration */
#define IF_ARB_MSGVAL		BIT(15)
#define IF_ARB_MSGXTD		BIT(14)
#define IF_ARB_TRANSMIT		BIT(13)

/* IFx message control */
#define IF_MCONT_NEWDAT		BIT(15)
#define IF_MCONT_MSGLST		BIT(14)
#define IF_MCONT_CLR_MSGLST	(0 << 14)
#define IF_MCONT_INTPND		BIT(13)
#define IF_MCONT_UMASK		BIT(12)
#define IF_MCONT_TXIE		BIT(11)
#define IF_MCONT_RXIE		BIT(10)
#define IF_MCONT_RMTEN		BIT(9)
#define IF_MCONT_TXRQST		BIT(8)
#define IF_MCONT_EOB		BIT(7)
#define IF_MCONT_DLC_MASK	0xf

/*
 * IFx register masks:
 * allow easy operation on 16-bit registers when the
 * argument is 32-bit instead
 */
#define IFX_WRITE_LOW_16BIT(x)	((x) & 0xFFFF)
#define IFX_WRITE_HIGH_16BIT(x)	(((x) & 0xFFFF0000) >> 16)

/* message object split */
#define C_CAN_NO_OF_OBJECTS	32
#define C_CAN_MSG_OBJ_RX_NUM	16
#define C_CAN_MSG_OBJ_TX_NUM	16

#define C_CAN_MSG_OBJ_RX_FIRST	1
#define C_CAN_MSG_OBJ_RX_LAST	(C_CAN_MSG_OBJ_RX_FIRST + \
				C_CAN_MSG_OBJ_RX_NUM - 1)

#define C_CAN_MSG_OBJ_TX_FIRST	(C_CAN_MSG_OBJ_RX_LAST + 1)
#define C_CAN_MSG_OBJ_TX_LAST	(C_CAN_MSG_OBJ_TX_FIRST + \
				C_CAN_MSG_OBJ_TX_NUM - 1)

#define C_CAN_MSG_OBJ_RX_SPLIT	9
#define C_CAN_MSG_RX_LOW_LAST	(C_CAN_MSG_OBJ_RX_SPLIT - 1)

#define C_CAN_NEXT_MSG_OBJ_MASK	(C_CAN_MSG_OBJ_TX_NUM - 1)
#define RECEIVE_OBJECT_BITS	0x0000ffff

/* status interrupt */
#define STATUS_INTERRUPT	0x8000

/* global interrupt masks */
#define ENABLE_ALL_INTERRUPTS	1
#define DISABLE_ALL_INTERRUPTS	0

/* minimum timeout for checking BUSY status */
#define MIN_TIMEOUT_VALUE	6

/* Wait for ~1 sec for INIT bit */
#define INIT_WAIT_MS		1000

/* napi related */
#define C_CAN_NAPI_WEIGHT	C_CAN_MSG_OBJ_RX_NUM

/* c_can lec values */
enum c_can_lec_type {
	LEC_NO_ERROR = 0,
	LEC_STUFF_ERROR,
	LEC_FORM_ERROR,
	LEC_ACK_ERROR,
	LEC_BIT1_ERROR,
	LEC_BIT0_ERROR,
	LEC_CRC_ERROR,
	LEC_UNUSED,
};

/*
 * c_can error types:
 * Bus errors (BUS_OFF, ERROR_WARNING, ERROR_PASSIVE) are supported
 */
enum c_can_bus_error_types {
	C_CAN_NO_ERROR = 0,
	C_CAN_BUS_OFF,
	C_CAN_ERROR_WARNING,
	C_CAN_ERROR_PASSIVE,
};

static struct can_bittiming_const c_can_bittiming_const = {
	.name = "c_can",
	.tseg1_min = 2,		/* Time segment 1 = prop_seg + phase_seg1 */
	.tseg1_max = 16,
	.tseg2_min = 1,		/* Time segment 2 = phase_seg2 */
	.tseg2_max = 8,
	.sjw_max = 4,
	.brp_min = 1,
	.brp_max = 1024,	/* 6-bit BRP field + 4-bit BRPE field*/
	.brp_inc = 1,
};

static char *c_can_ctrl_name = "DCAN";
static char *my_board_name = "BBB";


static inline void c_can_pm_runtime_enable(const struct c_can_priv *priv)
{
	if (priv->device)
		pm_runtime_enable(priv->device);
}

static inline void c_can_pm_runtime_disable(const struct c_can_priv *priv)
{
	if (priv->device)
		pm_runtime_disable(priv->device);
}

static inline void c_can_pm_runtime_get_sync(const struct c_can_priv *priv)
{
	if (priv->device)
		pm_runtime_get_sync(priv->device);
}

static inline void c_can_pm_runtime_put_sync(const struct c_can_priv *priv)
{
	if (priv->device)
		pm_runtime_put_sync(priv->device);
}

static inline void c_can_reset_ram(const struct c_can_priv *priv, bool enable)
{
	if (priv->raminit)
		priv->raminit(priv, enable);
}

static inline int get_tx_next_msg_obj(const struct c_can_priv *priv)
{
	return (priv->tx_next & C_CAN_NEXT_MSG_OBJ_MASK) +
			C_CAN_MSG_OBJ_TX_FIRST;
}

static inline int get_tx_echo_msg_obj(const struct c_can_priv *priv)
{
	return (priv->tx_echo & C_CAN_NEXT_MSG_OBJ_MASK) +
			C_CAN_MSG_OBJ_TX_FIRST;
}

static u32 c_can_read_reg32(struct c_can_priv *priv, enum reg index)
{
	u32 val = priv->read_reg(priv, index);
	val |= ((u32) priv->read_reg(priv, index + 1)) << 16;
	return val;
}

static void c_can_enable_all_interrupts(struct c_can_priv *priv,
						int enable)
{
	unsigned int cntrl_save = priv->read_reg(priv,
						C_CAN_CTRL_REG);

	if (enable)
		cntrl_save |= (CONTROL_SIE | CONTROL_EIE | CONTROL_IE);
	else
		cntrl_save &= ~(CONTROL_EIE | CONTROL_IE | CONTROL_SIE);

	priv->write_reg(priv, C_CAN_CTRL_REG, cntrl_save);
}

static inline int c_can_msg_obj_is_busy(struct c_can_priv *priv, int iface)
{
	int count = MIN_TIMEOUT_VALUE;

	while (count && priv->read_reg(priv,
				C_CAN_IFACE(COMREQ_REG, iface)) &
				IF_COMR_BUSY) {
		count--;
		udelay(1);
	}

	if (!count)
		return 1;

	return 0;
}

static inline void c_can_object_get(struct rtcan_device *dev,
					int iface, int objno, int mask)
{
	struct c_can_priv *priv = rtcan_priv(dev);

	/*
	 * As per specs, after writting the message object number in the
	 * IF command request register the transfer b/w interface
	 * register and message RAM must be complete in 6 CAN-CLK
	 * period.
	 */
	priv->write_reg(priv, C_CAN_IFACE(COMMSK_REG, iface),
			IFX_WRITE_LOW_16BIT(mask));
	priv->write_reg(priv, C_CAN_IFACE(COMREQ_REG, iface),
			IFX_WRITE_LOW_16BIT(objno));

	if (c_can_msg_obj_is_busy(priv, iface))
		rtcandev_err(dev, "timed out in object get\n");
}

static inline void c_can_object_put(struct rtcan_device *dev,
					int iface, int objno, int mask)
{
	struct c_can_priv *priv = rtcan_priv(dev);

	/*
	 * As per specs, after writting the message object number in the
	 * IF command request register the transfer b/w interface
	 * register and message RAM must be complete in 6 CAN-CLK
	 * period.
	 */
	priv->write_reg(priv, C_CAN_IFACE(COMMSK_REG, iface),
			(IF_COMM_WR | IFX_WRITE_LOW_16BIT(mask)));
	priv->write_reg(priv, C_CAN_IFACE(COMREQ_REG, iface),
			IFX_WRITE_LOW_16BIT(objno));

	if (c_can_msg_obj_is_busy(priv, iface))
		rtcandev_err(dev, "timed out in object put\n");
}

static void c_can_write_msg_object(struct rtcan_device *dev,
			int iface, struct can_frame *frame, int objno)
{
	int i;
	u16 flags = 0;
	unsigned int id;
	struct c_can_priv *priv = rtcan_priv(dev);

	if (!(frame->can_id & CAN_RTR_FLAG))
		flags |= IF_ARB_TRANSMIT;

	if (frame->can_id & CAN_EFF_FLAG) {
		id = frame->can_id & CAN_EFF_MASK;
		flags |= IF_ARB_MSGXTD;
	} else
		id = ((frame->can_id & CAN_SFF_MASK) << 18);

	flags |= IF_ARB_MSGVAL;

	priv->write_reg(priv, C_CAN_IFACE(ARB1_REG, iface),
				IFX_WRITE_LOW_16BIT(id));
	priv->write_reg(priv, C_CAN_IFACE(ARB2_REG, iface), flags |
				IFX_WRITE_HIGH_16BIT(id));

	for (i = 0; i < frame->can_dlc; i += 2) {
		priv->write_reg(priv, C_CAN_IFACE(DATA1_REG, iface) + i / 2,
				frame->data[i] | (frame->data[i + 1] << 8));
	}

	/* enable interrupt for this message object */
	priv->write_reg(priv, C_CAN_IFACE(MSGCTRL_REG, iface),
			IF_MCONT_TXIE | IF_MCONT_TXRQST | IF_MCONT_EOB |
			frame->can_dlc);
	c_can_object_put(dev, iface, objno, IF_COMM_ALL);
}

static inline void c_can_mark_rx_msg_obj(struct rtcan_device *dev,
						int iface, int ctrl_mask,
						int obj)
{
	struct c_can_priv *priv = rtcan_priv(dev);

	priv->write_reg(priv, C_CAN_IFACE(MSGCTRL_REG, iface),
			ctrl_mask & ~(IF_MCONT_MSGLST | IF_MCONT_INTPND));
	c_can_object_put(dev, iface, obj, IF_COMM_CONTROL);

}

static inline void c_can_activate_all_lower_rx_msg_obj(struct rtcan_device *dev,
						int iface,
						int ctrl_mask)
{
	int i;
	struct c_can_priv *priv = rtcan_priv(dev);

	for (i = C_CAN_MSG_OBJ_RX_FIRST; i <= C_CAN_MSG_RX_LOW_LAST; i++) {
		priv->write_reg(priv, C_CAN_IFACE(MSGCTRL_REG, iface),
				ctrl_mask & ~(IF_MCONT_MSGLST |
					IF_MCONT_INTPND | IF_MCONT_NEWDAT));
		c_can_object_put(dev, iface, i, IF_COMM_CONTROL);
	}
}

static inline void c_can_activate_rx_msg_obj(struct rtcan_device *dev,
						int iface, int ctrl_mask,
						int obj)
{
	struct c_can_priv *priv = rtcan_priv(dev);

	priv->write_reg(priv, C_CAN_IFACE(MSGCTRL_REG, iface),
			ctrl_mask & ~(IF_MCONT_MSGLST |
				IF_MCONT_INTPND | IF_MCONT_NEWDAT));
	c_can_object_put(dev, iface, obj, IF_COMM_CONTROL);
}

static void c_can_handle_lost_msg_obj(struct rtcan_device *dev,
					int iface, int objno)
{
	struct c_can_priv *priv = rtcan_priv(dev);

	struct rtcan_skb skb;
	struct rtcan_rb_frame *cf = &skb.rb_frame;

	rtcandev_err(dev, "msg lost in buffer %d\n", objno);

	c_can_object_get(dev, iface, objno, IF_COMM_ALL & ~IF_COMM_TXRQST);

	priv->write_reg(priv, C_CAN_IFACE(MSGCTRL_REG, iface),
			IF_MCONT_CLR_MSGLST);

	c_can_object_put(dev, 0, objno, IF_COMM_CONTROL);

	cf->can_id |= CAN_ERR_CRTL;
	cf->data[1] = CAN_ERR_CRTL_RX_OVERFLOW;

	rtcan_rcv(dev, &skb);
}

static int c_can_read_msg_object(struct rtcan_device *dev, int iface, int ctrl, struct rtcan_skb *skb)
{
	u16 flags, data;
	int i;
	unsigned int val;
	struct c_can_priv *priv = rtcan_priv(dev);
	struct rtcan_rb_frame *frame = &skb->rb_frame;

	frame->can_dlc = get_can_dlc(ctrl & 0x0F);
	skb->rb_frame_size = EMPTY_RB_FRAME_SIZE + CAN_ERR_DLC;

	flags =	priv->read_reg(priv, C_CAN_IFACE(ARB2_REG, iface));
	val = priv->read_reg(priv, C_CAN_IFACE(ARB1_REG, iface)) |
		(flags << 16);

	if (flags & IF_ARB_MSGXTD)
		frame->can_id = (val & CAN_EFF_MASK) | CAN_EFF_FLAG;
	else
		frame->can_id = (val >> 18) & CAN_SFF_MASK;

	if (flags & IF_ARB_TRANSMIT)
		frame->can_id |= CAN_RTR_FLAG;
	else {
		for (i = 0; i < frame->can_dlc; i += 2) {
			data = priv->read_reg(priv,
				C_CAN_IFACE(DATA1_REG, iface) + i / 2);
			frame->data[i] = data;
			frame->data[i + 1] = data >> 8;
		}
	}

	return 0;
}

static void c_can_setup_receive_object(struct rtcan_device *dev, int iface,
					int objno, unsigned int mask,
					unsigned int id, unsigned int mcont)
{
	struct c_can_priv *priv = rtcan_priv(dev);

	priv->write_reg(priv, C_CAN_IFACE(MASK1_REG, iface),
			IFX_WRITE_LOW_16BIT(mask));

	/* According to C_CAN documentation, the reserved bit
	 * in IFx_MASK2 register is fixed 1
	 */
	priv->write_reg(priv, C_CAN_IFACE(MASK2_REG, iface),
			IFX_WRITE_HIGH_16BIT(mask) | BIT(13));

	priv->write_reg(priv, C_CAN_IFACE(ARB1_REG, iface),
			IFX_WRITE_LOW_16BIT(id));
	priv->write_reg(priv, C_CAN_IFACE(ARB2_REG, iface),
			(IF_ARB_MSGVAL | IFX_WRITE_HIGH_16BIT(id)));

	priv->write_reg(priv, C_CAN_IFACE(MSGCTRL_REG, iface), mcont);
	c_can_object_put(dev, iface, objno, IF_COMM_ALL & ~IF_COMM_TXRQST);

	//rtcandev_dbg(dev, "setup obj no:%d, msgval:0x%08x\n", objno,
	//		c_can_read_reg32(priv, C_CAN_MSGVAL1_REG));
}

static void c_can_inval_msg_object(struct rtcan_device *dev, int iface, int objno)
{
	struct c_can_priv *priv = rtcan_priv(dev);

	priv->write_reg(priv, C_CAN_IFACE(ARB1_REG, iface), 0);
	priv->write_reg(priv, C_CAN_IFACE(ARB2_REG, iface), 0);
	priv->write_reg(priv, C_CAN_IFACE(MSGCTRL_REG, iface), 0);

	c_can_object_put(dev, iface, objno, IF_COMM_ARB | IF_COMM_CONTROL);

	//rtcandev_dbg(dev, "invalidate obj no:%d, msgval:0x%08x\n", objno,
	//		c_can_read_reg32(priv, C_CAN_MSGVAL1_REG));
}

static inline int c_can_is_next_tx_obj_busy(struct c_can_priv *priv, int objno)
{
	int val = c_can_read_reg32(priv, C_CAN_TXRQST1_REG);

	/*
	 * as transmission request register's bit n-1 corresponds to
	 * message object n, we need to handle the same properly.
	 */
	if (val & (1 << (objno - 1)))
		return 1;

	return 0;
}

static int c_can_start_xmit(struct rtcan_device *dev, struct can_frame *cf)
{
	u32 msg_obj_no;
	struct c_can_priv *priv = rtcan_priv(dev);
	
	msg_obj_no = get_tx_next_msg_obj(priv);

	/* prepare message object for transmission */
	c_can_write_msg_object(dev, 0, cf, msg_obj_no);

	/*
	 * we have to stop the queue in case of a wrap around or
	 * if the next TX message object is still in use
	 */
	priv->tx_next++;

	return 0;
}

static int c_can_set_bittiming(struct rtcan_device *dev)
{
	unsigned int reg_btr, reg_brpe, ctrl_save;
	u8 brp, brpe, sjw, tseg1, tseg2;
	u32 ten_bit_brp;
	struct c_can_priv *priv = rtcan_priv(dev);
	struct can_bittime *bt = &priv->bit_time;
	
	/* c_can provides a 6-bit brp and 4-bit brpe fields */
	ten_bit_brp = bt->std.brp - 1;
	brp = ten_bit_brp & BTR_BRP_MASK;
	brpe = ten_bit_brp >> 6;

	sjw = bt->std.sjw - 1;
	tseg1 = bt->std.prop_seg + bt->std.phase_seg1 - 1;
	tseg2 = bt->std.phase_seg2 - 1;
	reg_btr = brp | (sjw << BTR_SJW_SHIFT) | (tseg1 << BTR_TSEG1_SHIFT) |
			(tseg2 << BTR_TSEG2_SHIFT);
	reg_brpe = brpe & BRP_EXT_BRPE_MASK;

	rtcandev_info(dev,"setting BTR=%04x BRPE=%04x\n", reg_btr, reg_brpe);

	ctrl_save = priv->read_reg(priv, C_CAN_CTRL_REG);
	priv->write_reg(priv, C_CAN_CTRL_REG,
			ctrl_save | CONTROL_CCE | CONTROL_INIT);
	priv->write_reg(priv, C_CAN_BTR_REG, reg_btr);
	priv->write_reg(priv, C_CAN_BRPEXT_REG, reg_brpe);
	priv->write_reg(priv, C_CAN_CTRL_REG, ctrl_save);

	return 0;
}

/*
 * Configure C_CAN message objects for Tx and Rx purposes:
 * C_CAN provides a total of 32 message objects that can be configured
 * either for Tx or Rx purposes. Here the first 16 message objects are used as
 * a reception FIFO. The end of reception FIFO is signified by the EoB bit
 * being SET. The remaining 16 message objects are kept aside for Tx purposes.
 * See user guide document for further details on configuring message
 * objects.
 */
static void c_can_configure_msg_objects(struct rtcan_device *dev)
{
	int i;

	/* first invalidate all message objects */
	for (i = C_CAN_MSG_OBJ_RX_FIRST; i <= C_CAN_NO_OF_OBJECTS; i++)
		c_can_inval_msg_object(dev, 0, i);

	/* setup receive message objects */
	for (i = C_CAN_MSG_OBJ_RX_FIRST; i < C_CAN_MSG_OBJ_RX_LAST; i++)
		c_can_setup_receive_object(dev, 0, i, 0, 0,
			(IF_MCONT_RXIE | IF_MCONT_UMASK) & ~IF_MCONT_EOB);

	c_can_setup_receive_object(dev, 0, C_CAN_MSG_OBJ_RX_LAST, 0, 0,
			IF_MCONT_EOB | IF_MCONT_RXIE | IF_MCONT_UMASK);
}

/*
 * Configure C_CAN chip:
 * - enable/disable auto-retransmission
 * - set operating mode
 * - configure message objects
 */
static void c_can_chip_config(struct rtcan_device *dev)
{
	struct c_can_priv *priv = rtcan_priv(dev);

	/* enable automatic retransmission */
	priv->write_reg(priv, C_CAN_CTRL_REG,
			CONTROL_ENABLE_AR);

	if ((dev->ctrl_mode & CAN_CTRLMODE_LISTENONLY) &&
	    (dev->ctrl_mode & CAN_CTRLMODE_LOOPBACK)) {
		/* loopback + silent mode : useful for hot self-test */
		priv->write_reg(priv, C_CAN_CTRL_REG, CONTROL_EIE |
				CONTROL_SIE | CONTROL_IE | CONTROL_TEST);
		priv->write_reg(priv, C_CAN_TEST_REG,
				TEST_LBACK | TEST_SILENT);
	} else if (dev->ctrl_mode & CAN_CTRLMODE_LOOPBACK) {
		/* loopback mode : useful for self-test function */
		priv->write_reg(priv, C_CAN_CTRL_REG, CONTROL_EIE |
				CONTROL_SIE | CONTROL_IE | CONTROL_TEST);
		priv->write_reg(priv, C_CAN_TEST_REG, TEST_LBACK);
	} else if (dev->ctrl_mode & CAN_CTRLMODE_LISTENONLY) {
		/* silent mode : bus-monitoring mode */
		priv->write_reg(priv, C_CAN_CTRL_REG, CONTROL_EIE |
				CONTROL_SIE | CONTROL_IE | CONTROL_TEST);
		priv->write_reg(priv, C_CAN_TEST_REG, TEST_SILENT);
	} else
		/* normal mode*/
		priv->write_reg(priv, C_CAN_CTRL_REG,
				CONTROL_EIE | CONTROL_SIE | CONTROL_IE);

	/* configure message objects */
	c_can_configure_msg_objects(dev);

	/* set a `lec` value so that we can check for updates later */
	priv->write_reg(priv, C_CAN_STS_REG, LEC_UNUSED);

	/* set bittiming params */
	c_can_set_bittiming(dev);
}

static int c_can_save_bit_time(struct rtcan_device *dev,
				 struct can_bittime *bt,
				 rtdm_lockctx_t *lock_ctx)
{
	struct c_can_priv *priv = rtcan_priv(dev);

	memcpy(&priv->bit_time, bt, sizeof(*bt));

	return 0;
}

static int c_can_mode_start(struct rtcan_device *dev, rtdm_lockctx_t *lock_ctx)
{
	struct c_can_priv *priv = rtcan_priv(dev);
	int err;
	
	switch (dev->state) {
	
	case CAN_STATE_ACTIVE:
	case CAN_STATE_BUS_WARNING:
	case CAN_STATE_BUS_PASSIVE:
		//rtcandev_info(dev, "Mode start: state active, bus warning, or passive\n");
		break;

	case CAN_STATE_STOPPED:
		/* Register IRQ handler and pass device structure as arg */
		err = rtdm_irq_request(&dev->irq_handle, priv->irq,
				       c_can_interrupt, 0, DRV_NAME,
				       (void *)dev);
		if (err) {
			rtcandev_err(dev, "couldn't request irq %d\n",priv->irq);
			goto out;
		}
		
		c_can_pm_runtime_get_sync(priv);
		c_can_reset_ram(priv, true);
		
		/* start chip and queuing */
		c_can_chip_config(dev);
		dev->state = CAN_STATE_ERROR_ACTIVE;

		/* reset tx helper pointers */
		priv->tx_next = priv->tx_echo = 0;

		/* enable status change, error and module interrupts */
		c_can_enable_all_interrupts(priv, ENABLE_ALL_INTERRUPTS);
		
		/* Set up sender "mutex" */
		rtdm_sem_init(&dev->tx_sem, C_CAN_MSG_OBJ_TX_NUM);

		break;

	case CAN_STATE_BUS_OFF:
		/* Set up sender "mutex" */
		rtdm_sem_init(&dev->tx_sem, C_CAN_MSG_OBJ_TX_NUM);
		/* start chip and queuing */
		c_can_pm_runtime_get_sync(priv);
		c_can_reset_ram(priv, true);
		c_can_chip_config(dev);
		dev->state = CAN_STATE_ERROR_ACTIVE;
		/* reset tx helper pointers */
		priv->tx_next = priv->tx_echo = 0;
		/* enable status change, error and module interrupts */
		c_can_enable_all_interrupts(priv, ENABLE_ALL_INTERRUPTS);
		break;

	case CAN_STATE_SLEEPING:
		//rtcandev_info(dev, "Mode start: state sleeping\n");
	default:
		/* Never reached, but we don't want nasty compiler warnings ... */
		break;
	}

	return 0;
out:
	c_can_pm_runtime_put_sync(priv);
	return err;
}

static void c_can_mode_stop(struct rtcan_device *dev, rtdm_lockctx_t *lock_ctx)
{
	struct c_can_priv *priv = rtcan_priv(dev);
	can_state_t state;

	
	
	state = dev->state;
	/* If controller is not operating anyway, go out */
	if (!CAN_STATE_OPERATING(state))
		return;
	
	//rtcandev_info(dev, "Mode stop.\n");
	/* disable all interrupts */
	c_can_enable_all_interrupts(priv, DISABLE_ALL_INTERRUPTS);

	/* set the state as STOPPED */
	dev->state = CAN_STATE_STOPPED;
	
	/* Wake up waiting senders */
	rtdm_sem_destroy(&dev->tx_sem);

	rtdm_irq_free(&dev->irq_handle);
	c_can_pm_runtime_put_sync(priv);
}


int c_can_set_mode(struct rtcan_device *dev, can_mode_t mode, rtdm_lockctx_t *lock_ctx)
{
	int err = 0;
	//rtcandev_info(dev, "Set mode.\n");

	switch (mode) {

	case CAN_MODE_STOP:
		//rtcandev_info(dev, "Set mode: stop\n");
		c_can_mode_stop(dev, lock_ctx);
		break;

	case CAN_MODE_START:
		//rtcandev_info(dev, "Set mode: start\n");
		err = c_can_mode_start(dev, lock_ctx);
		break;

	case CAN_MODE_SLEEP:
	default:
		err = -EOPNOTSUPP;
		break;
	}

	return err;
}

/*
 * theory of operation:
 *
 * priv->tx_echo holds the number of the oldest can_frame put for
 * transmission into the hardware, but not yet ACKed by the CAN tx
 * complete IRQ.
 *
 * We iterate from priv->tx_echo to priv->tx_next and check if the
 * packet has been transmitted, echo it back to the CAN framework.
 * If we discover a not yet transmitted packet, stop looking for more.
 */
static void c_can_do_tx(struct rtcan_device *dev)
{
	u32 val;
	u32 msg_obj_no;
	struct c_can_priv *priv = rtcan_priv(dev);

	for (/* nix */; (priv->tx_next - priv->tx_echo) > 0; priv->tx_echo++) {
		msg_obj_no = get_tx_echo_msg_obj(priv);
		val = c_can_read_reg32(priv, C_CAN_TXRQST1_REG);
		if (!(val & (1 << (msg_obj_no - 1)))) {
			//can_get_echo_skb(dev, msg_obj_no - C_CAN_MSG_OBJ_TX_FIRST);
			c_can_inval_msg_object(dev, 0, msg_obj_no);
		} else {
			rtdm_sem_up(&dev->tx_sem);
			break;
		}
	}

	/* restart queue if wrap-up or if queue stalled on last pkt */
	if (((priv->tx_next & C_CAN_NEXT_MSG_OBJ_MASK) != 0) ||
			((priv->tx_echo & C_CAN_NEXT_MSG_OBJ_MASK) == 0)){
		rtdm_sem_up(&dev->tx_sem);
	}
}

/*
 * theory of operation:
 *
 * c_can core saves a received CAN message into the first free message
 * object it finds free (starting with the lowest). Bits NEWDAT and
 * INTPND are set for this message object indicating that a new message
 * has arrived. To work-around this issue, we keep two groups of message
 * objects whose partitioning is defined by C_CAN_MSG_OBJ_RX_SPLIT.
 *
 * To ensure in-order frame reception we use the following
 * approach while re-activating a message object to receive further
 * frames:
 * - if the current message object number is lower than
 *   C_CAN_MSG_RX_LOW_LAST, do not clear the NEWDAT bit while clearing
 *   the INTPND bit.
 * - if the current message object number is equal to
 *   C_CAN_MSG_RX_LOW_LAST then clear the NEWDAT bit of all lower
 *   receive message objects.
 * - if the current message object number is greater than
 *   C_CAN_MSG_RX_LOW_LAST then clear the NEWDAT bit of
 *   only this message object.
 */
static int c_can_do_rx_poll(struct rtcan_device *dev)
{
	u32 num_rx_pkts = 0;
	unsigned int msg_obj, msg_ctrl_save;
	struct c_can_priv *priv = rtcan_priv(dev);
	u32 val = c_can_read_reg32(priv, C_CAN_INTPND1_REG);
	
	struct rtcan_skb skb;

	for (msg_obj = C_CAN_MSG_OBJ_RX_FIRST;
			msg_obj <= C_CAN_MSG_OBJ_RX_LAST;
			val = c_can_read_reg32(priv, C_CAN_INTPND1_REG),
			msg_obj++) {
		/*
		 * as interrupt pending register's bit n-1 corresponds to
		 * message object n, we need to handle the same properly.
		 */
		if (val & (1 << (msg_obj - 1))) {
			c_can_object_get(dev, 0, msg_obj, IF_COMM_ALL &
					~IF_COMM_TXRQST);
			msg_ctrl_save = priv->read_reg(priv,
					C_CAN_IFACE(MSGCTRL_REG, 0));

			if (msg_ctrl_save & IF_MCONT_EOB)
				return num_rx_pkts;

			if (msg_ctrl_save & IF_MCONT_MSGLST) {
				c_can_handle_lost_msg_obj(dev, 0, msg_obj);
				num_rx_pkts++;
				continue;
			}

			if (!(msg_ctrl_save & IF_MCONT_NEWDAT))
				continue;

			/* read the data from the message object */
			c_can_read_msg_object(dev, 0, msg_ctrl_save, &skb);

			if (msg_obj < C_CAN_MSG_RX_LOW_LAST)
				c_can_mark_rx_msg_obj(dev, 0,
						msg_ctrl_save, msg_obj);
			else if (msg_obj > C_CAN_MSG_RX_LOW_LAST)
				/* activate this msg obj */
				c_can_activate_rx_msg_obj(dev, 0,
						msg_ctrl_save, msg_obj);
			else if (msg_obj == C_CAN_MSG_RX_LOW_LAST)
				/* activate all lower message objects */
				c_can_activate_all_lower_rx_msg_obj(dev,
						0, msg_ctrl_save);
			
			rtcan_rcv(dev, &skb);
			num_rx_pkts++;
		}
	}

	return num_rx_pkts;
}

static int c_can_handle_state_change(struct rtcan_device *dev,
				enum c_can_bus_error_types error_type)
{
	unsigned int reg_err_counter;
	u8 txerr;
	u8 rxerr;
	unsigned int rx_err_passive;
	struct c_can_priv *priv = rtcan_priv(dev);
	struct rtcan_skb skb;
	struct rtcan_rb_frame *cf = &skb.rb_frame;
	
	/* propagate the error condition to the CAN stack */
	reg_err_counter = priv->read_reg(priv, C_CAN_ERR_CNT_REG);
	rxerr = (reg_err_counter & ERR_CNT_REC_MASK) >> ERR_CNT_REC_SHIFT;
	txerr = reg_err_counter & ERR_CNT_TEC_MASK;
	rx_err_passive = (reg_err_counter & ERR_CNT_RP_MASK) >>
				ERR_CNT_RP_SHIFT;

	switch (error_type) {
	case C_CAN_ERROR_WARNING:
		/* error warning state */
		skb.rb_frame_size = EMPTY_RB_FRAME_SIZE + CAN_ERR_DLC;
		dev->state = CAN_STATE_ERROR_WARNING;
		cf->can_id |= CAN_ERR_CRTL;
		cf->data[1] = (txerr > rxerr) ?
			CAN_ERR_CRTL_TX_WARNING :
			CAN_ERR_CRTL_RX_WARNING;
		cf->data[6] = txerr;
		cf->data[7] = rxerr;

		break;
	case C_CAN_ERROR_PASSIVE:
		/* error passive state */
		skb.rb_frame_size = EMPTY_RB_FRAME_SIZE + CAN_ERR_DLC;
		dev->state = CAN_STATE_ERROR_PASSIVE;
		cf->can_id |= CAN_ERR_CRTL;
		if (rx_err_passive)
			cf->data[1] |= CAN_ERR_CRTL_RX_PASSIVE;
		if (txerr > 127)
			cf->data[1] |= CAN_ERR_CRTL_TX_PASSIVE;

		cf->data[6] = txerr;
		cf->data[7] = rxerr;
		break;
	case C_CAN_BUS_OFF:
		/* bus-off state */
		skb.rb_frame_size = EMPTY_RB_FRAME_SIZE + CAN_ERR_DLC;
		dev->state = CAN_STATE_BUS_OFF;
		cf->can_id |= CAN_ERR_BUSOFF;
		/*
		 * disable all interrupts in bus-off mode to ensure that
		 * the CPU is not hogged down
		 */
		c_can_enable_all_interrupts(priv, DISABLE_ALL_INTERRUPTS);
		/* Wake up waiting senders */
		rtdm_sem_destroy(&dev->tx_sem);
		break;
	default:
		break;
	}

	rtcan_rcv(dev, &skb);

	return 1;
}

static int c_can_handle_bus_err(struct rtcan_device *dev,
				enum c_can_lec_type lec_type)
{
	struct c_can_priv *priv = rtcan_priv(dev);
	struct rtcan_skb skb;
	struct rtcan_rb_frame *cf = &skb.rb_frame;
	skb.rb_frame_size = EMPTY_RB_FRAME_SIZE + CAN_ERR_DLC;
	/*
	 * early exit if no lec update or no error.
	 * no lec update means that no CAN bus event has been detected
	 * since CPU wrote 0x7 value to status reg.
	 */
	if (lec_type == LEC_UNUSED || lec_type == LEC_NO_ERROR)
		return 0;

	/* propagate the error condition to the CAN stack */
	//skb = alloc_can_err_skb(dev, &cf);
	//if (unlikely(!skb))
	//	return 0;

	/*
	 * check for 'last error code' which tells us the
	 * type of the last error to occur on the CAN bus
	 */

	/* common for all type of bus errors */

	cf->can_id |= CAN_ERR_PROT | CAN_ERR_BUSERROR;
	cf->data[2] |= CAN_ERR_PROT_UNSPEC;

	switch (lec_type) {
	case LEC_STUFF_ERROR:
		rtcandev_dbg(dev, "stuff error\n");
		cf->data[2] |= CAN_ERR_PROT_STUFF;
		break;
	case LEC_FORM_ERROR:
		rtcandev_dbg(dev, "form error\n");
		cf->data[2] |= CAN_ERR_PROT_FORM;
		break;
	case LEC_ACK_ERROR:
		rtcandev_dbg(dev, "ack error\n");
		cf->data[3] |= (CAN_ERR_PROT_LOC_ACK |
				CAN_ERR_PROT_LOC_ACK_DEL);
		break;
	case LEC_BIT1_ERROR:
		rtcandev_dbg(dev, "bit1 error\n");
		cf->data[2] |= CAN_ERR_PROT_BIT1;
		break;
	case LEC_BIT0_ERROR:
		rtcandev_dbg(dev, "bit0 error\n");
		cf->data[2] |= CAN_ERR_PROT_BIT0;
		break;
	case LEC_CRC_ERROR:
		rtcandev_dbg(dev, "CRC error\n");
		cf->data[3] |= (CAN_ERR_PROT_LOC_CRC_SEQ |
				CAN_ERR_PROT_LOC_CRC_DEL);
		break;
	default:
		break;
	}

	/* set a `lec` value so that we can check for updates later */
	priv->write_reg(priv, C_CAN_STS_REG, LEC_UNUSED);

	rtcan_rcv(dev, &skb);

	return 1;
}

static int c_can_interrupt(rtdm_irq_t *irq_handle)
{
	struct rtcan_device *dev = rtdm_irq_get_arg(irq_handle, void);
	struct c_can_priv *priv = rtcan_priv(dev);
	//u16 irqstatus;
	int lec_type = 0;
	int recv_lock_free = 1;
	int ret = RTDM_IRQ_NONE;
	
	priv->irqstatus = priv->read_reg(priv, C_CAN_INT_REG);
	if (!priv->irqstatus)
		return RTDM_IRQ_NONE;
	
	c_can_enable_all_interrupts(priv, DISABLE_ALL_INTERRUPTS);
	
	rtdm_lock_get(&dev->device_lock);
	
	/* status events have the highest priority */
	if (priv->irqstatus == STATUS_INTERRUPT) {
		priv->current_status = priv->read_reg(priv,
					C_CAN_STS_REG);

		/* handle Tx/Rx events */
		if (priv->current_status & STATUS_TXOK){
			priv->write_reg(priv, C_CAN_STS_REG,
					priv->current_status & ~STATUS_TXOK);
			//rtcandev_info(dev, "IRQ: TX OK.\r\n");
		}

		if (priv->current_status & STATUS_RXOK){
			priv->write_reg(priv, C_CAN_STS_REG,
					priv->current_status & ~STATUS_RXOK);
			//rtcandev_info(dev, "IRQ: RX OK.\r\n");
		}

		/* handle state changes */
		if ((priv->current_status & STATUS_EWARN) &&
				(!(priv->last_status & STATUS_EWARN))) {
			rtcandev_dbg(dev, "entered error warning state\n");
			c_can_handle_state_change(dev,C_CAN_ERROR_WARNING);
			if (recv_lock_free) {
				recv_lock_free = 0;
				rtdm_lock_get(&rtcan_recv_list_lock);
				rtdm_lock_get(&rtcan_socket_lock);
			}
			ret = RTDM_IRQ_HANDLED;
		}
		if ((priv->current_status & STATUS_EPASS) &&
				(!(priv->last_status & STATUS_EPASS))) {
			rtcandev_dbg(dev, "entered error passive state\n");
			c_can_handle_state_change(dev, C_CAN_ERROR_PASSIVE);
			if (recv_lock_free) {
				recv_lock_free = 0;
				rtdm_lock_get(&rtcan_recv_list_lock);
				rtdm_lock_get(&rtcan_socket_lock);
			}
			ret = RTDM_IRQ_HANDLED;
		}
		if ((priv->current_status & STATUS_BOFF) &&
				(!(priv->last_status & STATUS_BOFF))) {
			rtcandev_dbg(dev, "entered bus off state\n");
			c_can_handle_state_change(dev,C_CAN_BUS_OFF);
			if (recv_lock_free) {
				recv_lock_free = 0;
				rtdm_lock_get(&rtcan_recv_list_lock);
				rtdm_lock_get(&rtcan_socket_lock);
			}
			ret = RTDM_IRQ_HANDLED;
		}

		/* handle bus recovery events */
		if ((!(priv->current_status & STATUS_BOFF)) &&
				(priv->last_status & STATUS_BOFF)) {
			rtcandev_dbg(dev, "left bus off state\n");
			dev->state = CAN_STATE_ERROR_ACTIVE;
			ret = RTDM_IRQ_HANDLED;
		}
		if ((!(priv->current_status & STATUS_EPASS)) &&
				(priv->last_status & STATUS_EPASS)) {
			rtcandev_dbg(dev, "left error passive state\n");
			dev->state = CAN_STATE_ERROR_ACTIVE;
			ret = RTDM_IRQ_HANDLED;
		}

		priv->last_status = priv->current_status;

		/* handle lec errors on the bus */
		lec_type = (priv->current_status & LEC_UNUSED);
		if (lec_type)
			c_can_handle_bus_err(dev, lec_type);
			
			if (recv_lock_free) {
				recv_lock_free = 0;
				rtdm_lock_get(&rtcan_recv_list_lock);
				rtdm_lock_get(&rtcan_socket_lock);
			}
			ret = RTDM_IRQ_HANDLED;
			
	} 
	else if ((priv->irqstatus >= C_CAN_MSG_OBJ_RX_FIRST) &&
			(priv->irqstatus <= C_CAN_MSG_OBJ_RX_LAST)) {
		/* handle events corresponding to receive message objects */
		c_can_do_rx_poll(dev);
		
		if (recv_lock_free) {
			recv_lock_free = 0;
			rtdm_lock_get(&rtcan_recv_list_lock);
			rtdm_lock_get(&rtcan_socket_lock);
		}

		ret = RTDM_IRQ_HANDLED;
		
	} 
	else if ((priv->irqstatus >= C_CAN_MSG_OBJ_TX_FIRST) &&
			(priv->irqstatus <= C_CAN_MSG_OBJ_TX_LAST)) {
		/* handle events corresponding to transmit message objects */
		c_can_do_tx(dev);
		
		if (rtcan_loopback_pending(dev)) {
			if (recv_lock_free) {
				recv_lock_free = 0;
				rtdm_lock_get(&rtcan_recv_list_lock);
				rtdm_lock_get(&rtcan_socket_lock);
			}
			rtcan_loopback(dev);
		}
		ret = RTDM_IRQ_HANDLED;
	}

	if (!recv_lock_free) {
		rtdm_lock_put(&rtcan_socket_lock);
		rtdm_lock_put(&rtcan_recv_list_lock);
	}
	rtdm_lock_put(&dev->device_lock);
	c_can_enable_all_interrupts(priv, ENABLE_ALL_INTERRUPTS);
	
	return ret;
}

struct rtcan_device *alloc_c_can_dev(void)
{
	struct rtcan_device *dev;
	struct c_can_priv *priv;

	dev = rtcan_dev_alloc(sizeof(struct c_can_priv), 0);
	if (!dev)
		return NULL;

	priv = rtcan_priv(dev);

	priv->dev = dev;
	return dev;
}
//EXPORT_SYMBOL_GPL(alloc_c_can_dev);

#ifdef CONFIG_PM
int c_can_power_down(struct rtcan_device *dev)
{
	u32 val;
	unsigned long time_out;
	struct c_can_priv *priv = rtcan_priv(dev);

	WARN_ON(priv->type != BOSCH_D_CAN);

	/* set PDR value so the device goes to power down mode */
	val = priv->read_reg(priv, C_CAN_CTRL_EX_REG);
	val |= CONTROL_EX_PDR;
	priv->write_reg(priv, C_CAN_CTRL_EX_REG, val);

	/* Wait for the PDA bit to get set */
	time_out = jiffies + msecs_to_jiffies(INIT_WAIT_MS);
	while (!(priv->read_reg(priv, C_CAN_STS_REG) & STATUS_PDA) &&
				time_after(time_out, jiffies))
		cpu_relax();

	if (time_after(jiffies, time_out))
		return -ETIMEDOUT;

	/* disable all interrupts */
	c_can_enable_all_interrupts(priv, DISABLE_ALL_INTERRUPTS);

	/* set the state as STOPPED */
	dev->state = CAN_STATE_STOPPED;

	c_can_reset_ram(priv, false);
	c_can_pm_runtime_put_sync(priv);

	return 0;
}
//EXPORT_SYMBOL_GPL(c_can_power_down);

int c_can_power_up(struct rtcan_device *dev)
{
	u32 val;
	unsigned long time_out;
	struct c_can_priv *priv = rtcan_priv(dev);

	WARN_ON(priv->type != BOSCH_D_CAN);

	c_can_pm_runtime_get_sync(priv);
	c_can_reset_ram(priv, true);

	/* Clear PDR and INIT bits */
	val = priv->read_reg(priv, C_CAN_CTRL_EX_REG);
	val &= ~CONTROL_EX_PDR;
	priv->write_reg(priv, C_CAN_CTRL_EX_REG, val);
	val = priv->read_reg(priv, C_CAN_CTRL_REG);
	val &= ~CONTROL_INIT;
	priv->write_reg(priv, C_CAN_CTRL_REG, val);

	/* Wait for the PDA bit to get clear */
	time_out = jiffies + msecs_to_jiffies(INIT_WAIT_MS);
	while ((priv->read_reg(priv, C_CAN_STS_REG) & STATUS_PDA) &&
				time_after(time_out, jiffies))
		cpu_relax();

	if (time_after(jiffies, time_out))
		return -ETIMEDOUT;

	return 0;
}
//EXPORT_SYMBOL_GPL(c_can_power_up);
#endif


int register_c_candev(struct rtcan_device *dev)
{
	int err; 
	struct c_can_priv *priv = rtcan_priv(dev);
	
	c_can_pm_runtime_enable(priv);
	
	err = rtcan_dev_register(dev);
	if (err)
		goto out_chip_disable;

	return 0;

out_chip_disable:
	c_can_pm_runtime_disable(priv);

	return err;
}

void unregister_c_candev(struct rtcan_device *dev)
{
	c_can_mode_stop(dev, NULL);
	rtcan_dev_unregister(dev);
}


/*PLATFORM part from here:*/
#define CAN_RAMINIT_START_MASK(i)	(1 << (i))

/*
 * 16-bit c_can registers can be arranged differently in the memory
 * architecture of different implementations. For example: 16-bit
 * registers can be aligned to a 16-bit boundary or 32-bit boundary etc.
 * Handle the same by providing a common read/write interface.
 */
static u16 c_can_plat_read_reg_aligned_to_16bit(struct c_can_priv *priv,
						enum reg index)
{
	return readw(priv->base + priv->regs[index]);
}

static void c_can_plat_write_reg_aligned_to_16bit(struct c_can_priv *priv,
						enum reg index, u16 val)
{
	writew(val, priv->base + priv->regs[index]);
}

static u16 c_can_plat_read_reg_aligned_to_32bit(struct c_can_priv *priv,
						enum reg index)
{
	return readw(priv->base + 2 * priv->regs[index]);
}

static void c_can_plat_write_reg_aligned_to_32bit(struct c_can_priv *priv,
						enum reg index, u16 val)
{
	writew(val, priv->base + 2 * priv->regs[index]);
}

static void c_can_hw_raminit(const struct c_can_priv *priv, bool enable)
{
	u32 val;

	val = readl(priv->raminit_ctrlreg);
	if (enable)
		val |= CAN_RAMINIT_START_MASK(priv->instance);
	else
		val &= ~CAN_RAMINIT_START_MASK(priv->instance);
	writel(val, priv->raminit_ctrlreg);
}

static struct platform_device_id c_can_id_table[] = {
	[BOSCH_C_CAN_PLATFORM] = {
		.name = "c_can",
		.driver_data = BOSCH_C_CAN,
	},
	[BOSCH_C_CAN] = {
		.name = "c_can",
		.driver_data = BOSCH_C_CAN,
	},
	[BOSCH_D_CAN] = {
		.name = "d_can",
		.driver_data = BOSCH_D_CAN,
	}, {
	}
};
MODULE_DEVICE_TABLE(platform, c_can_id_table);

static const struct of_device_id c_can_of_table[] = {
	{ .compatible = "bosch,c_can", .data = &c_can_id_table[BOSCH_C_CAN] },
	{ .compatible = "bosch,d_can", .data = &c_can_id_table[BOSCH_D_CAN] },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, c_can_of_table);

static int c_can_plat_probe(struct platform_device *pdev)
{
	int ret;
	void __iomem *addr;
	struct rtcan_device *dev;
	struct c_can_priv *priv;
	const struct of_device_id *match;
	const struct platform_device_id *id;
	struct pinctrl *pinctrl;
	struct resource *mem, *res;
	int irq;
	struct clk *clk;

	if (pdev->dev.of_node) {
		match = of_match_device(c_can_of_table, &pdev->dev);
		if (!match) {
			dev_err(&pdev->dev, "Failed to find matching dt id\n");
			ret = -EINVAL;
			goto exit;
		}
		id = match->data;
	} else {
		id = platform_get_device_id(pdev);
	}

	pinctrl = devm_pinctrl_get_select_default(&pdev->dev);
	if (IS_ERR(pinctrl))
		dev_warn(&pdev->dev,
			"failed to configure pins from driver\n");

	/* get the appropriate clk */
	clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(clk)) {
		dev_err(&pdev->dev, "no clock defined\n");
		ret = -ENODEV;
		goto exit;
	}
	
	dev_info(&pdev->dev, "setting up step 1: platform_get_resource\n");
	
	/* get the platform data */
	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	irq = platform_get_irq(pdev, 0);
	if (!mem || irq <= 0) {
		ret = -ENODEV;
		goto exit_free_clk;
	}
	
	dev_info(&pdev->dev, "setting up step 2: request mem region. Start %x, size %d\n", mem->start, resource_size(mem));
	
	if (!request_mem_region(mem->start, resource_size(mem),
				"c_can")) {
		dev_err(&pdev->dev, "resource unavailable\n");
		ret = -ENODEV;
		goto exit_free_clk;
	}
	
	dev_info(&pdev->dev, "setting up step 3: ioremap. Start %x, size %d\n", mem->start, resource_size(mem));

	addr = ioremap(mem->start, resource_size(mem));
	if (!addr) {
		dev_err(&pdev->dev, "failed to map can port\n");
		ret = -ENOMEM;
		goto exit_release_mem;
	}
	
	dev_info(&pdev->dev, "alloc dev...\n");
	
	/* allocate the c_can device */
	dev = alloc_c_can_dev();
	if (!dev) {
		ret = -ENOMEM;
		goto exit_iounmap;
	}

	priv = rtcan_priv(dev);
	switch (id->driver_data) {
	case BOSCH_C_CAN:
		priv->regs = reg_map_c_can;
		switch (mem->flags & IORESOURCE_MEM_TYPE_MASK) {
		case IORESOURCE_MEM_32BIT:
			priv->read_reg = c_can_plat_read_reg_aligned_to_32bit;
			priv->write_reg = c_can_plat_write_reg_aligned_to_32bit;
			break;
		case IORESOURCE_MEM_16BIT:
		default:
			priv->read_reg = c_can_plat_read_reg_aligned_to_16bit;
			priv->write_reg = c_can_plat_write_reg_aligned_to_16bit;
			break;
		}
		break;
	case BOSCH_D_CAN:
		priv->regs = reg_map_d_can;
		priv->read_reg = c_can_plat_read_reg_aligned_to_16bit;
		priv->write_reg = c_can_plat_write_reg_aligned_to_16bit;

		if (pdev->dev.of_node)
			priv->instance = of_alias_get_id(pdev->dev.of_node, "d_can");
		else
			priv->instance = pdev->id;
		
		dev_info(&pdev->dev, "platform_get_resource...\n");
		
		res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
		
		dev_info(&pdev->dev, "devm request and ioremap..\n");
		
		priv->raminit_ctrlreg =	devm_request_and_ioremap(&pdev->dev, res);
		
		if (!priv->raminit_ctrlreg || priv->instance < 0)
			dev_info(&pdev->dev, "control memory is not used for raminit\n");
		else
			priv->raminit = c_can_hw_raminit;
		break;
	default:
		ret = -EINVAL;
		goto exit_free_device;
	}

	priv->irq = irq;
	priv->base = addr;
	priv->device = &pdev->dev;
	priv->priv = clk;
	priv->type = id->driver_data;

	platform_set_drvdata(pdev, dev);
	
	dev->ctrl_name = c_can_ctrl_name;
	dev->board_name = my_board_name;
	dev->base_addr = (unsigned long)addr;
	dev->can_sys_clock = clk_get_rate(clk);
	dev->hard_start_xmit = c_can_start_xmit;
	dev->do_set_mode = c_can_set_mode;
	dev->do_set_bit_time = c_can_save_bit_time;
	dev->bittiming_const = &c_can_bittiming_const;
	dev->state = CAN_STATE_STOPPED;
	
	/* Give device an interface name */
	strncpy(dev->name, DEV_NAME, IFNAMSIZ);

	ret = register_c_candev(dev);
	if (ret) {
		dev_err(&pdev->dev, "registering %s failed (err=%d)\n",
			"c_can", ret);
		goto exit_free_device;
	}

	dev_info(&pdev->dev, "%s device registered (regs=%p, irq=%d)\n",
		 "c_can", priv->base, priv->irq);
	return 0;

exit_free_device:
	platform_set_drvdata(pdev, NULL);
exit_iounmap:
	iounmap(addr);
exit_release_mem:
	release_mem_region(mem->start, resource_size(mem));
exit_free_clk:
	clk_put(clk);
exit:
	dev_err(&pdev->dev, "probe failed\n");

	return ret;
}

static int c_can_plat_remove(struct platform_device *pdev)
{
	struct rtcan_device *dev = platform_get_drvdata(pdev);
	struct c_can_priv *priv = rtcan_priv(dev);
	struct resource *mem;

	unregister_c_candev(dev);
	platform_set_drvdata(pdev, NULL);

	iounmap(priv->base);

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	release_mem_region(mem->start, resource_size(mem));

	clk_put(priv->priv);
	
	rtcan_dev_free(dev);

	return 0;
}

static struct platform_driver c_can_plat_driver = {
	.driver = {
		/* For legacy platform support */
		.name = "c_can",
		.owner = THIS_MODULE,
#ifdef CONFIG_OF
		.of_match_table = of_match_ptr(c_can_of_table),
#endif
	},
	.id_table = c_can_id_table,
	.probe = c_can_plat_probe,
	.remove = c_can_plat_remove,
};

module_platform_driver(c_can_plat_driver);


MODULE_AUTHOR("Stephen J. Battazzo <stephen.j.battazzo@nasa.gov>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("CAN bus RTDM driver for Bosch C_CAN controller");
