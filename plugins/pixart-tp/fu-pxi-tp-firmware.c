// =============================================================
// file: fu-pxi-tp-firmware.c (pure C, offset-based parsing; no packed structs)
// =============================================================
#include "config.h"

#include <fwupdplugin.h>

#include "fu-pxi-tp-firmware.h"
#include "fu-pxi-tp-struct.h"

struct _FuPxiTpFirmware {
	FuFirmware parent_instance;

	/* header fields (host-endian) */
	guint16 header_len;
	guint16 header_ver;
	guint16 file_ver;
	guint16 ic_part_id;
	guint16 flash_sectors;
	guint32 file_crc32;
	guint32 header_crc32;
	guint16 num_sections; /* as stored */

	/* parsed sections */
	GPtrArray *sections; /* FuPxiTpSection*, free with g_free */
};

G_DEFINE_TYPE(FuPxiTpFirmware, fu_pxi_tp_firmware, FU_TYPE_FIRMWARE)

/* ------------------------- helpers ------------------------- */
/**
 * _fu_trim_ascii_nul:
 * @dst: destination buffer containing a fixed-width ASCII field
 * @dstsz: total size of @dst in bytes
 *
 * Ensures the buffer is a valid C string by forcing a NUL terminator at
 * the last byte and trimming any trailing spaces or NULs from the end.
 * Useful for on-disk fixed-length fields that may be padded.
 */
static void
_fu_trim_ascii_nul(char *dst, size_t dstsz)
{
	if (dstsz == 0)
		return;
	dst[dstsz - 1] = '\0'; /* guarantee termination */

	for (gssize i = (gssize)dstsz - 2; i >= 0; i--) {
		if (dst[i] == '\0' || dst[i] == ' ')
			dst[i] = '\0'; /* strip trailing space or redundant NULs */
		else
			break; /* stop at first non-space content */
	}
}

/**
 * _fu_resolve_internal_offset:
 * @internal_file_start: offset reported in the section header
 * @section_len: length of the section payload
 * @header_len: total header length (bytes)
 * @file_size: total file size (bytes)
 * @resolved: (out) absolute file offset on success
 *
 * Converts a section's "internal file start" to an absolute file offset.
 * Some files store this as an absolute offset-from-file-start, others as
 * an offset-from-end-of-header. We try both and validate bounds.
 *
 * Returns: %TRUE and sets @resolved if a valid interpretation is found.
 */
static gboolean
_fu_resolve_internal_offset(guint64 internal_file_start,
			    guint64 section_len,
			    guint64 header_len,
			    guint64 file_size,
			    guint64 *resolved)
{
	if (internal_file_start + section_len <= file_size) { /* absolute */
		*resolved = internal_file_start;
		return TRUE;
	}
	if (internal_file_start + section_len <= (file_size - header_len)) { /* after header */
		*resolved = header_len + internal_file_start;
		return TRUE;
	}
	return FALSE;
}

/**
 * fu_pxi_tp_firmware_finalize:
 * @obj: instance to finalize
 *
 * GObject finalizer: releases the sections array and chains up to the
 * parent finalize. No other resources are owned.
 */
static void
fu_pxi_tp_firmware_finalize(GObject *obj)
{
	FuPxiTpFirmware *self = FU_PXI_TP_FIRMWARE(obj);
	if (self->sections != NULL) {
		g_ptr_array_unref(self->sections); /* safe: has free_func=g_free */
		self->sections = NULL;
	}
	G_OBJECT_CLASS(fu_pxi_tp_firmware_parent_class)->finalize(obj);
}

/* ---------------------- FuFirmware vfuncs ------------------ */
/**
 * fu_pxi_tp_firmware_validate:
 * @firmware: the firmware container object
 * @stream: unused (fwupd vfunc signature)
 * @offset: vfunc signature; we only accept 0
 * @error: return location for a #GError, or %NULL
 *
 * Quick sanity checks to determine whether a blob looks like a PixArt
 * FWHD v1.0 container. Verifies magic string and header length.
 *
 * Returns: %TRUE if the blob looks parseable, %FALSE otherwise.
 */
