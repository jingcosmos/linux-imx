// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2011-2013 Freescale Semiconductor, Inc.
 * Copyright 2011 Linaro Ltd.
 */

#include <linux/clk.h>
#include <linux/irqchip.h>
#include <linux/of_platform.h>
#include <linux/pci.h>
#include <linux/phy.h>
#include <linux/regmap.h>
#include <linux/micrel_phy.h>
/* For rtl8211fs lan */
#include <linux/motorcomm_phy.h>
#include <linux/mfd/syscon.h>
#include <linux/mfd/syscon/imx6q-iomuxc-gpr.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include "common.h"
#include "cpuidle.h"
#include "hardware.h"

#ifdef CONFIG_ARCH_ADVANTECH
#include <linux/proc-board.h>
#endif
/* For imx6q sabrelite board: set KSZ9021RN RGMII pad skew */
static int ksz9021rn_phy_fixup(struct phy_device *phydev)
{
	if (IS_BUILTIN(CONFIG_PHYLIB)) {
		/* min rx data delay */
		phy_write(phydev, MICREL_KSZ9021_EXTREG_CTRL,
			0x8000 | MICREL_KSZ9021_RGMII_RX_DATA_PAD_SCEW);
		phy_write(phydev, MICREL_KSZ9021_EXTREG_DATA_WRITE, 0x0000);

		/* max rx/tx clock delay, min rx/tx control delay */
		phy_write(phydev, MICREL_KSZ9021_EXTREG_CTRL,
			0x8000 | MICREL_KSZ9021_RGMII_CLK_CTRL_PAD_SCEW);
		phy_write(phydev, MICREL_KSZ9021_EXTREG_DATA_WRITE, 0xf0f0);
		phy_write(phydev, MICREL_KSZ9021_EXTREG_CTRL,
			MICREL_KSZ9021_RGMII_CLK_CTRL_PAD_SCEW);
	}

	return 0;
}

static void mmd_write_reg(struct phy_device *dev, int device, int reg, int val)
{
	phy_write(dev, 0x0d, device);
	phy_write(dev, 0x0e, reg);
	phy_write(dev, 0x0d, (1 << 14) | device);
	phy_write(dev, 0x0e, val);
}

static int ksz9031rn_phy_fixup(struct phy_device *dev)
{
	/*
	 * min rx data delay, max rx/tx clock delay,
	 * min rx/tx control delay
	 */
	mmd_write_reg(dev, 2, 4, 0);
	mmd_write_reg(dev, 2, 5, 0);
	mmd_write_reg(dev, 2, 8, 0x003ff);

	return 0;
}

/*
 * fixup for PLX PEX8909 bridge to configure GPIO1-7 as output High
 * as they are used for slots1-7 PERST#
 */
static void ventana_pciesw_early_fixup(struct pci_dev *dev)
{
	u32 dw;

	if (!of_machine_is_compatible("gw,ventana"))
		return;

	if (dev->devfn != 0)
		return;

	pci_read_config_dword(dev, 0x62c, &dw);
	dw |= 0xaaa8; // GPIO1-7 outputs
	pci_write_config_dword(dev, 0x62c, dw);

	pci_read_config_dword(dev, 0x644, &dw);
	dw |= 0xfe;   // GPIO1-7 output high
	pci_write_config_dword(dev, 0x644, dw);

	msleep(100);
}
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_PLX, 0x8609, ventana_pciesw_early_fixup);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_PLX, 0x8606, ventana_pciesw_early_fixup);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_PLX, 0x8604, ventana_pciesw_early_fixup);

