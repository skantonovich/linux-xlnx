/*
 * Driver for the Texas Instruments DP83867 PHY
 *
 * Copyright (C) 2015 Texas Instruments Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/ethtool.h>
#include <linux/kernel.h>
#include <linux/mii.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/phy.h>

#include <dt-bindings/net/ti-dp83867.h>

#define DP83867_PHY_ID		0x2000a231
#define DP83867_DEVADDR		0x1f

#define MII_DP83867_PHYCTRL	0x10
#define MII_DP83867_MICR	0x12
#define MII_DP83867_ISR		0x13
#define MII_DP83867_CFG2	0x14
#define MII_DP83867_BISCR	0x16
#define DP83867_CTRL		0x1f
#define DP83867_CFG3		0x1e

/* Extended Registers */
#define DP83867_CFG4		0x0031
#define DP83867_RGMIICTL	0x0032
#define DP83867_STRAP_STS1	0x006E
#define DP83867_RGMIIDCTL	0x0086
#define DP83867_IO_MUX_CFG	0x0170
#define DP83867_SGMIICTL	0x00D3

#define DP83867_SW_RESET	BIT(15)
#define DP83867_SW_RESTART	BIT(14)

/* MICR Interrupt bits */
#define MII_DP83867_MICR_AN_ERR_INT_EN		BIT(15)
#define MII_DP83867_MICR_SPEED_CHNG_INT_EN	BIT(14)
#define MII_DP83867_MICR_DUP_MODE_CHNG_INT_EN	BIT(13)
#define MII_DP83867_MICR_PAGE_RXD_INT_EN	BIT(12)
#define MII_DP83867_MICR_AUTONEG_COMP_INT_EN	BIT(11)
#define MII_DP83867_MICR_LINK_STS_CHNG_INT_EN	BIT(10)
#define MII_DP83867_MICR_FALSE_CARRIER_INT_EN	BIT(8)
#define MII_DP83867_MICR_SLEEP_MODE_CHNG_INT_EN	BIT(4)
#define MII_DP83867_MICR_WOL_INT_EN		BIT(3)
#define MII_DP83867_MICR_XGMII_ERR_INT_EN	BIT(2)
#define MII_DP83867_MICR_POL_CHNG_INT_EN	BIT(1)
#define MII_DP83867_MICR_JABBER_INT_EN		BIT(0)

/* RGMIICTL bits */
#define DP83867_RGMII_TX_CLK_DELAY_EN		BIT(1)
#define DP83867_RGMII_RX_CLK_DELAY_EN		BIT(0)

/* STRAP_STS1 bits */
#define DP83867_STRAP_STS1_RESERVED		BIT(11)

/* PHY CTRL bits */
#define DP83867_PHYCR_FIFO_DEPTH_SHIFT		14
#define DP83867_PHYCR_FIFO_DEPTH_MASK		(3 << 14)
#define DP83867_MDI_CROSSOVER		5
#define DP83867_MDI_CROSSOVER_AUTO	0b10
#define DP83867_MDI_CROSSOVER_MDIX	0b01
#define DP83867_PHYCTRL_SGMIIEN			0x0800
#define DP83867_PHYCTRL_RXFIFO_SHIFT	12
#define DP83867_PHYCTRL_TXFIFO_SHIFT	14
#define DP83867_PHYCR_RESERVED_MASK		BIT(11)

/* RGMIIDCTL bits */
#define DP83867_RGMII_TX_CLK_DELAY_SHIFT	4

/* CFG2 bits */
#define MII_DP83867_CFG2_SPEEDOPT_10EN		0x0040
#define MII_DP83867_CFG2_SGMII_AUTONEGEN	0x0080
#define MII_DP83867_CFG2_SPEEDOPT_ENH		0x0100
#define MII_DP83867_CFG2_SPEEDOPT_CNT		0x0800
#define MII_DP83867_CFG2_SPEEDOPT_INTLOW	0x2000
#define MII_DP83867_CFG2_MASK			0x003F

