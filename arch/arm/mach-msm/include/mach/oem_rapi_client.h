/* Copyright (c) 2009, Code Aurora Forum. All rights reserved.
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

#ifndef __ASM__ARCH_OEM_RAPI_CLIENT_H
#define __ASM__ARCH_OEM_RAPI_CLIENT_H

/*
 * OEM RAPI CLIENT Driver header file
 */

#include <linux/types.h>
#include <mach/msm_rpcrouter.h>

enum {
	OEM_RAPI_CLIENT_EVENT_NONE = 0,

	/*
	 * list of oem rapi client events
	 */

#if defined (CONFIG_LGE_SUPPORT_RAPI)
	/* LGE_CHANGES_S [khlee@lge.com] 2009-12-04, [VS740] use OEMRAPI */
	LG_FW_RAPI_START = 100,
	LG_FW_RAPI_CLIENT_EVENT_GET_LINE_TYPE = LG_FW_RAPI_START,
	LG_FW_TESTMODE_EVENT_FROM_ARM11 = LG_FW_RAPI_START + 1,
	LG_FW_A2M_BATT_INFO_GET = LG_FW_RAPI_START + 2,
	LG_FW_A2M_PSEUDO_BATT_INFO_SET = LG_FW_RAPI_START + 3,
	LG_FW_MEID_GET = LG_FW_RAPI_START + 4,
	//LGSI_LS670_Froyo_ToGB_Raghupathy_26Apr2011_Start
	LG_FW_SET_OPERATION_MODE = LG_FW_RAPI_START + 5,
	/* LGE_CHANGE_S 
	 * SUPPORT TESTMODE FOR AIRPLAN MODE
	 * 2010-07-12 taehung.kim@lge.com
	 */
	LG_FW_SET_OPERATIN_MODE = LG_FW_RAPI_START + 5,
/* LGE_CHANGES_S [woonghee.park@lge.com] 2010-05-18, [VS740], 
	 * LG_FW_CHARGING_TIMER
	 */
	LG_FW_SET_CHARGING_TIMER = LG_FW_RAPI_START + 6,
	LG_FW_GET_CHARGING_TIMER = LG_FW_RAPI_START + 7,
	/* LGE_CHANGES_E [woonghee.park@lge.com] */
	/* LGE_CHANGE [dojip.kim@lge.com] 2010-05-29, [LS670] PCB Version */
	LG_FW_GET_PCB_VERSION = LG_FW_RAPI_START + 8,
	/* LGE_CHANGE [dojip.kim@lge.com] 2010-05-29, [LS670] LG_FW_RTN_RESET */
	LG_FW_RAPI_CLIENT_EVENT_SET_RTN_RESET= LG_FW_RAPI_START + 9,
	/* LGE_CHANGE [dojip.kim@lge.com] 2010-08-09, [LS670] 
	 * no stop charging even if hot or cold battery 
	 */
	LG_FW_RAPI_CLIENT_EVENT_SET_THM_NO_STOP_CHARGING = LG_FW_RAPI_START + 10,
	/* LGE_CHANGE [dojip.kim@lge.com] 2010-08-9 */
	//LGSI_LS670_Froyo_ToGB_Raghupathy_26Apr2011_Start
	LG_FW_A2M_BLOCK_CHARGING_SET = LG_FW_RAPI_START + 11,
	/* LGE_CHANGE [james.jang@lge.com] 2010-08-25 */
	LG_FW_CIQ_EXCEPTION_ERROR_TEST = LG_FW_RAPI_START + 12,
	/* LGE_CHANGE [dojip.kim@lge.com] 2010-09-01 */
	LG_FW_SET_CHARGING_STAT_REALTIME_UPDATE = LG_FW_RAPI_START + 13,
	LG_FW_GET_CHARGING_STAT_REALTIME_UPDATE = LG_FW_RAPI_START + 14,
	//LGSI_LS670_Froyo_ToGB_Raghupathy_26Apr2011_End
	/* LGE_CHANGE [dojip.kim@lge.com] 2010-09-12, prl version */
	LG_FW_GET_PRL_VERSION = LG_FW_RAPI_START + 15,
	/* LGE_CHANGE [dojip.kim@lge.com] 2010-09-28, ftm boot */
	LG_FW_SET_FTM_BOOT = LG_FW_RAPI_START + 16,
	LG_FW_GET_FTM_BOOT = LG_FW_RAPI_START + 17,
#endif
	OEM_RAPI_CLIENT_EVENT_MAX

};

struct oem_rapi_client_streaming_func_cb_arg {
	uint32_t  event;
	void      *handle;
	uint32_t  in_len;
	char      *input;
	uint32_t out_len_valid;
	uint32_t output_valid;
	uint32_t output_size;
};

struct oem_rapi_client_streaming_func_cb_ret {
	uint32_t *out_len;
	char *output;
};

struct oem_rapi_client_streaming_func_arg {
	uint32_t event;
	int (*cb_func)(struct oem_rapi_client_streaming_func_cb_arg *,
		       struct oem_rapi_client_streaming_func_cb_ret *);
	void *handle;
	uint32_t in_len;
	char *input;
	uint32_t out_len_valid;
	uint32_t output_valid;
	uint32_t output_size;
};

struct oem_rapi_client_streaming_func_ret {
	uint32_t *out_len;
	char *output;
};

int oem_rapi_client_streaming_function(
	struct msm_rpc_client *client,
	struct oem_rapi_client_streaming_func_arg *arg,
	struct oem_rapi_client_streaming_func_ret *ret);

int oem_rapi_client_close(void);

struct msm_rpc_client *oem_rapi_client_init(void);

#endif
