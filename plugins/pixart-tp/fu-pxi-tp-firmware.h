/*
 * Copyright {{Year}} {{Author}} <{{Email}}>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_PXI_TP_FIRMWARE (fu_pxi_tp_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuPxiTpFirmware, fu_pxi_tp_firmware, FU, PXI_TP_FIRMWARE, FuFirmware)

FuFirmware *
fu_pxi_tp_firmware_new(void);
guint16
fu_pxi_tp_firmware_get_start_addr(FuPxiTpFirmware *self);
