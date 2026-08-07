#!/usr/bin/env bash
# Chrono Trigger (Cocos2d-x 3.14.1) — runtime unico multi-device.
#
# Molde estrutural: ports/hitmango/run.sh e ports/huntdown (ports aprovados),
# contrato de suportando_outros_devices/launcher-portmaster.md.
#
# O launcher NAO administra o frontend: nao para, nao reinicia e nao mascara o
# EmulationStation, nao usa systemctl, nohup nem setsid. Ele valida os dados,
# monta o ambiente do runtime e supervisiona o jogo em foreground.
#
# Sem `set -u`: o control.txt do PortMaster consulta variaveis que ainda nao
# existem. Nao e' codigo nosso.

GAMEDIR=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" 2>/dev/null && pwd -P) ||
  exit 1

# O LOG VEM ANTES DE TUDO (licao Stardew v1.1.6): falha antes do
# redirecionamento vira "nao abre, sem log". Cartao somente-leitura cai no /tmp.
if [ -s "$GAMEDIR/debug.log" ]; then
  mv -f -- "$GAMEDIR/debug.log" "$GAMEDIR/debug.prev.log" 2>/dev/null
fi
if : > "$GAMEDIR/debug.log" 2>/dev/null; then
  CT_LOG=$GAMEDIR/debug.log
else
  CT_LOG=${TMPDIR:-/tmp}/chrono-debug.log
fi
exec >> "$CT_LOG" 2>&1
printf '=== Chrono Trigger (NextOS) | release %s | %s ===\n' \
  "$(tr -d '\r\n' < "$GAMEDIR/version.txt" 2>/dev/null || echo unknown)" \
  "$(date -Is 2>/dev/null || date)"
printf '[runtime] gamedir=%s log=%s shell=%s\n' \
  "$GAMEDIR" "$CT_LOG" "${BASH_VERSION:-?}"

cd "$GAMEDIR" || { printf '[runtime] nao consegui entrar em %s\n' "$GAMEDIR"; exit 1; }

CT_BIN=$GAMEDIR/chrono-universal

# ---- PortMaster opcional -----------------------------------------------------
XDG_DATA_HOME=${XDG_DATA_HOME:-$HOME/.local/share}
for _cf in /opt/system/Tools/PortMaster /opt/tools/PortMaster \
           "$XDG_DATA_HOME/PortMaster" /roms/ports/PortMaster \
           /roms2/ports/PortMaster /userdata/roms/ports/PortMaster \
           /storage/.config/PortMaster; do
  [ -d "$_cf" ] && { controlfolder=$_cf; break; }
done
: "${controlfolder:=/storage/.config/PortMaster}"
if [ -f "$controlfolder/control.txt" ]; then
  # shellcheck disable=SC1091
  source "$controlfolder/control.txt"
  case "${CFW_NAME:-}" in
    ''|*[!A-Za-z0-9._-]*) ;;
    *) [ -f "$controlfolder/mod_${CFW_NAME}.txt" ] &&
         source "$controlfolder/mod_${CFW_NAME}.txt" ;;
  esac
  declare -F get_controls >/dev/null 2>&1 && get_controls
fi
: "${ESUDO:=}"
: "${CUR_TTY:=/dev/tty0}"
printf '[runtime] cfw=%s controlfolder=%s esudo=%s\n' \
  "${CFW_NAME:-nenhum}" "$controlfolder" "${ESUDO:-nenhum}"

# Erro NUNCA mudo: log + aviso no TTY do frontend quando existir.
runtime_error() {
  printf '[runtime] ERRO: %s\n' "$*"
  printf 'Chrono Trigger: %s\n' "$*" > "$CUR_TTY" 2>/dev/null || true
  command -v pm_finish >/dev/null 2>&1 && pm_finish 2>/dev/null
  exit 1
}

$ESUDO chmod 666 "$CUR_TTY" /dev/uinput 2>/dev/null || true
# Cartao FAT/exFAT come o bit +x; o binario e' invocado direto, entao repor o
# bit e' util, mas nenhuma etapa CRITICA depende de `[ -x ... ]`.
$ESUDO chmod +x "$CT_BIN" 2>/dev/null || true

# ---- ABI: este port e' AArch64-only (as libs do jogo so' existem em arm64) ----
ct_arch=$(uname -m 2>/dev/null)
case "$ct_arch" in
  aarch64|arm64) ;;
  *) runtime_error "este port precisa de userland AArch64 (kernel/userland detectado: $ct_arch); as bibliotecas originais do Chrono Trigger existem apenas em arm64-v8a" ;;
