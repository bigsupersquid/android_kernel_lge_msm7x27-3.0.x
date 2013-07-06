/* Copyright (c) 2009-2011, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

/*
 * this needs to be before <linux/kernel.h> is loaded,
 * and <linux/sched.h> loads <linux/kernel.h>
 */
#define DEBUG  1

#include <linux/slab.h>
#include <linux/earlysuspend.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/sched.h>
#include <linux/signal.h>
#include <linux/uaccess.h>
#include <linux/wait.h>
#include <linux/workqueue.h>

#include <asm/atomic.h>

#include <mach/msm_rpcrouter.h>
#include <mach/msm_battery.h>

#define BATTERY_RPC_PROG	0x30000089
#define BATTERY_RPC_VER_1_1	0x00010001

#define CHG_RPC_PROG		0x3000001a
#define CHG_RPC_VER_1_1		0x00010001

#define BATTERY_LOW             3200
#define BATTERY_HIGH            4200

#define ONCRPC_CHARGER_API_VERSIONS_PROC	0xffffffff

#define ONCRPC_LG_CHG_GET_GENERAL_STATUS_PROC 20
#define BATT_RPC_TIMEOUT    5000	/* 5 sec */

#define RPC_TYPE_REQ     0
#define RPC_TYPE_REPLY   1
#define RPC_REQ_REPLY_COMMON_HEADER_SIZE   (3 * sizeof(uint32_t))

#if DEBUG
#define DBG_LIMIT(x...) do {if (printk_ratelimit()) pr_debug(x); } while (0)
#else
#define DBG_LIMIT(x...) do {} while (0)
#endif

enum {
	BATTERY_VOLTAGE_UP = 0,
	BATTERY_VOLTAGE_DOWN,
	BATTERY_VOLTAGE_ABOVE_THIS_LEVEL,
	BATTERY_VOLTAGE_BELOW_THIS_LEVEL,
	BATTERY_VOLTAGE_LEVEL,
	BATTERY_ALL_ACTIVITY,
	BATTERY_VOLTAGE_UNKNOWN,
};

/*
 * This enum contains defintions of the charger hardware status
 */
enum chg_charger_status_type {
	/* The charger is good      */
	CHARGER_STATUS_GOOD,
	/* The charger is bad       */
	CHARGER_STATUS_BAD,
	/* The charger is weak      */
	CHARGER_STATUS_WEAK,
	/* Invalid charger status.  */
	CHARGER_STATUS_INVALID
};

/*
 *This enum contains defintions of the charger hardware type
 */
enum chg_charger_hardware_type {
	/* The charger is removed                 */
	CHARGER_TYPE_NONE,
	/* The charger is a regular wall charger   */
	CHARGER_TYPE_WALL,
	/* The charger is a PC USB                 */
	CHARGER_TYPE_USB_PC,
	/* The charger is a wall USB charger       */
	CHARGER_TYPE_USB_WALL,
	/* The charger is a USB carkit             */
	CHARGER_TYPE_USB_CARKIT,
	/* Invalid charger hardware status.        */
	CHARGER_TYPE_INVALID
};

/*
 *  This enum contains defintions of the battery status
 */
enum chg_battery_status_type {
	/* The battery is good        */
	BATTERY_STATUS_GOOD,
	/* The battery is cold/hot    */
	BATTERY_STATUS_BAD_TEMP,
	/* The battery is bad         */
	BATTERY_STATUS_BAD,
	/* The battery is removed     */
	BATTERY_STATUS_REMOVED,		/* on v2.2 only */
	BATTERY_STATUS_INVALID_v1 = BATTERY_STATUS_REMOVED,
	/* Invalid battery status.    */
	BATTERY_STATUS_INVALID
};

/*
 *This enum contains defintions of the battery voltage level
 */
enum chg_battery_level_type {
	/* The battery voltage is dead/very low (less than 3.2V) */
	BATTERY_LEVEL_DEAD,
	/* The battery voltage is weak/low (between 3.2V and 3.4V) */
	BATTERY_LEVEL_WEAK,
	/* The battery voltage is good/normal(between 3.4V and 4.2V) */
	BATTERY_LEVEL_GOOD,
	/* The battery voltage is up to full (close to 4.2V) */
	BATTERY_LEVEL_FULL,
	/* Invalid battery voltage level. */
	BATTERY_LEVEL_INVALID
};

struct rpc_reply_batt_chg_v1 {
	struct rpc_reply_hdr hdr;
	u32 	more_data;