static gboolean
fu_pxi_tp_firmware_validate(FuFirmware *firmware,
			    GInputStream *stream,
			    gsize offset,
			    GError **error)
{
	/* we only support offset 0; quiet FALSE on others */
	if (offset != 0)
		return FALSE;

	/* read just the fixed header */
	g_autoptr(GBytes) hdr =
	    fu_input_stream_read_bytes(stream, offset, PXI_TP_HEADER_V1_LEN, NULL, error);
	if (hdr == NULL)
		return FALSE;

	gsize hdrsz = g_bytes_get_size(hdr);
	const guint8 *d = g_bytes_get_data(hdr, NULL);

	/* magic */
	if (memcmp(d + PXI_TP_O_MAGIC, PXI_TP_MAGIC, 4) != 0)
		return FALSE;

	/* header length (LE16) -- 注意參數順序：&hdrlen 在前，endian 在後 */
	guint16 hdrlen = 0;
	if (!fu_memread_uint16_safe(d, hdrsz, PXI_TP_O_HDRLEN, &hdrlen, G_LITTLE_ENDIAN, NULL))
		return FALSE;

	if (hdrlen != PXI_TP_HEADER_V1_LEN)
		return FALSE;

	return TRUE;
}

/**
 * fu_pxi_tp_firmware_parse:
 * @firmware: the firmware container object
 * @stream: unused (fwupd vfunc signature)
 * @flags: parse flags
 * @error: return location for a #GError, or %NULL
 *
 * Parses the FWHD v1.0 header, verifies header CRC32 and payload CRC32,
 * and decodes up to 8 section descriptors into a private array. For
 * internal sections, computes absolute file offsets for later flashing.
 * Exposes header fields as firmware metadata and sets the visible version.
 *
 * Returns: %TRUE on success, %FALSE with @error set on failure.
 */
