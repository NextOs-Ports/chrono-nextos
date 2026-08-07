/*
 * Chrono Trigger (Cocos2d-x 3.14.1) -> aarch64 Linux so-loader (Mali-450 fbdev)
 *
 * Carrega libchrono.so, resolve imports, monta JNIEnv falso e dirige o
 * fluxo Cocos2dxActivity/Cocos2dxRenderer sem ART:
 *   JNI_OnLoad -> nativeSetApkPath/setAssetManager/nativeSetContext
 *   -> nativeInit(w,h) [cria GLView, cocos_android_app_init, Application::run]
 *   -> loop: nativeRender() [Director::mainLoop] + SwapWindow
 * Input: SDL -> GameControllerAdapter (cocos Controller::Key) e/ou nativeKeyEvent.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/mman.h>
#include <SDL2/SDL.h>

#include "ct_platform.h"
#include "error.h"
#include "imports.h"
#include "jni_shim.h"
#include "so_util.h"
#include "util.h"

typedef int jint;
typedef unsigned char jboolean;

#define MEMORY_MB 256
#define SO_NAME "libchrono.so"

/* ---- Android keycodes (para nativeKeyEvent / fallback teclado) ---- */
#define AKEYCODE_DPAD_UP 19
#define AKEYCODE_DPAD_DOWN 20
#define AKEYCODE_DPAD_LEFT 21
#define AKEYCODE_DPAD_RIGHT 22
#define AKEYCODE_DPAD_CENTER 23
#define AKEYCODE_BUTTON_A 96
#define AKEYCODE_BUTTON_B 97
#define AKEYCODE_BUTTON_X 99
#define AKEYCODE_BUTTON_Y 100
#define AKEYCODE_BUTTON_L1 102
#define AKEYCODE_BUTTON_R1 103
#define AKEYCODE_BUTTON_START 108
#define AKEYCODE_BUTTON_SELECT 109
#define AKEYCODE_ENTER 66
#define AKEYCODE_BACK 4

/* ---- cocos2d::Controller::Key (CCController.h) ---- */
enum {
  CK_JOYSTICK_LEFT_X = 1000, CK_JOYSTICK_LEFT_Y, CK_JOYSTICK_RIGHT_X, CK_JOYSTICK_RIGHT_Y,
  CK_BUTTON_A, CK_BUTTON_B, CK_BUTTON_C, CK_BUTTON_X, CK_BUTTON_Y, CK_BUTTON_Z,
  CK_BUTTON_DPAD_UP, CK_BUTTON_DPAD_DOWN, CK_BUTTON_DPAD_LEFT, CK_BUTTON_DPAD_RIGHT, CK_BUTTON_DPAD_CENTER,
  CK_BUTTON_LEFT_SHOULDER, CK_BUTTON_RIGHT_SHOULDER, CK_AXIS_LEFT_TRIGGER, CK_AXIS_RIGHT_TRIGGER,
  CK_BUTTON_LEFT_THUMBSTICK, CK_BUTTON_RIGHT_THUMBSTICK, CK_BUTTON_START, CK_BUTTON_SELECT, CK_BUTTON_PAUSE
};

/* ---- Cocos2d-x JNI entry points ---- */
static jint (*JNI_OnLoad)(void *vm, void *reserved);
static void (*nativeSetContext)(void *env, void *thiz, void *ctx, void *assetmgr);
static void (*nativeSetApkPath)(void *env, void *thiz, void *apkPath);
static void (*setAssetManager)(void *env, void *clazz, void *assetmgr);
static void (*setExternalStorageInfo)(void *env, void *clazz, void *a, void *b);
static void (*nativeInit)(void *env, void *thiz, int w, int h);
static void (*nativeRender)(void *env, void *thiz);
static void (*nativeOnPause)(void *env, void *thiz);
static void (*nativeOnResume)(void *env, void *thiz);
static void (*nativeKeyEvent)(void *env, void *thiz, int keyCode, jboolean isPressed);
static void (*ctrlConnected)(void *env, void *clazz, void *vendor, int controllerID);
/* ABI REAL (cocos2d-x): vendorName jstring vem ANTES do controllerID. */
static void (*ctrlButton)(void *env, void *clazz, void *vendor, int controllerID, int button, jboolean isPressed, float value);
static void (*ctrlAxis)(void *env, void *clazz, void *vendor, int controllerID, int axis, float value, jboolean analog);
static void (*nativeTouchesBegin)(void *env, void *thiz, int id, float x, float y);
static void (*nativeTouchesEnd)(void *env, void *thiz, int id, float x, float y);
static void (*nativeTouchesMove)(void *env, void *thiz, int id, float x, float y);

static SDL_GameController *g_gamepad = NULL;
static void *g_env = NULL;
static void *g_vendor = NULL; /* jstring nome do controle (reusado em todos os eventos) */
static int g_use_keyboard = 0; /* CHRONO_KEYBOARD=1 -> usar nativeKeyEvent */

/* CANARY BIONIC (provado em SOTN/Bully/Dysmantle): libchrono e' compilada p/
 * bionic e le a stack-canary de tpidr_el0+0x28 (TLS_SLOT_STACK_GUARD). Sob glibc
 * esse offset colide com TLS que o Mali/SDL escreve -> canary "muda" no meio da
 * funcao -> __stack_chk_fail FALSO. Este pad _Thread_local desloca o layout de
 * TLS estatico p/ tpidr+0x28 cair num pad nunca-escrito -> canary estavel. */
__attribute__((used, aligned(16))) _Thread_local char g_bionic_guard_pad[256];

/* salva/restaura tpidr+0x28 ao redor de chamadas SDL_GL (Mali escreve la). */
static SDL_GLContext gl_create_context_guarded(SDL_Window *w) {
  unsigned long tp; __asm__ volatile("mrs %0, tpidr_el0" : "=r"(tp));
  unsigned long g = *(unsigned long *)(tp + 0x28);
  SDL_GLContext c = SDL_GL_CreateContext(w);
  *(unsigned long *)(tp + 0x28) = g;
  return c;
}
static void gl_swap_guarded(SDL_Window *w) {
  unsigned long tp; __asm__ volatile("mrs %0, tpidr_el0" : "=r"(tp));
  unsigned long g = *(unsigned long *)(tp + 0x28);
  SDL_GL_SwapWindow(w);
  *(unsigned long *)(tp + 0x28) = g;
}

