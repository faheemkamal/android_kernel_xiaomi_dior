/* Copyright (c) 2012-2014, The Linux Foundation. All rights reserved.
 * Copyright (C) 2015 XiaoMi, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/i2c/i2c-qup.h>
#include <linux/gpio.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/of_fdt.h>
#include <linux/of_irq.h>
#include <linux/memory.h>
#include <linux/regulator/cpr-regulator.h>
#include <linux/regulator/fan53555.h>
#include <linux/regulator/onsemi-ncp6335d.h>
#include <linux/regulator/qpnp-regulator.h>
#include <linux/msm_tsens.h>
#ifdef CONFIG_ANDROID_RAM_CONSOLE
#include <linux/persistent_ram.h>
#include <linux/memblock.h>
#endif
#include <asm/mach/map.h>
#include <asm/hardware/gic.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <mach/board.h>
#include <mach/msm_bus.h>
#include <mach/gpiomux.h>
#include <mach/msm_iomap.h>
#include <mach/restart.h>
#ifdef CONFIG_ION_MSM
#include <mach/ion.h>
#endif
#include <mach/msm_memtypes.h>
#include <mach/socinfo.h>
#include <mach/board.h>
#include <mach/clk-provider.h>
#include <mach/msm_smd.h>
#include <mach/rpm-smd.h>
#include <mach/rpm-regulator-smd.h>
#include <mach/msm_smem.h>
#include <linux/msm_thermal.h>
#include "board-dt.h"
#include "clock.h"
#include "platsmp.h"
#include "spm.h"
#include "pm.h"
#include "modem_notifier.h"
#include "spm-regulator.h"
#ifdef CONFIG_KEXEC_HARDBOOT
#include <asm/setup.h>
#include <asm/memory.h>
#include <linux/memblock.h>
#define XIAOMI_PERSISTENT_RAM_SIZE	(SZ_1M)
#endif

static struct memtype_reserve msm8226_reserve_table[] __initdata = {
	[MEMTYPE_SMI] = {
	},
	[MEMTYPE_EBI0] = {
		.flags	=	MEMTYPE_FLAGS_1M_ALIGN,
	},
	[MEMTYPE_EBI1] = {
		.flags	=	MEMTYPE_FLAGS_1M_ALIGN,
	},
};

#ifdef CONFIG_ANDROID_RAM_CONSOLE
static struct persistent_ram_descriptor desc = {
        .name = "ram_console",
};

static struct persistent_ram ram = {
        .descs = &desc,
        .num_descs = 1,
};

void __init ram_console_debug_reserve(unsigned long ram_console_size)
{
        int ret;

        ram.start = memblock_end_of_DRAM() - ram_console_size;
        ram.size = ram_console_size;
        ram.descs->size = ram_console_size;
        INIT_LIST_HEAD(&ram.node);

        ret = persistent_ram_early_init(&ram);
        if (ret) {
                pr_err("%s:ram console persistent_ram_early_init failed\n",__func__);
                goto fail;
        }

        return;

fail:
        pr_err("Failed to reserve memory block for ram console\n");
}

static struct resource ram_console_resources[] = {
        {
                .flags = IORESOURCE_MEM,
        },
};

static struct platform_device ram_console_device = {
        .name           = "ram_console",
        .id             = -1,
        .num_resources  = ARRAY_SIZE(ram_console_resources),
        .resource       = ram_console_resources,
};

void __init ram_console_debug_init(void)
{
        int err;
        err = platform_device_register(&ram_console_device);
        if (err)
                pr_err("%s: ram console registration failed (%d)!\n",
                        __func__, err);
}
#endif

static int msm8226_paddr_to_memtype(unsigned int paddr)
{
	return MEMTYPE_EBI1;
}

static struct of_dev_auxdata msm_hsic_host_adata[] = {
	OF_DEV_AUXDATA("qcom,hsic-host", 0xF9A00000, "msm_hsic_host", NULL),
	{}
};

static struct of_dev_auxdata msm8226_auxdata_lookup[] __initdata = {
	OF_DEV_AUXDATA("qcom,msm-sdcc", 0xF9824000, \
			"msm_sdcc.1", NULL),
	OF_DEV_AUXDATA("qcom,msm-sdcc", 0xF98A4000, \
			"msm_sdcc.2", NULL),
	OF_DEV_AUXDATA("qcom,msm-sdcc", 0xF9864000, \
			"msm_sdcc.3", NULL),
	OF_DEV_AUXDATA("qcom,sdhci-msm", 0xF9824900, \
			"msm_sdcc.1", NULL),
	OF_DEV_AUXDATA("qcom,sdhci-msm", 0xF98A4900, \
			"msm_sdcc.2", NULL),
	OF_DEV_AUXDATA("qcom,sdhci-msm", 0xF9864900, \
			"msm_sdcc.3", NULL),
	OF_DEV_AUXDATA("qcom,hsic-host", 0xF9A00000, "msm_hsic_host", NULL),
	OF_DEV_AUXDATA("qcom,hsic-smsc-hub", 0, "msm_smsc_hub",
			msm_hsic_host_adata),

	{}
};

static struct reserve_info msm8226_reserve_info __initdata = {
	.memtype_reserve_table = msm8226_reserve_table,
	.paddr_to_memtype = msm8226_paddr_to_memtype,
};

static void __init msm8226_early_memory(void)
{
	reserve_info = &msm8226_reserve_info;
	of_scan_flat_dt(dt_scan_for_memory_hole, msm8226_reserve_table);
}

static void __init msm8226_reserve(void)
{
#ifdef CONFIG_KEXEC_HARDBOOT
	// Reserve space for hardboot page - just after ram_console,
	// at the start of second memory bank
	int ret;
	phys_addr_t start;
	struct membank* bank;

	if (meminfo.nr_banks < 2) {
		pr_err("%s: not enough membank\n", __func__);
		return;
	}

	bank = &meminfo.bank[1];
	start = bank->start + SZ_1M + XIAOMI_PERSISTENT_RAM_SIZE;
	ret = memblock_remove(start, SZ_1M);
	if(!ret)
		pr_info("Hardboot page reserved at 0x%X\n", start);
	else
		pr_err("Failed to reserve space for hardboot page at 0x%X!\n", start);
#endif
	reserve_info = &msm8226_reserve_info;
	of_scan_flat_dt(dt_scan_for_memory_reserve, msm8226_reserve_table);
	msm_reserve();
#ifdef CONFIG_ANDROID_RAM_CONSOLE
	ram_console_debug_reserve(SZ_1M *2);
#endif
}

/*
 * Used to satisfy dependencies for devices that need to be
 * run early or in a particular order. Most likely your device doesn't fall
 * into this category, and thus the driver should not be added here. The
 * EPROBE_DEFER can satisfy most dependency problems.
 */