static gboolean
fu_pxi_tp_firmware_parse(FuFirmware *firmware,
			 GInputStream *stream,
			 FuFirmwareParseFlags flags,
			 GError **error)
{
	g_message("fu_pxi_tp_firmware_parse");
	FuPxiTpFirmware *self = FU_PXI_TP_FIRMWARE(firmware);

	/* read the entire blob through the fwupd streaming helper */
	g_autoptr(GBytes) fw = fu_input_stream_read_bytes(stream, 0, G_MAXSIZE, NULL, error);
	if (fw == NULL)
		return FALSE;

	const guint8 *d = g_bytes_get_data(fw, NULL);
	gsize sz = g_bytes_get_size(fw);

	/* lightweight checks again (with errors reported here) */
	if (sz < PXI_TP_HEADER_V1_LEN) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "file too small: %" G_GSIZE_FORMAT " bytes (need >= %u)",
			    sz,
			    (guint)PXI_TP_HEADER_V1_LEN);
		return FALSE;
	}
	if (memcmp(d + PXI_TP_O_MAGIC, PXI_TP_MAGIC, 4) != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "bad magic, expected 'FWHD'");
		return FALSE;
	}

	g_message("/* --- parse fixed header (all via safe readers) --- */");
	/* --- parse fixed header (all via safe readers) --- */
	if (!fu_memread_uint16_safe(d,
				    sz,
				    PXI_TP_O_HDRLEN,
				    &self->header_len,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;
	if (self->header_len != PXI_TP_HEADER_V1_LEN || self->header_len > sz ||
	    self->header_len < 4) {
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE, "Header Length Error");
		return FALSE;
	}

	if (!fu_memread_uint16_safe(d,
				    sz,
				    PXI_TP_O_HDRVER,
				    &self->header_ver,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;
	if (!fu_memread_uint16_safe(d,
				    sz,
				    PXI_TP_O_FILEVER,
				    &self->file_ver,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;
	if (!fu_memread_uint16_safe(d,
				    sz,
				    PXI_TP_O_PARTID,
				    &self->ic_part_id,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;
	if (!fu_memread_uint16_safe(d,
				    sz,
				    PXI_TP_O_SECTORS,
				    &self->flash_sectors,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;
	if (!fu_memread_uint32_safe(d,
				    sz,
				    PXI_TP_O_TOTALCRC,
				    &self->file_crc32,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;
	if (!fu_memread_uint16_safe(d,
				    sz,
				    PXI_TP_O_NUMSECTIONS,
				    &self->num_sections,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;

	g_message("/* header CRC is at the tail of header: PXI_TP_O_HDRCRC(header_len) */");
	/* header CRC is at the tail of header: PXI_TP_O_HDRCRC(header_len) */
	if (!fu_memread_uint32_safe(d,
				    sz,
				    PXI_TP_O_HDRCRC(self->header_len),
				    &self->header_crc32,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;

	if (sz < self->header_len) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "truncated header: %" G_GSIZE_FORMAT " < %u",
			    sz,
			    (guint)self->header_len);
		return FALSE;
	}
	if (self->num_sections > PXI_TP_MAX_SECTIONS) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "num_sections %u exceeds max %u",
			    (guint)self->num_sections,
			    (guint)PXI_TP_MAX_SECTIONS);
		return FALSE;
	}
	g_message("/* --- verify header CRC32: [0 .. header_len-4) --- */");
	/* --- verify header CRC32: [0 .. header_len-4) --- */
	if ((flags & FU_FIRMWARE_PARSE_FLAG_IGNORE_CHECKSUM) == 0) {
		guint32 hdr_crc_calc = fu_crc32(FU_CRC_KIND_B32_STANDARD, d, self->header_len - 4);
		if (hdr_crc_calc != self->header_crc32) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "header CRC mismatch, got 0x%08x, expected 0x%08x",
				    (guint)hdr_crc_calc,
				    (guint)self->header_crc32);
			return FALSE;
		}
	}
	g_message("/* --- verify payload CRC32: [header_len .. end) --- */");
	/* --- verify payload CRC32: [header_len .. end) --- */
	if ((flags & FU_FIRMWARE_PARSE_FLAG_IGNORE_CHECKSUM) == 0 && sz > self->header_len) {
		guint32 file_crc_calc =
		    fu_crc32(FU_CRC_KIND_B32_STANDARD, d + self->header_len, sz - self->header_len);
		if (file_crc_calc != self->file_crc32) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "file CRC mismatch, got 0x%08x, expected 0x%08x",
				    (guint)file_crc_calc,
				    (guint)self->file_crc32);
			return FALSE;
		}
	}
	g_message("/* --- parse section headers --- */");
	/* --- parse section headers --- */
	if (self->sections != NULL)
		g_ptr_array_set_size(self->sections, 0);
	else
		self->sections = g_ptr_array_new_with_free_func(g_free);

	const guint64 sectab_end = (guint64)PXI_TP_O_SECTIONS_BASE +
				   (guint64)self->num_sections * (guint64)PXI_TP_SECTION_SIZE;

	if (sectab_end > self->header_len) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "section table exceeds header: need %" G_GUINT64_FORMAT
			    " bytes within header_len=%u",
			    sectab_end - (guint64)PXI_TP_O_SECTIONS_BASE,
			    (guint)self->header_len);
		return FALSE;
	}

	for (guint i = 0; i < self->num_sections; i++) {
		const guint64 sec_off =
		    (guint64)PXI_TP_O_SECTIONS_BASE + (guint64)i * PXI_TP_SECTION_SIZE;
		if (sec_off + PXI_TP_SECTION_SIZE > self->header_len) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "section %u header out of range",
				    i);
			return FALSE;
		}

		const guint8 *sec = d + PXI_TP_O_SECTIONS_BASE + i * PXI_TP_SECTION_SIZE;
		FuPxiTpSection *s = g_new0(FuPxiTpSection, 1);

		s->update_type = sec[PXI_TP_S_O_TYPE];
		s->update_info = sec[PXI_TP_S_O_INFO];
		s->is_valid_update = (s->update_info & PXI_TP_UI_VALID) != 0;
		s->is_external = (s->update_info & PXI_TP_UI_EXTERNAL) != 0;

		/* numeric fields via safe reads using absolute offsets into d */
		if (!fu_memread_uint32_safe(
			d,
			sz,
			(PXI_TP_O_SECTIONS_BASE + i * PXI_TP_SECTION_SIZE + PXI_TP_S_O_FLASHADDR),
			&s->target_flash_start,
			G_LITTLE_ENDIAN,
			error)) {
			g_free(s);
			return FALSE;
		}
		if (!fu_memread_uint32_safe(
			d,
			sz,
			(PXI_TP_O_SECTIONS_BASE + i * PXI_TP_SECTION_SIZE + PXI_TP_S_O_INTSTART),
			&s->internal_file_start,
			G_LITTLE_ENDIAN,
			error)) {
			g_free(s);
			return FALSE;
		}
		if (!fu_memread_uint32_safe(
			d,
			sz,
			(PXI_TP_O_SECTIONS_BASE + i * PXI_TP_SECTION_SIZE + PXI_TP_S_O_INTLEN),
			&s->section_length,
			G_LITTLE_ENDIAN,
			error)) {
			g_free(s);
			return FALSE;
		}

		/* copy fixed-width external filename safely */
		const gsize dstcap = sizeof(s->external_file_name);
		const gsize want = (PXI_TP_S_EXTNAME_LEN < (dstcap > 0 ? dstcap - 1 : 0))
				       ? PXI_TP_S_EXTNAME_LEN
				       : (dstcap > 0 ? dstcap - 1 : 0);
		if (!fu_memcpy_safe(s->external_file_name,
				    dstcap,
				    0,
				    sec,
				    PXI_TP_SECTION_SIZE,
				    PXI_TP_S_O_EXTNAME,
				    want,
				    error)) {
			g_free(s);
			return FALSE;
		}
		if (dstcap > 0)
			s->external_file_name[want] = '\0';
		_fu_trim_ascii_nul(s->external_file_name, dstcap);

		if (!s->is_external && s->is_valid_update) {
			guint64 resolved = 0;
			if (!_fu_resolve_internal_offset(s->internal_file_start,
							 s->section_length,
							 self->header_len,
							 sz,
							 &resolved)) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_FILE,
					    "section %u out of range (off=0x%" G_GINT64_MODIFIER
					    "x, len=0x%" G_GINT64_MODIFIER "x)",
					    i,
					    (gint64)s->internal_file_start,
					    (gint64)s->section_length);
				g_free(s);
				return FALSE;
			}
			s->resolved_offset = resolved;
		}

		if (s->update_type != PXI_TP_UPDATE_TYPE_GENERAL &&
		    s->update_type != PXI_TP_UPDATE_TYPE_TF_FORCE) {
			g_debug("pxi-tp: unknown update_type %u for section %u", s->update_type, i);
		}

		g_ptr_array_add(self->sections, s);
	}

	/* expose header values as standard metadata */
	g_autofree gchar *ver = g_strdup_printf("0x%04x", self->file_ver);
	fu_firmware_set_version(firmware, ver);

	/* your container size is the whole file */
	fu_firmware_set_size(firmware, sz);
	return TRUE;
}