	u32	battery_level;
	u32 voltage_now;
	u32 batt_valid_id;
	u32 batt_therm;
	u32	batt_temp;
	u32	charger_status;
	u32	charger_type;
	u32	battery_status;
	u32 charger_valid;
	u32 battery_charging;
	u32 battery_valid;
};

struct rpc_reply_batt_chg_v2 {
	struct rpc_reply_batt_chg_v1	v1;

	u32	is_charger_valid;
	u32	is_charging;
	u32	is_battery_valid;
	u32	ui_event;
};

union rpc_reply_batt_chg {
	struct rpc_reply_batt_chg_v1	v1;
	struct rpc_reply_batt_chg_v2	v2;
};

static union rpc_reply_batt_chg rep_batt_chg;

struct msm_battery_info {
	u32 voltage_max_design;
	u32 voltage_min_design;
	u32 chg_api_version;
	u32 batt_technology;
	u32 batt_api_version;

	u32 avail_chg_sources;
	u32 current_chg_source;

	u32 batt_status;
	u32 batt_health;
	u32 charger_valid;
	u32 batt_valid;
	u32 batt_capacity; /* in percentage */

	u32 charger_status;
	u32 charger_type;
	u32 battery_status;
	u32 battery_level;
	u32 voltage_now; /* in millie volts */
	u32 batt_temp;  /* in celsius */
	u32 batt_therm;
	u32(*calculate_capacity) (u32 voltage);

	struct power_supply *msm_psy_ac;
	struct power_supply *msm_psy_usb;
	struct power_supply *msm_psy_batt;
	struct power_supply *current_ps;

	struct msm_rpc_client *batt_client;
	struct msm_rpc_endpoint *chg_ep;

	wait_queue_head_t wait_q;

	struct early_suspend early_suspend;
};

static struct msm_battery_info msm_batt_info = {
	.charger_status = CHARGER_STATUS_BAD,
	.charger_type = CHARGER_TYPE_INVALID,
	.battery_status = BATTERY_STATUS_GOOD,
	.battery_level = BATTERY_LEVEL_FULL,
	.voltage_now = BATTERY_HIGH,
	.batt_capacity = 100,
	.batt_status = POWER_SUPPLY_STATUS_DISCHARGING,
	.batt_health = POWER_SUPPLY_HEALTH_GOOD,
	.batt_valid  = 1,
	.batt_temp = 23,
	.batt_therm = 75,
};

static enum power_supply_property msm_power_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
};

static char *msm_power_supplied_to[] = {
	"battery",
};

static int msm_power_get_property(struct power_supply *psy,
				  enum power_supply_property psp,
				  union power_supply_propval *val)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		if (psy->type == POWER_SUPPLY_TYPE_MAINS) {
			val->intval = msm_batt_info.current_chg_source & AC_CHG
			    ? 1 : 0;
		}
		if (psy->type == POWER_SUPPLY_TYPE_USB) {
			val->intval = msm_batt_info.current_chg_source & USB_CHG
			    ? 1 : 0;
		}
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static struct power_supply msm_psy_ac = {
	.name = "ac",
	.type = POWER_SUPPLY_TYPE_MAINS,
	.supplied_to = msm_power_supplied_to,
	.num_supplicants = ARRAY_SIZE(msm_power_supplied_to),
	.properties = msm_power_props,
	.num_properties = ARRAY_SIZE(msm_power_props),
	.get_property = msm_power_get_property,
};

static struct power_supply msm_psy_usb = {
	.name = "usb",
	.type = POWER_SUPPLY_TYPE_USB,
	.supplied_to = msm_power_supplied_to,
	.num_supplicants = ARRAY_SIZE(msm_power_supplied_to),
	.properties = msm_power_props,
	.num_properties = ARRAY_SIZE(msm_power_props),
	.get_property = msm_power_get_property,
};

static enum power_supply_property msm_batt_power_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_TEMP,
};

static int msm_batt_power_get_property(struct power_supply *psy,
				       enum power_supply_property psp,
				       union power_supply_propval *val)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = msm_batt_info.batt_status;
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		val->intval = msm_batt_info.batt_health;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = msm_batt_info.batt_valid;
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = msm_batt_info.batt_technology;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
		val->intval = msm_batt_info.voltage_max_design;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN:
		val->intval = msm_batt_info.voltage_min_design;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
/* 2011-04-11 by hyuncheol0@lge.com
 * We use "voltage_now" attribute for the voltage of battery.
 * The unit of voltage_now is micro voltage.
 * So, we convert it here.
 */
		val->intval = (msm_batt_info.voltage_now)*1000;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = msm_batt_info.batt_capacity;
		break;
	case POWER_SUPPLY_PROP_TEMP:
/* 2011-04-11 by hyuncheol0@lge.com
 * We use "temp" attribute for the temperature of battery.
 */
		val->intval = msm_batt_info.batt_temp;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static struct power_supply msm_psy_batt = {
	.name = "battery",
	.type = POWER_SUPPLY_TYPE_BATTERY,
	.properties = msm_batt_power_props,
	.num_properties = ARRAY_SIZE(msm_batt_power_props),
	.get_property = msm_batt_power_get_property,
};

#define	be32_to_cpu_self(v)	(v = be32_to_cpu(v))

static int msm_batt_get_batt_chg_status(void)
{
	int rc;

	struct rpc_req_batt_chg {
		struct rpc_request_hdr hdr;
		u32 more_data;
	} req_batt_chg;
	struct rpc_reply_batt_chg_v1 *v1p;

	req_batt_chg.more_data = cpu_to_be32(1);

	memset(&rep_batt_chg, 0, sizeof(rep_batt_chg));

	v1p = &rep_batt_chg.v1;
	rc = msm_rpc_call_reply(msm_batt_info.chg_ep,
				ONCRPC_LG_CHG_GET_GENERAL_STATUS_PROC,
				&req_batt_chg, sizeof(req_batt_chg),
				&rep_batt_chg, sizeof(rep_batt_chg),
				msecs_to_jiffies(BATT_RPC_TIMEOUT));
	if (rc < 0) {
		pr_err("%s: ERROR. msm_rpc_call_reply failed! proc=%d rc=%d\n",
		       __func__, ONCRPC_LG_CHG_GET_GENERAL_STATUS_PROC, rc);
		return rc;
	} else if (be32_to_cpu(v1p->more_data)) {
		be32_to_cpu_self(v1p->battery_level);
		be32_to_cpu_self(v1p->voltage_now);
		be32_to_cpu_self(v1p->batt_valid_id);
		be32_to_cpu_self(v1p->batt_therm);
		be32_to_cpu_self(v1p->batt_temp);
		be32_to_cpu_self(v1p->battery_valid);
		be32_to_cpu_self(v1p->battery_charging);
//		v1p->charger_status=CHARGER_STATUS_INVALID;
		v1p->battery_status=BATTERY_STATUS_GOOD;
//		v1p->charger_type=CHARGER_TYPE_INVALID;

#if 0
		DBG_LIMIT("%s() \n ----- charger / battery status --------\n", __func__);
		DBG_LIMIT("\t battery_level=%d\n", v1p->battery_level);
		DBG_LIMIT("\t voltage_now=%d\n", v1p->voltage_now);
		DBG_LIMIT("\t batt_valid_id=%d\n", v1p->batt_valid_id);
		DBG_LIMIT("\t batt_therm=%d\n", v1p->batt_therm);
		DBG_LIMIT("\t batt_temp=%d\n", v1p->batt_temp);
		DBG_LIMIT("\t battery_valid=%d\n", v1p->battery_valid);
		DBG_LIMIT("\t battery_charging=%d\n", v1p->battery_charging);
		DBG_LIMIT("\t charger_status=%d\n", v1p->charger_status);
		DBG_LIMIT("\t battery_status=%d\n", v1p->battery_status);
		DBG_LIMIT("\t charger_type=%d\n", v1p->charger_type);
#endif
	} else {
		pr_err("%s: No battery/charger data in RPC reply\n", __func__);
		return -EIO;
	}

	return 0;
}

/* 2010-12-14 by baborobo@lge.com
 * if it is updateing of battery-status by rpc,
 * don't request updateing of battery-status
 */