/* SDL controller button -> cocos Controller::Key */
static int map_btn_cocos(int b) {
  switch (b) {
    case SDL_CONTROLLER_BUTTON_A: return CK_BUTTON_A;
    case SDL_CONTROLLER_BUTTON_B: return CK_BUTTON_B;
    case SDL_CONTROLLER_BUTTON_X: return CK_BUTTON_X;
    case SDL_CONTROLLER_BUTTON_Y: return CK_BUTTON_Y;
    case SDL_CONTROLLER_BUTTON_START: return CK_BUTTON_START;
    case SDL_CONTROLLER_BUTTON_BACK: return CK_BUTTON_SELECT;
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return CK_BUTTON_LEFT_SHOULDER;
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return CK_BUTTON_RIGHT_SHOULDER;
    case SDL_CONTROLLER_BUTTON_DPAD_UP: return CK_BUTTON_DPAD_UP;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return CK_BUTTON_DPAD_DOWN;
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return CK_BUTTON_DPAD_LEFT;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return CK_BUTTON_DPAD_RIGHT;
    case SDL_CONTROLLER_BUTTON_LEFTSTICK: return CK_BUTTON_LEFT_THUMBSTICK;
    case SDL_CONTROLLER_BUTTON_RIGHTSTICK: return CK_BUTTON_RIGHT_THUMBSTICK;
    default: return -1;
  }
}
/* SDL controller button -> Android keycode (fallback teclado) */
static int map_btn_android(int b) {
  switch (b) {
    case SDL_CONTROLLER_BUTTON_A: return AKEYCODE_BUTTON_A;
    case SDL_CONTROLLER_BUTTON_B: return AKEYCODE_BUTTON_B;
    case SDL_CONTROLLER_BUTTON_X: return AKEYCODE_BUTTON_X;
    case SDL_CONTROLLER_BUTTON_Y: return AKEYCODE_BUTTON_Y;
    case SDL_CONTROLLER_BUTTON_START: return AKEYCODE_BUTTON_START;
    case SDL_CONTROLLER_BUTTON_BACK: return AKEYCODE_BUTTON_SELECT;
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return AKEYCODE_BUTTON_L1;
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return AKEYCODE_BUTTON_R1;
    case SDL_CONTROLLER_BUTTON_DPAD_UP: return AKEYCODE_DPAD_UP;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return AKEYCODE_DPAD_DOWN;
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return AKEYCODE_DPAD_LEFT;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return AKEYCODE_DPAD_RIGHT;
    default: return -1;
  }
}
/* SDL keyboard -> Android keycode */
static int map_key_android(SDL_Keycode k) {
  switch (k) {
    case SDLK_UP: return AKEYCODE_DPAD_UP;
    case SDLK_DOWN: return AKEYCODE_DPAD_DOWN;
    case SDLK_LEFT: return AKEYCODE_DPAD_LEFT;
    case SDLK_RIGHT: return AKEYCODE_DPAD_RIGHT;
    case SDLK_SPACE: case SDLK_z: return AKEYCODE_BUTTON_A;
    case SDLK_LCTRL: case SDLK_x: return AKEYCODE_BUTTON_B;
    case SDLK_LSHIFT: case SDLK_a: return AKEYCODE_BUTTON_X;
    case SDLK_LALT: case SDLK_s: return AKEYCODE_BUTTON_Y;
    case SDLK_q: return AKEYCODE_BUTTON_L1;
    case SDLK_w: return AKEYCODE_BUTTON_R1;
    case SDLK_RETURN: return AKEYCODE_BUTTON_START;
    case SDLK_BACKSPACE: return AKEYCODE_BUTTON_SELECT;
    case SDLK_ESCAPE: return AKEYCODE_BACK;
    default: return -1;
  }
}

/* SELECT+START pelo caminho SDL. Junto com o combo cru do evdev
   (ct_exit_chord_poll) e com o SIGTERM, cai no MESMO pedido de shutdown. */
static int g_chord_select, g_chord_start;

static void track_exit_chord(int sdl_button, int pressed) {
  if (sdl_button == SDL_CONTROLLER_BUTTON_BACK) g_chord_select = pressed;
  else if (sdl_button == SDL_CONTROLLER_BUTTON_START) g_chord_start = pressed;
  if (g_chord_select && g_chord_start) ct_request_exit("SELECT+START (SDL)");
}

/* LATCH: um toque mais curto que um frame chega como DOWN e UP no MESMO
   SDL_PollEvent. A engine so' le o estado 1x por frame (GameController::update),
   entao o toque sumiria. Guardamos o release para o frame seguinte. */
#define CT_BUTTONS 24
static unsigned char g_btn_down[CT_BUTTONS];
static unsigned char g_btn_pending_release[CT_BUTTONS];
static unsigned char g_btn_down_this_frame[CT_BUTTONS];

static void send_button(int sdl_button, int pressed);

static void flush_pending_releases(void) {
  for (int b = 0; b < CT_BUTTONS; b++) {
    g_btn_down_this_frame[b] = 0;
    if (g_btn_pending_release[b]) {
      g_btn_pending_release[b] = 0;
      send_button(b, 0);
    }
  }
}

/* Nenhum botao pode ficar preso quando o foco/controle vai embora. */
static void release_all_buttons(void) {
  for (int b = 0; b < CT_BUTTONS; b++) {
    g_btn_pending_release[b] = 0;
    if (g_btn_down[b]) send_button(b, 0);
  }
  g_chord_select = g_chord_start = 0;
}

