#pragma once

#include "fu-pxi-tp-define.h"
#include "fu-pxi-tp-device.h"

#define WRITE_REG(bank, addr, val)                                                                 \
	do {                                                                                       \
		if (!fu_pxi_tp_register_write(self, (bank), (addr), (val), error)) {               \
			PXI_FAIL(error,                                                            \
				 "write register failed (0x%02x, 0x%02x, 0x%02x)",                 \
				 (guint)(bank),                                                    \
				 (guint)(addr),                                                    \
				 (guint)(val));                                                    \
			return FALSE;                                                              \
		}                                                                                  \
	} while (0)

#define READ_REG(bank, addr, out_val_ptr)                                                          \
	do {                                                                                       \
		if (!fu_pxi_tp_register_read(self, (bank), (addr), (out_val_ptr), error)) {        \
			PXI_FAIL(error,                                                            \
				 "read register failed (0x%02x, 0x%02x",                           \
				 (guint)(bank),                                                    \
				 (guint)(addr));                                                   \
			return FALSE;                                                              \
		}                                                                                  \
	} while (0)

#define READ_USR_REG(bank, addr, out_val_ptr)                                                      \
	do {                                                                                       \
		if (!fu_pxi_tp_register_user_read(self, (bank), (addr), (out_val_ptr), error)) {   \
			PXI_FAIL(error,                                                            \
				 "read user register failed (0x%02x, 0x%02x",                      \
				 (guint)(bank),                                                    \
				 (guint)(addr));                                                   \
			return FALSE;                                                              \
		}                                                                                  \
	} while (0)

gboolean
fu_pxi_tp_register_write(FuPxiTpDevice *self, guint8 bank, guint8 addr, guint8 val, GError **error);
gboolean
fu_pxi_tp_register_read(FuPxiTpDevice *self,
			guint8 bank,
			guint8 addr,
			guint8 *out_val,
			GError **error);

gboolean
fu_pxi_tp_register_user_write(FuPxiTpDevice *self,
			      guint8 bank,
			      guint8 addr,
			      guint8 val,
			      GError **error);
gboolean
fu_pxi_tp_register_user_read(FuPxiTpDevice *self,
			     guint8 bank,
			     guint8 addr,
			     guint8 *out_val,
			     GError **error);

gboolean
fu_pxi_tp_register_burst_write(FuPxiTpDevice *self, const guint8 *buf, gsize bufsz, GError **error);
gboolean
fu_pxi_tp_register_burst_read(FuPxiTpDevice *self, guint8 *buf, gsize bufsz, GError **error);
