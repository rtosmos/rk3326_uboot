/*
 * (C) Copyright 2020 Hardkernel Co., Ltd
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#include <common.h>
#include <command.h>
#include <adc.h>

#define check_range(min,max,val) (val > 0 && val > min && val < max ? 1 : 0)

int do_hwrev(cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[])
{
	unsigned int hwrev_adc;

	if (adc_channel_single_shot("saradc", 0, &hwrev_adc)) {
		printf("board hw rev failed\n");
		return CMD_RET_FAILURE;
	}
	/* RG351MP */
	//if (check_range(146, 186, hwrev_adc)) {
	if (1) {
		env_set("hwrev", "rg351mp");
		env_set("dtb_uboot", "rg351mp-uboot.dtb");
		env_set("dtb_kernel", "rk3326-rg351mp-linux.dtb");
	}
	/* RG351V */
	else if (check_range(494, 534, hwrev_adc)) {
		env_set("hwrev", "rg351v");
		env_set("dtb_uboot", "rg351v-uboot.dtb");
		env_set("dtb_kernel", "rk3326-rg351v-linux.dtb");
	}
	/* RG351P */
	else if (check_range(655, 695, hwrev_adc)) {
		env_set("hwrev", "rg351p");
		env_set("dtb_uboot", "rg351p-uboot.dtb");
		env_set("dtb_kernel", "rk3326-rg351p-linux.dtb");
	}
	/* Unknown */
	else {
		env_set("hwrev", "v00");
		env_set("dtb_uboot", "rg351p-uboot.dtb");
		env_set("dtb_kernel", "rk3326-rg351p-linux.dtb");
	}
	printf("adc0 (hw rev) %d\n", hwrev_adc);
printf("Model = %s\n",env_get("hwrev"));
	return CMD_RET_SUCCESS;
}

U_BOOT_CMD(
	hwrev, 1, 1, do_hwrev,
	"check hw revision of OGA",
	""
);