/* ===== helpers for export ===== */

static const char *
fu_pxi_tp_firmware_update_type_to_str(guint8 t)
{
	switch (t) {
	case PXI_TP_UPDATE_TYPE_GENERAL:
		return "GENERAL";
	case PXI_TP_UPDATE_TYPE_TF_FORCE:
		return "TF_FORCE";
	default:
		return "UNKNOWN";
	}
}

static gchar *
fu_pxi_tp_firmware_update_info_to_flags(guint8 ui)
{
	GString *s = g_string_new(NULL);
	if (ui & PXI_TP_UI_VALID)
		g_string_append(s, "VALID|");
	if (ui & PXI_TP_UI_EXTERNAL)
		g_string_append(s, "EXTERNAL|");
	if (s->len > 0)
		s->str[s->len - 1] = '\0';
	return s->str[0] ? g_string_free(s, FALSE) : (g_string_free(s, TRUE), g_strdup("0"));
}

/* insert both hex (kx) and decimal (kv ... _dec) */
static void
fu_pxi_tp_firmware_kx_and_dec(XbBuilderNode *bn, const char *key, guint64 v)
{
	fu_xmlb_builder_insert_kx(bn, key, v); /* hex */
	g_autofree gchar *kdec = g_strdup_printf("%s_dec", key);
	g_autofree gchar *vdec = g_strdup_printf("%" G_GUINT64_FORMAT, v);
	fu_xmlb_builder_insert_kv(bn, kdec, vdec);
}

