#!/usr/bin/env bash
# Bateria host-side do launcher PortMaster único a partir do ZIP real.
# Não executa loader, jogo, SDL nem hardware: substitui apenas o hook
# nxbootstrap por uma fixture que confirma a descoberta do diretório.
set -euo pipefail

ZIP=${1:?uso: prerelease-battery.sh <chrono.zip>}
ZIP=$(cd -- "$(dirname -- "$ZIP")" && pwd -P)/$(basename -- "$ZIP")
WORK=$(mktemp -d "${TMPDIR:-/tmp}/chrono-battery.XXXXXX")
trap 'rm -rf -- "$WORK"' EXIT
fails=0

stub_bootstrap() {
  cat > "$1" <<'STUB'
#!/usr/bin/env bash
nxbootstrap_main() {
  printf 'NXBOOTSTRAP_OK %s\n' "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
  return 0
}
STUB
}

check_layout() {
  local label=$1 ports_dir=$2
  local root="$WORK/$label"
  rm -rf -- "$root"
  mkdir -p "$root/$ports_dir"
  unzip -qo "$ZIP" -d "$root/$ports_dir"
  if [[ -e $root/$ports_dir/chrono/run.sh ]]; then
    printf 'FALHA %-34s ZIP contém run.sh proibido\n' "$label"
    fails=$((fails + 1))
    return
  fi
  stub_bootstrap "$root/$ports_dir/chrono/nxbootstrap.sh"
  chmod -x "$root/$ports_dir/chrono/nxbootstrap.sh" \
    "$root/$ports_dir/Chrono Trigger.sh"

  mkdir -p "$root/link"
  ln -sf "$root/$ports_dir/Chrono Trigger.sh" "$root/link/Chrono Trigger.sh"

  local out
  if out=$(cd "$root/link" && bash "./Chrono Trigger.sh" 2>&1); then
    case "$out" in
      *"NXBOOTSTRAP_OK $root/$ports_dir/chrono"*)
        printf 'OK    %-34s launcher → nxbootstrap\n' "$label" ;;
      *)
        printf 'FALHA %-34s saída inesperada: %s\n' "$label" "$out"
        fails=$((fails + 1)) ;;
    esac
  else
    printf 'FALHA %-34s launcher retornou erro: %s\n' "$label" "$out"
    fails=$((fails + 1))
  fi
}

check_layout roms          "ports"
check_layout roms2         "ports"
check_layout storage-roms  "ports"
check_layout muos-mmc      "ROMS/ports"
check_layout muos-sdcard   "ports"
check_layout batocera      "userdata/roms/ports"
check_layout ports-scripts "ports_scripts"

root="$WORK/sem-runtime"
mkdir -p "$root/ports"
unzip -qo "$ZIP" -d "$root/ports"
rm -rf -- "$root/ports/chrono"
if (cd "$root/ports" && bash "./Chrono Trigger.sh" >/dev/null 2>&1); then
  printf 'FALHA %-34s deveria falhar sem o runtime\n' "sem-runtime"
  fails=$((fails + 1))
elif compgen -G "$root/ports/chrono-launcher-error.*.log" >/dev/null; then
  printf 'OK    %-34s gravou log de erro por PID\n' "sem-runtime"
else
  printf 'FALHA %-34s falhou mudo (sem log)\n' "sem-runtime"
  fails=$((fails + 1))
fi

root="$WORK/sem-bit-x"
mkdir -p "$root/ports"
unzip -qo "$ZIP" -d "$root/ports"
stub_bootstrap "$root/ports/chrono/nxbootstrap.sh"
find "$root" -type f -exec chmod 0644 {} +
if out=$(cd "$root/ports" && bash "./Chrono Trigger.sh" 2>&1) &&
   [[ $out == *NXBOOTSTRAP_OK* ]]; then
  printf 'OK    %-34s nada crítico depende de +x\n' "sem-bit-x"
else
  printf 'FALHA %-34s quebrou sem +x: %s\n' "sem-bit-x" "$out"
  fails=$((fails + 1))
fi

root="$WORK/bootstrap-symlink"
mkdir -p "$root/ports"
unzip -qo "$ZIP" -d "$root/ports"
rm -f -- "$root/ports/chrono/nxbootstrap.sh"
ln -s /dev/null "$root/ports/chrono/nxbootstrap.sh"
if (cd "$root/ports" && bash "./Chrono Trigger.sh" >/dev/null 2>&1); then
  printf 'FALHA %-34s aceitou bootstrap symlink\n' "bootstrap-symlink"
  fails=$((fails + 1))
else
  printf 'OK    %-34s recusou bootstrap symlink\n' "bootstrap-symlink"
fi

printf '\n%s\n' "$([[ $fails -eq 0 ]] && echo 'BATERIA VERDE' || echo "BATERIA COM $fails FALHA(S)")"
exit "$fails"
