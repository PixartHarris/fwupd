/*
 * Copyright {{Year}} {{Author}} <{{Email}}>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-pxi-tp-common.h"
#include "fu-pxi-tp-device.h"
#include "fu-pxi-tp-firmware.h"
#include "fu-pxi-tp-register.h"
#include "fu-pxi-tp-struct.h"

/* this can be set using Flags=example in the quirk file  */
// #define FU_PXI_TP_DEVICE_FLAG_EXAMPLE "example"

struct _FuPxiTpDevice {
	FuHidrawDevice parent_instance;
	guint8 sram_select;
};

G_DEFINE_TYPE(FuPxiTpDevice, fu_pxi_tp_device, FU_TYPE_HIDRAW_DEVICE)

static gboolean
fu_pxi_tp_device_flash_execute(FuDevice *device,
			       guint8 inst_cmd,
			       guint32 ccr_cmd,
			       guint16 data_cnt,
			       GError **error)
{
	FuPxiTpDevice *self = FU_PXI_TP_DEVICE(device);

	g_message("fu_pxi_tp_device_flash_execute.");

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
		g_prefix_error(error, "Flash executes failure.");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_pxi_tp_device_flash_write_enable(FuDevice *device, GError **error)
{
	FuPxiTpDevice *self = FU_PXI_TP_DEVICE(device);

	g_message("fu_pxi_tp_device_flash_write_enable.");

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
		g_prefix_error(error, "Flash write enable failure.");
		g_message("Flash write enable failure.");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_pxi_tp_device_flash_wait_busy(FuDevice *device, GError **error)
{
	FuPxiTpDevice *self = FU_PXI_TP_DEVICE(device);

	g_message("fu_pxi_tp_device_flash_wait_busy.");

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
		g_prefix_error(error, "Flash wait busy failure.");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_pxi_tp_device_flash_erase_sector(FuDevice *device, guint8 sector_cnt, GError **error)
{
	FuPxiTpDevice *self = FU_PXI_TP_DEVICE(device);

	g_message("Start Erase Sector.");

	guint32 flash_address = (guint32)(sector_cnt) * 4096;

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

	g_message("Start Flash Program.");

	guint32 flash_address = (guint32)(sector) * 4096 + (guint32)(page) * 256;

	if (!fu_pxi_tp_device_flash_wait_busy(device, error))
		return FALSE;

	if (!fu_pxi_tp_device_flash_write_enable(device, error))
		return FALSE;

	WRITE_REG(0x04, 0x48, (flash_address >> 0) & 0xff);
	WRITE_REG(0x04, 0x49, (flash_address >> 8) & 0xff);
	WRITE_REG(0x04, 0x4a, (flash_address >> 16) & 0xff);
	WRITE_REG(0x04, 0x4b, (flash_address >> 24) & 0xff);

	if (!fu_pxi_tp_device_flash_execute(device, 0x84, 0x01002502, 256, error))
		return FALSE;

	g_message("Flash Program Compeleted.");

	return TRUE;
}

static gboolean
fu_pxi_tp_device_write_sram_256b(FuDevice *device, const guint8 *data, GError **error)
{
	FuPxiTpDevice *self = FU_PXI_TP_DEVICE(device);

	g_message("Write 256 bytes");

	WRITE_REG(0x06, 0x10, 0x00);
	WRITE_REG(0x06, 0x11, 0x00);

	WRITE_REG(0x06, 0x09, self->sram_select);

	WRITE_REG(0x06, 0x0a, 0x00);
	if (!fu_pxi_tp_register_burst_write(self, data, 256, error)) {
		g_prefix_error(error, "Burst write buffer failure.");
		g_message("Burst write buffer failure.");
		return FALSE;
	}
	WRITE_REG(0x06, 0x0a, 0x01);

	return TRUE;
}

static gboolean
fu_pxi_tp_device_update_flash_process(FuDevice *device,
				      guint32 data_size,
				      guint8 start_sector,
				      GByteArray *data,
				      GError **error)
{
	FuPxiTpDevice *self = FU_PXI_TP_DEVICE(device);

	g_message("fu_pxi_tp_device_update_flash_process");

	guint8 sector_cnt = 0;
	guint8 page_cnt = 0;
	guint16 offset = 0;
	guint8 max_sector_cnt = (data_size >> 12) + (((data_size & 0x00000fff) == 0) ? 0 : 1);

	g_message("test 1");

	WRITE_REG(0x01, 0x2c, 0xaa);
	fu_device_sleep(device, 30);
	WRITE_REG(0x01, 0x2d, 0xcc);

	fu_device_sleep(device, 10);

	WRITE_REG(0x02, 0x0d, 0x02);

	for (sector_cnt = 0; sector_cnt < max_sector_cnt; sector_cnt++) {
		if (!fu_pxi_tp_device_flash_erase_sector(device,
							 start_sector + sector_cnt,
							 error)) {
			g_prefix_error(error, "Flash erase failure.");
			g_message("Error: %s", (*error)->message);
			return FALSE;
		}
	}

	for (sector_cnt = 0; sector_cnt < max_sector_cnt; sector_cnt++) {
		for (page_cnt = 1; page_cnt < 16; page_cnt++) {
			offset = (sector_cnt * 4096) + (page_cnt * 256);

			g_message("offset: %u", offset);

			g_autoptr(GByteArray) buf = g_byte_array_new();

			g_byte_array_append(buf, &data->data[offset], 256);

			for (int i = 0; i < 256; i++) {
				g_message("Buffer: %02X", buf->data[i]);
			}

			if (!fu_pxi_tp_device_write_sram_256b(device, buf->data, error)) {
				g_prefix_error(error, "Write SRAM failure.");
				g_message("Error: %s", (*error)->message);
				return FALSE;
			}
			if (!fu_pxi_tp_device_flash_program_256b_to_flash(device,
									  start_sector + sector_cnt,
									  page_cnt,
									  error)) {
				g_prefix_error(error, "Flash program failure.");
				g_message("Error: %s", (*error)->message);
				return FALSE;
			}

			// fu_byte_array_set_size(buf->data, 256, 0xFF);

			// for (int i = 0; i < 256; i ++){
			// 	g_message("Clear Buffer: %02X", &buf->data[i]);
			// }
		}

		offset = sector_cnt * 4096;

		g_autoptr(GByteArray) buf = g_byte_array_new();

		g_byte_array_append(buf, &data->data[offset], 256);

		if (!fu_pxi_tp_device_write_sram_256b(device, buf->data, error)) {
			g_prefix_error(error, "Write SRAM failure.");
			g_message("Error: %s", (*error)->message);
			return FALSE;
		}
		if (!fu_pxi_tp_device_flash_program_256b_to_flash(device,
								  start_sector + sector_cnt,
								  0,
								  error)) {
			g_prefix_error(error, "Flash program failure.");
			g_message("Error: %s", (*error)->message);
			return FALSE;
		}
	}

	g_message("Engineer mode exit.");

	WRITE_REG(0x01, 0x2c, 0xaa);
	fu_device_sleep(device, 30);
	WRITE_REG(0x01, 0x2d, 0xbb);
}

static void
fu_pxi_tp_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuPxiTpDevice *self = FU_PXI_TP_DEVICE(device);
	// fwupd_codec_string_append_hex(str, idt, "StartAddr", self->start_addr);
}

static gboolean
fu_pxi_tp_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuPxiTpDevice *self = FU_PXI_TP_DEVICE(device);

	/* sanity check */
	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		g_debug("already in bootloader mode, skipping");
		return TRUE;
	}

	/* TODO: switch the device into bootloader mode */
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static gboolean
fu_pxi_tp_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuPxiTpDevice *self = FU_PXI_TP_DEVICE(device);

	/* sanity check */
	if (!fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		g_debug("already in runtime mode, skipping");
		return TRUE;
	}

	/* TODO: switch the device into runtime mode */
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static gboolean
fu_pxi_tp_device_reload(FuDevice *device, GError **error)
{
	FuPxiTpDevice *self = FU_PXI_TP_DEVICE(device);
	/* TODO: reprobe the hardware, or delete this vfunc to use ->setup() as a fallback */
	return TRUE;
}

static gboolean
fu_pxi_tp_device_probe(FuDevice *device, GError **error)
{
	FuPxiTpDevice *self = FU_PXI_TP_DEVICE(device);

	// /* FuHidrawDevice->probe */
	// if (!FU_DEVICE_CLASS(fu_pxi_tp_device_parent_class)->probe(device, error))
	// 	return FALSE;

	// /* TODO: probe the device for properties available before it is opened */
	// if (fu_device_has_private_flag(device, FU_PXI_TP_DEVICE_FLAG_EXAMPLE))
	// 	self->start_addr = 0x100;

	return TRUE;
}

static gboolean
fu_pxi_tp_device_setup(FuDevice *device, GError **error)
{
	FuPxiTpDevice *self = FU_PXI_TP_DEVICE(device);
	guint8 out_val;
	fu_pxi_tp_register_user_read(self, 0, 0, &out_val, error);
	g_message("out_val = 0x%02X (%u)", out_val, out_val);

	WRITE_REG(0x06, 0x72, 0xaa);
	fu_pxi_tp_register_read(self, 6, 0x72, &out_val, error);
	g_message("out_val = 0x%02X (%u)", out_val, out_val);

	g_autoptr(GByteArray) buf = g_byte_array_new();

	// fu_byte_array_set_size(buf, 4096, 0xAA);

	self->sram_select = 0x0f;

	// fu_pxi_tp_device_update_flash_process(device, 4096, 0, buf, error);
	// /* HidrawDevice->setup */
	// if (!FU_DEVICE_CLASS(fu_pxi_tp_device_parent_class)->setup(device, error))
	// 	return FALSE;

	/* TODO: get the version and other properties from the hardware while open */
	// 不同裝置的HID Version放在不同位置 可能需要 quirk 來區分
	guint16 ver_u16 = 0;
	g_autofree gchar *ver = g_strdup_printf("0x%04x", ver_u16); // 跟 format 對齊
	fu_device_set_version(device, ver);
	return TRUE;
}

static gboolean
fu_pxi_tp_device_prepare(FuDevice *device,
			 FuProgress *progress,
			 FwupdInstallFlags flags,
			 GError **error)
{
	FuPxiTpDevice *self = FU_PXI_TP_DEVICE(device);
	/* TODO: anything the device has to do before the update starts */
	return TRUE;
}

static gboolean
fu_pxi_tp_device_cleanup(FuDevice *device,
			 FuProgress *progress,
			 FwupdInstallFlags flags,
			 GError **error)
{
	FuPxiTpDevice *self = FU_PXI_TP_DEVICE(device);
	/* TODO: anything the device has to do when the update has completed */
	return TRUE;
}

static FuFirmware *
fu_pxi_tp_device_prepare_firmware(FuDevice *device,
				  GInputStream *stream,
				  FuProgress *progress,
				  FwupdInstallFlags flags,
				  GError **error)
{
	FuPxiTpDevice *self = FU_PXI_TP_DEVICE(device);
	g_autoptr(FuFirmware) firmware = fu_pxi_tp_firmware_new();

	/* TODO: you do not need to use this vfunc if not checking attributes */
	if (!fu_firmware_parse_stream(firmware, stream, 0x0, flags, error))
		return NULL;
	return g_steal_pointer(&firmware);
}

/* TODO: 把這個 stub 改成你的 HID 傳輸實作
 * 回傳 TRUE 表示這個 block 寫入成功
 */
static gboolean
fu_pxi_tp_device_write_block(FuPxiTpDevice *self,
			     guint32 flash_addr,
			     const guint8 *data,
			     gsize len,
			     GError **error)
{
	/* 這裡先做成乾跑/日誌：確保能連結通過；等你有傳輸格式再填 */
	g_debug("WRITE_BLOCK addr=0x%08x len=%zu", flash_addr, len);
	(void)self;
	(void)flash_addr;
	(void)data;
	(void)len;
	(void)error;
	return TRUE;
}

static gboolean
fu_pxi_tp_device_write_firmware(FuDevice *device,
				FuFirmware *firmware,
				FuProgress *progress,
				FwupdInstallFlags flags,
				GError **error)
{
	FuPxiTpDevice *self = FU_PXI_TP_DEVICE(device);

	/* 進度設定：寫入 90%、驗證 10%（你可調整權重） */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 90, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 10, NULL);

	/* 型別檢查：一定要是我們的 firmware container */
	if (!FU_IS_PXI_TP_FIRMWARE(firmware)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "expected FuPxiTpFirmware container");
		return FALSE;
	}
	FuPxiTpFirmware *ctn = FU_PXI_TP_FIRMWARE(firmware);

	/* 把要寫入的 bytes 拿出來（含 patches） */
	g_autoptr(GBytes) fw = fu_firmware_get_bytes_with_patches(firmware, error);
	if (fw == NULL)
		return FALSE;

	gsize fw_sz = 0;
	const guint8 *fw_data = g_bytes_get_data(fw, &fw_sz);

	/* 取 sections */
	const GPtrArray *secs = fu_pxi_tp_firmware_get_sections(ctn);
	if (secs == NULL || secs->len == 0) {
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "no sections to write");
		return FALSE;
	}

	/* 計算要寫入的總 bytes（只算 internal & valid） */
	gsize total_bytes = 0;
	for (guint i = 0; i < secs->len; i++) {
		FuPxiTpSection *s = g_ptr_array_index((GPtrArray *)secs, i);
		if (!s->is_valid_update || s->is_external)
			continue;
		if ((guint64)s->resolved_offset + (guint64)s->section_length > (guint64)fw_sz) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "section %u is out of file range",
				    i);
			return FALSE;
		}
		total_bytes += s->section_length;
	}

	if (total_bytes == 0) {
		/* 沒有需要寫入的資料，直接把兩個步驟結束 */
		fu_progress_step_done(progress); /* WRITE */
		fu_progress_step_done(progress); /* VERIFY */
		return TRUE;
	}

	/* 取「寫入步驟」的子進度，用百分比更新 */
	FuProgress *prog_write = fu_progress_get_child(progress);

	/* 依你的 HID payload 能力調整 chunk 大小 */
	const gboolean do_write = TRUE;

	/* 逐 section 寫入 */
	//     for (guint i = 0; i < secs->len; i++) {
	//         FuPxiTpSection *s = g_ptr_array_index((GPtrArray *)secs, i);
	//         if (!s->is_valid_update || s->is_external)
	//             continue;

	//         const guint8 *p = fw_data + s->resolved_offset;
	//         gsize remain = s->section_length;
	//         guint32 flash_addr = s->target_flash_start;
	// 	if (do_write) {
	// // 		static gboolean
	// // fu_pxi_tp_device_update_flash_process(FuDevice *device,
	// // 				      guint32 data_size,
	// // 				      guint8 start_sector,
	// // 				      GByteArray *data,
	// // 				      GError **error)

	// 	if (!fu_pxi_tp_device_update_flash_process(self, flash_addr, p, n, error)) {
	// 		g_prefix_error(error, "write section %u @0x%08x failed: ", i, flash_addr);
	// 		return FALSE;
	// 	}
	// 	} else {
	// 	g_debug("DRYRUN: would write section %u: addr=0x%08x len=%zu", i, flash_addr, n);
	// 	}
	//     }

	/* 完成寫入步驟 */
	fu_progress_step_done(progress);

	/* （可選）驗證步驟：如果有裝置端 CRC/摘要比對，請在這裡實作 */
	FuProgress *prog_verify = fu_progress_get_child(progress);
	(void)flags; /* 目前未使用，可依 flags 控制是否啟用 verify 等 */
	fu_progress_set_percentage(prog_verify, 100);
	fu_progress_step_done(progress);

	return TRUE;
}

