/*
 * (C) Copyright 2017 Rockchip Electronics Co., Ltd
 *
 * SPDX-License-Identifier:     GPL-2.0+
 */

#include <asm/io.h>
#include <common.h>
#include <boot_rkimg.h>
#include <console.h>
#include <dm.h>
#include <errno.h>
#include <key.h>
#include <led.h>
#include <rtc.h>
#include <pwm.h>
#include <asm/arch/rockchip_smccc.h>
#include <asm/suspend.h>
#include <linux/input.h>
#include <power/charge_display.h>
#include <power/charge_animation.h>
#include <power/rockchip_pm.h>
#include <power/fuel_gauge.h>
#include <power/pmic.h>
#include <power/rk8xx_pmic.h>
#include <power/regulator.h>
#include <video_rockchip.h>
#ifdef CONFIG_IRQ
#include <irq-generic.h>
#include <rk_timer_irq.h>
#endif
#if defined(CONFIG_PLATFORM_ODROID_GOADV)
#include <rockchip_display_cmds.h>
#include <blk.h>
#endif

DECLARE_GLOBAL_DATA_PTR;

#define IMAGE_RESET_IDX				-1
#define IMAGE_SOC_100_IDX(n)			((n) - 2)
#define IMAGE_LOWPOWER_IDX(n)			((n) - 1)
#define SYSTEM_SUSPEND_DELAY_MS			5000
#define FUEL_GAUGE_POLL_MS			1000

#define LED_CHARGING_NAME			"battery_charging"
#define LED_CHARGING_FULL_NAME			"battery_full"

struct charge_image {
	const char *name;
	int soc;
	int period;	/* ms */
};

struct charge_animation_priv {
	struct udevice *pmic;
	struct udevice *fg;
	struct udevice *charger;
	struct udevice *rtc;
#ifdef CONFIG_LED
	struct udevice *led_charging;
	struct udevice *led_full;
#endif
	const struct charge_image *image;
	int image_num;

	int auto_wakeup_key_state;
	ulong auto_screen_off_timeout;	/* ms */
	ulong suspend_delay_timeout;	/* ms */
};

/*
 * IF you want to use your own charge images, please:
 *
 * 1. Update the following 'image[]' to point to your own images;
 * 2. You must set the failed image as last one and soc = -1 !!!
 */
#if defined(CONFIG_PLATFORM_ODROID_GOADV)
static const struct charge_image image[] = {
	{ .name = "battery_0.bmp", .soc = 5, .period = 800 },
	{ .name = "battery_1.bmp", .soc = 60, .period = 800 },
	{ .name = "battery_2.bmp", .soc = 80, .period = 800 },
	{ .name = "battery_3.bmp", .soc = 100, .period = 800 },
	{ .name = "battery_fail.bmp", .soc = -1, .period = 1000 },
};

static const char *image_sf[] = {
	"st_battery_0",
	"st_battery_1",
	"st_battery_2",
	"st_battery_3",
	"st_battery_fail",
};
#else
static const struct charge_image image[] = {
	{ .name = "battery_0.bmp", .soc = 5, .period = 600 },
	{ .name = "battery_1.bmp", .soc = 20, .period = 600 },
	{ .name = "battery_2.bmp", .soc = 40, .period = 600 },
	{ .name = "battery_3.bmp", .soc = 60, .period = 600 },
	{ .name = "battery_4.bmp", .soc = 80, .period = 600 },
	{ .name = "battery_5.bmp", .soc = 100, .period = 600 },
	{ .name = "battery_fail.bmp", .soc = -1, .period = 1000 },
};
#endif

