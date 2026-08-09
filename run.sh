#!/usr/bin/env bash
# Generated nxbootstrap entry point. Engine/device code belongs in the loader.

NXPORT_RUN_FILE=${BASH_SOURCE[0]}
[[ -f $NXPORT_RUN_FILE && ! -L $NXPORT_RUN_FILE ]] || exit 1
NXPORT_GAME_DIR=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" 2>/dev/null && pwd -P) ||
  exit 1
NXPORT_ID=chrono
NXPORT_TITLE='Chrono Trigger'
NXPORT_SCHEMA_VERSION=2
NXPORT_ARCH=aarch64
NXPORT_INTERPRETER=/lib/ld-linux-aarch64.so.1
NXPORT_EXECUTABLE=chrono-universal
NXPORT_ARGUMENT_MODE=game-dir-and-passthrough
NXPORT_HOME_MODE=port
NXPORT_NXEXTRACT=yes
NXPORT_NXEXTRACT_VERSION=1.2.6
NXPORT_REQUIRED_FILES='chrono-universal
libchrono.so
libc++_shared.so
assets/resources.bin
assets/001.dat
assets/Shaders/example_Simple.vsh'
NXPORT_PRIVATE_LIBRARY_PATHS=''
NXPORT_PREPARE_SCRIPT=''
NXPORT_REQUIRED_CAPABILITIES='host.portmaster
graphics.window
graphics.gles2
graphics.egl
graphics.egl-config
graphics.drawable
input.controller-mapping
input.controller-api'
NXPORT_ENABLED_QUIRKS='game.chrono.present-alpha-one
game.chrono.present-finish'
NXPORT_RUNTIME_REPORT=log-and-logo

if [[ $NXPORT_ARCH == armv7 ]]; then
  export PORT_32BIT=Y
fi

# Invoked through bash deliberately: FAT/exFAT/ZIP tools may lose mode bits.
[[ -f $NXPORT_GAME_DIR/nxbootstrap.sh &&
   ! -L $NXPORT_GAME_DIR/nxbootstrap.sh ]] || exit 1
source "$NXPORT_GAME_DIR/nxbootstrap.sh" || exit 1
nxbootstrap_main "$@"
