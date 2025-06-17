/*
 * Copyright {{Year}} {{Author}} <{{Email}}>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_PXI_TP_DEVICE (fu_pxi_tp_device_get_type())
G_DECLARE_FINAL_TYPE(FuPxiTpDevice, fu_pxi_tp_device, FU, PXI_TP_DEVICE, FuHidrawDevice)

guint16
fu_pxi_tp_device_get_start_addr(FuPxiTpDevice *self);