static int charge_animation_ofdata_to_platdata(struct udevice *dev)
{
	struct charge_animation_pdata *pdata = dev_get_platdata(dev);

	/* charge mode */
	pdata->uboot_charge =
		dev_read_u32_default(dev, "rockchip,uboot-charge-on", 0);
	pdata->android_charge =
		dev_read_u32_default(dev, "rockchip,android-charge-on", 0);

	pdata->exit_charge_level =
		dev_read_u32_default(dev, "rockchip,uboot-exit-charge-level", 0);
	pdata->exit_charge_voltage =
		dev_read_u32_default(dev, "rockchip,uboot-exit-charge-voltage", 0);

	pdata->low_power_voltage =
		dev_read_u32_default(dev, "rockchip,uboot-low-power-voltage", 0);
#if defined(CONFIG_PLATFORM_ODROID_GOADV)
	/* show charge animation if battery voltage is over low-power-voltage */
	pdata->screen_on_voltage =
		dev_read_u32_default(dev, "rockchip,uboot-low-power-voltage", 0);
#else
	pdata->screen_on_voltage =
		dev_read_u32_default(dev, "rockchip,screen-on-voltage", 0);
#endif
	pdata->system_suspend =
		dev_read_u32_default(dev, "rockchip,system-suspend", 0);

	pdata->auto_wakeup_interval =
		dev_read_u32_default(dev, "rockchip,auto-wakeup-interval", 0);
	pdata->auto_wakeup_screen_invert =
		dev_read_u32_default(dev, "rockchip,auto-wakeup-screen-invert", 0);

	pdata->auto_off_screen_interval =
		dev_read_u32_default(dev, "rockchip,auto-off-screen-interval", 15);

	if (pdata->screen_on_voltage > pdata->exit_charge_voltage)
		pdata->screen_on_voltage = pdata->exit_charge_voltage;

	debug("mode: uboot=%d, android=%d; exit: soc=%d%%, voltage=%dmv;\n"
	      "lp_voltage=%d%%, screen_on=%dmv\n",
	      pdata->uboot_charge, pdata->android_charge,
	      pdata->exit_charge_level, pdata->exit_charge_voltage,
	      pdata->low_power_voltage, pdata->screen_on_voltage);

	return 0;
}

static int check_key_press(struct udevice *dev)
{
	struct charge_animation_pdata *pdata = dev_get_platdata(dev);
	struct charge_animation_priv *priv = dev_get_priv(dev);
	u32 state, rtc_state = 0;

#ifdef CONFIG_DM_RTC
	if (priv->rtc)
		rtc_state = rtc_alarm_trigger(priv->rtc);
#endif
	if (rtc_state) {
		printf("rtc alarm trigger...\n");
		return KEY_PRESS_LONG_DOWN;
	}

	state = key_read(KEY_POWER);
	if (state < 0)
		printf("read power key failed: %d\n", state);
	else if (state == KEY_PRESS_DOWN)
		printf("power key pressed...\n");
	else if (state == KEY_PRESS_LONG_DOWN)
		printf("power key long pressed...\n");

	/* Fixup key state for following cases */
	if (pdata->auto_wakeup_interval) {
		if (pdata->auto_wakeup_screen_invert) {
			if (priv->auto_wakeup_key_state == KEY_PRESS_DOWN) {
				/* Value is updated in timer interrupt */
				priv->auto_wakeup_key_state = KEY_PRESS_NONE;
				state = KEY_PRESS_DOWN;
			}
		}
	} else if (pdata->auto_off_screen_interval) {
		if (priv->auto_screen_off_timeout &&
		    get_timer(priv->auto_screen_off_timeout) >
		    pdata->auto_off_screen_interval * 1000) {	/* 1000ms */
			state = KEY_PRESS_DOWN;
			printf("Auto screen off\n");
		}
	}

	return state;
}

/*
 * If not enable CONFIG_IRQ, cpu can't suspend to ATF or wfi, so that wakeup
 * period timer is useless.
 */
#if !defined(CONFIG_IRQ) || !defined(CONFIG_ARM_CPU_SUSPEND)
static int system_suspend_enter(struct udevice *dev)
{
	return 0;
}

static void autowakeup_timer_init(struct udevice *dev, uint32_t seconds) {}
static void autowakeup_timer_uninit(void) {}

#else
static int system_suspend_enter(struct udevice *dev)
{
	struct charge_animation_pdata *pdata = dev_get_platdata(dev);
	struct charge_animation_priv *priv = dev_get_priv(dev);

	/*
	 * When cpu is in wfi and we try to give a long key press event without
	 * key release, cpu would wakeup and enter wfi again immediately. So
	 * here is the problem: cpu can only wakeup when long key released.
	 *
	 * Actually, we want cpu can detect long key event without key release,
	 * so we give a suspend delay timeout for cpu to detect this.
	 */
	if (priv->suspend_delay_timeout &&
	    get_timer(priv->suspend_delay_timeout) <= SYSTEM_SUSPEND_DELAY_MS)
		return 0;

	if (pdata->system_suspend && IS_ENABLED(CONFIG_ARM_SMCCC)) {
		printf("\nSystem suspend: ");
		putc('0');
		regulators_enable_state_mem(false);
		putc('1');
		local_irq_disable();
		putc('2');
		irqs_suspend();
		putc('3');
		device_suspend();
		putc('4');
		putc('\n');

		/* Trap into ATF for low power mode */
		cpu_suspend(0, psci_system_suspend);

		putc('\n');
		putc('4');
		device_resume();
		putc('3');
		irqs_resume();
		putc('2');
		local_irq_enable();
		putc('1');
		putc('\n');
	} else {
		irqs_suspend();
		printf("\nWfi\n");
		wfi();
		putc('1');
		irqs_resume();
	}

	priv->suspend_delay_timeout = get_timer(0);

	/*
	 * We must wait for key release event finish, otherwise
	 * we may read key state too early.
	 */
	mdelay(300);

	return 0;
}