static gboolean
fu_pxi_tp_device_set_quirk_kv(FuDevice *device,
			      const gchar *key,
			      const gchar *value,
			      GError **error)
{
	FuPxiTpDevice *self = FU_PXI_TP_DEVICE(device);

	// /* TODO: parse value from quirk file */
	// if (g_strcmp0(key, "PxiTpStartAddr") == 0) {
	// 	guint64 tmp = 0;
	// 	if (!fu_strtoull(value, &tmp, 0, G_MAXUINT16, FU_INTEGER_BASE_AUTO, error))
	// 		return FALSE;
	// 	self->start_addr = tmp;
	// 	return TRUE;
	// }

	return TRUE;
	/* failed */
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "quirk key not supported");
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
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_icon(FU_DEVICE(self), "icon-name");
}

static void
fu_pxi_tp_device_finalize(GObject *object)
{
	FuPxiTpDevice *self = FU_PXI_TP_DEVICE(object);

	/* TODO: free any allocated instance state here */
	G_OBJECT_CLASS(fu_pxi_tp_device_parent_class)->finalize(object);
}

static void
fu_pxi_tp_device_class_init(FuPxiTpDeviceClass *klass)
{
	g_message("fu_pxi_tp_device_class_init");
	// GObjectClass *object_class = G_OBJECT_CLASS(klass);
	// FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	// object_class->finalize = fu_pxi_device_finalize;
	// device_class->to_string = fu_pxi_device_to_string;
	// device_class->probe = fu_pxi_device_probe;
	// device_class->setup = fu_pxi_device_setup;
	// device_class->reload = fu_pxi_device_reload;
	// device_class->prepare = fu_pxi_device_prepare;
	// device_class->cleanup = fu_pxi_device_cleanup;
	// device_class->attach = fu_pxi_device_attach;
	// device_class->detach = fu_pxi_device_detach;
	// device_class->prepare_firmware = fu_pxi_device_prepare_firmware;
	// device_class->write_firmware = fu_pxi_device_write_firmware;
	// device_class->set_quirk_kv = fu_pxi_device_set_quirk_kv;
	// device_class->set_progress = fu_pxi_device_set_progress;
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	klass_device->probe = fu_pxi_tp_device_probe;
	klass_device->setup = fu_pxi_tp_device_setup;
	klass_device->write_firmware = fu_pxi_tp_device_write_firmware;
}