static int is_run_batt_update = 0;
static void msm_batt_update_psy_status(void)
{
	u32	charger_status;
	u32	charger_type;
	u32	battery_status;
	u32	battery_level;
	u32 voltage_now;
	u32	batt_temp;
	u32 batt_therm;
	struct	power_supply	*supp;

  /* 2010-12-14 by baborobo@lge.com
   * to check the updating-status
   */
	if(is_run_batt_update)
		return;

	is_run_batt_update = 1;
	
	if (msm_batt_get_batt_chg_status())	{
		is_run_batt_update = 0;
		return;
	}

	charger_status = rep_batt_chg.v1.charger_status;
	charger_type = rep_batt_chg.v1.charger_type;
	battery_status = rep_batt_chg.v1.battery_status;
	battery_level = rep_batt_chg.v1.battery_level;
	voltage_now = rep_batt_chg.v1.voltage_now;
	batt_temp = rep_batt_chg.v1.batt_temp;
	batt_therm = rep_batt_chg.v1.batt_therm;

	if (battery_status == BATTERY_STATUS_INVALID &&
	    battery_level != BATTERY_LEVEL_INVALID) {
		DBG_LIMIT("BATT: change status(%d) to (%d) for level=%d\n",
			 battery_status, BATTERY_STATUS_GOOD, battery_level);
		battery_status = BATTERY_STATUS_GOOD;
	}

	if (msm_batt_info.charger_type != charger_type) {
		if (charger_type == CHARGER_TYPE_USB_WALL ||
		    charger_type == CHARGER_TYPE_USB_PC ||
		    charger_type == CHARGER_TYPE_USB_CARKIT) {
			DBG_LIMIT("BATT: USB charger plugged in\n");
			msm_batt_info.current_chg_source = USB_CHG;
			supp = &msm_psy_usb;
		} else if (charger_type == CHARGER_TYPE_WALL) {
			DBG_LIMIT("BATT: AC Wall changer plugged in\n");
			msm_batt_info.current_chg_source = AC_CHG;
			supp = &msm_psy_ac;
		} else {
			if (msm_batt_info.current_chg_source & AC_CHG)
				DBG_LIMIT("BATT: AC Wall charger removed\n");
			else if (msm_batt_info.current_chg_source & USB_CHG)
				DBG_LIMIT("BATT: USB charger removed\n");
			else
				DBG_LIMIT("BATT: No charger present\n");
			msm_batt_info.current_chg_source = 0;
			supp = &msm_psy_batt;

			/* Correct charger status */
			if (charger_status != CHARGER_STATUS_INVALID) {
				DBG_LIMIT("BATT: No charging!\n");
				charger_status = CHARGER_STATUS_INVALID;
				msm_batt_info.batt_status =
					POWER_SUPPLY_STATUS_NOT_CHARGING;
			}
		}
	} else
		supp = NULL;

	if (msm_batt_info.charger_status != charger_status) {
		if (charger_status == CHARGER_STATUS_GOOD ||
		    charger_status == CHARGER_STATUS_WEAK) {
			if (msm_batt_info.current_chg_source) {
		/* LGE_CHANGE
		 * add for Full charging
		 * 2010-05-04 baborobo@lge.com
		 */
			  if(battery_level == BATTERY_LEVEL_FULL)	{
					DBG_LIMIT("BATT: FULL.\n");
					msm_batt_info.batt_status =
						POWER_SUPPLY_STATUS_FULL;
				}else
				{
					DBG_LIMIT("BATT: Charging.\n");
					msm_batt_info.batt_status =
						POWER_SUPPLY_STATUS_CHARGING;
				}

				/* Correct when supp==NULL */
				if (msm_batt_info.current_chg_source & AC_CHG)
					supp = &msm_psy_ac;
				else
					supp = &msm_psy_usb;
			}
		/* LGE_CHANGE
		 * add for unpluged status of battery
		 * 2010-04-28 baborobo@lge.com
		 */
			if (battery_status == BATTERY_STATUS_INVALID	&&
				battery_level == BATTERY_LEVEL_INVALID) {	
				DBG_LIMIT("BATT: No Battery.\n");
				msm_batt_info.batt_status =
						POWER_SUPPLY_STATUS_UNKNOWN;
			}
		} else {
			DBG_LIMIT("BATT: No charging.\n");
			msm_batt_info.batt_status =
				POWER_SUPPLY_STATUS_NOT_CHARGING;
			supp = &msm_psy_batt;
		}
	} else {
		/* LGE_CHANGE
		 * add for unpluged status of battery
		 * 2010-04-07 baborobo@lge.com
		 */
		if (battery_status == BATTERY_STATUS_INVALID	&&
			battery_level == BATTERY_LEVEL_INVALID) {	
			DBG_LIMIT("BATT: No Battery\n");
			msm_batt_info.batt_status =
					POWER_SUPPLY_STATUS_UNKNOWN;
		} else
		/* Correct charger status */
		if (charger_type != CHARGER_TYPE_INVALID &&
		    charger_status == CHARGER_STATUS_GOOD) {
		/* LGE_CHANGE
		 * add for Full charging
		 * 2010-05-04 baborobo@lge.com
		 */
		  if(battery_level == BATTERY_LEVEL_FULL)	{
				DBG_LIMIT("BATT: FULL\n");
				msm_batt_info.batt_status =
					POWER_SUPPLY_STATUS_FULL;
			}else
			{
				DBG_LIMIT("BATT: In charging\n");
				msm_batt_info.batt_status =
					POWER_SUPPLY_STATUS_CHARGING;
			}
		}
	}

	if (battery_status == BATTERY_STATUS_INVALID) {
		if (battery_level != BATTERY_LEVEL_INVALID) {
			if (voltage_now >= msm_batt_info.voltage_min_design &&
			    voltage_now <= msm_batt_info.voltage_max_design) {
				DBG_LIMIT("BATT: Battery valid\n");
				msm_batt_info.batt_valid = 1;
				battery_status = BATTERY_STATUS_GOOD;
			}
			// LGE_CHANGE_S dangwoo.choi@lge.com
			else {
				voltage_now = 0;
			}
		}
	}

	if (msm_batt_info.battery_status != battery_status) {
		if (battery_status != BATTERY_STATUS_INVALID) {
			msm_batt_info.batt_valid = 1;

			if (battery_status == BATTERY_STATUS_BAD) {
				DBG_LIMIT("BATT: Battery bad.\n");
				msm_batt_info.batt_health =
					POWER_SUPPLY_HEALTH_DEAD;
			} else if (battery_status == BATTERY_STATUS_BAD_TEMP) {
				DBG_LIMIT("BATT: Battery overheat.\n");
				msm_batt_info.batt_health =
					POWER_SUPPLY_HEALTH_OVERHEAT;
			} else {
				DBG_LIMIT("BATT: Battery good.\n");
				msm_batt_info.batt_health =
					POWER_SUPPLY_HEALTH_GOOD;
			}
		} else {
			msm_batt_info.batt_valid = 0;
			DBG_LIMIT("BATT: Battery invalid.\n");
			msm_batt_info.batt_health = POWER_SUPPLY_HEALTH_UNKNOWN;
		}

		if (msm_batt_info.batt_status != POWER_SUPPLY_STATUS_CHARGING
			&& msm_batt_info.batt_status != POWER_SUPPLY_STATUS_FULL)
		{
			if (battery_status == BATTERY_STATUS_INVALID) {
				DBG_LIMIT("BATT: Battery -> unknown\n");
				msm_batt_info.batt_status =
					POWER_SUPPLY_STATUS_UNKNOWN;
			} else {
				DBG_LIMIT("BATT: Battery -> discharging\n");
				msm_batt_info.batt_status =
					POWER_SUPPLY_STATUS_DISCHARGING;
			}
		}

		if (!supp) {
			if (msm_batt_info.current_chg_source) {
				if (msm_batt_info.current_chg_source & AC_CHG)
					supp = &msm_psy_ac;
				else
					supp = &msm_psy_usb;
			} else
				supp = &msm_psy_batt;
		}
	}

	msm_batt_info.charger_status 	= charger_status;
	msm_batt_info.charger_type 	= charger_type;
	msm_batt_info.battery_status 	= battery_status;
	msm_batt_info.battery_level 	= battery_level;
	msm_batt_info.batt_temp 	= batt_temp;

	if (msm_batt_info.voltage_now != voltage_now) {
		msm_batt_info.voltage_now  	= voltage_now;
		msm_batt_info.batt_capacity =
			msm_batt_info.calculate_capacity(voltage_now);
		DBG_LIMIT("BATT: voltage = %u mV [capacity = %d%%]\n",
			 voltage_now, msm_batt_info.batt_capacity);

		if (!supp)
			supp = msm_batt_info.current_ps;
	}

	if (supp) {
		msm_batt_info.current_ps = supp;
		DBG_LIMIT("BATT: Supply = %s\n", supp->name);
		power_supply_changed(supp);
	}

  /* 2010-12-14 by baborobo@lge.com
   * to check the updating-status
   */
	is_run_batt_update = 0;
}