static void send_button(int sdl_button, int pressed) {
  if (sdl_button >= 0 && sdl_button < CT_BUTTONS) {
    if (pressed) {
      g_btn_down[sdl_button] = 1;
      g_btn_down_this_frame[sdl_button] = 1;
    } else {
      if (g_btn_down_this_frame[sdl_button]) {
        /* solta so' no proximo frame: a engine precisa VER o press. */
        g_btn_pending_release[sdl_button] = 1;
        return;
      }
      g_btn_down[sdl_button] = 0;
    }
  }
  track_exit_chord(sdl_button, pressed);
  if (g_use_keyboard) {
    if (!nativeKeyEvent) return;
    int kc = map_btn_android(sdl_button);
    if (kc >= 0) nativeKeyEvent(g_env, NULL, kc, pressed);
  } else {
    if (!ctrlButton) return;
    int ck = map_btn_cocos(sdl_button);
    if (ck >= 0) ctrlButton(g_env, NULL, g_vendor, 0, ck, pressed, pressed ? 1.0f : 0.0f);
  }
}

/* Abre TODOS os controles reconhecidos, nao so' o primeiro: em varios CFWs o
   pad embutido nao e' o indice 0, e um pad extra (BT/USB) precisa funcionar
   junto. Todos alimentam o mesmo boundary nativo da engine. */
#define CT_MAX_PADS 4
static SDL_GameController *g_pads[CT_MAX_PADS];
static void open_gamepad(void) {
  for (int i = 0; i < SDL_NumJoysticks(); i++) {
    if (!SDL_IsGameController(i)) continue;
    SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(i);
    int already = 0, slot = -1;
    for (int s = 0; s < CT_MAX_PADS; s++) {
      if (g_pads[s]) {
        if (SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(g_pads[s])) == id)
          already = 1;
      } else if (slot < 0) slot = s;
    }
    if (already || slot < 0) continue;
    SDL_GameController *c = SDL_GameControllerOpen(i);
    if (!c) continue;
    g_pads[slot] = c;
    if (!g_gamepad) g_gamepad = c;
    debugPrintf("Gamepad[%d]: %s (instance=%d)\n", slot,
                SDL_GameControllerName(c), (int)id);
  }
  if (!g_gamepad) debugPrintf("Nenhum controle reconhecido pelo SDL ainda\n");
}
static void close_gamepads(void) {
  for (int s = 0; s < CT_MAX_PADS; s++)
    if (g_pads[s]) { SDL_GameControllerClose(g_pads[s]); g_pads[s] = NULL; }
  g_gamepad = NULL;
}
static void drop_gamepad(SDL_JoystickID id) {
  for (int s = 0; s < CT_MAX_PADS; s++) {
    if (!g_pads[s]) continue;
    if (SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(g_pads[s])) != id)
      continue;
    if (g_gamepad == g_pads[s]) g_gamepad = NULL;
    SDL_GameControllerClose(g_pads[s]);
    g_pads[s] = NULL;
  }
  for (int s = 0; s < CT_MAX_PADS && !g_gamepad; s++)
    if (g_pads[s]) g_gamepad = g_pads[s];
}

extern void opensles_shim_pump_callbacks(void) __attribute__((weak));

/* valor do enum LocalizationLanguageType p/ ingles (ajustavel via CHRONO_LANG) */
int chrono_forced_lang(void) {
  const char *e = getenv("CHRONO_LANG");
  return e ? atoi(e) : 1;
}
/* GameController::isConnected forcado true -> menu poll o controle */
int chrono_force_connected(void) { return 1; }

/* trampolim passthrough: copia as 4 primeiras instrucoes (16B, nenhuma PC-rel)
   e salta p/ addr+16, permitindo logar + chamar a original. */