static gchar *
fu_pxi_tp_firmware_hexdump_slice(const guint8 *p, gsize n, gsize maxbytes)
{
	gsize m = n < maxbytes ? n : maxbytes;
	if (m == 0)
		return g_strdup("");
	GString *g = g_string_sized_new(m * 3);
	for (gsize i = 0; i < m; i++)
		g_string_append_printf(g, "%02X%s", p[i], (i + 1 == m) ? "" : " ");
	return g_string_free(g, FALSE);
}

/* ===== expanded export ===== */

static void
fu_pxi_tp_firmware_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuPxiTpFirmware *self = FU_PXI_TP_FIRMWARE(firmware);

	/* bytes for optional recompute / hexdumps */
	g_autoptr(GBytes) fw = fu_firmware_get_bytes_with_patches(firmware, NULL);
	const guint8 *d = fw ? g_bytes_get_data(fw, NULL) : NULL;
	gsize sz = fw ? g_bytes_get_size(fw) : 0;

	/* top-level identity and sizes */
	fu_xmlb_builder_insert_kv(bn, "magic", "FWHD");
	fu_pxi_tp_firmware_kx_and_dec(bn, "file_size", sz);
	fu_pxi_tp_firmware_kx_and_dec(bn, "header_length", self->header_len);
	/* payload size = total - header_len, bound to 0 */
	fu_pxi_tp_firmware_kx_and_dec(bn,
				      "payload_size",
				      (sz > self->header_len) ? (sz - self->header_len) : 0);

	/* header core fields */
	fu_pxi_tp_firmware_kx_and_dec(bn, "header_version", self->header_ver);
	fu_pxi_tp_firmware_kx_and_dec(bn, "file_version", self->file_ver);
	fu_pxi_tp_firmware_kx_and_dec(bn, "ic_part_id", self->ic_part_id);
	fu_pxi_tp_firmware_kx_and_dec(bn, "flash_sectors", self->flash_sectors);
	fu_pxi_tp_firmware_kx_and_dec(bn, "num_sections", self->num_sections);

	/* CRCs (stored) */
	fu_pxi_tp_firmware_kx_and_dec(bn, "header_crc32", self->header_crc32);
	fu_pxi_tp_firmware_kx_and_dec(bn, "file_crc32", self->file_crc32);

	/* CRCs (recomputed) + status */
	if (d != NULL && self->header_len >= 4 && self->header_len <= sz) {
		guint32 hdr_crc_calc = fu_crc32(FU_CRC_KIND_B32_STANDARD, d, self->header_len - 4);
		fu_pxi_tp_firmware_kx_and_dec(bn, "header_crc32_calc", hdr_crc_calc);
		fu_xmlb_builder_insert_kv(bn,
					  "header_crc32_ok",
					  (hdr_crc_calc == self->header_crc32) ? "true" : "false");
	}
	if (d != NULL && sz > self->header_len) {
		guint32 file_crc_calc =
		    fu_crc32(FU_CRC_KIND_B32_STANDARD, d + self->header_len, sz - self->header_len);
		fu_pxi_tp_firmware_kx_and_dec(bn, "file_crc32_calc", file_crc_calc);
		fu_xmlb_builder_insert_kv(bn,
					  "file_crc32_ok",
					  (file_crc_calc == self->file_crc32) ? "true" : "false");
	}

	/* ranges (handy for quick eyeballing) */
	if (self->header_len <= sz) {
		fu_pxi_tp_firmware_kx_and_dec(bn, "range_header_begin", 0);
		fu_pxi_tp_firmware_kx_and_dec(bn,
					      "range_header_end",
					      self->header_len); /* [0,header) */
		fu_pxi_tp_firmware_kx_and_dec(bn, "range_payload_begin", self->header_len);
		fu_pxi_tp_firmware_kx_and_dec(bn, "range_payload_end", sz);
	}

	/* sections */
	for (guint i = 0; self->sections && i < self->sections->len; i++) {
		FuPxiTpSection *s = g_ptr_array_index(self->sections, i);

		/* keys */
		g_autofree gchar *p_type = g_strdup_printf("section%u_update_type", i);
		g_autofree gchar *p_type_name = g_strdup_printf("section%u_update_type_name", i);
		g_autofree gchar *p_info = g_strdup_printf("section%u_update_info", i);
		g_autofree gchar *p_info_flags = g_strdup_printf("section%u_update_info_flags", i);
		g_autofree gchar *p_is_valid = g_strdup_printf("section%u_is_valid", i);
		g_autofree gchar *p_is_ext = g_strdup_printf("section%u_is_external", i);
		g_autofree gchar *p_flash_start =
		    g_strdup_printf("section%u_target_flash_start", i);
		g_autofree gchar *p_int_start = g_strdup_printf("section%u_internal_file_start", i);
		g_autofree gchar *p_len = g_strdup_printf("section%u_section_length", i);
		g_autofree gchar *p_res_off = g_strdup_printf("section%u_resolved_offset", i);
		g_autofree gchar *p_res_end = g_strdup_printf("section%u_resolved_end", i);
		g_autofree gchar *p_in_range = g_strdup_printf("section%u_in_file_range", i);
		g_autofree gchar *p_extname = g_strdup_printf("section%u_external_file", i);
		g_autofree gchar *p_sample_hex = g_strdup_printf("section%u_sample_hex_0_32", i);

		/* values */
		fu_pxi_tp_firmware_kx_and_dec(bn, p_type, s->update_type);
		fu_xmlb_builder_insert_kv(bn,
					  p_type_name,
					  fu_pxi_tp_firmware_update_type_to_str(s->update_type));
		fu_pxi_tp_firmware_kx_and_dec(bn, p_info, s->update_info);
		g_autofree gchar *flags_str =
		    fu_pxi_tp_firmware_update_info_to_flags(s->update_info);
		fu_xmlb_builder_insert_kv(bn, p_info_flags, flags_str);

		/* bools readable as true/false */
		fu_xmlb_builder_insert_kv(bn, p_is_valid, s->is_valid_update ? "true" : "false");
		fu_xmlb_builder_insert_kv(bn, p_is_ext, s->is_external ? "true" : "false");

		fu_pxi_tp_firmware_kx_and_dec(bn, p_flash_start, s->target_flash_start);
		fu_pxi_tp_firmware_kx_and_dec(bn, p_int_start, s->internal_file_start);
		fu_pxi_tp_firmware_kx_and_dec(bn, p_len, s->section_length);

		if (!s->is_external && s->is_valid_update) {
			fu_pxi_tp_firmware_kx_and_dec(bn, p_res_off, s->resolved_offset);
			fu_pxi_tp_firmware_kx_and_dec(bn,
						      p_res_end,
						      (guint64)s->resolved_offset +
							  (guint64)s->section_length);

			gboolean in_range =
			    (d != NULL) && (s->resolved_offset + s->section_length <= sz);
			fu_xmlb_builder_insert_kv(bn, p_in_range, in_range ? "true" : "false");

			if (in_range) {
				g_autofree gchar *hex =
				    fu_pxi_tp_firmware_hexdump_slice(d + s->resolved_offset,
								     s->section_length,
								     32);
				fu_xmlb_builder_insert_kv(bn, p_sample_hex, hex);
			}
		} else {
			fu_xmlb_builder_insert_kv(bn, p_in_range, "n/a");
		}

		fu_xmlb_builder_insert_kv(bn, p_extname, s->external_file_name);
	}
}

