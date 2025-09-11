/*
 * Copyright {{Year}} {{Author}} <{{Email}}>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include "fu-pxi-tp-common.h"
#include "fu-pxi-tp-define.h"
#include "fu-pxi-tp-device.h"
#include "fu-pxi-tp-firmware.h"
#include "fu-pxi-tp-register.h"
#include "fu-pxi-tp-struct.h"

struct _FuPxiTpDevice {
	FuHidrawDevice parent_instance;
	guint8 sram_select;
	guint8 ver_bank;
	guint16 ver_addr;
};

G_DEFINE_TYPE(FuPxiTpDevice, fu_pxi_tp_device, FU_TYPE_HIDRAW_DEVICE)

/* 直接對 sysfs 檔案寫入（不能用 g_file_set_contents，sysfs 不接受暫存檔/rename） */
static gboolean
fu_pxi_tp_device_sysfs_write(const gchar *path, const gchar *val, GError **error)
{
	g_return_val_if_fail(path != NULL, FALSE);
	g_return_val_if_fail(val != NULL, FALSE);

	int fd = g_open(path, O_WRONLY | O_CLOEXEC, 0);
	if (fd < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_PERMISSION_DENIED,
			    "open %s: %s",
			    path,
			    g_strerror(errno));
		return FALSE;
	}

	gsize len = strlen(val); /* sysfs 通常不吃換行，寫純字串 */
	ssize_t rc = write(fd, val, len);
	int saved_errno = errno;
	close(fd);

	if (rc < 0 || (gsize)rc != len) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_PERMISSION_DENIED,
			    "write %s: %s",
			    path,
			    g_strerror(saved_errno));
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_pxi_tp_device_flash_execute(FuDevice *device,
			       guint8 inst_cmd,
			       guint32 ccr_cmd,
			       guint16 data_cnt,
			       GError **error)
{
	FuPxiTpDevice *self = FU_PXI_TP_DEVICE(device);

	guint8 out_val;

	WRITE_REG(0x04, 0x2c, inst_cmd);

	WRITE_REG(0x04, 0x40, (ccr_cmd >> 0) & 0xff);
	WRITE_REG(0x04, 0x41, (ccr_cmd >> 8) & 0xff);
	WRITE_REG(0x04, 0x42, (ccr_cmd >> 16) & 0xff);
	WRITE_REG(0x04, 0x43, (ccr_cmd >> 24) & 0xff);

	WRITE_REG(0x04, 0x44, (data_cnt >> 0) & 0xff);
	WRITE_REG(0x04, 0x45, (data_cnt >> 8) & 0xff);

	WRITE_REG(0x04, 0x56, 0x01);

	for (guint i = 0; i < 10; i++) {
		fu_device_sleep(device, 1);
		READ_REG(0x04, 0x56, &out_val);
		if (out_val == 0)
			break;
	}

	if (out_val != 0) {
		PXI_FAIL(error, FWUPD_ERROR, FWUPD_ERROR_WRITE, "Flash executes failure.");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_pxi_tp_device_flash_write_enable(FuDevice *device, GError **error)
{
	FuPxiTpDevice *self = FU_PXI_TP_DEVICE(device);
	guint8 out_val;

	if (!fu_pxi_tp_device_flash_execute(device, 0x00, 0x00000106, 0, error))
		return FALSE;

	for (guint i = 0; i < 10; i++) {
		if (!fu_pxi_tp_device_flash_execute(device, 0x01, 0x01000105, 1, error))
			return FALSE;
		fu_device_sleep(device, 1);
		READ_REG(0x04, 0x1c, &out_val);
		if ((out_val & 0x02) == 0x02)
			break;
	}

	if ((out_val & 0x02) != 0x02) {
		PXI_FAIL(error, FWUPD_ERROR, FWUPD_ERROR_WRITE, "Flash write enable failure.");
		g_message("Flash write enable failure.");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_pxi_tp_device_flash_wait_busy(FuDevice *device, GError **error)
{
	FuPxiTpDevice *self = FU_PXI_TP_DEVICE(device);

	guint8 out_val;

	for (guint i = 0; i < 1000; i++) {
		if (!fu_pxi_tp_device_flash_execute(device, 0x01, 0x01000105, 1, error))
			return FALSE;
		fu_device_sleep(device, 1);
		READ_REG(0x04, 0x1c, &out_val);
		if ((out_val & 0x01) == 0x00)
			break;
	}

	if ((out_val & 0x01) != 0x00) {
		PXI_FAIL(error, FWUPD_ERROR, FWUPD_ERROR_WRITE, "Flash wait busy failure.");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_pxi_tp_device_flash_erase_sector(FuDevice *device, guint8 sector, GError **error)
{
	FuPxiTpDevice *self = FU_PXI_TP_DEVICE(device);

	g_message("Start Erase Sector.");

	guint32 flash_address = (guint32)(sector) * 4096;

	if (!fu_pxi_tp_device_flash_wait_busy(device, error))
		return FALSE;

	if (!fu_pxi_tp_device_flash_write_enable(device, error))
		return FALSE;

	WRITE_REG(0x04, 0x48, (flash_address >> 0) & 0xff);
	WRITE_REG(0x04, 0x49, (flash_address >> 8) & 0xff);
	WRITE_REG(0x04, 0x4a, (flash_address >> 16) & 0xff);
	WRITE_REG(0x04, 0x4b, (flash_address >> 24) & 0xff);

	if (!fu_pxi_tp_device_flash_execute(device, 0x00, 0x00002520, 0, error))
		return FALSE;

	g_message("Start Erase Sector Completed.");

	return TRUE;
}

static gboolean
fu_pxi_tp_device_flash_program_256b_to_flash(FuDevice *device,
					     guint8 sector,
					     guint8 page,
					     GError **error)
{
	FuPxiTpDevice *self = FU_PXI_TP_DEVICE(device);

	guint32 flash_address = (guint32)(sector) * 4096 + (guint32)(page) * 256;

	if (!fu_pxi_tp_device_flash_wait_busy(device, error))
		return FALSE;

	if (!fu_pxi_tp_device_flash_write_enable(device, error))
		return FALSE;

	WRITE_REG(0x04, 0x2e, 0x00);
	WRITE_REG(0x04, 0x2f, 0x00);

	WRITE_REG(0x04, 0x48, (flash_address >> 0) & 0xff);
	WRITE_REG(0x04, 0x49, (flash_address >> 8) & 0xff);
	WRITE_REG(0x04, 0x4a, (flash_address >> 16) & 0xff);
	WRITE_REG(0x04, 0x4b, (flash_address >> 24) & 0xff);

	if (!fu_pxi_tp_device_flash_execute(device, 0x84, 0x01002502, 256, error))
		return FALSE;

	return TRUE;
}

static gboolean
fu_pxi_tp_device_write_sram_256b(FuDevice *device, const guint8 *data, GError **error)
{
	FuPxiTpDevice *self = FU_PXI_TP_DEVICE(device);

	WRITE_REG(0x06, 0x10, 0x00);
	WRITE_REG(0x06, 0x11, 0x00);

	WRITE_REG(0x06, 0x09, self->sram_select);

	WRITE_REG(0x06, 0x0a, 0x00);
	if (!fu_pxi_tp_register_burst_write(self, data, 256, error)) {
		PXI_FAIL(error, FWUPD_ERROR, FWUPD_ERROR_WRITE, "Burst write buffer failure.");
		g_message("Burst write buffer failure.");
		return FALSE;
	}
	WRITE_REG(0x06, 0x0a, 0x01);

	return TRUE;
}

static gboolean
fu_pxi_tp_device_reset(FuDevice *device, guint8 key1, guint8 key2, GError **error)
{
	FuPxiTpDevice *self = FU_PXI_TP_DEVICE(device);

	WRITE_REG(0x01, 0x2c, key1);
	fu_device_sleep(device, 30);
	WRITE_REG(0x01, 0x2d, key2);

	if (key2 == 0xbb) {
		fu_device_sleep(device, 500);
	} else {
		fu_device_sleep(device, 10);
	}

	return TRUE;
}

static gboolean
fu_pxi_tp_device_firmware_clear(FuDevice *device, GError **error)
{
	guint32 start_address = fu_pxi_tp_firmware_get_firmware_address(device);

	if (!fu_pxi_tp_device_flash_erase_sector(device, (start_address / 4096), error)) {
		PXI_FAIL(error, FWUPD_ERROR, FWUPD_ERROR_WRITE, "Clear firmware failure.");
		g_message("Error: %s", (*error)->message);
		return FALSE;
	}
}

static guint32
fu_pxi_tp_device_crc_firmware(FuDevice *device, GError **error)
{
	FuPxiTpDevice *self = FU_PXI_TP_DEVICE(device);

	guint8 out_val = 0;
	guint8 swap_flag = 0;
	guint16 part_id = 0;
	guint32 return_value = 0;

	READ_REG(0x04, 0x29, &out_val);
	swap_flag = out_val;
	READ_REG(0x00, 0x78, &out_val);
	part_id = out_val;
	READ_REG(0x00, 0x79, &out_val);
	part_id += out_val << 8;

	switch (part_id) {
	case 0x0274:
		if (swap_flag) {
			fu_pxi_tp_register_user_write(self, 0x00, 0x82, 0x10, error);
		} else {
			fu_pxi_tp_register_user_write(self, 0x00, 0x82, 0x02, error);
		}
		break;
	default:
		fu_pxi_tp_register_user_write(self, 0x00, 0x82, 0x02, error);
		break;
	}

	for (guint i = 0; i < 1000; i++) {
		fu_device_sleep(device, 10);
		fu_pxi_tp_register_user_read(self, 0x00, 0x82, &out_val, error);
		if ((out_val & 0x01) == 0x00)
			break;
	}

	fu_pxi_tp_register_user_read(self, 0x00, 0x84, &out_val, error);
	return_value += out_val;
	fu_pxi_tp_register_user_read(self, 0x00, 0x85, &out_val, error);
	return_value += out_val << 8;
	fu_pxi_tp_register_user_read(self, 0x00, 0x86, &out_val, error);
	return_value += out_val << 16;
	fu_pxi_tp_register_user_read(self, 0x00, 0x87, &out_val, error);
	return_value += out_val << 24;

	g_message("Firmware CRC: 0x%08x", (guint)return_value);

	return return_value;
}

static guint32
fu_pxi_tp_device_crc_parameter(FuDevice *device, GError **error)
{
	FuPxiTpDevice *self = FU_PXI_TP_DEVICE(device);

	guint8 out_val = 0;
	guint8 swap_flag = 0;
	guint16 part_id = 0;
	guint32 return_value = 0;

	READ_REG(0x04, 0x29, &out_val);
	swap_flag = out_val;
	READ_REG(0x00, 0x78, &out_val);
	part_id = out_val;
	READ_REG(0x00, 0x79, &out_val);
	part_id += out_val << 8;

	switch (part_id) {
	case 0x0274:
		if (swap_flag) {
			fu_pxi_tp_register_user_write(self, 0x00, 0x82, 0x20, error);
		} else {
			fu_pxi_tp_register_user_write(self, 0x00, 0x82, 0x04, error);
		}
		break;
	default:
		fu_pxi_tp_register_user_write(self, 0x00, 0x82, 0x02, error);
		break;
	}

	for (guint i = 0; i < 1000; i++) {
		fu_device_sleep(device, 10);
		fu_pxi_tp_register_user_read(self, 0x00, 0x82, &out_val, error);
		if ((out_val & 0x01) == 0x00)
			break;
	}

	fu_pxi_tp_register_user_read(self, 0x00, 0x84, &out_val, error);
	return_value += out_val;
	fu_pxi_tp_register_user_read(self, 0x00, 0x85, &out_val, error);
	return_value += out_val << 8;
	fu_pxi_tp_register_user_read(self, 0x00, 0x86, &out_val, error);
	return_value += out_val << 16;
	fu_pxi_tp_register_user_read(self, 0x00, 0x87, &out_val, error);
	return_value += out_val << 24;

	g_message("Parameter CRC: 0x%08x", (guint)return_value);

	return return_value;
}

static gboolean
fu_pxi_tp_device_update_flash_process(FuDevice *device,
				      guint32 data_size,
				      guint8 start_sector,
				      GByteArray *data,
				      GError **error)
{
	FuPxiTpDevice *self = FU_PXI_TP_DEVICE(device);

	// g_message("fu_pxi_tp_device_update_flash_process");

	guint8 sector_cnt = 0;
	guint8 page_cnt = 0;
	guint16 offset = 0;
	guint8 max_sector_cnt = (data_size >> 12) + (((data_size & 0x00000fff) == 0) ? 0 : 1);

	WRITE_REG(0x02, 0x0d, 0x02);

	for (sector_cnt = 0; sector_cnt < max_sector_cnt; sector_cnt++) {
		if (!fu_pxi_tp_device_flash_erase_sector(device,
							 start_sector + sector_cnt,
							 error)) {
			PXI_FAIL(error,
				 FWUPD_ERROR,
				 FWUPD_ERROR_WRITE,
				 "Burst write buffer failure.");
			g_message("Error: %s", (*error)->message);
			return FALSE;
		}
	}

	for (sector_cnt = 0; sector_cnt < max_sector_cnt; sector_cnt++) {
		for (page_cnt = 1; page_cnt < 16; page_cnt++) {
			offset = (sector_cnt * 4096) + (page_cnt * 256);

			// g_message("offset: %u", offset);
			g_autoptr(GByteArray) buf = g_byte_array_new();
			gsize remain = data->len > offset ? data->len - offset : 0;
			gsize copy_len = remain < 256 ? remain : 256;

			if (copy_len == 0)
				break;

			g_byte_array_append(buf, &data->data[offset], copy_len);

			/* pad to 256 with 0xFF if needed */
			if (copy_len < 256) {
				guint8 pad[256];
				memset(pad, 0xFF, sizeof(pad));
				g_byte_array_append(buf, pad, 256 - copy_len);
			}

			if (!fu_pxi_tp_device_write_sram_256b(device, buf->data, error)) {
				PXI_FAIL(error,
					 FWUPD_ERROR,
					 FWUPD_ERROR_WRITE,
					 "Write SRAM failure.");
				g_message("Error: %s", (*error)->message);
				return FALSE;
			}
			if (!fu_pxi_tp_device_flash_program_256b_to_flash(device,
									  start_sector + sector_cnt,
									  page_cnt,
									  error)) {
				PXI_FAIL(error,
					 FWUPD_ERROR,
					 FWUPD_ERROR_WRITE,
					 "Flash program failure.");
				g_message("Error: %s", (*error)->message);
				return FALSE;
			}
		}

		offset = sector_cnt * 4096;

		g_autoptr(GByteArray) buf = g_byte_array_new();
		gsize remain = data->len > offset ? data->len - offset : 0;
		gsize copy_len = remain < 256 ? remain : 256;

		if (copy_len == 0)
			break;

		g_byte_array_append(buf, &data->data[offset], copy_len);

		/* pad to 256 with 0xFF if needed */
		if (copy_len < 256) {
			guint8 pad[256];
			memset(pad, 0xFF, sizeof(pad));
			g_byte_array_append(buf, pad, 256 - copy_len);
		}

		if (!fu_pxi_tp_device_write_sram_256b(device, buf->data, error)) {
			PXI_FAIL(error, FWUPD_ERROR, FWUPD_ERROR_WRITE, "Write SRAM failure.");
			g_message("Error: %s", (*error)->message);
			return FALSE;
		}
		if (!fu_pxi_tp_device_flash_program_256b_to_flash(device,
								  start_sector + sector_cnt,
								  0,
								  error)) {
			PXI_FAIL(error, FWUPD_ERROR, FWUPD_ERROR_WRITE, "Flash program failure.");
			g_message("Error: %s", (*error)->message);
			return FALSE;
		}
	}

	return TRUE;
}

static void
fu_pxi_tp_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuPxiTpDevice *self = FU_PXI_TP_DEVICE(device);
	// fwupd_codec_string_append_hex(str, idt, "StartAddr", self->start_addr);
}

static gboolean
fu_pxi_tp_device_probe(FuDevice *device, GError **error)
{
	return TRUE;
}

static gboolean
fu_pxi_tp_device_setup(FuDevice *device, GError **error)
{
	FuPxiTpDevice *self = FU_PXI_TP_DEVICE(device);

	/* sram_select 已可由 quirk 覆寫，無需強制設預設值 */
	guint8 lo = 0, hi = 0;

	READ_USR_REG(self->ver_bank, self->ver_addr + 0, &lo);
	READ_USR_REG(self->ver_bank, self->ver_addr + 1, &hi);

	guint16 ver_u16 = (guint16)lo | ((guint16)hi << 8); /* low byte first */

	/* 全部印出來看（含中間值） */
	g_message("PXI-TP setup: read version bytes: lo=0x%02x hi=0x%02x (LE) -> ver=0x%04x",
		  (guint)lo,
		  (guint)hi,
		  (guint)ver_u16);

	g_autofree gchar *ver_str = g_strdup_printf("0x%04x", ver_u16);
	fu_device_set_version(device, ver_str);
	return TRUE;
}

/* helper：generic FuFirmware → FuPxiTpFirmware（用 parse_stream；零拷貝） */
static FuPxiTpFirmware *
fu_pxi_tp_device_wrap_or_parse_ctn(FuFirmware *maybe_generic, GError **error)
{
	if (FU_IS_PXI_TP_FIRMWARE(maybe_generic))
		return FU_PXI_TP_FIRMWARE(maybe_generic);

	g_autoptr(GBytes) bytes = fu_firmware_get_bytes_with_patches(maybe_generic, error);
	if (bytes == NULL)
		return NULL;

	g_autoptr(GInputStream) mis = g_memory_input_stream_new_from_bytes(bytes);
	FuFirmware *ctn = FU_FIRMWARE(g_object_new(FU_TYPE_PXI_TP_FIRMWARE, NULL));
	if (!fu_firmware_parse_stream(ctn, mis, 0, FU_FIRMWARE_PARSE_FLAG_NONE, error)) {
		PXI_FAIL(error, FWUPD_ERROR, FU_FIRMWARE_PARSE_FLAG_NONE, "pxi-tp parse failed: ");
		g_object_unref(ctn);
		return NULL;
	}
	return FU_PXI_TP_FIRMWARE(ctn);
}

static gboolean
fu_pxi_tp_device_write_firmware(FuDevice *device,
				FuFirmware *firmware,
				FuProgress *progress,
				FwupdInstallFlags flags,
				GError **error)
{
	FuPxiTpDevice *self = FU_PXI_TP_DEVICE(device);

	/* 進度配置：寫入 90%、驗證 10% */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 90, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 10, NULL);

	/* 確保拿到 FuPxiTpFirmware */
	FuPxiTpFirmware *ctn = fu_pxi_tp_device_wrap_or_parse_ctn(firmware, error);
	if (ctn == NULL)
		return FALSE;

	const GPtrArray *secs = fu_pxi_tp_firmware_get_sections(ctn);
	if (secs == NULL || secs->len == 0) {
		PXI_FAIL(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE, "no sections to write");
		return FALSE;
	}

	/* 統計總 bytes（只算 internal & valid） */
	guint64 total_bytes = 0;
	for (guint i = 0; i < secs->len; i++) {
		FuPxiTpSection *s = g_ptr_array_index((GPtrArray *)secs, i);
		if (s->is_valid_update && !s->is_external && s->section_length > 0)
			total_bytes += (guint64)s->section_length;
	}
	if (total_bytes == 0) {
		PXI_FAIL(error,
			 FWUPD_ERROR,
			 FWUPD_ERROR_INVALID_FILE,
			 "no internal/valid sections to write");
		return FALSE;
	}

	FuProgress *prog_write = fu_progress_get_child(progress);
	//     const gchar *dry = g_getenv("PIXART_TP_DRYRUN");
	const gboolean do_write = TRUE;

	/* 逐段寫入 ———— 一定要用 resolved_offset！！！ */
	guint64 written = 0;
	for (guint i = 0; i < secs->len; i++) {
		FuPxiTpSection *s = g_ptr_array_index((GPtrArray *)secs, i);
		if (!s->is_valid_update || s->is_external || s->section_length == 0)
			continue;

		if (s->internal_file_start > G_MAXSIZE) {
			PXI_FAIL(error,
				 FWUPD_ERROR,
				 FWUPD_ERROR_INVALID_FILE,
				 "section %u file offset too large",
				 i);
			return FALSE;
		}
		/* 用 resolved_offset 切 payload（回傳 GByteArray*） */
		g_autoptr(GByteArray) data =
		    fu_pxi_tp_firmware_get_slice_by_file(ctn,
							 (gsize)s->internal_file_start,
							 (gsize)s->section_length,
							 error);
		if (data == NULL)
			return FALSE;

		guint8 start_sector = (guint8)(s->target_flash_start / 4096);

		g_message("PXI-TP: write section %u: flash=0x%08x, file_off=0x%08" G_GINT64_MODIFIER
			  "x, "
			  "len=%u, sector=%u, data_len=%u",
			  i,
			  s->target_flash_start,
			  (gint64)s->internal_file_start,
			  (guint)s->section_length,
			  start_sector,
			  (guint)data->len);

		if (do_write) {
			if (data->len == 0) {
				PXI_FAIL(error,
					 FWUPD_ERROR,
					 FWUPD_ERROR_INVALID_FILE,
					 "empty payload for section %u",
					 i);
				return FALSE;
			}
			if (!fu_pxi_tp_device_update_flash_process(self,
								   (guint)s->section_length,
								   start_sector,
								   data,
								   error)) {
				PXI_FAIL(error,
					 FWUPD_ERROR,
					 FWUPD_ERROR_WRITE,
					 "write section failed.");
				return FALSE;
			}
		} else {
			g_debug("DRYRUN: would write section %u (%u bytes) to sectors from %u",
				i,
				(guint)data->len,
				start_sector);
		}

		written += (guint64)s->section_length;
		guint pct = (written >= total_bytes) ? 100 : (guint)((written * 100) / total_bytes);
		fu_progress_set_percentage(prog_write, pct);
	}

	fu_progress_step_done(progress);

	/* CRC Check */
	if (!fu_pxi_tp_device_reset(device, 0xaa, 0xcc, error)) {
		return FALSE;
	}

	guint32 crc_value = fu_pxi_tp_device_crc_firmware(device, error);
	if (crc_value != fu_pxi_tp_firmware_get_file_firmware_crc(self)) {
		fu_pxi_tp_device_firmware_clear(device, error);
		return FALSE;
	}

	crc_value = fu_pxi_tp_device_crc_parameter(device, error);
	if (crc_value != fu_pxi_tp_firmware_get_file_parameter_crc(self)) {
		fu_pxi_tp_device_firmware_clear(device, error);
		return FALSE;
	}

	/* Verify（佔 10%）—你之後可改成真實的讀回/CRC 比對 */
	FuProgress *prog_verify = fu_progress_get_child(progress);
	(void)flags;
	fu_progress_set_percentage(prog_verify, 100);
	fu_progress_step_done(progress);

	return TRUE;
}

/* ===== 3) quirk 解析：解析到就印出 ===== */
static gboolean
fu_pxi_tp_device_set_quirk_kv(FuDevice *device,
			      const gchar *key,
			      const gchar *value,
			      GError **error)
{
	g_message("Set Quirk");
	FuPxiTpDevice *self = FU_PXI_TP_DEVICE(device);

	if (g_strcmp0(key, "HidVersionReg") == 0) {
		/* 允許格式： bank=0x00; addr=0x0b; （size/endian 省略，固定 2 bytes LE） */
		g_auto(GStrv) parts = g_strsplit(value, ";", -1);
		for (guint i = 0; parts && parts[i]; i++) {
			gchar *kv = g_strstrip(parts[i]);
			if (*kv == '\0')
				continue;
			g_auto(GStrv) kvp = g_strsplit(kv, "=", 2);
			if (!kvp[0] || !kvp[1])
				continue;

			const gchar *k = g_strstrip(kvp[0]);
			const gchar *v = g_strstrip(kvp[1]);
			guint64 tmp = 0;

			if (g_ascii_strcasecmp(k, "bank") == 0) {
				if (!fu_strtoull(v, &tmp, 0, 0xff, FU_INTEGER_BASE_AUTO, error))
					return FALSE;
				self->ver_bank = (guint8)tmp;
			} else if (g_ascii_strcasecmp(k, "addr") == 0) {
				if (!fu_strtoull(v, &tmp, 0, 0xffff, FU_INTEGER_BASE_AUTO, error))
					return FALSE;
				self->ver_addr = (guint16)tmp;
			} else {
				/* 忽略未知子鍵（或你也可選擇報錯） */
			}
		}

		g_message("quirk: HidVersionReg parsed => bank=0x%02x addr=0x%04x",
			  (guint)self->ver_bank,
			  (guint)self->ver_addr);
		return TRUE;
	}

	if (g_strcmp0(key, "SramSelect") == 0) {
		guint64 tmp = 0;
		if (!fu_strtoull(value, &tmp, 0, 0xff, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		self->sram_select = (guint8)tmp;
		g_message("quirk: SramSelect parsed => 0x%02x", (guint)self->sram_select);
		return TRUE;
	}

	/* 其他鍵不是本裝置支援的 quirk */
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_SUPPORTED,
		    "quirk key not supported: %s",
		    key);
	return FALSE;
}

static void
fu_pxi_tp_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 57, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 43, "reload");
}

static void
fu_pxi_tp_device_init(FuPxiTpDevice *self)
{
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_HEX);
	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
	fu_device_add_protocol(FU_DEVICE(self), "com.pixart.tp");
	fu_device_set_summary(FU_DEVICE(self), "Touchpad");
	fu_device_add_icon(FU_DEVICE(self), "input-touchpad");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);

	/* 預設值（可被 quirk 覆寫） */
	self->sram_select = 0x0f;
	self->ver_bank = 0x00;
	self->ver_addr = 0x0b;
}

static void
fu_pxi_tp_device_finalize(GObject *object)
{
	FuPxiTpDevice *self = FU_PXI_TP_DEVICE(object);

	/* TODO: free any allocated instance state here */
	G_OBJECT_CLASS(fu_pxi_tp_device_parent_class)->finalize(object);
}

/* 一個 udev 節點做一次 unbind/bind（依照該節點的 driver 與 subsystem） */
static gboolean
fu_pxi_tp_device_rebind_one(FuDevice *dev_self, FuUdevDevice *udev_node, GError **error)
{
	const gchar *sysfs = fu_udev_device_get_sysfs_path(udev_node);
	if (sysfs == NULL) {
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "no sysfs path");
		return FALSE;
	}

	/* /sys/.../driver -> /sys/bus/<subsystem>/drivers/<driver> */
	g_autofree gchar *drv_symlink = g_build_filename(sysfs, "driver", NULL);
	g_autofree gchar *drv_path = g_file_read_link(drv_symlink, NULL);
	if (drv_path == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "no 'driver' symlink for %s",
			    sysfs);
		return FALSE;
	}

	/* 要寫進 bind/unbind 檔案的字串，就是這個節點最後一段名稱 */
	g_autofree gchar *dev_name = g_path_get_basename(sysfs);
	g_autofree gchar *path_unbind = g_build_filename(drv_path, "unbind", NULL);
	g_autofree gchar *path_bind = g_build_filename(drv_path, "bind", NULL);

	/* 先關掉目前的 hidraw fd（best-effort） */
	(void)fu_device_close(dev_self, NULL);

	/* 解除綁定 → 等一下 → 再綁回去 */
	if (!fu_pxi_tp_device_sysfs_write(path_unbind, dev_name, error))
		return FALSE;

	fu_device_sleep(dev_self, 200); /* ms */

	if (!fu_pxi_tp_device_sysfs_write(path_bind, dev_name, error))
		return FALSE;

	/* 重新開啟 & 跑 setup 以便重抓 descriptor/暫存器 */
	if (!fu_device_open(dev_self, error))
		return FALSE;

	return fu_pxi_tp_device_setup(dev_self, error);
}