static int ar8031_phy_fixup(struct phy_device *dev)
{
	u16 val;

	/* Set RGMII IO voltage to 1.8V */
	phy_write(dev, 0x1d, 0x1f);
	phy_write(dev, 0x1e, 0x8);

	/* disable phy AR8031 SmartEEE function. */
	phy_write(dev, 0xd, 0x3);
	phy_write(dev, 0xe, 0x805d);
	phy_write(dev, 0xd, 0x4003);
	val = phy_read(dev, 0xe);
	val &= ~(0x1 << 8);
	phy_write(dev, 0xe, val);

	/* To enable AR8031 output a 125MHz clk from CLK_25M */
	phy_write(dev, 0xd, 0x7);
	phy_write(dev, 0xe, 0x8016);
	phy_write(dev, 0xd, 0x4007);

	val = phy_read(dev, 0xe);
	val &= 0xffe3;
	val |= 0x18;
	phy_write(dev, 0xe, val);

	/* introduce tx clock delay */
	phy_write(dev, 0x1d, 0x5);
	val = phy_read(dev, 0x1e);
	val |= 0x0100;
	phy_write(dev, 0x1e, val);

	return 0;
}

#define PHY_ID_AR8031	0x004dd074

#if defined(CONFIG_ARCH_ADVANTECH) && defined(CONFIG_REALTEK_PHY)
static int rtl8211f_phy_fixup(struct phy_device *dev)
{
	int val;

	phy_write(dev, 0x1f, 0x0d04);
	/*PHY LED OK*/
	phy_write(dev, 0x10, 0xa050);
	phy_write(dev, 0x11, 0x0000);
	phy_write(dev, 0x1f, 0x0000);

	phy_write(dev, 0x1f, 0x0d08);
	val = phy_read(dev, 0x11);
	val |= (0x1 << 8);//enable TX delay
	phy_write(dev, 0x11, val);

	val = phy_read(dev, 0x15);
	val |= (0x1 << 3);//enable RX delay
	phy_write(dev, 0x15, val);
	phy_write(dev, 0x1f, 0x0000);

	return 0;
}

static int rtl8211e_phy_fixup(struct phy_device *dev)
{
	int len=0;
	u32 value1,value2;
	const __be32 *parp;
	struct device_node *np;

	value1 = 0x0742;
	value2 = 0x0040;
	np = of_find_compatible_node(NULL, NULL, "fsl,imx6q-fec");
	if (np) {
		parp = of_get_property(np, "phy-led", &len);
		if (parp && (len / sizeof (int) == 2)) {
			value1 = be32_to_cpu(parp[0]);
			value2 = be32_to_cpu(parp[1]);
		}
	}
	/*PHY LED OK*/
	phy_write(dev, 0x1f, 0x0007);
	phy_write(dev, 0x1e, 0x002c);
	phy_write(dev, 0x1c, value1);
	phy_write(dev, 0x1a, value2);
	phy_write(dev, 0x1f, 0x0000);

	phy_write(dev, 0x1f, 0x0005);
	phy_write(dev, 0x05, 0x8b82);
	phy_write(dev, 0x06, 0x052b);
	phy_write(dev, 0x1f, 0x0000);
	return 0;
}

#define PHY_ID_RTL8211F	0x001cc916
#define PHY_ID_RTL8211E	0x001cc915
#define REALTEK_PHY_ID_MASK 0x001fffff
#endif
static int ar8035_phy_fixup(struct phy_device *dev)
{
	u16 val;

	/* Ar803x phy SmartEEE feature cause link status generates glitch,
	 * which cause ethernet link down/up issue, so disable SmartEEE
	 */
	phy_write(dev, 0xd, 0x3);
	phy_write(dev, 0xe, 0x805d);
	phy_write(dev, 0xd, 0x4003);

	val = phy_read(dev, 0xe);
	phy_write(dev, 0xe, val & ~(1 << 8));

	/*
	 * Enable 125MHz clock from CLK_25M on the AR8031.  This
	 * is fed in to the IMX6 on the ENET_REF_CLK (V22) pad.
	 * Also, introduce a tx clock delay.
	 *
	 * This is the same as is the AR8031 fixup.
	 */
	ar8031_phy_fixup(dev);

	/*check phy power*/
	val = phy_read(dev, 0x0);
	if (val & BMCR_PDOWN)
		phy_write(dev, 0x0, val & ~BMCR_PDOWN);

	return 0;
}