/**
 * fu_pxi_tp_firmware_write:
 * @firmware: the container object
 * @error: return location for a #GError, or %NULL
 *
 * Serializes the container back to bytes. This parser is read-only for now,
 * so we simply return the original blob plus any applied patches.
 *
 * Returns: a new #GByteArray on success, or %NULL on error.
 */
static GByteArray *
fu_pxi_tp_firmware_write(FuFirmware *firmware, GError **error)
{
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GBytes) fw = fu_firmware_get_bytes_with_patches(firmware, error);
	if (fw == NULL)
		return NULL;
	fu_byte_array_append_bytes(buf, fw);
	return g_steal_pointer(&buf);
}

/**
 * fu_pxi_tp_firmware_build:
 * @firmware: the container object
 * @n: parsed builder XML node (unused)
 * @error: return location for a #GError, or %NULL
 *
 * Optional builder hook for constructing a container from a .builder.xml.
 * Not used for the PixArt FWHD v1.0 parser; present to satisfy the vfunc.
 *
 * Returns: %TRUE always.
 */
static gboolean
fu_pxi_tp_firmware_build(FuFirmware *firmware, XbNode *n, GError **error)
{
	(void)firmware;
	(void)n;
	(void)error; /* parser-only */
	return TRUE;
}

static void
fu_pxi_tp_firmware_init(FuPxiTpFirmware *self)
{
	self->sections = g_ptr_array_new_with_free_func(g_free);
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_CHECKSUM);
}