/* 對某個父節點做 driver/unbind+bind；node_dev 由
 * fu_device_get_backend_parent_with_subsystem(...) 取得 */
static gboolean
fu_pxi_tp_device_rebind_node_from_dev(FuDevice *device, FuDevice *node_dev, GError **error)
{
	FuUdevDevice *node = FU_UDEV_DEVICE(node_dev);
	const gchar *sysfs = fu_udev_device_get_sysfs_path(node);
	const gchar *drv = fu_udev_device_get_driver(node);
	const gchar *subs = fu_udev_device_get_subsystem(node);
	g_autofree gchar *dev_name = NULL;
	g_autofree gchar *path_unbind = NULL;
	g_autofree gchar *path_bind = NULL;

	if (sysfs == NULL || drv == NULL || subs == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "node missing sysfs/driver/subsystem");
		return FALSE;
	}

	dev_name = g_path_get_basename(sysfs);
	path_unbind = g_build_filename("/sys/bus", subs, "drivers", drv, "unbind", NULL);
	path_bind = g_build_filename("/sys/bus", subs, "drivers", drv, "bind", NULL);

	g_message("rebind(bus): subsystem=%s driver=%s dev=%s", subs, drv, dev_name);

	/* 告訴 engine：我要 replug，請等我 */
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);

	/* best-effort：先關掉現有 fd，避免競爭；失敗不致命 */
	(void)fu_device_close(device, NULL);

	/* 先解除綁定 → 小睡一下 → 再綁回去 */
	if (!fu_pxi_tp_device_sysfs_write(path_unbind, dev_name, error))
		return FALSE;

	fu_device_sleep(device, 200); /* ms，視需要可調大到 500ms */

	if (!fu_pxi_tp_device_sysfs_write(path_bind, dev_name, error))
		return FALSE;

	/* 重要：此處不要再 open/setup；讓 fwupd 依 WAIT_FOR_REPLUG 流程自己重建裝置 */
	return TRUE;
}

