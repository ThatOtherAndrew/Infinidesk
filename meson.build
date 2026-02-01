project('infinidesk', 'c',
  version: '0.1.0',
  license: 'MIT',
  default_options: [
    'c_std=c11',
    'warning_level=2',
    'werror=false',
  ],
)

cc = meson.get_compiler('c')

# Dependencies
wlroots = dependency('wlroots-0.18', version: '>= 0.18.0', required: false)
if not wlroots.found()
  wlroots = dependency('wlroots', version: '>= 0.18.0')
endif

wayland_server = dependency('wayland-server')
wayland_protos = dependency('wayland-protocols')
xkbcommon = dependency('xkbcommon')
pixman = dependency('pixman-1')
math = cc.find_library('m', required: false)

# Wayland scanner for protocol generation
wayland_scanner = find_program('wayland-scanner')
wayland_protocols_dir = wayland_protos.get_variable('pkgdatadir')

# Find wlr-protocols directory
wlr_protocols = dependency('wlr-protocols', required: false)
if wlr_protocols.found()
  wlr_protocols_dir = wlr_protocols.get_variable('pkgdatadir')
else
  # Fallback: try to find it via pkg-config or common locations
  wlr_protocols_dir = '/usr/share/wlr-protocols'
endif

# Generate XDG shell protocol headers
xdg_shell_xml = wayland_protocols_dir / 'stable/xdg-shell/xdg-shell.xml'

xdg_shell_protocol_h = custom_target(
  'xdg-shell-protocol.h',
  input: xdg_shell_xml,
  output: 'xdg-shell-protocol.h',
  command: [wayland_scanner, 'server-header', '@INPUT@', '@OUTPUT@'],
)

# Generate wlr-layer-shell protocol header
wlr_layer_shell_xml = wlr_protocols_dir / 'unstable/wlr-layer-shell-unstable-v1.xml'

wlr_layer_shell_protocol_h = custom_target(
  'wlr-layer-shell-unstable-v1-protocol.h',
  input: wlr_layer_shell_xml,
  output: 'wlr-layer-shell-unstable-v1-protocol.h',
  command: [wayland_scanner, 'server-header', '@INPUT@', '@OUTPUT@'],
)

# Include directories
infinidesk_inc = include_directories('include')

# Source files
infinidesk_sources = files(
  'src/main.c',
  'src/server.c',
  'src/config.c',
  'src/canvas.c',
  'src/drawing.c',
  'src/view.c',
  'src/input.c',
  'src/keyboard.c',
  'src/cursor.c',
  'src/output.c',
  'src/xdg_shell.c',
  'src/layer_shell.c',
  'src/background.c',
)

# Compiler flags
add_project_arguments(
  '-DWLR_USE_UNSTABLE',
  language: 'c',
)

# Main executable
executable('infinidesk',
  infinidesk_sources,
  xdg_shell_protocol_h,
  wlr_layer_shell_protocol_h,
  include_directories: infinidesk_inc,
  dependencies: [
    wlroots,
    wayland_server,
    xkbcommon,
    pixman,
    math,
  ],
  install: true,
)