void __init msm8226_add_drivers(void)
{
	msm_smem_init();
	msm_init_modem_notifier_list();
	msm_smd_init();
	msm_rpm_driver_init();
	msm_spm_device_init();
	msm_pm_sleep_status_init();
	rpm_regulator_smd_driver_init();
	qpnp_regulator_init();
	spm_regulator_init();
	if (of_board_is_rumi())
		msm_clock_init(&msm8226_rumi_clock_init_data);
	else
		msm_clock_init(&msm8226_clock_init_data);
	msm_bus_fabric_init_driver();
	qup_i2c_init_driver();
	ncp6335d_regulator_init();
	fan53555_regulator_init();
	cpr_regulator_init();
	tsens_tm_init_driver();
	msm_thermal_device_init();
#ifdef CONFIG_ANDROID_RAM_CONSOLE
	ram_console_debug_init();
#endif
}

void __init msm8226_init(void)
{
	struct of_dev_auxdata *adata = msm8226_auxdata_lookup;

	if (socinfo_init() < 0)
		pr_err("%s: socinfo_init() failed\n", __func__);

	msm8226_init_gpiomux();
	board_dt_populate(adata);
	msm8226_add_drivers();
}

static const char *msm8226_dt_match[] __initconst = {
	"qcom,msm8226",
	"qcom,msm8926",
	"qcom,apq8026",
	NULL
};

DT_MACHINE_START(MSM8226_DT, "Qualcomm MSM 8x26 / MSM 8x28 (Flattened Device Tree)")
	.map_io = msm_map_msm8226_io,
	.init_irq = msm_dt_init_irq,
	.init_machine = msm8226_init,
	.handle_irq = gic_handle_irq,
	.timer = &msm_dt_timer,
	.dt_compat = msm8226_dt_match,
	.reserve = msm8226_reserve,
	.init_very_early = msm8226_early_memory,
	.restart = msm_restart,
	.smp = &arm_smp_ops,
MACHINE_END
