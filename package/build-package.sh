#!/usr/bin/env bash
# Monta e AUDITA o pacote publico universal BYO-data do Chrono Trigger.
# Molde estrutural: ports/hitmango/package/build-package.sh (port aprovado).
set -euo pipefail

export LC_ALL=C
export TZ=UTC
umask 022

fail() {
  printf 'package error: %s\n' "$*" >&2
  exit 1
}

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
PORT_DIR=$(cd -- "$SCRIPT_DIR/.." && pwd -P)
REPO_ROOT=$(cd -- "$PORT_DIR/../.." && pwd -P)
STATIC_DIR="$SCRIPT_DIR/universal"
BINARY=${CT_PACKAGE_BINARY:-"$PORT_DIR/chrono-nextos"}
OUTPUT=${1:-"$PORT_DIR/.build/Chrono.NextOS-v1.0.6.zip"}
SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH:-1785628800}

if [[ ${CT_SKIP_BUILD:-0} != 1 ]]; then
  "$PORT_DIR/build_universal.sh"
fi
[[ -f "$BINARY" ]] || fail "runtime universal ausente: $BINARY"

for tool in awk bash cmp dirname file find grep install mkdir mktemp python3 \
            readelf rm sed sha256sum sort touch unzip zip; do
  command -v "$tool" >/dev/null 2>&1 || fail "ferramenta ausente no host: $tool"
done

case "$SOURCE_DATE_EPOCH" in
  ''|*[!0-9]*) fail "SOURCE_DATE_EPOCH precisa ser um timestamp Unix" ;;
esac
(( SOURCE_DATE_EPOCH >= 315532800 )) || fail "SOURCE_DATE_EPOCH antes do ZIP"
(( SOURCE_DATE_EPOCH <= 4354819198 )) || fail "SOURCE_DATE_EPOCH depois do ZIP"
(( SOURCE_DATE_EPOCH % 2 == 0 )) || fail "SOURCE_DATE_EPOCH precisa ser par"

TMP_ROOT=$(mktemp -d "${TMPDIR:-/tmp}/chrono-package.XXXXXX")
STAGE="$TMP_ROOT/stage"
TMP_ZIP="$TMP_ROOT/chrono.zip"
trap 'rm -rf -- "$TMP_ROOT"' EXIT INT TERM
mkdir -p "$STAGE/chrono"

# The visible launcher is generated from the canonical nxbootstrap 0.6.3
# template. Packaging fails if either the checked-in launcher or manifest has
# drifted from that single source of truth.
GENERATED="$TMP_ROOT/generated"
PYTHONDONTWRITEBYTECODE=1 python3 -B \
  "$PORT_DIR/framework/nxbootstrap/tools/generate-port.py" \
  "$PORT_DIR/nxport.json" --output "$GENERATED"
cmp -s "$PORT_DIR/Chrono Trigger.sh" "$GENERATED/Chrono Trigger.sh" ||
  fail "launcher is not the canonical nxbootstrap 0.6.3 output"
cmp -s "$PORT_DIR/nxport.json" "$GENERATED/chrono/nxport.json" ||
  fail "nxport.json is not canonical"

put() {
  local mode=$1 source=$2 destination=$3
  [[ -f "$source" ]] || fail "fonte do pacote ausente: $source"
  install -D -m "$mode" -- "$source" "$STAGE/$destination"
}

put 0755 "$PORT_DIR/Chrono Trigger.sh"        "Chrono Trigger.sh"
put 0755 "$BINARY"                            "chrono/chrono-nextos"
put 0644 "$PORT_DIR/nxport.json"              "chrono/nxport.json"
put 0644 "$PORT_DIR/README.md"                "chrono/README.md"
put 0644 "$PORT_DIR/CHANGELOG.md"             "chrono/CHANGELOG.md"
put 0644 "$PORT_DIR/NOTICE.md"                "chrono/NOTICE.md"
put 0644 "$PORT_DIR/INSTALLATION.md"          "chrono/INSTALLATION.md"
if [[ -f "$PORT_DIR/LICENSE" ]]; then
  put 0644 "$PORT_DIR/LICENSE" "chrono/LICENSE"
else
  put 0644 "$REPO_ROOT/LICENSE" "chrono/LICENSE"
fi
put 0644 "$PORT_DIR/licenses/NXExtract-MIT.txt" "chrono/licenses/NXExtract-MIT.txt"
put 0644 "$PORT_DIR/fonts/NotoSans-Regular.ttf" "chrono/fonts/NotoSans-Regular.ttf"
put 0644 "$PORT_DIR/fonts/OFL.txt"              "chrono/fonts/OFL.txt"
put 0644 "$STATIC_DIR/gamedata/README.txt"      "chrono/gamedata/README.txt"
put 0644 "$STATIC_DIR/assets/README.txt"        "chrono/assets/README.txt"
put 0644 "$PORT_DIR/version.txt"                "chrono/version.txt"
put 0755 "$PORT_DIR/nxextract/nxextract.py"     "chrono/nxextract/nxextract.py"
put 0755 "$PORT_DIR/nxextract/nxextract-ui"     "chrono/nxextract/nxextract-ui"
put 0755 "$PORT_DIR/nxextract/nxextract-runtime-env.sh" \
  "chrono/nxextract/nxextract-runtime-env.sh"