/* CFG4 bits */
#define DP83867_CFG4_SGMII_AUTONEG_TIMER_MASK	0x60
#define DP83867_CFG4_SGMII_AUTONEG_TIMER_16MS	0x00
#define DP83867_CFG4_SGMII_AUTONEG_TIMER_2US	0x20
#define DP83867_CFG4_SGMII_AUTONEG_TIMER_800US	0x40
#define DP83867_CFG4_SGMII_AUTONEG_TIMER_11MS	0x60
#define DP83867_CFG4_RESVDBIT7	BIT(7)
#define DP83867_CFG4_RESVDBIT8	BIT(8)

/* IO_MUX_CFG bits */
#define DP83867_IO_MUX_CFG_IO_IMPEDANCE_CTRL	0x1f

#define DP83867_IO_MUX_CFG_IO_IMPEDANCE_MAX	0x0
#define DP83867_IO_MUX_CFG_IO_IMPEDANCE_MIN	0x1f
#define DP83867_IO_MUX_CFG_CLK_O_SEL_MASK	(0x1f << 8)
#define DP83867_IO_MUX_CFG_CLK_O_SEL_SHIFT	8

/* CFG4 bits */
#define DP83867_CFG4_PORT_MIRROR_EN              BIT(0)
/* SGMIICTL1 bits */
#define DP83867_SGMII_TYPE		BIT(14)

enum {
	DP83867_PORT_MIRROING_KEEP,
	DP83867_PORT_MIRROING_EN,
	DP83867_PORT_MIRROING_DIS,
};

struct dp83867_private {
	int rx_id_delay;
	int tx_id_delay;
	int fifo_depth;
	int io_impedance;
	int port_mirroring;
	bool rxctrl_strap_quirk;
	int clk_output_sel;
	bool sgmii_ref_clk_en;
};

static int dp83867_ack_interrupt(struct phy_device *phydev)
{
	int err = phy_read(phydev, MII_DP83867_ISR);

	if (err < 0)
		return err;

	return 0;
}

static int dp83867_config_intr(struct phy_device *phydev)
{
	int micr_status;

	if (phydev->interrupts == PHY_INTERRUPT_ENABLED) {
		micr_status = phy_read(phydev, MII_DP83867_MICR);
		if (micr_status < 0)
			return micr_status;

		micr_status |=
			(MII_DP83867_MICR_AN_ERR_INT_EN |
			MII_DP83867_MICR_SPEED_CHNG_INT_EN |
			MII_DP83867_MICR_AUTONEG_COMP_INT_EN |
			MII_DP83867_MICR_LINK_STS_CHNG_INT_EN |
			MII_DP83867_MICR_DUP_MODE_CHNG_INT_EN |
			MII_DP83867_MICR_SLEEP_MODE_CHNG_INT_EN);

		return phy_write(phydev, MII_DP83867_MICR, micr_status);
	}

	micr_status = 0x0;
	return phy_write(phydev, MII_DP83867_MICR, micr_status);
}

static int dp83867_config_port_mirroring(struct phy_device *phydev)
{
	struct dp83867_private *dp83867 =
		(struct dp83867_private *)phydev->priv;
	u16 val;

	val = phy_read_mmd(phydev, DP83867_DEVADDR, DP83867_CFG4);

	if (dp83867->port_mirroring == DP83867_PORT_MIRROING_EN)
		val |= DP83867_CFG4_PORT_MIRROR_EN;
	else
		val &= ~DP83867_CFG4_PORT_MIRROR_EN;

	phy_write_mmd(phydev, DP83867_DEVADDR, DP83867_CFG4, val);

	return 0;
}

