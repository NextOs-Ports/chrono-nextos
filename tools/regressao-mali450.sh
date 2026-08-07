#!/usr/bin/env bash
# Regressão do Mali-450 (NextOS/EmuELEC) com o binário UNIVERSAL.
#
# FERRAMENTA DE TESTE — não entra no pacote público. Roda no HOST e conduz o
# device por SSH. O IP é argumento obrigatório: o Claude nunca escolhe IP.
#
#   tools/regressao-mali450.sh <IP-do-Mali-450>
#
# Prova o MESMO ciclo já provado no R36S, para garantir que ganhar o ArkOS não
# custou o Mali-450: abre → título → menu em inglês → New Game → gameplay →
# save → SELECT+START → processo zerado.
set -uo pipefail

IP=${1:?uso: regressao-mali450.sh <IP>}
PORT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)
SSH=(sshpass -p emuelec ssh -o StrictHostKeyChecking=no -o ConnectTimeout=10 "root@$IP")
SCP=(sshpass -p emuelec scp -o StrictHostKeyChecking=no)
GAMEDIR=/storage/roms/ports/chrono

echo "== 1. aparelho responde? =="
"${SSH[@]}" 'hostname; uname -m; ldd --version | head -1' || {
  echo "FALHA: $IP nao responde. Avisar o NextOS e ESPERAR — nunca varrer outro IP." >&2
  exit 1
}

echo "== 2. o firmware fornece as libs que o binario universal pede? =="
"${SSH[@]}" 'for l in libSDL2-2.0.so.0 libGLESv2.so.2 libfreetype.so.6 libEGL.so.1; do
  printf "%-22s " "$l"; (ls /usr/lib/$l /usr/lib/*/$l 2>/dev/null | head -1) || echo AUSENTE
done'

echo "== 3. matar e CONFIRMAR zero instancias (pelo diretorio) =="
"${SSH[@]}" "for p in /proc/[0-9]*; do
    e=\$(readlink \$p/exe 2>/dev/null); c=\$(readlink \$p/cwd 2>/dev/null)
    case \"\$e\$c\" in *chrono*) echo \"matando \${p##*/} \$e\"; kill -9 \${p##*/};; esac
  done; sleep 3
  n=0; for p in /proc/[0-9]*; do e=\$(readlink \$p/exe 2>/dev/null)
    case \"\$e\" in *chrono*) n=\$((n+1));; esac; done
  echo \"instancias vivas: \$n\"; [ \$n -eq 0 ]" || { echo "FALHA: instancia viva" >&2; exit 1; }

echo "== 4. subir o binario universal e o launcher =="
"${SSH[@]}" "mkdir -p $GAMEDIR; rm -f $GAMEDIR/chrono-universal"
"${SCP[@]}" "$PORT_DIR/chrono-universal" "root@$IP:$GAMEDIR/" || exit 1
"${SCP[@]}" "$PORT_DIR/run.sh" "$PORT_DIR/tools/virtual-pad.py" "root@$IP:$GAMEDIR/" || exit 1
"${SCP[@]}" "$PORT_DIR/Chrono Trigger.sh" "root@$IP:/storage/roms/ports_scripts/" 2>/dev/null
"${SSH[@]}" "chmod +x $GAMEDIR/chrono-universal $GAMEDIR/run.sh;
  sha256sum $GAMEDIR/chrono-universal"
echo "sha local: $(sha256sum "$PORT_DIR/chrono-universal" | cut -d' ' -f1)"

echo "== 5. ciclo completo =="
"${SSH[@]}" "pkill -9 -f virtual-pad.py; cd $GAMEDIR || exit 1
  rm -f shot_*.raw out.pcm
  export CHRONO_SHOTEVERY=240 CHRONO_SHOTMAX=60 CHRONO_FPSLOG=1
  export CHRONO_PCMDUMP=1 CHRONO_PCMSKIP=150
  FIFO=/tmp/ctpad.fifo; rm -f \$FIFO; mkfifo \$FIFO
  python3 virtual-pad.py --daemon \$FIFO >/tmp/ctpad.log 2>&1 & PAD=\$!
  bash run.sh & RUN=\$!
  exec 3>\$FIFO; p(){ echo \"\$1\" >&3; }
  sleep 28
  p 'a:0.3'; sleep 5; p 'a:0.3'; sleep 8
  p 'dpad_left:0.3'; sleep 3; p 'a:0.3'; sleep 9; p 'a:0.3'; sleep 12
  p 'dpad_down:0.3'; sleep 3; p 'dpad_left:0.3'; sleep 3; p 'a:0.35'; sleep 5
  p 'dpad_right:0.3'; sleep 3; p 'a:0.35'; sleep 25
  p 'a:0.25'; sleep 5; p 'a:0.25'; sleep 5; p 'a:0.25'; sleep 8
  sleep 60
  p 'dpad_down:1.2'; sleep 2; p 'dpad_right:1.2'; sleep 2; p 'dpad_up:1.2'; sleep 3
  sleep 20
  p 'select+start:1.0'; sleep 1; exec 3>&-
  for i in \$(seq 1 30); do pgrep -f chrono-universal >/dev/null || break; sleep 1; done
  kill \$PAD 2>/dev/null
  echo '--- processos ---'; ps aux | grep -c '[c]hrono-universal'
  echo '--- video ---'; grep -E 'VIDEO backend|VIDEO eglconfig|glFinish' debug.log | head -3
  echo '--- saves ---'; ls -la userdata/ 2>/dev/null
  echo '--- saida ---'; grep -E 'Exiting|SHUTDOWN' debug.log | tail -3
  echo '--- fps (ultimos 40) ---'
  grep -oE '^FPS [0-9]+[.][0-9]' debug.log | tail -40 |
    awk '{s+=\$2; n++} END {printf \"media %.1f fps em %d amostras\n\", s/n, n}'
  echo '--- capturas ---'; ls shot_*.raw | wc -l"