static void timer_irq_handler(int irq, void *data)
{
	struct udevice *dev = data;
	struct charge_animation_priv *priv = dev_get_priv(dev);
	static long long count;

	writel(TIMER_CLR_INT, TIMER_BASE + TIMER_INTSTATUS);

	priv->auto_wakeup_key_state = KEY_PRESS_DOWN;
	printf("auto wakeup count: %lld\n", ++count);
}

static void autowakeup_timer_init(struct udevice *dev, uint32_t seconds)
{
	uint64_t period = 24000000ULL * seconds;

	/* Disable before conifg */
	writel(0, TIMER_BASE + TIMER_CTRL);

	/* Config */
	writel((uint32_t)period, TIMER_BASE + TIMER_LOAD_COUNT0);
	writel((uint32_t)(period >> 32), TIMER_BASE + TIMER_LOAD_COUNT1);
	writel(TIMER_CLR_INT, TIMER_BASE + TIMER_INTSTATUS);
	writel(TIMER_EN | TIMER_INT_EN, TIMER_BASE + TIMER_CTRL);

	/* IRQ */
	irq_install_handler(TIMER_IRQ, timer_irq_handler, dev);
	irq_handler_enable(TIMER_IRQ);
}

static void autowakeup_timer_uninit(void)
{
	irq_free_handler(TIMER_IRQ);
}
#endif

#ifdef CONFIG_DRM_ROCKCHIP
#if defined(CONFIG_PLATFORM_ODROID_GOADV)
extern unsigned char disp_offs;
static int bmp_dev;
static void charge_show_bmp(int idx, struct udevice *fg)
{
	unsigned long bmp_mem, bmp_copy;
	int ret;
	int battery;
	int current_avg;
	char cmd[64];
	int soc;
	int hundreds, tens, units;
	if (!fg)
		return;

	battery = fuel_gauge_get_voltage(fg);
	current_avg = fuel_gauge_get_current(fg);
	soc = fuel_gauge_update_get_soc(fg);
     printk(KERN_WARNING "L%d->%s()\n",__LINE__,__FUNCTION__);
	debug("charge_show_bmp idx %d, name %s, battery %d\n", idx, image[idx].name, battery);

	/* draw logo bmp */
	bmp_mem = lcd_get_mem();
	bmp_copy = bmp_mem + LCD_LOGO_SIZE;

	if (!bmp_dev) {
		sprintf(cmd, "fatload mmc 1:1 %p %s", (void *)bmp_mem, image[idx].name);
		ret = run_command(cmd, 0);
		if (ret != CMD_RET_SUCCESS)
			bmp_dev = 0xff;
		printf("bmp_dev: %#x \n",bmp_dev);
	}

	if (bmp_dev == 0xff) {
		ulong n, cnt, blk;
		blk = env_get_hex(image_sf[idx], 0);
		cnt = env_get_hex("sz_logo", 0);
             printf("blk: %#lx \n",blk);
			 printf("IF_TYPE_SPINOR: %d \n",IF_TYPE_SPINOR);
		/*
		n = blk_read_devnum(IF_TYPE_SPINOR, 1,
			blk, cnt,
			(ulong *)bmp_copy);
			*/
			n = blk_read_devnum(IF_TYPE_MMC, 1,
			blk, cnt,
			(ulong *)bmp_copy);
		if (n != cnt)
			printf("spinor file read error\n");

		sprintf(cmd, "unzip %p %p", (void *)bmp_copy, (void *)bmp_mem);
		run_command(cmd, 0);
		
	}

	if (show_bmp(bmp_mem))
		printf("[%s] show_bmp fail\n", __func__);

	/* show battery voltage level */
	sprintf(cmd, "battery : %d.%d V", (battery / 1000), ((battery % 1000) / 100));
	lcd_setfg_color("grey");
	//lcd_printf(0, 18 + disp_offs, 1, "%s", cmd);

	sprintf(cmd, "current : %d mA", current_avg);
	//lcd_printf(0, 19 + disp_offs, 1, "%s", cmd);
	hundreds = soc / 100;  // 取得百位数字，通过除以100取整得到
    tens = (soc / 10) % 10;  // 先除以10得到包含百位和十位的数，再对10取余得到十位数字
    units = soc % 10;  // 直接对10取余得到个位数字
	printf("hundreds: %d \n",hundreds);
	printf("tens: %d \n", tens);
	printf(" units: %d \n", units);
	
//printf("cmd: %d \n",cmd);
//printf("cmd: %s \n",cmd);
///*
if(hundreds==1)lcd_printf(300, 19 + disp_offs, 4, "1");
lcd_printf(335, 19 + disp_offs, 4, "%d",tens);
lcd_printf(370, 19 + disp_offs, 4, "%d",units);
lcd_printf(405, 19 + disp_offs, 4, "%");
//*/
//lcd_printf(350, 26 + disp_offs, 4, "2");
//lcd_printf(382, 26 + disp_offs, 4, "4");
//lcd_printf(200, 26 + disp_offs, 4, "5");
//lcd_printf(233, 26 + disp_offs, 4, "3");
//lcd_printf(0, 10, 4, "3");


	//printf("charge_show_bmp end!\n");
}
#else
static void charge_show_bmp(const char *name)
{
	rockchip_show_bmp(name);
}
#endif
static void charge_show_logo(void)
{
	rockchip_show_logo();
}
#else
static void charge_show_bmp(const char *name) {}
static void charge_show_logo(void) {}
#endif

