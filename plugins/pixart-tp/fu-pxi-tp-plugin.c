/*
 * Copyright {{Year}} {{Author}} <{{Email}}>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-pxi-tp-device.h"
#include "fu-pxi-tp-firmware.h"
#include "fu-pxi-tp-plugin.h"

struct _FuPxiTpPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuPxiTpPlugin, fu_pxi_tp_plugin, FU_TYPE_PLUGIN)

static void
fu_pxi_tp_plugin_object_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_set_name(plugin, "pixart_tp");
}

static void
fu_pxi_tp_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	FuContext *ctx = fu_plugin_get_context(plugin);
	g_message("🧪 Pixart TP Plugin has been loaded!");

	// FuDevice *dev = FU_DEVICE(g_object_new(FU_TYPE_PXI_DEVICE, NULL));
	// fu_plugin_add_device_gtype(plugin, FU_TYPE_PXI_DEVICE);
	// fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_PXI_FIRMWARE);

	fu_context_add_quirk_key(ctx, "PxiStartAddr");
	fu_plugin_add_udev_subsystem(plugin, "hidraw");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_PXI_TP_DEVICE);
	fu_plugin_add_firmware_gtype(plugin, "pixart", FU_TYPE_PXI_TP_FIRMWARE);
	g_message("🧪 Pixart TP Plugin fu_pxi_plugin_constructed Done!");
}

static void
fu_pxi_tp_plugin_class_init(FuPxiTpPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->constructed = fu_pxi_tp_plugin_object_constructed;
	plugin_class->constructed = fu_pxi_tp_plugin_constructed;
}

static void
fu_pxi_tp_plugin_init(FuPxiTpPlugin *self)
{
}