put 0755 "$PORT_DIR/nxextract/run-extractor.sh" \
  "chrono/nxextract/run-extractor.sh"
put 0644 "$PORT_DIR/extractor.json"             "chrono/extractor.json"
put 0644 "$PORT_DIR/nxextract-version.txt"      "chrono/nxextract-version.txt"

# GUARD DOS SHA PINADOS (licao Stardew v1.1.7): `nxextract-version.txt` registra
# o sha256 de cada arquivo vendorizado. Mexer no extrator ou na receita e esquecer
# de regerar o pino ja mandou release com o pino MENTINDO sobre o conteudo. Aqui
# o pacote FALHA em vez de sair com registro errado.
verify_pins() {
  local pins=$PORT_DIR/nxextract-version.txt name expected actual bad=0
  [ -f "$pins" ] || fail "nxextract-version.txt ausente"
  while read -r name expected; do
    case "$name" in ''|'#'*) continue ;; esac
    [ -f "$PORT_DIR/$name" ] || fail "pino aponta para arquivo ausente: $name"
    actual=$(sha256sum -- "$PORT_DIR/$name" | cut -d' ' -f1)
    if [ "$actual" != "$expected" ]; then
      printf 'pino DESATUALIZADO: %s\n  registrado: %s\n  real:       %s\n' \
        "$name" "$expected" "$actual" >&2
      bad=1
    fi
  done <<EOF
$(sed -n 's/^\([^ ]*\) sha256=\([0-9a-f]\{64\}\)$/\1 \2/p' "$pins")
EOF
  [ "$bad" -eq 0 ] ||
    fail "regenere nxextract-version.txt antes de empacotar (sha pinado != arquivo real)"
  printf 'pinos do NXExtract conferidos contra os arquivos reais\n'
}
verify_pins

glibc_at_most() {
  local candidate=$1 maximum=$2 newest version major minor machine
  machine=$(readelf -h "$candidate" |
    sed -n 's/^[[:space:]]*Machine:[[:space:]]*//p')
  [[ $machine == AArch64 ]] ||
    fail "${candidate#"$STAGE/"} nao e' AArch64 (achado: $machine)"
  newest=$(readelf --version-info "$candidate" 2>/dev/null |
    grep -oE 'GLIBC_[0-9]+([.][0-9]+)*' | sort -Vu | tail -1)
  [[ -n $newest ]] || fail "ABI glibc indeterminada: ${candidate#"$STAGE/"}"
  version=${newest#GLIBC_}
  major=${version%%.*}
  minor=${version#*.}; minor=${minor%%.*}
  if (( major > 2 || (major == 2 && minor > maximum) )); then
    fail "${candidate#"$STAGE/"} exige $newest (maximo: GLIBC_2.$maximum)"
  fi
}

# Auditoria: o loader e a UI do NXExtract sao os UNICOS ELFs permitidos. As
# bibliotecas Android originais entram depois, fornecidas pelo dono do jogo.
while IFS= read -r -d '' candidate; do
  kind=$(file -b "$candidate")
  case "$kind" in
    *ELF*)
      relative=${candidate#"$STAGE/"}
      case "$relative" in
        chrono/chrono-nextos|chrono/nxextract/nxextract-ui) ;;
        *) fail "ELF inesperado entrou no pacote: $relative" ;;
      esac
      glibc_at_most "$candidate" 30
      ;;
    *PE32*|*Mach-O*)
      fail "executavel estrangeiro no pacote: ${candidate#"$STAGE/"}"
      ;;
  esac
done < <(find "$STAGE" -type f -print0)

PAD_LAYOUT=$(readelf -sW "$STAGE/chrono/chrono-nextos" |
  awk '$4 == "TLS" && $8 == "g_bionic_guard_pad" {
    value=$2 ":" $3
  } END { print value }')
[[ $PAD_LAYOUT == 0000000000000000:256 ]] ||
  fail "layout TLS auditado mudou: pad=$PAD_LAYOUT"

# Bits +x esperados: nenhuma etapa critica depende deles, mas o zip precisa
# entregar o conjunto certo (a lista nao pode encolher junto com refactor).
for expected in "Chrono Trigger.sh" chrono/chrono-nextos \
                chrono/nxextract/nxextract.py \
                chrono/nxextract/nxextract-ui \
                chrono/nxextract/nxextract-runtime-env.sh \
                chrono/nxextract/run-extractor.sh; do
  [[ -x "$STAGE/$expected" ]] || fail "faltou bit +x esperado: $expected"