#define PHY_ID_AR8035 0x004dd072

static int yt8521_phy_fixup(struct phy_device *dev)
{
	int ret;
	int val;

	/* disable auto sleep */
	ret = phy_write(dev, REG_DEBUG_ADDR_OFFSET, 0xa000);
	if (ret < 0)
		return ret;
	ret = phy_write(dev, REG_DEBUG_DATA, 0);
	if (ret < 0)
		return ret;
	ret = phy_write(dev, REG_DEBUG_ADDR_OFFSET, 0x27);
	if (ret < 0)
		return ret;
	val = phy_read(dev, REG_DEBUG_DATA);
	if (val < 0)
		return val;
	val &= ~(0x1 << 15);
	ret = phy_write(dev, REG_DEBUG_ADDR_OFFSET, 0x27);
	if (ret < 0)
		return ret;
	ret = phy_write(dev, REG_DEBUG_DATA, val);
	if (ret < 0)
		return ret;

	/* enable RXC clock when no wire plug */
	ret = phy_write(dev, REG_DEBUG_ADDR_OFFSET, 0xa000);
	if (ret < 0)
		return ret;
	ret = phy_write(dev, REG_DEBUG_DATA, 0);
	if (ret < 0)
		return ret;
	ret = phy_write(dev, REG_DEBUG_ADDR_OFFSET, 0xc);
	if (ret < 0)
		return ret;
	val = phy_read(dev, REG_DEBUG_DATA);
	if (val < 0)
		return val;
	val &= ~(1 << 12);
	ret = phy_write(dev, REG_DEBUG_DATA, val);
	if (ret < 0)
		return ret;

	/* enable PHY SyncE clock output */
	ret = phy_write(dev, REG_DEBUG_ADDR_OFFSET, 0xa000);
	if (ret < 0)
		return ret;
	ret = phy_write(dev, REG_DEBUG_DATA, 0);
	if (ret < 0)
		return ret;
	ret = phy_write(dev, REG_DEBUG_ADDR_OFFSET, 0xa012);
	if (ret < 0)
		return ret;
	val = phy_read(dev, REG_DEBUG_DATA);
	if (val < 0)
		return val;
	val &= ~(3 << 1);
	val |= (7 << 3);
	ret = phy_write(dev, REG_DEBUG_DATA, val);
	if (ret < 0)
		return ret;

#if 0
	/* config PHY Rxc_dly */
	ret = phy_write(dev, REG_DEBUG_ADDR_OFFSET, 0xa000);
	if (ret < 0)
		return ret;
	ret = phy_write(dev, REG_DEBUG_DATA, 0);
	if (ret < 0)
		return ret;
	ret = phy_write(dev, REG_DEBUG_ADDR_OFFSET, 0xa001);
	if (ret < 0)
		return ret;
	val = phy_read(dev, REG_DEBUG_DATA);
	if (val < 0)
		return val;
	val &= ~(1 << 8);
	ret = phy_write(dev, REG_DEBUG_DATA, val);
	if (ret < 0)
		return ret;

	/* config PHY Txc_dly */
	ret = phy_write(dev, REG_DEBUG_ADDR_OFFSET, 0xa000);
	if (ret < 0)
		return ret;
	ret = phy_write(dev, REG_DEBUG_DATA, 0);
	if (ret < 0)
		return ret;
	ret = phy_write(dev, REG_DEBUG_ADDR_OFFSET, 0xa003);
	if (ret < 0)
		return ret;
	val = phy_read(dev, REG_DEBUG_DATA);
	if (val < 0)
		return val;
	val &= ~0xff;
	ret = phy_write(dev, REG_DEBUG_DATA, val);
	if (ret < 0)
		return ret;
#endif

	/* config LED0 as 1000M link on */
	ret = phy_write(dev, REG_DEBUG_ADDR_OFFSET, 0xa000);
	if (ret < 0)
		return ret;
	ret = phy_write(dev, REG_DEBUG_DATA, 0);
	if (ret < 0)
		return ret;
	ret = phy_write(dev, REG_DEBUG_ADDR_OFFSET, 0xa00c);
	if (ret < 0)
		return ret;
	val = phy_read(dev, REG_DEBUG_DATA);
	if (val < 0)
		return val;
	val = 0xc040;
	ret = phy_write(dev, REG_DEBUG_DATA, val);
	if (ret < 0)
		return ret;

	/* config LED1 as ACT blinking */
	ret = phy_write(dev, REG_DEBUG_ADDR_OFFSET, 0xa000);
	if (ret < 0)
		return ret;
	ret = phy_write(dev, REG_DEBUG_DATA, 0);
	if (ret < 0)
		return ret;
	ret = phy_write(dev, REG_DEBUG_ADDR_OFFSET, 0xa00d);
	if (ret < 0)
		return ret;
	val = phy_read(dev, REG_DEBUG_DATA);
	if (val < 0)
		return val;
	val = 0xe600;
	ret = phy_write(dev, REG_DEBUG_DATA, val);
	if (ret < 0)
		return ret;

	/* config LED2 as 100M link on */
	ret = phy_write(dev, REG_DEBUG_ADDR_OFFSET, 0xa000);
	if (ret < 0)
		return ret;
	ret = phy_write(dev, REG_DEBUG_DATA, 0);
	if (ret < 0)
		return ret;
	ret = phy_write(dev, REG_DEBUG_ADDR_OFFSET, 0xa00e);
	if (ret < 0)
		return ret;
	val = phy_read(dev, REG_DEBUG_DATA);
	if (val < 0)
		return val;
	val = 0xc020;
	ret = phy_write(dev, REG_DEBUG_DATA, val);
	if (ret < 0)
		return ret;

	return 0;
}
static void __init imx6q_enet_phy_init(void)
{
	if (IS_BUILTIN(CONFIG_PHYLIB)) {
		phy_register_fixup_for_uid(PHY_ID_KSZ9021, MICREL_PHY_ID_MASK,
				ksz9021rn_phy_fixup);
		phy_register_fixup_for_uid(PHY_ID_KSZ9031, MICREL_PHY_ID_MASK,
				ksz9031rn_phy_fixup);
		phy_register_fixup_for_uid(PHY_ID_AR8031, 0xffffffef,
				ar8031_phy_fixup);
		phy_register_fixup_for_uid(PHY_ID_AR8035, 0xffffffef,
				ar8035_phy_fixup);
		phy_register_fixup_for_uid(PHY_ID_YT8521, MOTORCOMM_PHY_ID_MASK,
				yt8521_phy_fixup);
#if defined(CONFIG_ARCH_ADVANTECH) && defined(CONFIG_REALTEK_PHY)
		phy_register_fixup_for_uid(PHY_ID_RTL8211E, REALTEK_PHY_ID_MASK,
				rtl8211e_phy_fixup);
		phy_register_fixup_for_uid(PHY_ID_RTL8211F, REALTEK_PHY_ID_MASK,
				rtl8211f_phy_fixup);
#endif
	}
}

