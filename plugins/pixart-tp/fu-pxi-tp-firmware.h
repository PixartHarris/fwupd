// =============================================================
// file: fu-pxi-tp-firmware.h  (pure C)
// =============================================================
#pragma once

#include <fwupdplugin.h>

#include "fu-pxi-tp-struct.h"

#define FU_TYPE_PXI_TP_FIRMWARE (fu_pxi_tp_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuPxiTpFirmware, fu_pxi_tp_firmware, FU, PXI_TP_FIRMWARE, FuFirmware)

FuFirmware *
fu_pxi_tp_firmware_new(void);

/* Lightweight getters useful for the device plugin */
guint16
fu_pxi_tp_firmware_get_header_version(FuPxiTpFirmware *self);
guint16
fu_pxi_tp_firmware_get_file_version(FuPxiTpFirmware *self);
guint16
fu_pxi_tp_firmware_get_ic_part_id(FuPxiTpFirmware *self);
guint16
fu_pxi_tp_firmware_get_total_flash_sectors(FuPxiTpFirmware *self);
guint16
fu_pxi_tp_firmware_get_num_valid_sections(FuPxiTpFirmware *self);
const GPtrArray *
fu_pxi_tp_firmware_get_sections(FuPxiTpFirmware *self); /* element-type: FuPxiTpSection* */
GBytes *
fu_pxi_tp_firmware_get_slice_by_file(FuPxiTpFirmware *self,
				     gsize file_off,
				     gsize len,
				     GError **error);
GBytes *
fu_pxi_tp_firmware_get_slice_by_flash(FuPxiTpFirmware *self,
				      guint32 flash_addr,
				      gsize len,
				      GError **error);