esac
[ -e /lib/ld-linux-aarch64.so.1 ] || [ -e /lib64/ld-linux-aarch64.so.1 ] ||
  runtime_error "userland AArch64 nao encontrado (/lib/ld-linux-aarch64.so.1 ausente)"
[ -s "$CT_BIN" ] || runtime_error "runtime ausente: $(basename "$CT_BIN")"

# ---- instancia unica: TERM, prazo, rescan e KILL restrito -------------------
# A trava definitiva e' o flock que o PROPRIO BINARIO adquire; isto aqui limpa
# instancia antiga comprovada por /proc antes de tentar.
ct_matches() {
  ct_pid=${1##*/}
  [ "$ct_pid" != "$$" ] || return 1
  ct_exe=$(readlink "$1/exe" 2>/dev/null || true)
  ct_comm=$(cat "$1/comm" 2>/dev/null || true)
  ct_cwd=$(readlink "$1/cwd" 2>/dev/null || true)
  ct_cmd=$(tr '\000' ' ' < "$1/cmdline" 2>/dev/null || true)
  case "$ct_exe" in
    "$CT_BIN"|"$CT_BIN (deleted)"|"$GAMEDIR/chrono"|"$GAMEDIR/chrono (deleted)") return 0 ;;
  esac
  case "$ct_cmd" in
    *"$CT_BIN"*|*"./chrono-universal"*|*"$GAMEDIR/chrono"*|*"./chrono"*)
      [ "$ct_cwd" = "$GAMEDIR" ] && return 0 ;;
  esac
  if [ "$ct_cwd" = "$GAMEDIR" ]; then
    case "$ct_comm" in chrono|chrono-universal) return 0 ;; esac
  fi
  return 1
}
ct_pids() {
  for ct_proc in /proc/[0-9]*; do
    [ -d "$ct_proc" ] || continue
    ct_matches "$ct_proc" && printf '%s\n' "${ct_proc##*/}"
  done
}
ct_old=$(ct_pids)
if [ -n "$ct_old" ]; then
  for ct_pid in $ct_old; do
    printf '[runtime] instancia antiga pid=%s comm=%s exe=%s\n' "$ct_pid" \
      "$(cat /proc/$ct_pid/comm 2>/dev/null)" \
      "$(readlink /proc/$ct_pid/exe 2>/dev/null)"
  done
  kill -TERM $ct_old 2>/dev/null || true
  ct_wait=0
  while [ -n "$(ct_pids)" ] && [ "$ct_wait" -lt 16 ]; do
    sleep 0.5; ct_wait=$((ct_wait + 1))
  done
fi
ct_old=$(ct_pids)
if [ -n "$ct_old" ]; then
  printf '[runtime] prazo esgotado; encerrando pid(s): %s\n' "$ct_old"
  kill -KILL $ct_old 2>/dev/null || true
  sleep 1
fi
ct_old=$(ct_pids)
[ -z "$ct_old" ] || runtime_error "outra instancia do Chrono Trigger continua viva: $ct_old"

# ---- NXExtract: dados BYO validados e instalados de forma transacional -------
# A UI do extrator usa SDL/EGL/GLES do FIRMWARE (nxextract-runtime-env.sh cuida
# do escopo). O APK legal do usuario nunca e' apagado.
chmod +x "$GAMEDIR/run-extractor.sh" "$GAMEDIR/nxextract-runtime-env.sh" \
  "$GAMEDIR/nxextract.py" "$GAMEDIR/nxextract-ui" 2>/dev/null || true
if [ -f "$GAMEDIR/extractor.json" ] && [ -f "$GAMEDIR/run-extractor.sh" ]; then
  command -v python3 >/dev/null 2>&1 ||
    runtime_error "este firmware nao tem python3; o instalador de dados nao pode rodar"
  NXEXTRACT_GAME_DIR=$GAMEDIR \
    NXEXTRACT_FIRMWARE_LIBRARY_PATH=/usr/local/lib/aarch64-linux-gnu:/usr/local/lib:/usr/lib/aarch64-linux-gnu:/lib/aarch64-linux-gnu:/usr/lib:/lib \
    bash "$GAMEDIR/run-extractor.sh" ||
    runtime_error "dados do jogo ausentes ou invalidos. Coloque o APK do Chrono Trigger que voce possui (arm64) em ports/chrono/gamedata/ e abra de novo"
fi

