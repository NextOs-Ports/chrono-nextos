# Installation / Instalação

## English

1. Copy the whole ZIP content into your **ports** folder, keeping the layout:

   ```text
   <ROMS>/ports/Chrono Trigger.sh
   <ROMS>/ports/chrono/…
   ```

   `<ROMS>` is whatever your firmware uses — `/roms`, `/roms2`,
   `/storage/roms`, `/mnt/mmc/ROMS`, `/mnt/sdcard/ROMS` or
   `/userdata/roms`. On EmuELEC/NextOS the visible `Chrono Trigger.sh` may also
   live in `ports_scripts/`; the launcher finds the `chrono/` runtime directory
   and loads `chrono/nxbootstrap.sh` directly either way.

2. Put the **Chrono Trigger APK you legally own** (arm64 / arm64-v8a) into
   `<ROMS>/ports/chrono/gamedata/`. The file name does not matter: the
   installer identifies the package by content (`.apk`, `.apkm`, `.apks`,
   `.xapk` and `.zip` all work).

3. Launch **Chrono Trigger** from the Ports menu. The first launch extracts,
   validates and publishes the data (a few minutes on a slow card); later
   launches go straight to the game.

4. Your APK is never deleted. You can remove it after the install if you want
   the space back — keep a copy, it is required to reinstall.

**Controls** — native pad, Xbox layout. `A` confirms, `B` cancels, the D-pad
navigates. **SELECT + START quits** (saving through the game's own pause path).

**Troubleshooting** — everything is logged next to the port:
`chrono/debug.log` (runtime) and `chrono/nxextract.log` (data installer).
If the launcher itself cannot start, it writes `chrono-launcher-error.<pid>.log`
beside the visible script. "Nothing happened and there is no log" is not a
possible state — if it happens, that is the bug to report.

## Português

1. Copie todo o conteúdo do ZIP para a sua pasta **ports**, mantendo o layout:

   ```text
   <ROMS>/ports/Chrono Trigger.sh
   <ROMS>/ports/chrono/…
   ```

   `<ROMS>` é o que o seu firmware usar — `/roms`, `/roms2`, `/storage/roms`,
   `/mnt/mmc/ROMS`, `/mnt/sdcard/ROMS` ou `/userdata/roms`. No EmuELEC/NextOS o
   `Chrono Trigger.sh` visível também pode ficar em `ports_scripts/`; o launcher
   acha o diretório `chrono/` e carrega `chrono/nxbootstrap.sh` diretamente.

2. Coloque o **APK do Chrono Trigger que você possui legalmente** (arm64 /
   arm64-v8a) em `<ROMS>/ports/chrono/gamedata/`. O nome do arquivo não importa:
   o instalador identifica o pacote pelo conteúdo (`.apk`, `.apkm`, `.apks`,
   `.xapk` e `.zip` funcionam).

3. Abra **Chrono Trigger** no menu Ports. A primeira abertura extrai, valida e
   publica os dados (alguns minutos em cartão lento); as próximas vão direto
   para o jogo.

4. O seu APK nunca é apagado. Você pode removê-lo depois da instalação se
   quiser o espaço de volta — guarde uma cópia, ela é necessária para
   reinstalar.

**Controles** — controle nativo, padrão Xbox. `A` confirma, `B` cancela, o
direcional navega. **SELECT + START fecha** (salvando pelo caminho de pausa do
próprio jogo).

**Se der errado** — tudo fica registrado ao lado do port: `chrono/debug.log`
(runtime) e `chrono/nxextract.log` (instalador de dados). Se nem o launcher
subir, ele grava `chrono-launcher-error.<pid>.log` ao lado do script visível.
"Não aconteceu nada e não tem log" não é um estado possível — se acontecer, é
esse o bug a relatar.