static void *make_passthrough(uintptr_t addr) {
  uint32_t *tr = mmap(NULL, 64, PROT_READ|PROT_WRITE|PROT_EXEC, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
  if (tr == MAP_FAILED) return NULL;
  uint32_t *orig = (uint32_t *)addr;
  for (int i = 0; i < 4; i++) tr[i] = orig[i];
  tr[4] = 0x58000051u; /* LDR X17,#8 */
  tr[5] = 0xd61f0220u; /* BR X17 */
  *(uint64_t *)(tr + 6) = addr + 16;
  __builtin___clear_cache((char *)tr, (char *)tr + 64);
  return tr;
}
typedef void (*director_end_t)(void *);
static director_end_t g_director_end_orig = NULL;
static void my_director_end(void *self) {
  ct_request_exit("cocos2d::Director::end");
  if (g_director_end_orig) g_director_end_orig(self);
}

typedef void (*onkd_t)(void *, void *, int, void *);
static onkd_t g_onkd_orig = NULL;
static void my_onKeyDown(void *self, void *ctrl, int key, void *ev) {
  debugPrintf("ONKEYDOWN key=%d (0x%x) self=%p ctrl=%p\n", key, key, self, ctrl);
  if (g_onkd_orig) g_onkd_orig(self, ctrl, key, ev);
}

/* screenshot confiavel via glReadPixels (fb0 falha durante render Mali). */
extern const unsigned char *glGetString(unsigned name);
extern void glReadPixels(int x, int y, int w, int h, unsigned fmt, unsigned type, void *px);
extern void glFinish(void);

/* ------------------------------------------------- scanout OPACO no present --
 * O Cocos deixa no backbuffer o alpha do PROPRIO jogo. Medido neste port no
 * Mali-450: a tela de menu sai com alpha 0 em 78,5% dos pixels e alpha 255 so'
 * onde ha arte; durante o fade do titulo chega a 97,6% de alpha 0.
 *
 * Compositor que IGNORA o alpha por pixel mostra a imagem assim mesmo -- e' o
 * caso do OSD do Amlogic como o NextOS o configura e do plano opaco do KMSDRM
 * no ArkOS, os dois aparelhos onde o port foi validado. Compositor que HONRA o
 * alpha entende a mesma tela como quase toda transparente e o painel fica
 * PRETO exatamente depois de o titulo desaparecer.
 *
 * Mesmo mecanismo ja pago no Horizon Chase, no LSWTFA e nos quatro Zenonia,
 * que forcam o alpha opaco antes do swap. Aqui faltava.
 *
 * ARMADILHA (custou a v1.2.0 do Horizon Chase): o driver GLES3 chega no swap
 * com um FBO != 0 amarrado, e o clear vai parar no FBO em vez do backbuffer --
 * a flag fica ligada e o conserto nao acontece. Por isso lemos o desenho
 * amarrado, trocamos por 0, limpamos SO' o alpha e devolvemos o que estava; e
 * logamos UMA vez que rodou de fato, com o FBO que estava la. */
extern void glColorMask(unsigned char r, unsigned char g, unsigned char b, unsigned char a);
extern void glClear(unsigned mask);
extern void glClearColor(float r, float g, float b, float a);
extern void glBindFramebuffer(unsigned target, unsigned framebuffer);
extern void glGetIntegerv(unsigned pname, int *params);
extern void glGetFloatv(unsigned pname, float *params);
extern unsigned char glIsEnabled(unsigned cap);
extern void glEnable(unsigned cap);
extern void glDisable(unsigned cap);

#define CT_GL_SCISSOR_TEST      0x0C11u
#define CT_GL_COLOR_CLEAR_VALUE 0x0C22u
#define CT_GL_COLOR_BUFFER_BIT  0x4000u
#define CT_GL_FRAMEBUFFER       0x8D40u
/* 0x8CA6 e' GL_FRAMEBUFFER_BINDING no ES2 e GL_DRAW_FRAMEBUFFER_BINDING no
   ES3: mesmo valor, entao a consulta serve nos dois. */
#define CT_GL_FRAMEBUFFER_BINDING 0x8CA6u

static void ct_force_opaque_scanout(void) {
  static int enabled = -1;
  if (enabled < 0) {
    const char *e = getenv("CHRONO_OPAQUE");   /* =0 desliga, so' p/ comparar */
    enabled = e ? (atoi(e) != 0) : 1;
    if (!enabled) debugPrintf("VIDEO scanout opaco DESLIGADO por CHRONO_OPAQUE=0\n");
  }
  if (!enabled) return;

  int prev_fb = 0;
  glGetIntegerv(CT_GL_FRAMEBUFFER_BINDING, &prev_fb);
  unsigned char scissor = glIsEnabled(CT_GL_SCISSOR_TEST);
  float cc[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
  glGetFloatv(CT_GL_COLOR_CLEAR_VALUE, cc);

  if (scissor) glDisable(CT_GL_SCISSOR_TEST);
  if (prev_fb) glBindFramebuffer(CT_GL_FRAMEBUFFER, 0u);
  glColorMask(0, 0, 0, 1);
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(CT_GL_COLOR_BUFFER_BIT);
  glColorMask(1, 1, 1, 1);
  glClearColor(cc[0], cc[1], cc[2], cc[3]);
  if (prev_fb) glBindFramebuffer(CT_GL_FRAMEBUFFER, (unsigned)prev_fb);
  if (scissor) glEnable(CT_GL_SCISSOR_TEST);

  static int logged = 0;
  if (!logged) {
    logged = 1;
    debugPrintf("VIDEO scanout opaco aplicado (FBO amarrado no swap=%d, "
                "scissor=%d)\n", prev_fb, (int)scissor);
  }
}
static void chrono_dump_shot(int w, int h, int frame) {
  size_t n = (size_t)w * h * 4;
  unsigned char *buf = malloc(n);
  if (!buf) return;
  glReadPixels(0, 0, w, h, 0x1908 /*GL_RGBA*/, 0x1401 /*GL_UNSIGNED_BYTE*/, buf);
  const char *home = getenv("HOME"); if (!home) home = ".";
  char path[256]; snprintf(path, sizeof path, "%s/shot_%04d.raw", home, frame);
  FILE *f = fopen(path, "wb");
  if (f) { fwrite(buf, 1, n, f); fclose(f); debugPrintf("SHOT %s (%dx%d)\n", path, w, h); }
  free(buf);
}

int main(int argc, char *argv[]) {
  { volatile char c = g_bionic_guard_pad[0]; (void)c; } // anchor TLS pad
  {
    unsigned long tp; __asm__ volatile("mrs %0, tpidr_el0" : "=r"(tp));
    debugPrintf("TLSDIAG tp=0x%lx pad=%p tp+0x28=0x%lx (*=0x%lx) pad_in_range=%d\n",
                tp, (void *)g_bionic_guard_pad, tp + 0x28,
                *(unsigned long *)(tp + 0x28),
                ((uintptr_t)g_bionic_guard_pad <= tp + 0x28 &&
                 tp + 0x28 < (uintptr_t)g_bionic_guard_pad + 256));
  }
  debugPrintf("=== Chrono Trigger (Cocos2d-x) AARCH64 so-loader ===\n");
  g_use_keyboard = getenv("CHRONO_KEYBOARD") != NULL;

  /* Trava no PROPRIO BINARIO: script morto nao pode liberar a trava enquanto o
     jogo continua dono do display/audio. */
  if (ct_single_instance_lock("/proc/self/exe") < 0)
    fatal_error("outra instancia do Chrono Trigger ja esta rodando");
  ct_install_signal_handlers();
  debugPrintf("RAM MemTotal=%ld kB\n", ct_mem_total_kb());

  /* VIDEO e AUDIO sao subsistemas INDEPENDENTES (audio-backend.md §2). Um
     PulseAudio herdado e morto ja derrubou o SDL_Init inteiro e o jogo nao
     abria — custou release corretiva no Oceanhorn v1.0.2 e no Horizon v1.0.3.
     Aqui: sem video nao ha jogo (fatal); sem audio o jogo abre mudo e diz. */
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) < 0) {
    /* Backend HERDADO invalido: uma unica nova tentativa com a variavel
       removida, devolvendo a autodeteccao ao SDL (video-backend.md §1, SOR4).
       Nao escolhemos outro backend — apenas paramos de impor o herdado. */
    const char *inherited = getenv("SDL_VIDEODRIVER");
    debugPrintf("VIDEO: SDL_Init falhou (%s); herdado=%s\n", SDL_GetError(),
                inherited ? inherited : "nenhum");
    if (!inherited) fatal_error("SDL_Init: %s", SDL_GetError());
    unsetenv("SDL_VIDEODRIVER");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) < 0)
      fatal_error("SDL_Init: %s", SDL_GetError());
    debugPrintf("VIDEO: autodeteccao aceita apos remover o SDL_VIDEODRIVER herdado\n");
  }
  ct_init_audio_subsystem();

  SDL_DisplayMode dm;
  if (SDL_GetDesktopDisplayMode(0, &dm) != 0)
    fatal_error("SDL_GetDesktopDisplayMode: %s", SDL_GetError());

  /* Escada de EGLConfig (video-backend.md §4): a PRIMEIRA linha e' exatamente o
     que o port ja validou no Mali-450 e no R36S; as seguintes so' entram se o
     driver recusar a anterior. Driver sem RGBA8888/D24S8 deixava de abrir. */
  static const struct { int alpha, depth, stencil; } ladder[] = {
    { 8, 24, 8 }, { 8, 16, 0 }, { 8, 0, 0 }, { 0, 24, 8 }, { 0, 16, 0 }, { 0, 0, 0 }
  };
  SDL_Window *window = NULL;
  SDL_GLContext glc = NULL;
  for (size_t attempt = 0; attempt < sizeof ladder / sizeof ladder[0]; ++attempt) {
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, ladder[attempt].alpha);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, ladder[attempt].depth);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, ladder[attempt].stencil);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    if (!window) {
      window = SDL_CreateWindow("Chrono Trigger",
          SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, dm.w, dm.h,
          SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_FULLSCREEN);
      if (!window) {
        debugPrintf("VIDEO: SDL_CreateWindow falhou com A%d D%d S%d (%s)\n",
                    ladder[attempt].alpha, ladder[attempt].depth,
                    ladder[attempt].stencil, SDL_GetError());
        continue;
      }
    }
    glc = gl_create_context_guarded(window);
    if (glc) {
      if (attempt)
        debugPrintf("VIDEO: contexto obtido na tentativa %zu (A%d D%d S%d)\n",
                    attempt + 1, ladder[attempt].alpha, ladder[attempt].depth,
                    ladder[attempt].stencil);
      /* Mesa pode devolver DESKTOP GL: os shaders do Cocos sao GLSL ES e a tela
         fica preta (video-backend.md §5). Validar depois de criar e, se vier
         desktop, refazer UMA vez pedindo explicitamente o driver GLES. */
      const char *gl_version = (const char *)glGetString(0x1F02u /*GL_VERSION*/);
      if (gl_version && !strstr(gl_version, "OpenGL ES")) {
        debugPrintf("VIDEO: driver devolveu contexto DESKTOP (%s); refazendo com "
                    "o driver GLES\n", gl_version);
        SDL_GL_DeleteContext(glc);
        glc = NULL;
        SDL_SetHint(SDL_HINT_OPENGL_ES_DRIVER, "1");
        glc = gl_create_context_guarded(window);
        gl_version = glc ? (const char *)glGetString(0x1F02u) : NULL;
        if (!glc || (gl_version && !strstr(gl_version, "OpenGL ES"))) {
          debugPrintf("VIDEO: ainda sem contexto GLES (%s); tentando a proxima "
                      "config\n", gl_version ? gl_version : SDL_GetError());
          if (glc) { SDL_GL_DeleteContext(glc); glc = NULL; }
          SDL_DestroyWindow(window);
          window = NULL;
          continue;
        }
      }
      break;
    }
    debugPrintf("VIDEO: contexto recusado com A%d D%d S%d (%s)\n",
                ladder[attempt].alpha, ladder[attempt].depth,
                ladder[attempt].stencil, SDL_GetError());
    SDL_DestroyWindow(window);
    window = NULL;
  }
  if (!window) fatal_error("SDL_CreateWindow: %s", SDL_GetError());
  if (!glc) fatal_error("SDL_GL_CreateContext: %s", SDL_GetError());
  /* O DRAWABLE REAL manda: 1280x720 no Mali-450 fbdev, 640x480 no R36S. Nada
     de resolucao cravada — tudo (nativeInit, toque injetado, screenshot) usa
     estes w/h. */
  int w, h; SDL_GL_GetDrawableSize(window, &w, &h);
  if (w <= 0 || h <= 0) { w = dm.w; h = dm.h; }
  ct_log_video_profile(window, w, h);

  /* KMSDRM precisa do glFinish antes do swap (30 -> 60fps); fbdev nao. */
  int finish_before_swap = ct_needs_finish_before_swap();
  debugPrintf("VIDEO glFinish antes do swap: %s\n", finish_before_swap ? "sim" : "nao");

  open_gamepad();
  ct_exit_chord_open();

  /* ---- 1) libc++_shared.so (LLVM libc++ do Android, namespace std::__ndk1) ----
     libchrono importa centenas de simbolos dela; carregamos como modulo auxiliar. */
  size_t cxx_size = 32 * 1024 * 1024;
  void *cxx_heap = mmap(NULL, cxx_size, PROT_READ | PROT_WRITE | PROT_EXEC,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (cxx_heap == MAP_FAILED) fatal_error("mmap libc++ heap");
  if (so_load("libc++_shared.so", cxx_heap, cxx_size) < 0) fatal_error("so_load libc++_shared.so");
  debugPrintf("Loaded libc++_shared.so: text=%p+%zu\n", text_base, text_size);
  if (so_relocate() < 0) fatal_error("so_relocate libc++");
  if (so_resolve(dynlib_functions, dynlib_functions_count, 0) < 0) fatal_error("so_resolve libc++");
  //so_debug_scan_got();
  so_make_text_writable();
  so_flush_caches();
  so_execute_init_array();
  so_module *m_cxx = so_save();

  /* ---- 2) libchrono.so (engine Cocos2d-x + jogo), resolve contra libc++ ---- */
  size_t heap_size = (size_t)MEMORY_MB * 1024 * 1024;
  void *heap = mmap(NULL, heap_size, PROT_READ | PROT_WRITE | PROT_EXEC,
                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (heap == MAP_FAILED) fatal_error("mmap heap %d MB", MEMORY_MB);

  if (so_load(SO_NAME, heap, heap_size) < 0) fatal_error("so_load %s", SO_NAME);
  debugPrintf("Loaded %s: text=%p+%zu data=%p+%zu\n", SO_NAME, text_base, text_size, data_base, data_size);
  so_set_aux_module(m_cxx);
  if (so_relocate() < 0) fatal_error("so_relocate");
  if (so_resolve(dynlib_functions, dynlib_functions_count, 0) < 0) fatal_error("so_resolve");
  so_make_text_writable();
  so_flush_caches();
  so_execute_init_array();

  JNI_OnLoad      = (void *)so_find_addr("JNI_OnLoad");
  nativeSetContext= (void *)so_find_addr("Java_org_cocos2dx_lib_Cocos2dxHelper_nativeSetContext");
  nativeSetApkPath= (void *)so_find_addr("Java_org_cocos2dx_lib_Cocos2dxHelper_nativeSetApkPath");
  setAssetManager = (void *)so_find_addr("Java_org_cocos2dx_cpp_AppActivity_setAssetManager");
  setExternalStorageInfo = (void *)so_find_addr("Java_org_cocos2dx_cpp_AppActivity_setExternalStorageInfo");
  nativeInit      = (void *)so_find_addr("Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeInit");
  nativeRender    = (void *)so_find_addr("Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeRender");
  nativeOnPause   = (void *)so_find_addr("Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeOnPause");
  nativeOnResume  = (void *)so_find_addr("Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeOnResume");
  nativeKeyEvent  = (void *)so_find_addr("Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeKeyEvent");
  ctrlConnected   = (void *)so_find_addr("Java_org_cocos2dx_lib_GameControllerAdapter_nativeControllerConnected");
  ctrlButton      = (void *)so_find_addr("Java_org_cocos2dx_lib_GameControllerAdapter_nativeControllerButtonEvent");
  ctrlAxis        = (void *)so_find_addr("Java_org_cocos2dx_lib_GameControllerAdapter_nativeControllerAxisEvent");
  nativeTouchesBegin = (void *)so_find_addr("Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeTouchesBegin");
  nativeTouchesEnd   = (void *)so_find_addr("Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeTouchesEnd");
  nativeTouchesMove  = (void *)so_find_addr("Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeTouchesMove");

  if (!nativeInit || !nativeRender)
    fatal_error("missing Cocos2dxRenderer nativeInit/nativeRender");

  /* FORCAR INGLES (jamais japones): hooka DeviceInfo::getCurrentLanguage p/
     retornar o enum de ingles (CHRONO_LANG, default 1). Todas as leituras de
     idioma passam por aqui -> jogo carrega 007-en.dat e UI em ingles. */
  {
    uintptr_t glang = so_find_addr_safe("_ZN10DeviceInfo18getCurrentLanguageEv");
    if (glang) {
      extern int chrono_forced_lang(void);
      hook_arm64(glang, (uintptr_t)&chrono_forced_lang);
      debugPrintf("hooked DeviceInfo::getCurrentLanguage -> forced lang %d\n", chrono_forced_lang());
    } else debugPrintf("WARN: getCurrentLanguage symbol nao achado\n");
  }
  /* isConnected -> 1 por PADRAO: o controller fica registrado no startup mas
     a instancia GameController de cada cena pode perder o evento CONNECTED;
     forcar isConnected garante que toda cena processe o input do controle.
     (CHRONO_NOFORCECONN=1 desativa.) */
  if (!getenv("CHRONO_NOFORCECONN")) {
    uintptr_t isc = so_find_addr_safe("_ZN14GameController11isConnectedEv");
    if (isc) { hook_arm64(isc, (uintptr_t)&chrono_force_connected); debugPrintf("hooked GameController::isConnected -> 1\n"); }
  }
  if (getenv("CHRONO_HOOKKD")) {
    uintptr_t kd = so_find_addr_safe("_ZN14GameController9onKeyDownEPN7cocos2d10ControllerEiPNS0_5EventE");
    if (kd) { g_onkd_orig = (onkd_t)make_passthrough(kd); hook_arm64(kd, (uintptr_t)&my_onKeyDown);
              debugPrintf("hooked GameController::onKeyDown @0x%lx (passthrough=%p)\n", kd, (void*)g_onkd_orig); }
    else debugPrintf("WARN onKeyDown symbol nao achado\n");
  }

  /* Ultimo caminho de quit da engine: cocos2d::Director::end(). Passa pelo
     MESMO pedido de shutdown (SELECT+START / SIGTERM / exit() do JNI) e ainda
     chama a original, para o cocos limpar as cenas normalmente. */
  {
    uintptr_t dend = so_find_addr_safe("_ZN7cocos2d8Director3endEv");
    if (dend) {
      g_director_end_orig = (director_end_t)make_passthrough(dend);
      hook_arm64(dend, (uintptr_t)&my_director_end);
      debugPrintf("hooked cocos2d::Director::end -> shutdown unico\n");
    } else debugPrintf("WARN: Director::end nao achado\n");
  }

  void *fake_vm = NULL, *fake_env = NULL;
  jni_shim_init(&fake_vm, &fake_env);
  g_env = fake_env;
  g_vendor = jni_make_string("Xbox Wireless Controller"); /* nome Xbox padrao */

  debugPrintf("JNI_OnLoad...\n");
  if (JNI_OnLoad) JNI_OnLoad(fake_vm, NULL);

  void *dummy = (void *)0xDEADBEEF;
  /* Caminhos derivados do diretorio REAL do port, nunca cravados: o CFW decide
     se a raiz de ROM e' /roms, /roms2, /storage/roms, /mnt/mmc... */
  static char apk_path[1024], writable_path[1024], assets_path[1024];
  snprintf(apk_path, sizeof apk_path, "%s/base.apk", ct_gamedir());
  snprintf(writable_path, sizeof writable_path, "%s/userdata/", ct_gamedir());
  snprintf(assets_path, sizeof assets_path, "%s/assets/", ct_gamedir());
  jni_set_writable_path(writable_path);
  jni_set_assets_path(assets_path);
  debugPrintf("gamedir=%s writable=%s assets=%s\n",
              ct_gamedir(), writable_path, assets_path);
  void *apk = jni_make_string(apk_path);
  if (nativeSetApkPath) { debugPrintf("nativeSetApkPath\n"); nativeSetApkPath(fake_env, NULL, apk); }
  if (setAssetManager) { debugPrintf("setAssetManager\n"); setAssetManager(fake_env, NULL, dummy); }
  if (nativeSetContext) { debugPrintf("nativeSetContext\n"); nativeSetContext(fake_env, NULL, dummy, dummy); }

  debugPrintf("nativeInit(%d,%d)...\n", w, h);
  nativeInit(fake_env, NULL, w, h);

  /* O controle so' e' ANUNCIADO com a cena viva. Anunciar junto do nativeInit,
     antes de a primeira cena existir, deixa a engine num estado em que a tela
     seguinte ao titulo nunca desenha (medido no R36S com dois controles
     presentes no SDL_Init) — e a HANDOFF ja registrava SIGSEGV ao injetar
     controle antes de existir cena. O anuncio agora e' adiado alguns frames e
     repetido a cada hotplug. */
  if (nativeOnResume) nativeOnResume(fake_env, NULL);

  debugPrintf("Entering main loop...\n");
  SDL_Event e;
  int announce_at = 120; /* ~2 s de cena viva antes de anunciar o controle */
  while (!ct_exit_requested()) {
    flush_pending_releases();
    ct_exit_chord_poll();
    if (announce_at > 0 && --announce_at == 0 && ctrlConnected && !g_use_keyboard) {
      ctrlConnected(g_env, NULL, g_vendor, 0);
      debugPrintf("Controle anunciado a engine (cena viva)\n");
    }
    while (SDL_PollEvent(&e)) {
      switch (e.type) {
        case SDL_QUIT: ct_request_exit("SDL_QUIT"); break;
        case SDL_CONTROLLERDEVICEADDED:
          open_gamepad();
          if (ctrlConnected && !g_use_keyboard) ctrlConnected(g_env, NULL, g_vendor, 0);
          break;
        case SDL_WINDOWEVENT:
          if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST) release_all_buttons();
          break;
        case SDL_CONTROLLERDEVICEREMOVED:
          /* Hotplug/perda de foco nao pode deixar direcao presa. */
          release_all_buttons();
          drop_gamepad(e.cdevice.which);
          debugPrintf("Gamepad instance=%d removido\n", (int)e.cdevice.which);
          break;
        case SDL_KEYDOWN: case SDL_KEYUP: {
          if (e.key.repeat) break;
          if (e.key.keysym.sym == SDLK_ESCAPE && e.type == SDL_KEYDOWN) { ct_request_exit("ESC"); break; }
          if (nativeKeyEvent) {
            int kc = map_key_android(e.key.keysym.sym);
            if (kc >= 0) nativeKeyEvent(g_env, NULL, kc, e.type == SDL_KEYDOWN);
          }
          break;
        }
        case SDL_CONTROLLERBUTTONDOWN:
          send_button(e.cbutton.button, 1); break;
        case SDL_CONTROLLERBUTTONUP:
          send_button(e.cbutton.button, 0); break;
        case SDL_CONTROLLERAXISMOTION:
          if (g_gamepad && !g_use_keyboard && ctrlAxis) {
            int a = -1;
            if (e.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX) a = CK_JOYSTICK_LEFT_X;
            else if (e.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) a = CK_JOYSTICK_LEFT_Y;
            else if (e.caxis.axis == SDL_CONTROLLER_AXIS_RIGHTX) a = CK_JOYSTICK_RIGHT_X;
            else if (e.caxis.axis == SDL_CONTROLLER_AXIS_RIGHTY) a = CK_JOYSTICK_RIGHT_Y;
            if (a >= 0) ctrlAxis(g_env, NULL, g_vendor, 0, a, e.caxis.value / 32767.0f, 1);
          }
          break;
      }
    }
    /* CHRONO_AUTOPRESS=1: 0-12s toque(passa titulo); 12s+ conecta controle e
       alterna DPAD_RIGHT/LEFT (controller nativo E teclado) p/ navegar menu. */
    if (getenv("CHRONO_AUTOPRESS")) {
      static int fc = 0; fc++;
      if (fc < 720) {
        int sub = fc % 120;
        if (sub == 5 && nativeTouchesBegin) nativeTouchesBegin(g_env, NULL, 0, w/2.0f, h/2.0f);
        if (sub == 20 && nativeTouchesEnd) nativeTouchesEnd(g_env, NULL, 0, w/2.0f, h/2.0f);
      } else {
        if (fc % 120 == 0 && ctrlConnected) { ctrlConnected(g_env,NULL,g_vendor,0); }
        /* segura DPAD_RIGHT repetido: se navegar, seleção verde vai p/ Extras */
        int ck = CK_BUTTON_DPAD_RIGHT, ak = AKEYCODE_DPAD_RIGHT;
        int sub = fc % 90;
        if (sub == 10) {
          if (ctrlButton) ctrlButton(g_env,NULL,g_vendor,0,ck,1,1.0f);
          if (nativeKeyEvent) nativeKeyEvent(g_env,NULL,ak,1);
          debugPrintf("AUTONAV RIGHT down\n");
        }
        if (sub == 25) {
          if (ctrlButton) ctrlButton(g_env,NULL,g_vendor,0,ck,0,0.0f);
          if (nativeKeyEvent) nativeKeyEvent(g_env,NULL,ak,0);
        }
      }
    }
    /* CHRONO_MAP=1: mapear timeline intro->titulo->menu. Taps esparsos p/
       passar a intro/titulo + fotos em varios frames. SEM injetar controle. */
    if (getenv("CHRONO_MAP")) {
      static int f = 0; f++;
      /* taps esparsos p/ avancar intro/titulo (a cada ~150f, breve) */
      if (f % 150 == 30 && nativeTouchesBegin) nativeTouchesBegin(g_env,NULL,0,w/2.0f,h/2.0f);
      if (f % 150 == 45 && nativeTouchesEnd)   nativeTouchesEnd(g_env,NULL,0,w/2.0f,h/2.0f);
      int marks[] = {200,400,600,800,1000,1200,1400,1600,1800};
      for (unsigned i=0;i<sizeof(marks)/sizeof(marks[0]);i++)
        if (f == marks[i]) chrono_dump_shot(w,h,marks[i]);
      if (f == 1810) debugPrintf("MAP done\n");
    }
    /* CHRONO_NAVTEST=1: fluxo 100% CONTROLE (sem toque) p/ validar o caminho real
       do handheld: connect -> BUTTON_A passa "Touch to Start" -> DPAD navega. */
    if (getenv("CHRONO_NAVTEST")) {
      static int f = 0; f++;
      #define CK_PRESS(k)   ctrlButton(g_env,NULL,g_vendor,0,(k),1,1.0f)
      #define CK_RELEASE(k) ctrlButton(g_env,NULL,g_vendor,0,(k),0,0.0f)
      if (f == 120 && ctrlConnected) { ctrlConnected(g_env,NULL,g_vendor,0); debugPrintf("NAV connect\n"); }
      if (f == 200) chrono_dump_shot(w,h,1);   /* titulo "Touch to Start" */
      /* BUTTON_A p/ passar o titulo (3 tentativas) */
      if (f==250||f==320||f==390) { if(ctrlButton){CK_PRESS(CK_BUTTON_A);} debugPrintf("NAV A down\n"); }
      if (f==265||f==335||f==405) { if(ctrlButton){CK_RELEASE(CK_BUTTON_A);} }
      if (f == 460) chrono_dump_shot(w,h,2);   /* menu apareceu via A? */
      /* DPAD_RIGHT -> Extras */
      if (f == 520) { if(ctrlButton){CK_PRESS(CK_BUTTON_DPAD_RIGHT);} debugPrintf("NAV DPAD_RIGHT\n"); }
      if (f == 535) { if(ctrlButton){CK_RELEASE(CK_BUTTON_DPAD_RIGHT);} }
      if (f == 580) chrono_dump_shot(w,h,3);   /* selecao em Extras? */
      /* DPAD_LEFT -> volta New Game */
      if (f == 620) { if(ctrlButton){CK_PRESS(CK_BUTTON_DPAD_LEFT);} debugPrintf("NAV DPAD_LEFT\n"); }
      if (f == 635) { if(ctrlButton){CK_RELEASE(CK_BUTTON_DPAD_LEFT);} }
      if (f == 680) chrono_dump_shot(w,h,4);   /* selecao volta New Game? */
      if (f == 720) debugPrintf("NAVTEST done\n");
    }
    /* refill de audio agora roda na thread dedicada do opensles_shim
       (desacoplado do framerate) -> sem gagueira por hitch de frame. */
    nativeRender(g_env, NULL);

    /* Antes de qualquer leitura ou present: o que vai pro scanout tem que ser
       opaco. Fica ANTES das capturas de proposito -- assim o shot mostra o que
       o painel recebe, e nao so' o que a engine desenhou. */
    ct_force_opaque_scanout();

    /* CHRONO_SHOTS="200,600,1200": captura glReadPixels nesses frames, SEM
       injetar nada. glReadPixels precisa vir ANTES do swap. */
    /* CHRONO_SHOTEVERY=N: filmstrip de diagnostico, 1 captura a cada N frames
       (limitado por CHRONO_SHOTMAX, default 80). */
    if (getenv("CHRONO_SHOTEVERY")) {
      static int ef = 0, ec = 0;
      int every = atoi(getenv("CHRONO_SHOTEVERY"));
      const char *maxenv = getenv("CHRONO_SHOTMAX");
      int maxshots = maxenv ? atoi(maxenv) : 80;
      if (every > 0 && ++ef % every == 0 && ec < maxshots) {
        ec++; chrono_dump_shot(w, h, ef);
      }
    }
    if (getenv("CHRONO_SHOTS")) {
      static int sf = 0; sf++;
      const char *list = getenv("CHRONO_SHOTS");
      for (const char *p = list; *p;) {
        int v = (int)strtol(p, (char **)&p, 10);
        if (v == sf) { chrono_dump_shot(w, h, sf); break; }
        while (*p && *p != ',') p++;
        if (*p == ',') p++;
      }
    }
    if (finish_before_swap) glFinish();
    gl_swap_guarded(window);

    /* CHRONO_FPSLOG=1: fps medido 1x/s (nao entra no caminho normal). */
    if (getenv("CHRONO_FPSLOG")) {
      static Uint32 t0 = 0; static int frames = 0;
      Uint32 now = SDL_GetTicks();
      if (!t0) t0 = now;
      frames++;
      if (now - t0 >= 1000) {
        debugPrintf("FPS %.1f (avg %.2f ms/frame) VmHWM=%ld kB\n",
                    frames * 1000.0 / (now - t0), (double)(now - t0) / frames,
                    ct_peak_rss_kb());
        t0 = now; frames = 0;
      }
    }

    /* RAM curta (R36S tem 639 MB): registrar o pico medido, 1x a cada ~30s.
       Medicao, nao GC proprio — economia so' local e comprovada. */
    if (getenv("CHRONO_MEMSTAT")) {
      static int mf = 0;
      if (++mf % 1800 == 0)
        debugPrintf("RAM VmHWM=%ld kB (frame %d)\n", ct_peak_rss_kb(), mf);
    }
  }

  /* SELECT+START, SIGTERM e quits da engine convergem AQUI: pausa (o cocos
     grava o estado no onPause), fecha audio/video e retorna ao frontend. */
  debugPrintf("Exiting (%s)... VmHWM=%ld kB\n", ct_exit_reason(), ct_peak_rss_kb());
  ct_start_shutdown_watchdog(8);
  if (nativeOnPause) nativeOnPause(g_env, NULL);
  ct_exit_chord_close();
  close_gamepads();
  SDL_GL_DeleteContext(glc);
  SDL_DestroyWindow(window);
  SDL_Quit();
  ct_single_instance_unlock();
  return 0;
}
