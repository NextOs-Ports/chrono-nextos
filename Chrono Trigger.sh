#!/bin/bash
# PORTMASTER: chrono, Chrono Trigger.sh
# Ponto de entrada visivel do frontend. Toda a logica mora em chrono/run.sh.
#
# Resolve o PROPRIO caminho real antes de procurar: em varios frontends o
# arquivo visivel e' um symlink ou uma copia (muOS faz isso) e `dirname $0`
# apontaria para o lugar errado. A busca relativa ao script vem primeiro; a
# lista fixa cobre os layouts de raiz de ROM conhecidos por CFW.
# Padrao herdado do fix Stardew Valley v1.1.6 (relato muOS/RG 40XX-H).

SELF=$0
[ -L "$SELF" ] && SELF=$(readlink -f -- "$SELF" 2>/dev/null || printf '%s' "$0")
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$SELF")" 2>/dev/null && pwd -P) ||
  exit 1

for launcher in \
  "$SCRIPT_DIR/chrono/run.sh" \
  "$SCRIPT_DIR/../ports/chrono/run.sh" \
  "$SCRIPT_DIR/../../ports/chrono/run.sh" \
  /roms/ports/chrono/run.sh \
  /roms2/ports/chrono/run.sh \
  /storage/roms/ports/chrono/run.sh \
  /mnt/mmc/ports/chrono/run.sh \
  /mnt/mmc/roms/ports/chrono/run.sh \
  /mnt/mmc/ROMS/ports/chrono/run.sh \
  /mnt/sdcard/ports/chrono/run.sh \
  /mnt/sdcard/roms/ports/chrono/run.sh \
  /mnt/sdcard/ROMS/ports/chrono/run.sh \
  /userdata/roms/ports/chrono/run.sh
do
  # Invocado por `bash`: cartao FAT/exFAT e zip extraido a mao perdem o bit +x.
  if [ -f "$launcher" ] && [ ! -L "$launcher" ]; then
    exec bash "$launcher" "$@"
  fi
done

# Falhar em silencio e' o pior modo de falha: o frontend descarta o stderr, o
# port volta ao menu e NENHUM arquivo e' gerado. O erro fica gravado em disco.
message="Chrono Trigger: chrono/run.sh nao encontrado (script=$SELF dir=$SCRIPT_DIR)"
printf '%s\n' "$message" >&2
for spot in "$SCRIPT_DIR/chrono-launcher-error.log" \
            "${TMPDIR:-/tmp}/chrono-launcher-error.log"
do
  if printf '%s\n' "$message" > "$spot" 2>/dev/null; then
    printf 'Chrono Trigger: detalhes em %s\n' "$spot" >&2
    printf 'Chrono Trigger: detalhes em %s\n' "$spot" > /dev/tty0 2>/dev/null
    break
  fi
done
exit 1