static void
fu_pxi_tp_firmware_class_init(FuPxiTpFirmwareClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);

	object_class->finalize = fu_pxi_tp_firmware_finalize;

	firmware_class->validate = fu_pxi_tp_firmware_validate;
	firmware_class->parse = fu_pxi_tp_firmware_parse;
	firmware_class->export = fu_pxi_tp_firmware_export;
	firmware_class->write = fu_pxi_tp_firmware_write;
	firmware_class->build = fu_pxi_tp_firmware_build;
}

FuFirmware *
fu_pxi_tp_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_PXI_TP_FIRMWARE, NULL));
}

/* -------------------------- getters ----------------------- */
guint16
fu_pxi_tp_firmware_get_header_version(FuPxiTpFirmware *self)
{
	g_return_val_if_fail(FU_IS_PXI_TP_FIRMWARE(self), 0);
	return self->header_ver;
}

guint16
fu_pxi_tp_firmware_get_file_version(FuPxiTpFirmware *self)
{
	g_return_val_if_fail(FU_IS_PXI_TP_FIRMWARE(self), 0);
	return self->file_ver;
}

guint16
fu_pxi_tp_firmware_get_ic_part_id(FuPxiTpFirmware *self)
{
	g_return_val_if_fail(FU_IS_PXI_TP_FIRMWARE(self), 0);
	return self->ic_part_id;
}

guint16
fu_pxi_tp_firmware_get_total_flash_sectors(FuPxiTpFirmware *self)
{
	g_return_val_if_fail(FU_IS_PXI_TP_FIRMWARE(self), 0);
	return self->flash_sectors;
}

guint16
fu_pxi_tp_firmware_get_num_valid_sections(FuPxiTpFirmware *self)
{
	g_return_val_if_fail(FU_IS_PXI_TP_FIRMWARE(self), 0);
	return self->num_sections;
}

const GPtrArray *
fu_pxi_tp_firmware_get_sections(FuPxiTpFirmware *self)
{
	g_return_val_if_fail(FU_IS_PXI_TP_FIRMWARE(self), NULL);
	return self->sections;
}