#ifdef CONFIG_LED
static int leds_update(struct udevice *dev, int soc)
{
	struct charge_animation_priv *priv = dev_get_priv(dev);
	int ret, ledst;
#if !defined(CONFIG_PLATFORM_ODROID_GOADV)
	static int old_soc = -1;

	if (old_soc == soc)
		return 0;

	old_soc = soc;
#endif
	if (priv->led_charging) {
#if defined(CONFIG_PLATFORM_ODROID_GOADV)
		ledst = (soc < 100) ? LEDST_TOGGLE : LEDST_OFF;
#else
		ledst = (soc < 100) ? LEDST_ON : LEDST_OFF;
#endif
		ret = led_set_state(priv->led_charging, ledst);
		if (ret) {
			printf("set charging led %s failed, ret=%d\n",
			       (ledst == LEDST_ON) ? "ON" : "OFF", ret);
			return ret;
		}
	}

	if (priv->led_full) {
		ledst = (soc == 100) ? LEDST_ON : LEDST_OFF;
		ret = led_set_state(priv->led_full, ledst);
		if (ret) {
			printf("set charging full led %s failed, ret=%d\n",
			       ledst == LEDST_ON ? "ON" : "OFF", ret);
			return ret;
		}
	}

	return 0;
}
#else
static int leds_update(struct udevice *dev, int soc) { return 0; }
#endif

static int fg_charger_get_chrg_online(struct udevice *dev)
{
	struct charge_animation_priv *priv = dev_get_priv(dev);
	struct udevice *charger;

	charger = priv->charger ? : priv->fg;

	return fuel_gauge_get_chrg_online(charger);
}

static int charge_extrem_low_power(struct udevice *dev)
{
	struct charge_animation_pdata *pdata = dev_get_platdata(dev);
	struct charge_animation_priv *priv = dev_get_priv(dev);
	struct udevice *pmic = priv->pmic;
	struct udevice *fg = priv->fg;
	int voltage, soc, charging = 1;
	static int timer_initialized;
	int ret;
	bool screen_on = true; /* screen is activated from lcd_init */
	ulong disp_start = 0;

	voltage = fuel_gauge_get_voltage(fg);
	if (voltage < 0)
		return -EINVAL;

	while (voltage < pdata->low_power_voltage + 50) {
		/* Check charger online */
		charging = fg_charger_get_chrg_online(dev);
		if (charging <= 0) {
			printf("%s: Not charging, online=%d. Shutdown...\n",
			       __func__, charging);
			/* wait uart flush before shutdown */
			mdelay(5);
			/* PMIC shutdown */
			pmic_shutdown(pmic);

			printf("Cpu should never reach here, shutdown failed !\n");
			continue;
		}

		/* Enable auto wakeup */
		if (!timer_initialized) {
			timer_initialized = 1;
			autowakeup_timer_init(dev, 5);
		}

		/*
		 * Just for fuel gauge to update something important,
		 * including charge current, coulometer or other.
		 */
		soc = fuel_gauge_update_get_soc(fg);
		if (soc < 0 || soc > 100) {
			printf("get soc failed: %d\n", soc);
			continue;
		}

		/* Update led */
		ret = leds_update(dev, soc);
		if (ret)
			printf("update led failed: %d\n", ret);

		if (!disp_start) {
			disp_start = get_timer(0);

			/* display low voltage status */
			charge_show_bmp(4, fg); /* battery fail */

			lcd_printf(0, 15 + disp_offs, 1, "Extreme Low Battery, please wait until changed to 5%");
			lcd_printf(0, 16 + disp_offs, 1, " LCD will be off soon.");
		}

		if ((get_timer(disp_start) > 5000) && screen_on) {
			lcd_onoff(false);
			screen_on = false; /* never turn on again here */
		}

		printf("Extrem low power, force charging... threshold=%dmv, now=%dmv\n",
		       pdata->low_power_voltage, voltage);

		/* System suspend */
		system_suspend_enter(dev);

		/* Update voltage */
		voltage = fuel_gauge_get_voltage(fg);
		if (voltage < 0) {
			printf("get voltage failed: %d\n", voltage);
			continue;
		}

		if (ctrlc()) {
			printf("Extrem low charge: exit by ctrl+c\n");
			break;
		}

		mdelay(500);

	}

	if (!screen_on)
		lcd_onoff(true);

	autowakeup_timer_uninit();

	return 0;
}