#ifdef CONFIG_OF_MDIO
static int dp83867_of_init(struct phy_device *phydev)
{
	struct dp83867_private *dp83867 = phydev->priv;
	struct device *dev = &phydev->mdio.dev;
	struct device_node *of_node = dev->of_node;
	int ret;

	if (!of_node)
		return -ENODEV;

	dp83867->io_impedance = -EINVAL;

	/* Optional configuration */
	ret = of_property_read_u32(of_node, "ti,clk-output-sel",
				   &dp83867->clk_output_sel);
	if (ret || dp83867->clk_output_sel > DP83867_CLK_O_SEL_REF_CLK)
		/* Keep the default value if ti,clk-output-sel is not set
		 * or too high
		 */
		dp83867->clk_output_sel = DP83867_CLK_O_SEL_REF_CLK;

	if (of_property_read_bool(of_node, "ti,max-output-impedance"))
		dp83867->io_impedance = DP83867_IO_MUX_CFG_IO_IMPEDANCE_MAX;
	else if (of_property_read_bool(of_node, "ti,min-output-impedance"))
		dp83867->io_impedance = DP83867_IO_MUX_CFG_IO_IMPEDANCE_MIN;

	dp83867->rxctrl_strap_quirk = of_property_read_bool(of_node,
					"ti,dp83867-rxctrl-strap-quirk");
	dp83867->sgmii_ref_clk_en = of_property_read_bool(of_node,
					"ti,sgmii-ref-clock-output-enable");

	ret = of_property_read_u32(of_node, "ti,rx-internal-delay",
				   &dp83867->rx_id_delay);
	if (ret &&
	    (phydev->interface == PHY_INTERFACE_MODE_RGMII_ID ||
	     phydev->interface == PHY_INTERFACE_MODE_RGMII_RXID))
		return ret;

	ret = of_property_read_u32(of_node, "ti,tx-internal-delay",
				   &dp83867->tx_id_delay);
	if (ret &&
	    (phydev->interface == PHY_INTERFACE_MODE_RGMII_ID ||
	     phydev->interface == PHY_INTERFACE_MODE_RGMII_TXID))
		return ret;

	if (of_property_read_bool(of_node, "enet-phy-lane-swap"))
		dp83867->port_mirroring = DP83867_PORT_MIRROING_EN;

	if (of_property_read_bool(of_node, "enet-phy-lane-no-swap"))
		dp83867->port_mirroring = DP83867_PORT_MIRROING_DIS;

	return of_property_read_u32(of_node, "ti,fifo-depth",
				   &dp83867->fifo_depth);
}
#else
static int dp83867_of_init(struct phy_device *phydev)
{
	return 0;
}
#endif /* CONFIG_OF_MDIO */

