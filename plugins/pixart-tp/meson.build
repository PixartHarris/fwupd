cargs = ['-DG_LOG_DOMAIN="FuPluginPxiTp"']

plugins += {meson.current_source_dir().split('/')[-1]: true}
plugin_quirks += files('pixart-tp.quirk')
plugin_builtins += static_library('fu_plugin_pxi_tp',
  rustgen.process('fu-pxi-tp.rs'),
  sources: [
    'fu-pxi-tp-common.c',
    'fu-pxi-tp-device.c',
    'fu-pxi-tp-firmware.c',
    'fu-pxi-tp-plugin.c',
    'fu-pxi-tp-register.c'
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)

device_tests += files(
  'tests/pxi-tp.json',
  'tests/pxi-tp-setup.json',
)
