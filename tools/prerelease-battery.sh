#!/usr/bin/env bash
# Bateria pre-release do wrapper visivel, rodada no HOST a partir do ZIP real.
#
# Cobre os itens 1 e 2 de publicando_ports/licoes-das-releases.md:
#   1. zip virgem em arvore simulando CADA layout de raiz de ROM (inclusive
#      /mnt/mmc e /mnt/sdcard) + wrapper chamado por SYMLINK -> ou acha o
#      runtime, ou grava o <port>-launcher-error.log;
#   2. zip passado por FAT (perde o bit +x) -> nada critico depende de `+x`.
#
# Uso: tools/prerelease-battery.sh caminho/para/chrono.zip
set -euo pipefail

ZIP=${1:?uso: prerelease-battery.sh <chrono.zip>}
ZIP=$(cd -- "$(dirname -- "$ZIP")" && pwd -P)/$(basename -- "$ZIP")
WORK=$(mktemp -d "${TMPDIR:-/tmp}/chrono-battery.XXXXXX")
trap 'rm -rf -- "$WORK"' EXIT
fails=0

# O run.sh real chamaria o jogo; aqui so' queremos provar que o WRAPPER acha o
# run.sh certo em cada layout. O stub grava onde foi chamado e termina.
stub_run_sh() {
  cat > "$1" <<'STUB'
#!/usr/bin/env bash
printf 'RUNSH_OK %s\n' "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
exit 0
STUB
}

check_layout() {
  local label=$1 ports_dir=$2
  local root="$WORK/$label"
  rm -rf "$root"
  mkdir -p "$root/$ports_dir"
  unzip -qo "$ZIP" -d "$root/$ports_dir"
  stub_run_sh "$root/$ports_dir/chrono/run.sh"
  chmod -x "$root/$ports_dir/chrono/run.sh" "$root/$ports_dir/Chrono Trigger.sh"

  # O frontend costuma criar um SYMLINK para o arquivo visivel (muOS).
  mkdir -p "$root/link"
  ln -sf "$root/$ports_dir/Chrono Trigger.sh" "$root/link/Chrono Trigger.sh"

  local out
  if out=$(cd "$root/link" && bash "./Chrono Trigger.sh" 2>&1); then
    case "$out" in
      *"RUNSH_OK $root/$ports_dir/chrono"*)
        printf 'OK    %-34s achou o run.sh pelo symlink\n' "$label" ;;
      *)
        printf 'FALHA %-34s saida inesperada: %s\n' "$label" "$out"; fails=$((fails+1)) ;;
    esac
  else
    printf 'FALHA %-34s wrapper retornou erro: %s\n' "$label" "$out"; fails=$((fails+1))
  fi
}

# --- item 1: todos os layouts de raiz de ROM, sempre via symlink ---
check_layout roms            "ports"
check_layout roms2           "ports"
check_layout storage-roms    "ports"
check_layout muos-mmc        "ROMS/ports"
check_layout muos-sdcard     "ports"
check_layout batocera        "userdata/roms/ports"
check_layout ports-scripts   "ports_scripts"

# --- item 1b: sem run.sh nenhum -> erro gravado, nunca falha muda ---
root="$WORK/sem-runtime"
mkdir -p "$root/ports"
unzip -qo "$ZIP" -d "$root/ports"
rm -rf "$root/ports/chrono"
if (cd "$root/ports" && bash "./Chrono Trigger.sh" >/dev/null 2>&1); then
  printf 'FALHA %-34s deveria ter falhado sem o run.sh\n' "sem-runtime"; fails=$((fails+1))
else
  if [ -s "$root/ports/chrono-launcher-error.log" ] ||
     [ -s "${TMPDIR:-/tmp}/chrono-launcher-error.log" ]; then
    printf 'OK    %-34s gravou chrono-launcher-error.log\n' "sem-runtime"
  else
    printf 'FALHA %-34s falhou MUDO (sem log)\n' "sem-runtime"; fails=$((fails+1))
  fi
fi

# --- item 2: zip passado por FAT (nenhum bit +x sobrevive) ---
root="$WORK/sem-bit-x"
mkdir -p "$root/ports"
unzip -qo "$ZIP" -d "$root/ports"
stub_run_sh "$root/ports/chrono/run.sh"
find "$root" -type f -exec chmod 0644 {} +
if out=$(cd "$root/ports" && bash "./Chrono Trigger.sh" 2>&1) &&
   case "$out" in *RUNSH_OK*) true ;; *) false ;; esac; then
  printf 'OK    %-34s nada critico depende do bit +x\n' "sem-bit-x"
else
  printf 'FALHA %-34s quebrou sem o bit +x: %s\n' "sem-bit-x" "$out"; fails=$((fails+1))
fi

# --- item 2b: run.sh que e' symlink deve ser recusado ---
root="$WORK/runsh-symlink"
mkdir -p "$root/ports/chrono"
unzip -qo "$ZIP" -d "$root/ports"
rm -f "$root/ports/chrono/run.sh"
ln -s /dev/null "$root/ports/chrono/run.sh"
if (cd "$root/ports" && bash "./Chrono Trigger.sh" >/dev/null 2>&1); then
  printf 'FALHA %-34s aceitou run.sh que e symlink\n' "runsh-symlink"; fails=$((fails+1))
else
  printf 'OK    %-34s recusou run.sh que e symlink\n' "runsh-symlink"
fi

printf '\n%s\n' "$([ "$fails" -eq 0 ] && echo 'BATERIA VERDE' || echo "BATERIA COM $fails FALHA(S)")"
exit "$fails"