#ifdef CONFIG_HAS_EARLYSUSPEND
void msm_batt_early_suspend(struct early_suspend *h)
{
	pr_debug("%s: enter\n", __func__);

//	msm_batt_update_psy_status();

	pr_debug("%s: exit\n", __func__);
}

void msm_batt_late_resume(struct early_suspend *h)
{
	pr_debug("%s: enter\n", __func__);

	msm_batt_update_psy_status();

	pr_debug("%s: exit\n", __func__);
}
#endif

#if defined CONFIG_MACH_LGE && defined CONFIG_PM
static int msm_batt_suspend(struct platform_device *pdev, pm_message_t state)
{

	pr_debug(KERN_INFO "[msm_battery] %s()...\n", __func__);

	msm_batt_update_psy_status();

	return 0;
}

static int msm_batt_resume(struct platform_device *pdev)
{
	pr_debug(KERN_INFO "[msm_battery] %s()...\n", __func__);

	msm_batt_update_psy_status();
	return 0;
}
#endif //#if defined CONFIG_MACH_LGE && defined CONFIG_PM

static int msm_batt_cleanup(void)
{
	int rc = 0;

	if (msm_batt_info.msm_psy_ac)
		power_supply_unregister(msm_batt_info.msm_psy_ac);

	if (msm_batt_info.msm_psy_usb)
		power_supply_unregister(msm_batt_info.msm_psy_usb);
	if (msm_batt_info.msm_psy_batt)
		power_supply_unregister(msm_batt_info.msm_psy_batt);

	if (msm_batt_info.chg_ep) {
		rc = msm_rpc_close(msm_batt_info.chg_ep);
		if (rc < 0) {
			pr_err("%s: FAIL. msm_rpc_close(chg_ep). rc=%d\n",
			       __func__, rc);
		}
	}

#ifdef CONFIG_HAS_EARLYSUSPEND
	if (msm_batt_info.early_suspend.suspend == msm_batt_early_suspend)
		unregister_early_suspend(&msm_batt_info.early_suspend);
#endif
	return rc;
}