static void __init imx6q_1588_init(void)
{
	struct device_node *np;
	struct clk *ptp_clk;
	struct clk *enet_ref;
	struct regmap *gpr;
	u32 clksel;

	np = of_find_compatible_node(NULL, NULL, "fsl,imx6q-fec");
	if (!np) {
		pr_warn("%s: failed to find fec node\n", __func__);
		return;
	}

	ptp_clk = of_clk_get(np, 2);
	if (IS_ERR(ptp_clk)) {
		pr_warn("%s: failed to get ptp clock\n", __func__);
		goto put_node;
	}

	enet_ref = clk_get_sys(NULL, "enet_ref");
	if (IS_ERR(enet_ref)) {
		pr_warn("%s: failed to get enet clock\n", __func__);
		goto put_ptp_clk;
	}

	/*
	 * If enet_ref from ANATOP/CCM is the PTP clock source, we need to
	 * set bit IOMUXC_GPR1[21].  Or the PTP clock must be from pad
	 * (external OSC), and we need to clear the bit.
	 */
	clksel = clk_is_match(ptp_clk, enet_ref) ?
				IMX6Q_GPR1_ENET_CLK_SEL_ANATOP :
				IMX6Q_GPR1_ENET_CLK_SEL_PAD;
	gpr = syscon_regmap_lookup_by_compatible("fsl,imx6q-iomuxc-gpr");
	if (!IS_ERR(gpr))
		regmap_update_bits(gpr, IOMUXC_GPR1,
				IMX6Q_GPR1_ENET_CLK_SEL_MASK,
				clksel);
	else
		pr_err("failed to find fsl,imx6q-iomuxc-gpr regmap\n");

	clk_put(enet_ref);
put_ptp_clk:
	clk_put(ptp_clk);
put_node:
	of_node_put(np);
}