# Gate de artefatos: recusar iniciar com payload incompleto, nunca pular calado.
for ct_required in \
  libchrono.so \
  libc++_shared.so \
  assets/resources.bin \
  assets/001.dat \
  assets/007-en.dat \
  assets/Shaders/example_Simple.vsh
do
  [ -s "$GAMEDIR/$ct_required" ] ||
    runtime_error "dado obrigatorio ausente: $ct_required (veja gamedata/README.txt)"
done

mkdir -p "$GAMEDIR/userdata"

# ---- ambiente do runtime -----------------------------------------------------
# As libs do FIRMWARE vem primeiro; as libs Android do jogo sao abertas pelo
# proprio so-loader e nunca substituem SDL/EGL/GLES/libc do host.
export CHRONO_GAMEDIR="$GAMEDIR"
export HOME="$GAMEDIR"
ct_system_libs=/usr/local/lib/aarch64-linux-gnu:/usr/local/lib:/usr/lib/aarch64-linux-gnu:/lib/aarch64-linux-gnu:/usr/lib:/lib
export LD_LIBRARY_PATH="$ct_system_libs:$controlfolder/libs:$controlfolder/libs.aarch64:$GAMEDIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export MALLOC_ARENA_MAX=${MALLOC_ARENA_MAX:-2}

# NUNCA fixar SDL_VIDEODRIVER/SDL_AUDIODRIVER: quem escolhe e' o firmware.
# Se um valor HERDADO estiver invalido, o probe abaixo o remove uma unica vez e
# devolve a autodeteccao — nao escolhe outro backend.
if [ -n "${SDL_VIDEODRIVER:-}" ]; then
  printf '[runtime] SDL_VIDEODRIVER herdado=%s (preservado)\n' "$SDL_VIDEODRIVER"
fi
if [ -n "${SDL_AUDIODRIVER:-}" ]; then
  printf '[runtime] SDL_AUDIODRIVER herdado=%s (preservado)\n' "$SDL_AUDIODRIVER"
fi

# Controle NATIVO: o mapping do usuario/PortMaster vence a base do firmware.
export SDL_GAMECONTROLLER_USE_BUTTON_LABELS=0
[ -n "${sdl_controllerconfig:-}" ] &&
  export SDL_GAMECONTROLLERCONFIG="$sdl_controllerconfig"
if [ -z "${SDL_GAMECONTROLLERCONFIG_FILE:-}" ] && [ -n "${controlfolder:-}" ]; then
  for _db in "$controlfolder/gamecontrollerdb.txt" \
             "$controlfolder/gamecontrollerdb-SDL2.txt"; do
    [ -r "$_db" ] && [ ! -L "$_db" ] &&
      { export SDL_GAMECONTROLLERCONFIG_FILE=$_db; break; }
  done
fi

# gptokeyb NAO entra: o controle deste port e' NATIVO e um mapper por cima
# roubaria o pad. SELECT+START sai pelo proprio binario (SDL e evdev).
printf '[runtime] controle nativo; SELECT+START e SIGTERM saem pelo binario\n'
printf '[runtime] MemTotal=%s kB\n' \
  "$(awk '/^MemTotal:/ {print $2}' /proc/meminfo 2>/dev/null)"

# Fecha o splash do PortMaster e entrega o display. NAO mexe no frontend.
if command -v pm_platform_helper >/dev/null 2>&1; then
  pm_platform_helper "$CT_BIN" >/dev/null 2>&1 || true
fi

# ---- supervisao foreground ---------------------------------------------------
# O launcher continua vivo, dono do PID exato, e encaminha TERM ao filho: o
# binario trata SIGTERM no MESMO caminho do SELECT+START (pausa/save/saida).
child_pid=
stop_child() {
  [ -n "${child_pid:-}" ] || return 0
  kill -TERM "$child_pid" 2>/dev/null || return 0
  for _attempt in 1 2 3 4 5 6 7 8 9 10; do
    kill -0 "$child_pid" 2>/dev/null || return 0
    sleep 1
  done
  kill -KILL "$child_pid" 2>/dev/null || true
}
trap stop_child EXIT INT TERM

"$CT_BIN" "$GAMEDIR" &
child_pid=$!
while :; do
  wait "$child_pid"
  status=$?
  kill -0 "$child_pid" 2>/dev/null || break
done
child_pid=
trap - EXIT INT TERM

printf '[runtime] jogo terminou com status %s\n' "$status"
printf '\033c' >> "$CUR_TTY" 2>/dev/null || true
command -v pm_finish >/dev/null 2>&1 && pm_finish 2>/dev/null
exit "$status"