static u32 msm_batt_capacity(u32 current_voltage)
{
	u32 low_voltage = msm_batt_info.voltage_min_design;
	u32 high_voltage = msm_batt_info.voltage_max_design;

	if (current_voltage <= low_voltage)
		return 0;
	else if (current_voltage >= high_voltage)
		return 100;
	else
		return (current_voltage - low_voltage) * 100
			/ (high_voltage - low_voltage);
}

int msm_batt_get_charger_api_version(void)
{
	int rc ;
	struct rpc_reply_hdr *reply;

	struct rpc_req_chg_api_ver {
		struct rpc_request_hdr hdr;
		u32 more_data;
	} req_chg_api_ver;

	struct rpc_rep_chg_api_ver {
		struct rpc_reply_hdr hdr;
		u32 num_of_chg_api_versions;
		u32 *chg_api_versions;
	};

	u32 num_of_versions;

	struct rpc_rep_chg_api_ver *rep_chg_api_ver;


	req_chg_api_ver.more_data = cpu_to_be32(1);

	msm_rpc_setup_req(&req_chg_api_ver.hdr, CHG_RPC_PROG, CHG_RPC_VER_1_1,
			  ONCRPC_CHARGER_API_VERSIONS_PROC);

	rc = msm_rpc_write(msm_batt_info.chg_ep, &req_chg_api_ver,
			sizeof(req_chg_api_ver));
	if (rc < 0) {
		pr_err("%s: FAIL: msm_rpc_write. proc=0x%08x, rc=%d\n",
		       __func__, ONCRPC_CHARGER_API_VERSIONS_PROC, rc);
		return rc;
	}

	for (;;) {
		rc = msm_rpc_read(msm_batt_info.chg_ep, (void *) &reply, -1,
				BATT_RPC_TIMEOUT);
		if (rc < 0)
			return rc;
		if (rc < RPC_REQ_REPLY_COMMON_HEADER_SIZE) {
			pr_err("%s: LENGTH ERR: msm_rpc_read. rc=%d (<%d)\n",
			       __func__, rc, RPC_REQ_REPLY_COMMON_HEADER_SIZE);

			rc = -EIO;
			break;
		}
		/* we should not get RPC REQ or call packets -- ignore them */
		if (reply->type == RPC_TYPE_REQ) {
			pr_err("%s: TYPE ERR: type=%d (!=%d)\n",
			       __func__, reply->type, RPC_TYPE_REQ);
			kfree(reply);
			continue;
		}

		/* If an earlier call timed out, we could get the (no
		 * longer wanted) reply for it.	 Ignore replies that
		 * we don't expect
		 */
		if (reply->xid != req_chg_api_ver.hdr.xid) {
			pr_err("%s: XID ERR: xid=%d (!=%d)\n", __func__,
			       reply->xid, req_chg_api_ver.hdr.xid);
			kfree(reply);
			continue;
		}
		if (reply->reply_stat != RPCMSG_REPLYSTAT_ACCEPTED) {
			rc = -EPERM;
			break;
		}
		if (reply->data.acc_hdr.accept_stat !=
				RPC_ACCEPTSTAT_SUCCESS) {
			rc = -EINVAL;
			break;
		}

		rep_chg_api_ver = (struct rpc_rep_chg_api_ver *)reply;

		num_of_versions =
			be32_to_cpu(rep_chg_api_ver->num_of_chg_api_versions);

		rep_chg_api_ver->chg_api_versions =  (u32 *)
			((u8 *) reply + sizeof(struct rpc_reply_hdr) +
			sizeof(rep_chg_api_ver->num_of_chg_api_versions));

		rc = be32_to_cpu(
			rep_chg_api_ver->chg_api_versions[num_of_versions - 1]);

		pr_debug("%s: num_of_chg_api_versions = %u. "
			"The chg api version = 0x%08x\n", __func__,
			num_of_versions, rc);
		break;
	}
	kfree(reply);
	return rc;
}