static int charge_animation_show(struct udevice *dev)
{
	struct charge_animation_pdata *pdata = dev_get_platdata(dev);
	struct charge_animation_priv *priv = dev_get_priv(dev);
	const struct charge_image *image = priv->image;
	struct udevice *pmic = priv->pmic;
	struct udevice *fg = priv->fg;
	const char *preboot = env_get("preboot");
	int image_num = priv->image_num;
	bool ever_lowpower_screen_off = false;
	bool screen_on = true;
	ulong show_start = 0, charge_start = 0, debug_start = 0;
	ulong delta;
	ulong ms = 0, sec = 0;
	int start_idx = 0, show_idx = -1, old_show_idx = IMAGE_RESET_IDX;
	int soc, voltage, current, key_state;
	int i, charging = 1, ret;
#ifdef CONFIG_RKIMG_BOOTLOADER
	int boot_mode;
#endif
	int first_poll_fg = 1;

/*
 * Check sequence:
 *
 * 1. Extrem low power charge?
 * 2. Preboot cmd?
 * 3. Valid boot mode?
 * 4. U-Boot charge enabled by dts config?
 * 5. Screen off before charge?
 * 6. Enter charge !
 *
 */
	if (!fuel_gauge_bat_is_exist(fg)) {
		printf("Exit charge: battery is not exist\n");
		return 0;
	}

#if defined(CONFIG_PLATFORM_ODROID_GOADV)
	/* lcd initialization */
	lcd_init();
#endif

	/* Extrem low power charge */
	ret = charge_extrem_low_power(dev);
	if (ret < 0) {
		printf("extrem low power charge failed, ret=%d\n", ret);
		return ret;
	}

	/* If there is preboot command, exit */
	if (preboot && !strstr(preboot, "dvfs")) {
		printf("Exit charge: due to preboot cmd '%s'\n", preboot);
		return 0;
	}

	/* Not valid charge mode, exit */
#ifdef CONFIG_RKIMG_BOOTLOADER
	boot_mode = rockchip_get_boot_mode();
#ifdef CONFIG_PLATFORM_ODROID_GOADV
	/* reboot flag is normal. */
	if (boot_mode == BOOT_MODE_NORMAL) {
		printf("Exit charge: due to boot mode=%d\n", boot_mode);
		return 0;
	}
#else
	if ((boot_mode != BOOT_MODE_CHARGING) &&
	    (boot_mode != BOOT_MODE_UNDEFINE)) {
		printf("Exit charge: due to boot mode=%d\n", boot_mode);
		/* FIXME : check this scenario in case of cold boot */
		/* return 0; */
	}
#endif
#endif

	/* Not charger online, exit */
	charging = fg_charger_get_chrg_online(dev);
	if (charging <= 0) {
		printf("Exit charge: due to charger offline\n");
		return 0;
	}

	/* Enter android charge, set property for kernel */
	if (pdata->android_charge) {
		env_update("bootargs", "androidboot.mode=charger");
		printf("Android charge mode\n");
	}

	/* Not enable U-Boot charge, exit */
	if (!pdata->uboot_charge) {
		printf("Exit charge: due to not enable uboot charge\n");
		return 0;
	}

	voltage = fuel_gauge_get_voltage(fg);
	if (voltage < 0) {
		printf("get voltage failed: %d\n", voltage);
		return -EINVAL;
	}

	/* If low power, turn off screen */
	if (voltage <= pdata->screen_on_voltage + 50) {
		screen_on = false;
		ever_lowpower_screen_off = true;
#if defined(CONFIG_PLATFORM_ODROID_GOADV)
		lcd_onoff(false);

		/* set CHG LED on before screen off */
		if (priv->led_charging)
			led_set_state(priv->led_charging, LEDST_ON);
#else
		charge_show_bmp(NULL);
#endif
	}

	/* Auto wakeup */
	if (pdata->auto_wakeup_interval) {
		printf("Auto wakeup: %dS\n", pdata->auto_wakeup_interval);
		autowakeup_timer_init(dev, pdata->auto_wakeup_interval);
	}

/* Give a message warning when CONFIG_IRQ is not enabled */
#ifdef CONFIG_IRQ
	printf("Enter U-Boot charging mode\n");
#else
	printf("Enter U-Boot charging mode(without IRQ)\n");
#endif

	charge_start = get_timer(0);
	delta = get_timer(0);

	/* Charging ! */
	while (1) {
		/*
		 * At the most time, fuel gauge is usually a i2c device, we
		 * should avoid read/write all the time. We had better set
		 * poll seconds to update fuel gauge info.
		 */
		if (!first_poll_fg && get_timer(delta) < FUEL_GAUGE_POLL_MS)
			goto show_images;

		delta = get_timer(0);

		debug("step1 (%d)... \n", screen_on);

		/*
		 * Most fuel gauge is I2C interface, it shouldn't be interrupted
		 * during transfer. The power key event depends on interrupt, so
		 * we should disable local irq when update fuel gauge.
		 */
		local_irq_disable();

		/* Step1: Is charging now ? */
		charging = fg_charger_get_chrg_online(dev);
		if (charging <= 0) {
			printf("Not charging, online=%d. Shutdown...\n",
			       charging);

			/* wait uart flush before shutdown */
			mdelay(5);
			printf("lcd_Shutdown...\n");
			lcd_onoff(false);	//突然拔掉充电线屏幕第二天会闪屏,添加这一句让屏幕正常下电再关机
			
			/* PMIC shutdown */
			pmic_shutdown(pmic);

			printf("Cpu should never reach here, shutdown failed !\n");
			continue;
		}

		debug("step2 (%d)... show_idx=%d\n", screen_on, show_idx);

		/* Step2: get soc and voltage */
		soc = fuel_gauge_update_get_soc(fg);
		if (soc < 0 || soc > 100) {
			printf("get soc failed: %d\n", soc);
			continue;
		}

		voltage = fuel_gauge_get_voltage(fg);
		if (voltage < 0) {
			printf("get voltage failed: %d\n", voltage);
			continue;
		}

		current = fuel_gauge_get_current(fg);
		if (current == -ENOSYS) {
			printf("get current failed: %d\n", current);
			continue;
		}

		first_poll_fg = 0;
		local_irq_enable();

show_images:
		/*
		 * Just for debug, otherwise there will be nothing output which
		 * is not good to know what happen.
		 */
		if (!debug_start)
			debug_start = get_timer(0);
		if (get_timer(debug_start) > 20000) {
			debug_start = get_timer(0);
			printf("[%8ld]: soc=%d%%, vol=%dmv, c=%dma, "
			       "online=%d, screen_on=%d\n",
			       get_timer(0)/1000, soc, voltage,
			       current, charging, screen_on);
		}

#if !defined(CONFIG_PLATFORM_ODROID_GOADV)
		/* Update leds */
		ret = leds_update(dev, soc);
		if (ret)
			printf("update led failed: %d\n", ret);
#endif
		/*
		 * If ever lowpower screen off, force screen_on=false, which
		 * means key event can't modify screen_on, only voltage higher
		 * then threshold can update screen_on=true;
		 */
		if (ever_lowpower_screen_off)
			screen_on = false;

		/*
		 * Auto turn on screen when voltage higher than Vol screen on.
		 * 'ever_lowpower_screen_off' means enter the while(1) loop with
		 * screen off.
		 */
		if ((ever_lowpower_screen_off) &&
		    (voltage > pdata->screen_on_voltage)) {
			ever_lowpower_screen_off = false;
			screen_on = true;
			show_idx = IMAGE_RESET_IDX;
		}

		/*
		 * IMAGE_RESET_IDX means show_idx show be update by start_idx.
		 * When short key pressed event trigged, we will set show_idx
		 * as IMAGE_RESET_IDX which updates images index from start_idx
		 * that calculate by current soc.
		 */
		if (show_idx == IMAGE_RESET_IDX) {
			for (i = 0; i < IMAGE_SOC_100_IDX(image_num); i++) {
				/* Find out which image we start to show */
				if ((soc >= image[i].soc) &&
				    (soc < image[i + 1].soc)) {
					start_idx = i;
					break;
				}

				if (soc >= 100) {
					start_idx = IMAGE_SOC_100_IDX(image_num);
					break;
				}
			}

			debug("%s: show_idx=%d, screen_on=%d\n",
			      __func__, show_idx, screen_on);

			/* Mark start index and start time */
			show_idx = start_idx;
			show_start = get_timer(0);
		}

		debug("step3 (%d)... show_idx=%d\n", screen_on, show_idx);

		/* Step3: show images */
		if (screen_on) {
			/* Don't call 'charge_show_bmp' unless image changed */
			if (old_show_idx != show_idx) {
				old_show_idx = show_idx;
				debug("SHOW: %s\n", image[show_idx].name);
#if defined(CONFIG_PLATFORM_ODROID_GOADV)
				charge_show_bmp(show_idx, fg);
				leds_update(dev, soc);
#else
				charge_show_bmp(image[show_idx].name);
#endif
			}
			/* Re-calculate timeout to off screen */
			if (priv->auto_screen_off_timeout == 0)
				priv->auto_screen_off_timeout = get_timer(0);
		} else {
			priv->auto_screen_off_timeout = 0;
			system_suspend_enter(dev);
		}

		mdelay(5);

		/* Every image shows period */
		if (get_timer(show_start) > image[show_idx].period) {
			show_start = get_timer(0);
			/* Update to next image */
			show_idx++;
			if (show_idx > IMAGE_SOC_100_IDX(image_num))
				show_idx = IMAGE_RESET_IDX;
		}

		debug("step4 (%d)... \n", screen_on);

		/*
		 * Step4: check key event.
		 *
		 * Short key event: turn on/off screen;
		 * Long key event: show logo and boot system or still charging.
		 */
		key_state = check_key_press(dev);
		if (key_state == KEY_PRESS_DOWN) {
			/*
			 * Clear current image index, and show image
			 * from start_idx
			 */
			old_show_idx = IMAGE_RESET_IDX;
			show_idx = IMAGE_RESET_IDX;

			/*
			 *	Reverse the screen state
			 *
			 * If screen_on=false, means this short key pressed
			 * event turn on the screen and we need show images.
			 *
			 * If screen_on=true, means this short key pressed
			 * event turn off the screen and we never show images.
			 */
			if (screen_on) {
#if defined(CONFIG_PLATFORM_ODROID_GOADV)
				lcd_onoff(false);

				/* set CHG LED on before screen off */
				if (priv->led_charging)
					led_set_state(priv->led_charging, LEDST_ON);
#else
				charge_show_bmp(NULL); /* Turn off screen */
#endif
				screen_on = false;
				priv->suspend_delay_timeout = get_timer(0);
			} else {
				screen_on = true;
#if defined(CONFIG_PLATFORM_ODROID_GOADV)
				lcd_onoff(true);
#endif
			}

			printf("screen %s\n", screen_on ? "on" : "off");
		} else if (key_state == KEY_PRESS_LONG_DOWN) {
			/* Set screen_on=true anyway when key long pressed */
			if (!screen_on) {
				screen_on = true;
#if defined(CONFIG_PLATFORM_ODROID_GOADV)
				lcd_onoff(true);
#endif
			}

			printf("screen %s\n", screen_on ? "on" : "off");

			/* Is able to boot now ? */
			if (soc < pdata->exit_charge_level) {
				printf("soc=%d%%, threshold soc=%d%%\n",
				       soc, pdata->exit_charge_level);
				printf("Low power, unable to boot, charging...\n");
				show_idx = IMAGE_LOWPOWER_IDX(image_num);
				continue;
			}

			if (voltage < pdata->exit_charge_voltage) {
				printf("voltage=%dmv, threshold voltage=%dmv\n",
				       voltage, pdata->exit_charge_voltage);
				printf("Low power, unable to boot, charging...\n");
				show_idx = IMAGE_LOWPOWER_IDX(image_num);
				continue;
			}

			/* Success exit charging */
			printf("Exit charge animation...\n");
			charge_show_logo();
			break;
		} else {
			/* Do nothing */
		}

		debug("step5 (%d)... \n", screen_on);

		/* Step5: Exit by ctrl+c */
		if (ctrlc()) {
			if (voltage >= pdata->screen_on_voltage)
				charge_show_logo();
			printf("Exit charge, due to ctrl+c\n");
			break;
		}
	}

	if (pdata->auto_wakeup_interval)
		autowakeup_timer_uninit();

	ms = get_timer(charge_start);
	if (ms >= 1000) {
		sec = ms / 1000;
		ms = ms % 1000;
	}

	printf("charging time total: %lu.%lus, soc=%d%%, vol=%dmv\n",
	       sec, ms, soc, voltage);

	return 0;
}