static int dp83867_config_init(struct phy_device *phydev)
{
	struct dp83867_private *dp83867;
	int ret, bs;
	u16 val, delay, cfg2;

	if (!phydev->priv) {
		dp83867 = devm_kzalloc(&phydev->mdio.dev, sizeof(*dp83867),
				       GFP_KERNEL);
		if (!dp83867)
			return -ENOMEM;

		phydev->priv = dp83867;
		ret = dp83867_of_init(phydev);
		if (ret)
			return ret;
	} else {
		dp83867 = (struct dp83867_private *)phydev->priv;
	}

	/* RX_DV/RX_CTRL strapped in mode 1 or mode 2 workaround */
	if (dp83867->rxctrl_strap_quirk) {
		val = phy_read_mmd(phydev, DP83867_DEVADDR, DP83867_CFG4);
		val &= ~BIT(7);
		phy_write_mmd(phydev, DP83867_DEVADDR, DP83867_CFG4, val);
	}

	if (phy_interface_is_rgmii(phydev)) {
		ret = phy_write(phydev, MII_DP83867_PHYCTRL,
			(DP83867_MDI_CROSSOVER_AUTO << DP83867_MDI_CROSSOVER) |
			(dp83867->fifo_depth << DP83867_PHYCR_FIFO_DEPTH_SHIFT));
		if (ret)
			return ret;

		val = phy_read(phydev, MII_DP83867_PHYCTRL);
		if (val < 0)
			return val;
		val &= ~DP83867_PHYCR_FIFO_DEPTH_MASK;
		val |= (dp83867->fifo_depth << DP83867_PHYCR_FIFO_DEPTH_SHIFT);

		/* The code below checks if "port mirroring" N/A MODE4 has been
		 * enabled during power on bootstrap.
		 *
		 * Such N/A mode enabled by mistake can put PHY IC in some
		 * internal testing mode and disable RGMII transmission.
		 *
		 * In this particular case one needs to check STRAP_STS1
		 * register's bit 11 (marked as RESERVED).
		 */

		bs = phy_read_mmd(phydev, DP83867_DEVADDR, DP83867_STRAP_STS1);
		if (bs & DP83867_STRAP_STS1_RESERVED)
			val &= ~DP83867_PHYCR_RESERVED_MASK;

		ret = phy_write(phydev, MII_DP83867_PHYCTRL, val);
		if (ret)
			return ret;

	} else {
		/* Set SGMIICTL1 6-wire mode */
		if (dp83867->sgmii_ref_clk_en)
			val |= DP83867_SGMII_TYPE;
		else
			val &= ~DP83867_SGMII_TYPE;

		phy_write_mmd(phydev, DP83867_DEVADDR,
			      DP83867_SGMIICTL, val);

		phy_write(phydev, MII_BMCR,
			  (BMCR_ANENABLE | BMCR_FULLDPLX | BMCR_SPEED1000));

		cfg2 = phy_read(phydev, MII_DP83867_CFG2);
		cfg2 &= MII_DP83867_CFG2_MASK;
		cfg2 |= (MII_DP83867_CFG2_SPEEDOPT_10EN |
			 MII_DP83867_CFG2_SGMII_AUTONEGEN |
			 MII_DP83867_CFG2_SPEEDOPT_ENH |
			 MII_DP83867_CFG2_SPEEDOPT_CNT |
			 MII_DP83867_CFG2_SPEEDOPT_INTLOW);
		phy_write(phydev, MII_DP83867_CFG2, cfg2);

		phy_write_mmd(phydev, DP83867_DEVADDR, DP83867_RGMIICTL, 0x0);

		phy_write(phydev, MII_DP83867_PHYCTRL,
			  DP83867_PHYCTRL_SGMIIEN |
			  (DP83867_MDI_CROSSOVER_AUTO << DP83867_MDI_CROSSOVER) |
			  (dp83867->fifo_depth << DP83867_PHYCTRL_RXFIFO_SHIFT) |
			  (dp83867->fifo_depth  << DP83867_PHYCTRL_TXFIFO_SHIFT));
		phy_write(phydev, MII_DP83867_BISCR, 0x0);

		/* This is a SW workaround for link instability if
		 * RX_CTRL is not strapped to mode 3 or 4 in HW.
		 */
		if (dp83867->rxctrl_strap_quirk) {
			val = phy_read_mmd(phydev, DP83867_DEVADDR,
					   DP83867_CFG4);
			val &= ~DP83867_CFG4_RESVDBIT7;
			val |= DP83867_CFG4_RESVDBIT8;
			val &= ~DP83867_CFG4_SGMII_AUTONEG_TIMER_MASK;
			val |= DP83867_CFG4_SGMII_AUTONEG_TIMER_11MS;
			phy_write_mmd(phydev, DP83867_DEVADDR, DP83867_CFG4,
				      val);
		}
	}

	if ((phydev->interface >= PHY_INTERFACE_MODE_RGMII_ID) &&
	    (phydev->interface <= PHY_INTERFACE_MODE_RGMII_RXID)) {
		val = phy_read_mmd(phydev, DP83867_DEVADDR, DP83867_RGMIICTL);

		if (phydev->interface == PHY_INTERFACE_MODE_RGMII_ID)
			val |= (DP83867_RGMII_TX_CLK_DELAY_EN | DP83867_RGMII_RX_CLK_DELAY_EN);

		if (phydev->interface == PHY_INTERFACE_MODE_RGMII_TXID)
			val |= DP83867_RGMII_TX_CLK_DELAY_EN;

		if (phydev->interface == PHY_INTERFACE_MODE_RGMII_RXID)
			val |= DP83867_RGMII_RX_CLK_DELAY_EN;

		phy_write_mmd(phydev, DP83867_DEVADDR, DP83867_RGMIICTL, val);

		delay = (dp83867->rx_id_delay |
			(dp83867->tx_id_delay << DP83867_RGMII_TX_CLK_DELAY_SHIFT));

		phy_write_mmd(phydev, DP83867_DEVADDR, DP83867_RGMIIDCTL,
			      delay);

		if (dp83867->io_impedance >= 0) {
			val = phy_read_mmd(phydev, DP83867_DEVADDR,
					   DP83867_IO_MUX_CFG);

			val &= ~DP83867_IO_MUX_CFG_IO_IMPEDANCE_CTRL;
			val |= dp83867->io_impedance &
			       DP83867_IO_MUX_CFG_IO_IMPEDANCE_CTRL;

			phy_write_mmd(phydev, DP83867_DEVADDR,
				      DP83867_IO_MUX_CFG, val);
		}
	}

	/* Enable Interrupt output INT_OE in CFG3 register */
	if (phy_interrupt_is_valid(phydev)) {
		val = phy_read(phydev, DP83867_CFG3);
		val |= BIT(7);
		phy_write(phydev, DP83867_CFG3, val);
	}

	if (dp83867->port_mirroring != DP83867_PORT_MIRROING_KEEP)
		dp83867_config_port_mirroring(phydev);

	/* Clock output selection if muxing property is set */
	if (dp83867->clk_output_sel != DP83867_CLK_O_SEL_REF_CLK) {
		val = phy_read_mmd(phydev, DP83867_DEVADDR, DP83867_IO_MUX_CFG);
		val &= ~DP83867_IO_MUX_CFG_CLK_O_SEL_MASK;
		val |= (dp83867->clk_output_sel << DP83867_IO_MUX_CFG_CLK_O_SEL_SHIFT);
		phy_write_mmd(phydev, DP83867_DEVADDR, DP83867_IO_MUX_CFG, val);
	}

	return 0;
}