static unsigned batt_volt;
static unsigned chg_curr_volt;
static unsigned batt_temp;
static unsigned batt_therm;
static unsigned batt_level;


static ssize_t msm_batt_volt_show(struct device* dev, struct device_attribute* attr, char* buf)
{
	batt_volt = msm_batt_info.voltage_now;
	return sprintf(buf,"%d\n", batt_volt);
}
static DEVICE_ATTR(batt_volt, S_IRUGO, msm_batt_volt_show, NULL);

static ssize_t msm_batt_therm_show(struct device* dev, struct device_attribute* attr, char* buf)
{
	batt_therm = msm_batt_info.batt_therm;
	return sprintf(buf,"%d\n", batt_therm);
}
static DEVICE_ATTR(batt_therm, S_IRUGO, msm_batt_therm_show, NULL);

static ssize_t msm_batt_chg_curr_volt_show(struct device* dev, struct device_attribute* attr, char* buf)
{
	chg_curr_volt = msm_batt_info.voltage_now;
	return sprintf(buf,"%d\n", chg_curr_volt);
}
static DEVICE_ATTR(chg_curr_volt, S_IRUGO, msm_batt_chg_curr_volt_show, NULL);

static ssize_t msm_batt_temp_show(struct device* dev, struct device_attribute* attr, char* buf)
{
	batt_temp = msm_batt_info.batt_temp;
	return sprintf(buf,"%d\n", batt_temp);
}
static DEVICE_ATTR(batt_temp, S_IRUGO, msm_batt_temp_show, NULL);

static ssize_t msm_batt_level_show(struct device* dev, struct device_attribute* attr, char* buf)
{
	batt_level = msm_batt_info.battery_level;
	return sprintf(buf,"%d\n", batt_level);
}
static DEVICE_ATTR(batt_level, S_IRUGO, msm_batt_level_show, NULL);

static struct attribute* dev_attrs_lge_batt_info[] = {
	&dev_attr_batt_volt.attr,
	&dev_attr_chg_curr_volt.attr,
	&dev_attr_batt_temp.attr,
	&dev_attr_batt_therm.attr,
	&dev_attr_batt_level.attr,	
	NULL,
};

static struct attribute_group dev_attr_grp_lge_batt_info = {
	.attrs = dev_attrs_lge_batt_info,
};

static int __devinit msm_batt_probe(struct platform_device *pdev)
{
	int rc;
	struct msm_psy_batt_pdata *pdata = pdev->dev.platform_data;

	if (pdev->id != -1) {
		dev_err(&pdev->dev,
			"%s: MSM chipsets Can only support one"
			" battery ", __func__);
		return -EINVAL;
	}

	if (pdata->avail_chg_sources & AC_CHG) {
		rc = power_supply_register(&pdev->dev, &msm_psy_ac);
		if (rc < 0) {
			dev_err(&pdev->dev,
				"%s: power_supply_register failed"
				" rc = %d\n", __func__, rc);
			msm_batt_cleanup();
			return rc;
		}
		msm_batt_info.msm_psy_ac = &msm_psy_ac;
		msm_batt_info.avail_chg_sources |= AC_CHG;
	}

	if (pdata->avail_chg_sources & USB_CHG) {
		rc = power_supply_register(&pdev->dev, &msm_psy_usb);
		if (rc < 0) {
			dev_err(&pdev->dev,
				"%s: power_supply_register failed"
				" rc = %d\n", __func__, rc);
			msm_batt_cleanup();
			return rc;
		}
		msm_batt_info.msm_psy_usb = &msm_psy_usb;
		msm_batt_info.avail_chg_sources |= USB_CHG;
	}

	if (!msm_batt_info.msm_psy_ac && !msm_batt_info.msm_psy_usb) {

		dev_err(&pdev->dev,
			"%s: No external Power supply(AC or USB)"
			"is avilable\n", __func__);
		msm_batt_cleanup();
		return -ENODEV;
	}

	msm_batt_info.voltage_max_design = pdata->voltage_max_design;
	msm_batt_info.voltage_min_design = pdata->voltage_min_design;
	msm_batt_info.batt_technology = pdata->batt_technology;
	msm_batt_info.calculate_capacity = pdata->calculate_capacity;

	if (!msm_batt_info.voltage_min_design)
		msm_batt_info.voltage_min_design = BATTERY_LOW;
	if (!msm_batt_info.voltage_max_design)
		msm_batt_info.voltage_max_design = BATTERY_HIGH;

	if (msm_batt_info.batt_technology == POWER_SUPPLY_TECHNOLOGY_UNKNOWN)
		msm_batt_info.batt_technology = POWER_SUPPLY_TECHNOLOGY_LION;

	if (!msm_batt_info.calculate_capacity)
		msm_batt_info.calculate_capacity = msm_batt_capacity;

	rc = power_supply_register(&pdev->dev, &msm_psy_batt);
	if (rc < 0) {
		dev_err(&pdev->dev, "%s: power_supply_register failed"
			" rc=%d\n", __func__, rc);
		msm_batt_cleanup();
		return rc;
	}
	msm_batt_info.msm_psy_batt = &msm_psy_batt;

#ifdef CONFIG_HAS_EARLYSUSPEND
	msm_batt_info.early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN;
	msm_batt_info.early_suspend.suspend = msm_batt_early_suspend;
	msm_batt_info.early_suspend.resume = msm_batt_late_resume;
	register_early_suspend(&msm_batt_info.early_suspend);
#endif
	msm_batt_update_psy_status();
		rc = sysfs_create_group(&pdev->dev.kobj, &dev_attr_grp_lge_batt_info);
		if(rc < 0) {
			dev_err(&pdev->dev,
				"%s: fail to create sysfs for lge batt info rc=%d\n", __func__, rc);
		}
	return 0;
}