static int fg_charger_get_device(struct udevice **fuel_gauge,
				 struct udevice **charger)
{
	struct udevice *dev;
	struct uclass *uc;
	int ret, cap;

	*fuel_gauge = NULL,
	*charger = NULL;

	ret = uclass_get(UCLASS_FG, &uc);
	if (ret)
		return ret;

	for (uclass_first_device(UCLASS_FG, &dev);
	     dev;
	     uclass_next_device(&dev)) {
		cap = fuel_gauge_capability(dev);
		if (cap == (FG_CAP_CHARGER | FG_CAP_FUEL_GAUGE)) {
			*fuel_gauge = dev;
			*charger = NULL;
		} else if (cap == FG_CAP_FUEL_GAUGE) {
			*fuel_gauge = dev;
		} else if (cap == FG_CAP_CHARGER) {
			*charger = dev;
		}
	}

	return (*fuel_gauge) ? 0 : -ENODEV;
}

static const struct dm_charge_display_ops charge_animation_ops = {
	.show = charge_animation_show,
};

static int charge_animation_probe(struct udevice *dev)
{
	struct charge_animation_priv *priv = dev_get_priv(dev);
	int ret, soc;
#if defined(CONFIG_PLATFORM_ODROID_GOADV)
	int battery;
#endif
	/* Get PMIC: used for power off system  */
	ret = uclass_get_device(UCLASS_PMIC, 0, &priv->pmic);
	if (ret) {
		if (ret == -ENODEV)
			printf("Can't find PMIC\n");
		else
			printf("Get UCLASS PMIC failed: %d\n", ret);
		return ret;
	}

	/* Get fuel gauge and charger(If need) */
	ret = fg_charger_get_device(&priv->fg, &priv->charger);
	if (ret) {
		if (ret == -ENODEV)
			debug("Can't find FG\n");
		else
			debug("Get UCLASS FG failed: %d\n", ret);
		return ret;
	}

#ifdef CONFIG_DM_RTC
	/* Get rtc: used for power on */
	ret = uclass_get_device(UCLASS_RTC, 0, &priv->rtc);
	if (ret) {
		if (ret == -ENODEV)
			debug("Can't find RTC\n");
		else
			debug("Get UCLASS RTC failed: %d\n", ret);
	}
#endif

	/* Get PWRKEY: used for wakeup and turn off/on LCD */
	if (key_read(KEY_POWER) == KEY_NOT_EXIST) {
		debug("Can't find power key\n");
		return -EINVAL;
	}

#if defined(CONFIG_PLATFORM_ODROID_GOADV)
	/* Check battery existence */
	battery = fuel_gauge_get_voltage(priv->fg);
	if (battery < 1000) {
		debug("No battery is connected : %d\n", battery);
		return -EINVAL;
	}
#endif

	/* Initialize charge current */
	soc = fuel_gauge_update_get_soc(priv->fg);
	if (soc < 0 || soc > 100) {
		debug("get soc failed: %d\n", soc);
		return -EINVAL;
	}

	/* Get leds */
#ifdef CONFIG_LED
	ret = led_get_by_label(LED_CHARGING_NAME, &priv->led_charging);
	if (!ret)
		printf("Found Charging LED\n");
	ret = led_get_by_label(LED_CHARGING_FULL_NAME, &priv->led_full);
	if (!ret)
		printf("Found Charging-Full LED\n");
#endif

	/* Get charge images */
	priv->image = image;
	priv->image_num = ARRAY_SIZE(image);

	printf("Enable charge animation display\n");

	return 0;
}

static const struct udevice_id charge_animation_ids[] = {
	{ .compatible = "rockchip,uboot-charge" },
	{ },
};

U_BOOT_DRIVER(charge_animation) = {
	.name = "charge-animation",
	.id = UCLASS_CHARGE_DISPLAY,
	.probe = charge_animation_probe,
	.of_match = charge_animation_ids,
	.ops = &charge_animation_ops,
	.ofdata_to_platdata = charge_animation_ofdata_to_platdata,
	.platdata_auto_alloc_size = sizeof(struct charge_animation_pdata),
	.priv_auto_alloc_size = sizeof(struct charge_animation_priv),
};
