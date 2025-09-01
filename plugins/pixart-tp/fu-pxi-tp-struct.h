// =============================================================
// file: fu-pxi-tp-struct.h  (pure C, no C++ guards, no packed structs)
// =============================================================
#pragma once

#include <glib.h>

/* PixArt Poco Touch FWHD v1.0 constants */
#define PXI_TP_MAGIC	     "FWHD"
#define PXI_TP_HEADER_V1_LEN ((guint16)0x0218)
#define PXI_TP_MAX_SECTIONS  8
#define PXI_TP_SECTION_SIZE  64

/* Update types */
#define PXI_TP_UPDATE_TYPE_GENERAL  0u	/* standard flash update */
#define PXI_TP_UPDATE_TYPE_TF_FORCE 16u /* TF Force (via DLL) */

/* Update Information bit definitions */
#define PXI_TP_UI_VALID	   (1u << 0) /* 1 = execute this section */
#define PXI_TP_UI_EXTERNAL (1u << 1) /* 1 = use external file */

/* Header field offsets (for clarity; all LE) */
#define PXI_TP_O_MAGIC	       0x00
#define PXI_TP_O_HDRLEN	       0x04 /* uint16 */
#define PXI_TP_O_HDRVER	       0x06 /* uint16 */
#define PXI_TP_O_FILEVER       0x08 /* uint16 */
#define PXI_TP_O_PARTID	       0x0A /* uint16 */
#define PXI_TP_O_SECTORS       0x0C /* uint16 */
#define PXI_TP_O_TOTALCRC      0x0E /* uint32 over payload */
#define PXI_TP_O_NUMSECTIONS   0x12 /* uint16 */
#define PXI_TP_O_SECTIONS_BASE 0x14 /* first section */
/* Each section: 64 bytes */
#define PXI_TP_O_HDRCRC(hlen) ((hlen) - 4) /* uint32 at 0x214 for v1.0 */

/* Section field offsets */
#define PXI_TP_S_O_TYPE	     0x00 /* uint8 */
#define PXI_TP_S_O_INFO	     0x01 /* uint8 (bitfield) */
#define PXI_TP_S_O_FLASHADDR 0x02 /* uint32 LE */
#define PXI_TP_S_O_INTSTART  0x06 /* uint32 LE */
#define PXI_TP_S_O_INTLEN    0x0A /* uint32 LE */
#define PXI_TP_S_O_EXTNAME   0x0E /* 50 bytes ASCII */
#define PXI_TP_S_EXTNAME_LEN 50

/* Parsed section (convenience for callers) */
typedef struct {
	guint8 update_type;				    /* 0 or 16 */
	guint8 update_info;				    /* raw bitfield */
	gboolean is_valid_update;			    /* (update_info & PXI_TP_UI_VALID) */
	gboolean is_external;				    /* (update_info & PXI_TP_UI_EXTERNAL) */
	guint32 target_flash_start;			    /* if type==0 */
	guint32 internal_file_start;			    /* if !is_external */
	guint32 section_length;				    /* if !is_external */
	gchar external_file_name[PXI_TP_S_EXTNAME_LEN + 1]; /* NUL-terminated */
	guint64 resolved_offset;			    /* absolute file offset if internal */
} FuPxiTpSection;