done

bash -n "$STAGE/Chrono Trigger.sh"
bash -n "$STAGE/chrono/nxextract/nxextract-runtime-env.sh"
bash -n "$STAGE/chrono/nxextract/run-extractor.sh"
if grep -En '^[[:space:]]*(export[[:space:]]+)?SDL_(VIDEO|AUDIO)DRIVER=' \
    "$STAGE/Chrono Trigger.sh"; then
  fail "o launcher nao pode fixar backend SDL de video ou audio"
fi
if find "$STAGE" \( -name 'nxbootstrap.sh' -o -name 'run.sh' -o \
    -name 'chrono-universal' \) -print -quit | grep -q .; then
  fail "legacy launcher layer or executable name entered the package"
fi
if grep -En \
    '(^|[[:space:]])(setsid|nohup|systemctl[[:space:]]+(stop|mask|restart)|pkill)([[:space:]]|$)' \
    "$STAGE/Chrono Trigger.sh" |
    grep -v '^[^:]*:[0-9]*:[[:space:]]*#'; then
  fail "o launcher contem comando de ciclo de vida proibido"
fi
if grep -En 'gptokeyb' "$STAGE/Chrono Trigger.sh" |
    grep -v '^[^:]*:[0-9]*:#'; then
  fail "gptokeyb roubaria o pad: o controle deste port e' NATIVO"
fi

# Nenhum dado de jogo, nenhuma lib proprietaria, nenhum artefato de dev.
if find "$STAGE" \( \
    -iname '*.apk' -o -iname '*.apkm' -o -iname '*.apks' -o \
    -iname '*.xapk' -o -iname '*.obb' -o -iname '*.dex' -o \
    -name 'libchrono.so' -o -name 'libc++_shared.so' -o \
    -name 'libencrypt.so' -o -name 'resources.bin' -o \
    -name '*.dat' -o -name 'Roboto*.ttf' \
  \) -print -quit | grep -q .; then
  fail "dado de jogo proprietario entrou na arvore publica"
fi
if find "$STAGE" \( \
    -iname '*.log' -o -iname '*.raw' -o -iname '*.ppm' -o -iname '*.pcm' -o \
    -name 'HANDOFF.md' -o -name '__pycache__' -o -name '*.pyc' -o \
    -name 'userdata' -o -name 'debug*.log' -o -name 'scratchpad' \
  \) -print -quit | grep -q .; then
  fail "artefato de desenvolvimento ou pessoal entrou na arvore publica"
fi
if grep -IRnE '192[.]168[.]|169[.]254[.]|10[.][0-9]+[.]|/home/|/media/|root@|[A-Za-z0-9._%+-]+@(gmail|hotmail|outlook|yahoo|proton)[.]' \
    "$STAGE" --include='*.sh' --include='*.md' --include='*.txt' \
    --include='*.json' --include='*.py'; then
  fail "texto da release contem endereco de teste ou caminho pessoal"
fi

(
  cd "$STAGE"
  find . -type f ! -path './chrono/PACKAGE-MANIFEST.sha256' \
    -printf '%P\n' | sort | while IFS= read -r relative; do
      sha256sum -- "$relative"
    done
) > "$STAGE/chrono/PACKAGE-MANIFEST.sha256"

find "$STAGE" -exec touch -h -d "@$SOURCE_DATE_EPOCH" {} +
(
  cd "$STAGE"
  find . -type f -printf '%P\n' | sort | zip -X -9 -q "$TMP_ZIP" -@
)
unzip -tq "$TMP_ZIP" >/dev/null

VERIFY="$TMP_ROOT/verify"
mkdir -p "$VERIFY"
unzip -q "$TMP_ZIP" -d "$VERIFY"
(
  cd "$VERIFY"
  sha256sum -c chrono/PACKAGE-MANIFEST.sha256 >/dev/null
)

mkdir -p "$(dirname -- "$OUTPUT")"
OUTPUT_DIR=$(cd -- "$(dirname -- "$OUTPUT")" && pwd -P)
OUTPUT="$OUTPUT_DIR/$(basename -- "$OUTPUT")"
install -m 0644 "$TMP_ZIP" "$OUTPUT"
(
  cd "$OUTPUT_DIR"
  sha256sum "$(basename -- "$OUTPUT")" > "$(basename -- "$OUTPUT").sha256"
)

printf 'PACKAGE OK: %s\n' "$OUTPUT"
printf 'loader ABI: GLIBC <= 2.30 | TLS pad %s\n' "$PAD_LAYOUT"
sha256sum "$BINARY" "$OUTPUT"