static void __init imx6q_csi_mux_init(void)
{
	/*
	 * MX6Q SabreSD board:
	 * IPU1 CSI0 connects to parallel interface.
	 * Set GPR1 bit 19 to 0x1.
	 *
	 * MX6DL SabreSD board:
	 * IPU1 CSI0 connects to parallel interface.
	 * Set GPR13 bit 0-2 to 0x4.
	 * IPU1 CSI1 connects to MIPI CSI2 virtual channel 1.
	 * Set GPR13 bit 3-5 to 0x1.
	 */
	struct regmap *gpr;

	gpr = syscon_regmap_lookup_by_compatible("fsl,imx6q-iomuxc-gpr");
	if (!IS_ERR(gpr)) {
		if (of_machine_is_compatible("fsl,imx6q-sabresd") ||
			of_machine_is_compatible("fsl,imx6q-sabreauto") ||
			of_machine_is_compatible("fsl,imx6qp-sabresd") ||
			of_machine_is_compatible("fsl,imx6qp-sabreauto"))
			regmap_update_bits(gpr, IOMUXC_GPR1, 1 << 19, 1 << 19);
		else if (of_machine_is_compatible("fsl,imx6dl-sabresd") ||
			 of_machine_is_compatible("fsl,imx6dl-sabreauto"))
			regmap_update_bits(gpr, IOMUXC_GPR13, 0x3F, 0x0C);
	} else {
		pr_err("%s(): failed to find fsl,imx6q-iomux-gpr regmap\n",
		       __func__);
	}
}

static void __init imx6q_enet_clk_sel(void)
{
	struct regmap *gpr;

	gpr = syscon_regmap_lookup_by_compatible("fsl,imx6q-iomuxc-gpr");
	if (!IS_ERR(gpr))
		regmap_update_bits(gpr, IOMUXC_GPR5,
				   IMX6Q_GPR5_ENET_TX_CLK_SEL, IMX6Q_GPR5_ENET_TX_CLK_SEL);
	else
		pr_err("failed to find fsl,imx6q-iomux-gpr regmap\n");
}

static inline void imx6q_enet_init(void)
{
	imx6_enet_mac_init("fsl,imx6q-fec", "fsl,imx6q-ocotp");
	imx6q_enet_phy_init();
	imx6q_1588_init();
	if (cpu_is_imx6q() && imx_get_soc_revision() >= IMX_CHIP_REVISION_2_0)
		imx6q_enet_clk_sel();
}