static int dp83867_phy_reset(struct phy_device *phydev)
{
	int err;

	err = phy_write(phydev, DP83867_CTRL, DP83867_SW_RESET);
	if (err < 0)
		return err;

	return dp83867_config_init(phydev);
}

static struct phy_driver dp83867_driver[] = {
	{
		.phy_id		= DP83867_PHY_ID,
		.phy_id_mask	= 0xfffffff0,
		.name		= "TI DP83867",
		.features	= PHY_GBIT_FEATURES,
		.flags		= PHY_HAS_INTERRUPT,

		.config_init	= dp83867_config_init,
		.soft_reset	= dp83867_phy_reset,

		/* IRQ related */
		.ack_interrupt	= dp83867_ack_interrupt,
		.config_intr	= dp83867_config_intr,

		.suspend	= genphy_suspend,
		.resume		= genphy_resume,
	},
};
module_phy_driver(dp83867_driver);

static struct mdio_device_id __maybe_unused dp83867_tbl[] = {
	{ DP83867_PHY_ID, 0xfffffff0 },
	{ }
};

MODULE_DEVICE_TABLE(mdio, dp83867_tbl);

MODULE_DESCRIPTION("Texas Instruments DP83867 PHY driver");
MODULE_AUTHOR("Dan Murphy <dmurphy@ti.com");
MODULE_LICENSE("GPL");