static gboolean
fu_pxi_tp_device_transport_rebind(FuDevice *device, GError **error)
{
	g_message("rebind: self=%s", fu_udev_device_get_sysfs_path(FU_UDEV_DEVICE(device)));

	/* 1) HID */
	{
		g_autoptr(FuDevice) hid_dev =
		    fu_device_get_backend_parent_with_subsystem(device, "hid", NULL);
		if (hid_dev != NULL) {
			if (fu_pxi_tp_device_rebind_node_from_dev(device, hid_dev, error))
				return TRUE;
			g_clear_error(error);
		}
	}
	/* 2) I2C */
	{
		g_autoptr(FuDevice) i2c_dev =
		    fu_device_get_backend_parent_with_subsystem(device, "i2c", NULL);
		if (i2c_dev != NULL) {
			if (fu_pxi_tp_device_rebind_node_from_dev(device, i2c_dev, error))
				return TRUE;
			g_clear_error(error);
		}
	}
	/* 3) USB */
	{
		g_autoptr(FuDevice) usb_dev =
		    fu_device_get_backend_parent_with_subsystem(device, "usb", NULL);
		if (usb_dev != NULL) {
			if (fu_pxi_tp_device_rebind_node_from_dev(device, usb_dev, error))
				return TRUE;
			g_clear_error(error);
		}
	}

	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_SUPPORTED,
		    "no suitable parent (hid/i2c/usb) to rebind");
	return FALSE;
}