static int __devexit msm_batt_remove(struct platform_device *pdev)
{
	int rc;
	
	sysfs_remove_group(&pdev->dev.kobj,&dev_attr_grp_lge_batt_info);

	rc = msm_batt_cleanup();

	if (rc < 0) {
		dev_err(&pdev->dev,
			"%s: msm_batt_cleanup  failed rc=%d\n", __func__, rc);
		return rc;
	}
	return 0;
}

static struct platform_driver msm_batt_driver = {
	.probe = msm_batt_probe,
	.remove = __devexit_p(msm_batt_remove),
#if defined CONFIG_MACH_LGE && defined CONFIG_PM
	.suspend = msm_batt_suspend,
	.resume = msm_batt_resume,
#endif
	.driver = {
		   .name = "msm-battery",
		   .owner = THIS_MODULE,
		   },
};

static int __devinit msm_batt_init_rpc(void)
{
	int rc;

		msm_batt_info.chg_ep = msm_rpc_connect_compatible(
				CHG_RPC_PROG, CHG_RPC_VER_1_1, 0);
		msm_batt_info.chg_api_version =  CHG_RPC_VER_1_1;
	if (IS_ERR(msm_batt_info.chg_ep)) {
		rc = PTR_ERR(msm_batt_info.chg_ep);
		pr_err("%s: FAIL: rpc connect for CHG_RPC_PROG. rc=%d\n",
		       __func__, rc);
		msm_batt_info.chg_ep = NULL;
		return rc;
	}

		msm_batt_info.chg_api_version = CHG_RPC_VER_1_1;
		msm_batt_info.batt_api_version =  BATTERY_RPC_VER_1_1;

	rc = platform_driver_register(&msm_batt_driver);

	if (rc < 0)
		pr_err("%s: FAIL: platform_driver_register. rc = %d\n",
		       __func__, rc);

	return rc;
}

static int __init msm_batt_init(void)
{
	int rc;

	pr_debug("%s: enter\n", __func__);

	rc = msm_batt_init_rpc();

	if (rc < 0) {
		pr_err("%s: FAIL: msm_batt_init_rpc.  rc=%d\n", __func__, rc);
		msm_batt_cleanup();
		return rc;
	}

	pr_info("%s: Charger/Battery = 0x%08x/0x%08x (RPC version)\n",
		__func__, msm_batt_info.chg_api_version,
		msm_batt_info.batt_api_version);

	return 0;
}

static void __exit msm_batt_exit(void)
{
	platform_driver_unregister(&msm_batt_driver);
}

module_init(msm_batt_init);
module_exit(msm_batt_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Kiran Kandi, Qualcomm Innovation Center, Inc.");
MODULE_DESCRIPTION("Battery driver for Qualcomm MSM chipsets.");
MODULE_VERSION("1.0");
MODULE_ALIAS("platform:msm_battery");
