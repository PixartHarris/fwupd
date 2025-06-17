#include "config.h"

#include "fu-pxi-tp-register.h"

#define REPORT_ID_SINGLE 0x42
#define REPORT_ID_BURST	 0x41
#define REPORT_ID_USER	 0x43
#define OP_READ		 0x10

static gboolean
fu_pxi_tp_register_send_feature(FuPxiTpDevice *self, const guint8 *buf, gsize len, GError **error)
{
	return fu_hidraw_device_set_feature(FU_HIDRAW_DEVICE(self),
					    buf,
					    len,
					    FU_IOCTL_FLAG_NONE,
					    error);
}

static gboolean
fu_pxi_tp_register_get_feature(FuPxiTpDevice *self,
			       guint8 report_id,
			       guint8 *buf,
			       gsize len,
			       GError **error)
{
	buf[0] = report_id;
	return fu_hidraw_device_get_feature(FU_HIDRAW_DEVICE(self),
					    buf,
					    len,
					    FU_IOCTL_FLAG_NONE,
					    error);
}

// --- System Register ---

gboolean
fu_pxi_tp_register_write(FuPxiTpDevice *self, guint8 bank, guint8 addr, guint8 val, GError **error)
{
	guint8 buf[4] = {REPORT_ID_SINGLE, addr, bank, val};
	return fu_pxi_tp_register_send_feature(self, buf, sizeof(buf), error);
}

gboolean
fu_pxi_tp_register_read(FuPxiTpDevice *self,
			guint8 bank,
			guint8 addr,
			guint8 *out_val,
			GError **error)
{
	guint8 cmd[4] = {REPORT_ID_SINGLE, addr, (bank | OP_READ), 0x00};
	if (!fu_pxi_tp_register_send_feature(self, cmd, sizeof(cmd), error))
		return FALSE;

	guint8 resp[4] = {REPORT_ID_SINGLE};
	if (!fu_pxi_tp_register_get_feature(self, REPORT_ID_SINGLE, resp, sizeof(resp), error))
		return FALSE;

	*out_val = resp[3];
	return TRUE;
}

// --- User Register ---

gboolean
fu_pxi_tp_register_user_write(FuPxiTpDevice *self,
			      guint8 bank,
			      guint8 addr,
			      guint8 val,
			      GError **error)
{
	guint8 buf[4] = {REPORT_ID_USER, addr, bank, val};
	return fu_pxi_tp_register_send_feature(self, buf, sizeof(buf), error);
}

gboolean
fu_pxi_tp_register_user_read(FuPxiTpDevice *self,
			     guint8 bank,
			     guint8 addr,
			     guint8 *out_val,
			     GError **error)
{
	guint8 cmd[4] = {REPORT_ID_USER, addr, (bank | OP_READ), 0x00};
	if (!fu_pxi_tp_register_send_feature(self, cmd, sizeof(cmd), error))
		return FALSE;

	guint8 resp[4] = {REPORT_ID_USER};
	if (!fu_pxi_tp_register_get_feature(self, REPORT_ID_USER, resp, sizeof(resp), error))
		return FALSE;

	*out_val = resp[3];
	return TRUE;
}

// --- Burst ---

gboolean
fu_pxi_tp_register_burst_write(FuPxiTpDevice *self, const guint8 *buf, gsize bufsz, GError **error)
{
	if (bufsz > 256)
		return FALSE;

	guint8 payload[257] = {REPORT_ID_BURST};
	fu_memcpy_safe(payload, sizeof(payload), 1, buf, bufsz, 0, bufsz, error);

	return fu_pxi_tp_register_send_feature(self, payload, sizeof(payload), error);
}

gboolean
fu_pxi_tp_register_burst_read(FuPxiTpDevice *self, guint8 *buf, gsize bufsz, GError **error)
{
	guint8 payload[257] = {REPORT_ID_BURST};
	if (!fu_pxi_tp_register_get_feature(self, REPORT_ID_BURST, payload, sizeof(payload), error))
		return FALSE;

	fu_memcpy_safe(buf,
		       sizeof(buf),
		       0,
		       payload,
		       sizeof(payload),
		       1,
		       MIN(bufsz, sizeof(payload) - 1),
		       error);
	return TRUE;
}