static gboolean
fu_pxi_tp_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	if (!fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER))
		return TRUE;

	FuPxiTpDevice *self = FU_PXI_TP_DEVICE(device);

	if (!fu_pxi_tp_device_reset(device, 0xaa, 0xbb, error)) {
		return FALSE;
	}

	fu_device_remove_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	g_message("Exit Bootloader");
	return TRUE;
}

static gboolean
fu_pxi_tp_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER))
		return TRUE;

	FuPxiTpDevice *self = FU_PXI_TP_DEVICE(device);

	if (!fu_pxi_tp_device_reset(device, 0xaa, 0xcc, error)) {
		return FALSE;
	}

	fu_pxi_tp_device_firmware_clear(device, error);

	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	g_message("Enter Bootloader");
	return TRUE;
}

static gboolean
fu_pxi_tp_device_cleanup(FuDevice *device,
			 FuProgress *progress,
			 FwupdInstallFlags flags,
			 GError **error)
{
	g_message("fu_pxi_tp_device_cleanup");
	FuPxiTpDevice *self = FU_PXI_TP_DEVICE(device);

	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		if (!fu_pxi_tp_device_reset(device, 0xaa, 0xbb, error)) {
			return FALSE;
		}
		fu_device_remove_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
		g_message("Exit Bootloader");
	}

	/* 觸發 transport rebind → 讓 fwupd 等 replug → 重新取 report descriptor */
	if (!fu_pxi_tp_device_transport_rebind(device, error)) {
		g_message("transport rebind failed: %s",
			  (error && *error) ? (*error)->message : "unknown");
		g_clear_error(error); /* cleanup 盡量收斂，不要讓錯冒出來 */
	}

	return TRUE; /* cleanup 盡量不傳錯 */
}

static void
fu_pxi_tp_device_class_init(FuPxiTpDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	klass_device->probe = fu_pxi_tp_device_probe;
	klass_device->setup = fu_pxi_tp_device_setup;
	klass_device->write_firmware = fu_pxi_tp_device_write_firmware;
	klass_device->attach = fu_pxi_tp_device_attach;
	klass_device->detach = fu_pxi_tp_device_detach;
	klass_device->cleanup = fu_pxi_tp_device_cleanup;
	klass_device->set_progress = fu_pxi_tp_device_set_progress;
	klass_device->set_quirk_kv = fu_pxi_tp_device_set_quirk_kv;
}