static void __init imx6q_axi_init(void)
{
	struct regmap *gpr;
	unsigned int mask;

	gpr = syscon_regmap_lookup_by_compatible("fsl,imx6q-iomuxc-gpr");
	if (!IS_ERR(gpr)) {
		/*
		 * Enable the cacheable attribute of VPU and IPU
		 * AXI transactions.
		 */
		mask = IMX6Q_GPR4_VPU_WR_CACHE_SEL |
			IMX6Q_GPR4_VPU_RD_CACHE_SEL |
			IMX6Q_GPR4_VPU_P_WR_CACHE_VAL |
			IMX6Q_GPR4_VPU_P_RD_CACHE_VAL_MASK |
			IMX6Q_GPR4_IPU_WR_CACHE_CTL |
			IMX6Q_GPR4_IPU_RD_CACHE_CTL;
		regmap_update_bits(gpr, IOMUXC_GPR4, mask, mask);

		/* Increase IPU read QoS priority */
		regmap_update_bits(gpr, IOMUXC_GPR6,
				IMX6Q_GPR6_IPU1_ID00_RD_QOS_MASK |
				IMX6Q_GPR6_IPU1_ID01_RD_QOS_MASK,
				(0xf << 16) | (0x7 << 20));
		regmap_update_bits(gpr, IOMUXC_GPR7,
				IMX6Q_GPR7_IPU2_ID00_RD_QOS_MASK |
				IMX6Q_GPR7_IPU2_ID01_RD_QOS_MASK,
				(0xf << 16) | (0x7 << 20));
	} else {
		pr_warn("failed to find fsl,imx6q-iomuxc-gpr regmap\n");
	}
}

static void __init imx6q_init_machine(void)
{
	if (cpu_is_imx6q() && imx_get_soc_revision() >= IMX_CHIP_REVISION_2_0)
		imx_print_silicon_rev("i.MX6QP", IMX_CHIP_REVISION_1_0);
	else
		imx_print_silicon_rev(cpu_is_imx6dl() ? "i.MX6DL" : "i.MX6Q",
				imx_get_soc_revision());

	of_platform_default_populate(NULL, NULL, NULL);

	imx_anatop_init();
	imx6q_enet_init();
	imx6q_csi_mux_init();
	cpu_is_imx6q() ?  imx6q_pm_init() : imx6dl_pm_init();
	imx6q_axi_init();
}

static void __init imx6q_init_late(void)
{
	/*
	 * WAIT mode is broken on imx6 Dual/Quad revision 1.0 and 1.1 so
	 * there is no point to run cpuidle on them.
	 *
	 * It does work on imx6 Solo/DualLite starting from 1.1
	 */
	if ((cpu_is_imx6q() && imx_get_soc_revision() > IMX_CHIP_REVISION_1_1) ||
	    (cpu_is_imx6dl() && imx_get_soc_revision() > IMX_CHIP_REVISION_1_0))
		imx6q_cpuidle_init();

	if (IS_ENABLED(CONFIG_ARM_IMX6Q_CPUFREQ))
		platform_device_register_simple("imx6q-cpufreq", -1, NULL, 0);
}

static void __init imx6q_map_io(void)
{
	debug_ll_io_init();
	imx_scu_map_io();
	imx6_pm_map_io();
	imx_busfreq_map_io();
}

static void __init imx6q_init_irq(void)
{
	imx_gpc_check_dt();
	imx_init_revision_from_anatop();
	imx_init_l2cache();
	imx_src_init();
	irqchip_init();
	imx6_pm_ccm_init("fsl,imx6q-ccm");
}

static const char * const imx6q_dt_compat[] __initconst = {
	"fsl,imx6dl",
	"fsl,imx6q",
	"fsl,imx6qp",
	NULL,
};

DT_MACHINE_START(IMX6Q, "Freescale i.MX6 Quad/DualLite (Device Tree)")
	.l2c_aux_val 	= 0,
	.l2c_aux_mask	= ~0,
	.smp		= smp_ops(imx_smp_ops),
	.map_io		= imx6q_map_io,
	.init_irq	= imx6q_init_irq,
	.init_machine	= imx6q_init_machine,
	.init_late      = imx6q_init_late,
	.dt_compat	= imx6q_dt_compat,
MACHINE_END
