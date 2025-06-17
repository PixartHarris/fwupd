/*
 * Copyright {{Year}} {{Author}} <{{Email}}>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-pxi-tp-common.h"

const gchar *
fu_pxi_tp_strerror(guint8 code)
{
	if (code == 0)
		return "success";
	return NULL;
}
