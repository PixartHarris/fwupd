/*
 * Copyright {{Year}} {{Author}} <{{Email}}>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-pxi-tp-firmware.h"
#include "fu-pxi-tp-struct.h"

struct _FuPxiTpFirmware {
	FuFirmware parent_instance;
	guint16 start_addr;
};

G_DEFINE_TYPE(FuPxiTpFirmware, fu_pxi_tp_firmware, FU_TYPE_FIRMWARE)

static void
fu_pxi_tp_firmware_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuPxiTpFirmware *self = FU_PXI_TP_FIRMWARE(firmware);
	fu_xmlb_builder_insert_kx(bn, "start_addr", self->start_addr);
}

static gboolean
fu_pxi_tp_firmware_build(FuFirmware *firmware, XbNode *n, GError **error)
{
	FuPxiTpFirmware *self = FU_PXI_TP_FIRMWARE(firmware);
	guint64 tmp;

	/* TODO: load from .builder.xml */
	tmp = xb_node_query_text_as_uint(n, "start_addr", NULL);
	if (tmp != G_MAXUINT64 && tmp <= G_MAXUINT16)
		self->start_addr = tmp;

	/* success */
	return TRUE;
}

static gboolean
fu_pxi_tp_firmware_validate(FuFirmware *firmware,
			    GInputStream *stream,
			    gsize offset,
			    GError **error)
{
	// return fu_struct_pxi_tp_header_validate_stream(stream,
	// 					     offset,
	// 					     error);
	/* success */
	return TRUE;
}

static gboolean
fu_pxi_tp_firmware_parse(FuFirmware *firmware,
			 GInputStream *stream,
			 FuFirmwareParseFlags flags,
			 GError **error)
{
	FuPxiTpFirmware *self = FU_PXI_TP_FIRMWARE(firmware);
	g_autoptr(GByteArray) st_hdr = NULL;

	/* TODO: parse firmware into images */
	// st_hdr = fu_struct_pxi_tp_hdr_parse_stream(stream, 0x0, error);
	// if (st_hdr == NULL)
	// 	return FALSE;
	// self->start_addr = 0x1234;
	fu_firmware_set_version(firmware, "1.2.3");
	// fu_firmware_set_bytes(firmware, fw);
	return TRUE;
}

static GByteArray *
fu_pxi_tp_firmware_write(FuFirmware *firmware, GError **error)
{
	FuPxiTpFirmware *self = FU_PXI_TP_FIRMWARE(firmware);
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GBytes) fw = NULL;

	/* data first */
	fw = fu_firmware_get_bytes_with_patches(firmware, error);
	if (fw == NULL)
		return NULL;

	/* TODO: write to the buffer with the correct format */
	fu_byte_array_append_bytes(buf, fw);

	/* success */
	return g_steal_pointer(&buf);
}

guint16
fu_pxi_tp_firmware_get_start_addr(FuPxiTpFirmware *self)
{
	g_return_val_if_fail(FU_IS_PXI_TP_FIRMWARE(self), G_MAXUINT16);
	return self->start_addr;
}

static void
fu_pxi_tp_firmware_init(FuPxiTpFirmware *self)
{
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_STORED_SIZE);
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_CHECKSUM);
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_VID_PID);
}

static void
fu_pxi_tp_firmware_class_init(FuPxiTpFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->validate = fu_pxi_tp_firmware_validate;
	firmware_class->parse = fu_pxi_tp_firmware_parse;
	firmware_class->write = fu_pxi_tp_firmware_write;
	firmware_class->build = fu_pxi_tp_firmware_build;
	firmware_class->export = fu_pxi_tp_firmware_export;
}

FuFirmware *
fu_pxi_tp_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_PXI_TP_FIRMWARE, NULL));
}
