/**
 * @file viewer_main.c
 * @brief GhostVidStream — multi-protocol SDL2 receive / viewer shell.
 *
 * Wired decoder plugins: libghost_ndihx (NDI|HX), srt, rtmp, rtsp. FULL NDI /
 * 2110 modules remain planned. Light overlays (controls + stats HUD +
 * capability-gated PTZ) — no GTK/Qt. Embeds that only need one protocol should
 * link that module directly (see docs/integration.md / modular-compatibility.md).
 */
#include "ghost_ndihx.h"
#include "ghost_rtmp.h"
#include "ghost_rtsp.h"
#include "ghost_srt.h"
#include "media_core.h"

#include <SDL.h>

/* Alpine 3.20 / older SDL2: BGRX32 may be missing; BGRX8888 is the classic alias. */
#ifndef SDL_PIXELFORMAT_BGRX32
#  ifdef SDL_PIXELFORMAT_BGRX8888
#    define SDL_PIXELFORMAT_BGRX32 SDL_PIXELFORMAT_BGRX8888
#  else
#    define SDL_PIXELFORMAT_BGRX32 SDL_PIXELFORMAT_BGRA32
#  endif
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#define PRODUCT_NAME "GhostVidStream"
#define VIEWER_VERSION "0.6.0"

typedef enum {
  VIEWER_PROTO_NDI_HX = 0,
  VIEWER_PROTO_SRT = 1,
  VIEWER_PROTO_RTMP = 2,
  VIEWER_PROTO_RTSP = 3
} ViewerProtocol;

typedef struct {
  ghost_ndihx_options_t lib;
  ghost_srt_options_t srt;
  ghost_rtmp_options_t rtmp;
  ghost_rtsp_options_t rtsp;
  ViewerProtocol protocol;
  bool list_only;
  bool fullscreen;
  bool stats; /* HUD on at start when --stats */
  bool auto_hide; /* hide chrome + cursor after idle (kiosk) */
  int auto_hide_ms; /* idle ms before hide; default 4000 */
  int max_w;
  int max_h;
  int fps_cap;
  int hz;
  int noframe_ms;
  const char *config_path;
} ViewerOptions;

/* -------------------------------------------------------------------------- */
/* Tiny 5x7 bitmap font (ASCII 32..126)                                       */
/* -------------------------------------------------------------------------- */

static const uint8_t FONT5X7[95][7] = {
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, /* space */
    {0x04, 0x04, 0x04, 0x04, 0x00, 0x04, 0x00}, {0x0A, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x0A, 0x1F, 0x0A, 0x1F, 0x0A, 0x00, 0x00}, {0x04, 0x0F, 0x14, 0x0E, 0x05, 0x1E, 0x04},
    {0x19, 0x19, 0x02, 0x04, 0x08, 0x13, 0x13}, {0x08, 0x14, 0x08, 0x15, 0x12, 0x0D, 0x00},
    {0x04, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00}, {0x02, 0x04, 0x04, 0x04, 0x04, 0x02, 0x00},
    {0x08, 0x04, 0x04, 0x04, 0x04, 0x08, 0x00}, {0x00, 0x0A, 0x04, 0x1F, 0x04, 0x0A, 0x00},
    {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00}, {0x00, 0x00, 0x00, 0x00, 0x04, 0x04, 0x08},
    {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}, {0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00},
    {0x01, 0x02, 0x04, 0x08, 0x10, 0x00, 0x00}, {0x0E, 0x11, 0x13, 0x15, 0x19, 0x0E, 0x00},
    {0x04, 0x0C, 0x04, 0x04, 0x04, 0x0E, 0x00}, {0x0E, 0x11, 0x01, 0x06, 0x08, 0x1F, 0x00},
    {0x1F, 0x02, 0x04, 0x02, 0x11, 0x0E, 0x00}, {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x00},
    {0x1F, 0x10, 0x1E, 0x01, 0x11, 0x0E, 0x00}, {0x06, 0x08, 0x10, 0x1E, 0x11, 0x0E, 0x00},
    {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x00}, {0x0E, 0x11, 0x0E, 0x11, 0x11, 0x0E, 0x00},
    {0x0E, 0x11, 0x0F, 0x01, 0x02, 0x0C, 0x00}, {0x00, 0x04, 0x00, 0x00, 0x04, 0x00, 0x00},
    {0x00, 0x04, 0x00, 0x00, 0x04, 0x04, 0x08}, {0x02, 0x04, 0x08, 0x04, 0x02, 0x00, 0x00},
    {0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00}, {0x08, 0x04, 0x02, 0x04, 0x08, 0x00, 0x00},
    {0x0E, 0x11, 0x02, 0x04, 0x00, 0x04, 0x00}, {0x0E, 0x11, 0x17, 0x15, 0x17, 0x10, 0x0E},
    {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x00}, {0x1E, 0x11, 0x1E, 0x11, 0x11, 0x1E, 0x00},
    {0x0E, 0x11, 0x10, 0x10, 0x11, 0x0E, 0x00}, {0x1C, 0x12, 0x11, 0x11, 0x12, 0x1C, 0x00},
    {0x1F, 0x10, 0x1E, 0x10, 0x10, 0x1F, 0x00}, {0x1F, 0x10, 0x1E, 0x10, 0x10, 0x10, 0x00},
    {0x0E, 0x11, 0x10, 0x17, 0x11, 0x0F, 0x00}, {0x11, 0x11, 0x1F, 0x11, 0x11, 0x11, 0x00},
    {0x0E, 0x04, 0x04, 0x04, 0x04, 0x0E, 0x00}, {0x01, 0x01, 0x01, 0x01, 0x11, 0x0E, 0x00},
    {0x11, 0x12, 0x1C, 0x12, 0x11, 0x11, 0x00}, {0x10, 0x10, 0x10, 0x10, 0x10, 0x1F, 0x00},
    {0x11, 0x1B, 0x15, 0x11, 0x11, 0x11, 0x00}, {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x00},
    {0x0E, 0x11, 0x11, 0x11, 0x11, 0x0E, 0x00}, {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x00},
    {0x0E, 0x11, 0x11, 0x15, 0x12, 0x0D, 0x00}, {0x1E, 0x11, 0x11, 0x1E, 0x12, 0x11, 0x00},
    {0x0F, 0x10, 0x0E, 0x01, 0x11, 0x0E, 0x00}, {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x00},
    {0x11, 0x11, 0x11, 0x11, 0x11, 0x0E, 0x00}, {0x11, 0x11, 0x11, 0x11, 0x0A, 0x04, 0x00},
    {0x11, 0x11, 0x11, 0x15, 0x1B, 0x11, 0x00}, {0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11, 0x00},
    {0x11, 0x0A, 0x04, 0x04, 0x04, 0x04, 0x00}, {0x1F, 0x02, 0x04, 0x08, 0x10, 0x1F, 0x00},
    {0x0E, 0x08, 0x08, 0x08, 0x08, 0x0E, 0x00}, {0x10, 0x08, 0x04, 0x02, 0x01, 0x00, 0x00},
    {0x0E, 0x02, 0x02, 0x02, 0x02, 0x0E, 0x00}, {0x04, 0x0A, 0x11, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x1F, 0x00}, {0x08, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x0E, 0x01, 0x0F, 0x11, 0x0F, 0x00}, {0x10, 0x10, 0x1E, 0x11, 0x11, 0x1E, 0x00},
    {0x00, 0x0E, 0x10, 0x10, 0x11, 0x0E, 0x00}, {0x01, 0x01, 0x0F, 0x11, 0x11, 0x0F, 0x00},
    {0x00, 0x0E, 0x11, 0x1F, 0x10, 0x0E, 0x00}, {0x06, 0x08, 0x1C, 0x08, 0x08, 0x08, 0x00},
    {0x00, 0x0F, 0x11, 0x0F, 0x01, 0x0E, 0x00}, {0x10, 0x10, 0x1E, 0x11, 0x11, 0x11, 0x00},
    {0x04, 0x00, 0x0C, 0x04, 0x04, 0x0E, 0x00}, {0x02, 0x00, 0x02, 0x02, 0x12, 0x0C, 0x00},
    {0x10, 0x12, 0x14, 0x18, 0x14, 0x12, 0x00}, {0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E, 0x00},
    {0x00, 0x1A, 0x15, 0x15, 0x15, 0x15, 0x00}, {0x00, 0x1E, 0x11, 0x11, 0x11, 0x11, 0x00},
    {0x00, 0x0E, 0x11, 0x11, 0x11, 0x0E, 0x00}, {0x00, 0x1E, 0x11, 0x1E, 0x10, 0x10, 0x00},
    {0x00, 0x0F, 0x11, 0x0F, 0x01, 0x01, 0x00}, {0x00, 0x16, 0x19, 0x10, 0x10, 0x10, 0x00},
    {0x00, 0x0F, 0x10, 0x0E, 0x01, 0x1E, 0x00}, {0x08, 0x1C, 0x08, 0x08, 0x09, 0x06, 0x00},
    {0x00, 0x11, 0x11, 0x11, 0x11, 0x0F, 0x00}, {0x00, 0x11, 0x11, 0x11, 0x0A, 0x04, 0x00},
    {0x00, 0x11, 0x11, 0x15, 0x15, 0x0A, 0x00}, {0x00, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x00},
    {0x00, 0x11, 0x11, 0x0F, 0x01, 0x0E, 0x00}, {0x00, 0x1F, 0x02, 0x04, 0x08, 0x1F, 0x00},
    {0x02, 0x04, 0x04, 0x08, 0x04, 0x04, 0x02}, {0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x00},
    {0x08, 0x04, 0x04, 0x02, 0x04, 0x04, 0x08}, {0x00, 0x08, 0x15, 0x02, 0x00, 0x00, 0x00},
};

static void draw_char(SDL_Renderer *ren, int x, int y, char ch, int scale, Uint8 r, Uint8 g,
                      Uint8 b, Uint8 a) {
  if (ch < 32 || ch > 126)
    ch = '?';
  const uint8_t *glyph = FONT5X7[ch - 32];
  SDL_SetRenderDrawColor(ren, r, g, b, a);
  for (int row = 0; row < 7; row++) {
    uint8_t bits = glyph[row];
    for (int col = 0; col < 5; col++) {
      if (bits & (0x10 >> col)) {
        SDL_Rect px = {x + col * scale, y + row * scale, scale, scale};
        SDL_RenderFillRect(ren, &px);
      }
    }
  }
}

static void draw_text(SDL_Renderer *ren, int x, int y, const char *s, int scale, Uint8 r,
                      Uint8 g, Uint8 b, Uint8 a) {
  if (!s)
    return;
  int cx = x;
  for (; *s; s++) {
    if (*s == '\n') {
      y += 8 * scale;
      cx = x;
      continue;
    }
    draw_char(ren, cx, y, *s, scale, r, g, b, a);
    cx += 6 * scale;
  }
}

static int text_width(const char *s, int scale) {
  int w = 0, line = 0;
  if (!s)
    return 0;
  for (; *s; s++) {
    if (*s == '\n') {
      if (line > w)
        w = line;
      line = 0;
      continue;
    }
    line += 6 * scale;
  }
  return line > w ? line : w;
}

/* -------------------------------------------------------------------------- */
/* Overlay hit targets                                                        */
/* -------------------------------------------------------------------------- */

typedef enum {
  HIT_NONE = 0,
  HIT_PANEL_BG,
  HIT_OPEN_CONTROLS,
  HIT_CLOSE_CONTROLS,
  HIT_BW_LOW,
  HIT_BW_HIGH,
  HIT_FPS_DOWN,
  HIT_FPS_UP,
  HIT_FPS_UNCAPPED,
  HIT_MAXW_DOWN,
  HIT_MAXW_UP,
  HIT_MAXH_DOWN,
  HIT_MAXH_UP,
  HIT_AUTO_TOGGLE,
  HIT_FULLSCREEN,
  HIT_RESCAN,
  HIT_STATS_TOGGLE,
  HIT_PTZ_N,
  HIT_PTZ_S,
  HIT_PTZ_E,
  HIT_PTZ_W,
  HIT_PTZ_ZOOM_IN,
  HIT_PTZ_ZOOM_OUT,
  HIT_PTZ_STOP,
  HIT_PTZ_HOME
} HitId;

typedef struct {
  HitId id;
  SDL_Rect r;
} HitTarget;

#define MAX_HITS 48

typedef struct {
  bool controls_open;
  bool hud_on;
  bool chrome_visible; /* false when auto-hide idle */
  Uint32 last_input_ticks;
  bool ptz_available;
  media_caps_t caps;
  HitTarget hits[MAX_HITS];
  int hit_count;
  HitId held_ptz;
  Uint32 last_ptz_probe;
} UiState;

static void ui_clear_hits(UiState *ui) { ui->hit_count = 0; }

static void ui_add_hit(UiState *ui, HitId id, SDL_Rect r) {
  if (ui->hit_count >= MAX_HITS)
    return;
  ui->hits[ui->hit_count].id = id;
  ui->hits[ui->hit_count].r = r;
  ui->hit_count++;
}

static HitId ui_hit_at(const UiState *ui, int x, int y) {
  for (int i = ui->hit_count - 1; i >= 0; i--) {
    const SDL_Rect *r = &ui->hits[i].r;
    if (x >= r->x && y >= r->y && x < r->x + r->w && y < r->y + r->h)
      return ui->hits[i].id;
  }
  return HIT_NONE;
}

static void draw_button(SDL_Renderer *ren, UiState *ui, HitId id, SDL_Rect r, const char *label,
                        bool active) {
  SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
  if (active)
    SDL_SetRenderDrawColor(ren, 40, 120, 100, 200);
  else
    SDL_SetRenderDrawColor(ren, 20, 28, 36, 180);
  SDL_RenderFillRect(ren, &r);
  SDL_SetRenderDrawColor(ren, 160, 200, 190, 220);
  SDL_RenderDrawRect(ren, &r);
  int tw = text_width(label, 2);
  int tx = r.x + (r.w - tw) / 2;
  int ty = r.y + (r.h - 14) / 2;
  draw_text(ren, tx, ty, label, 2, 230, 240, 235, 255);
  ui_add_hit(ui, id, r);
}

static void draw_label(SDL_Renderer *ren, int x, int y, const char *s) {
  draw_text(ren, x, y, s, 2, 210, 220, 215, 255);
}

static void draw_hud(SDL_Renderer *ren, const ViewerOptions *vo, const ghost_ndihx_source_t *src,
                     int tex_w, int tex_h, int present_fps, double src_fps, bool paused) {
  char cap[32];
  if (vo->fps_cap > 0)
    snprintf(cap, sizeof(cap), "%d", vo->fps_cap);
  else
    snprintf(cap, sizeof(cap), "off");

  char line[512];
  snprintf(line, sizeof(line), "%dx%d  src~%.1f  present %d fps  bw=%s  fps_cap=%s%s", tex_w,
           tex_h, src_fps, present_fps, ghost_ndihx_bandwidth_name(vo->lib.bandwidth), cap,
           paused ? "  [paused]" : "");

  char line2[640];
  snprintf(line2, sizeof(line2), "%s%s%s", src->name[0] ? src->name : "(no source)",
           src->url[0] ? "  " : "", src->url);

  SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
  int pad = 10;
  int box_w = text_width(line, 2);
  int w2 = text_width(line2, 2);
  if (w2 > box_w)
    box_w = w2;
  box_w += pad * 2;
  int box_h = 8 * 2 * 2 + pad * 2 + 4;
  SDL_Rect bg = {8, 8, box_w, box_h};
  SDL_SetRenderDrawColor(ren, 8, 12, 16, 150);
  SDL_RenderFillRect(ren, &bg);
  draw_text(ren, 8 + pad, 8 + pad, line, 2, 230, 240, 235, 255);
  draw_text(ren, 8 + pad, 8 + pad + 16, line2, 2, 180, 200, 190, 255);
}

static void draw_controls_chip(SDL_Renderer *ren, UiState *ui) {
  SDL_Rect r = {12, 0, 110, 28};
  int ww, wh;
  SDL_GetRendererOutputSize(ren, &ww, &wh);
  r.y = wh - 40;
  draw_button(ren, ui, HIT_OPEN_CONTROLS, r, "Controls", false);
}

static void layout_controls(SDL_Renderer *ren, UiState *ui, ViewerOptions *vo,
                            const ghost_ndihx_source_t *src, double src_fps) {
  int ww, wh;
  SDL_GetRendererOutputSize(ren, &ww, &wh);
  int panel_w = 360;
  int panel_h = ui->ptz_available ? 520 : 400;
  if (panel_h > wh - 20)
    panel_h = wh - 20;
  SDL_Rect panel = {ww - panel_w - 12, 12, panel_w, panel_h};

  SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(ren, 10, 14, 18, 160); /* light — does not dim whole frame */
  SDL_RenderFillRect(ren, &panel);
  SDL_SetRenderDrawColor(ren, 120, 180, 160, 220);
  SDL_RenderDrawRect(ren, &panel);
  /* Background first so reverse hit-test prefers buttons. */
  ui_add_hit(ui, HIT_PANEL_BG, panel);

  int x = panel.x + 14;
  int y = panel.y + 12;
  draw_label(ren, x, y, PRODUCT_NAME "  (c)");
  SDL_Rect close = {panel.x + panel.w - 36, panel.y + 8, 28, 24};
  draw_button(ren, ui, HIT_CLOSE_CONTROLS, close, "X", false);
  y += 22;
  draw_label(ren, x, y, "Shell · decoder: libghost_ndihx (NDI|HX)");
  y += 22;

  char hzbuf[64];
  snprintf(hzbuf, sizeof(hzbuf), "Source Hz ~ %.2f (sender)", src_fps);
  draw_label(ren, x, y, hzbuf);
  y += 22;

  draw_label(ren, x, y, "Bandwidth");
  y += 18;
  SDL_Rect b1 = {x, y, 140, 28};
  SDL_Rect b2 = {x + 150, y, 140, 28};
  draw_button(ren, ui, HIT_BW_LOW, b1, "Lowest", vo->lib.bandwidth == GHOST_NDIHX_BW_LOWEST);
  draw_button(ren, ui, HIT_BW_HIGH, b2, "Highest", vo->lib.bandwidth == GHOST_NDIHX_BW_HIGHEST);
  y += 40;

  char fpsl[48];
  {
    char t[16];
    if (vo->fps_cap > 0)
      snprintf(t, sizeof(t), "%d", vo->fps_cap);
    else
      snprintf(t, sizeof(t), "uncapped");
    snprintf(fpsl, sizeof(fpsl), "FPS cap: %s", t);
  }
  draw_label(ren, x, y, fpsl);
  y += 18;
  SDL_Rect fd = {x, y, 70, 28};
  SDL_Rect fu = {x + 80, y, 70, 28};
  SDL_Rect fz = {x + 160, y, 120, 28};
  draw_button(ren, ui, HIT_FPS_DOWN, fd, "-", false);
  draw_button(ren, ui, HIT_FPS_UP, fu, "+", false);
  draw_button(ren, ui, HIT_FPS_UNCAPPED, fz, "Uncapped", vo->fps_cap == 0);
  y += 40;

  char mw[48], mh[48];
  snprintf(mw, sizeof(mw), "Max W: %d", vo->max_w);
  snprintf(mh, sizeof(mh), "Max H: %d", vo->max_h);
  draw_label(ren, x, y, mw);
  y += 18;
  draw_button(ren, ui, HIT_MAXW_DOWN, (SDL_Rect){x, y, 70, 28}, "-", false);
  draw_button(ren, ui, HIT_MAXW_UP, (SDL_Rect){x + 80, y, 70, 28}, "+", false);
  y += 40;
  draw_label(ren, x, y, mh);
  y += 18;
  draw_button(ren, ui, HIT_MAXH_DOWN, (SDL_Rect){x, y, 70, 28}, "-", false);
  draw_button(ren, ui, HIT_MAXH_UP, (SDL_Rect){x + 80, y, 70, 28}, "+", false);
  y += 40;

  draw_button(ren, ui, HIT_AUTO_TOGGLE, (SDL_Rect){x, y, 160, 28},
              vo->lib.auto_search ? "Auto: ON" : "Auto: OFF", vo->lib.auto_search);
  draw_button(ren, ui, HIT_FULLSCREEN, (SDL_Rect){x + 170, y, 140, 28}, "Fullscreen", false);
  y += 36;
  draw_button(ren, ui, HIT_RESCAN, (SDL_Rect){x, y, 160, 28}, "Rescan", false);
  draw_button(ren, ui, HIT_STATS_TOGGLE, (SDL_Rect){x + 170, y, 140, 28},
              ui->hud_on ? "Stats: ON" : "Stats: OFF", ui->hud_on);
  y += 40;

  if (ui->ptz_available) {
    draw_label(ren, x, y, "PTZ");
    y += 20;
    int cx = x + 90;
    int cy = y + 40;
    draw_button(ren, ui, HIT_PTZ_N, (SDL_Rect){cx, cy - 36, 56, 32}, "N", false);
    draw_button(ren, ui, HIT_PTZ_S, (SDL_Rect){cx, cy + 36, 56, 32}, "S", false);
    draw_button(ren, ui, HIT_PTZ_W, (SDL_Rect){cx - 64, cy, 56, 32}, "W", false);
    draw_button(ren, ui, HIT_PTZ_E, (SDL_Rect){cx + 64, cy, 56, 32}, "E", false);
    draw_button(ren, ui, HIT_PTZ_ZOOM_IN, (SDL_Rect){x + 230, cy - 36, 70, 32}, "Z+", false);
    draw_button(ren, ui, HIT_PTZ_ZOOM_OUT, (SDL_Rect){x + 230, cy + 4, 70, 32}, "Z-", false);
    draw_button(ren, ui, HIT_PTZ_STOP, (SDL_Rect){x + 230, cy + 44, 70, 28}, "Stop", false);
    if (ui->caps & MEDIA_CAP_PTZ_HOME)
      draw_button(ren, ui, HIT_PTZ_HOME, (SDL_Rect){x, cy + 80, 100, 28}, "Home", false);
  }

  (void)src;
}

static void usage(const char *argv0) {
  fprintf(stderr,
          "%s — multi-protocol video receive shell\n"
          "Decoders: libghost_ndihx (NDI|HX), srt, rtmp, rtsp. Planned: FULL NDI, 2110.\n"
          "Usage: %s [options]\n"
          "\n"
          "Protocol\n"
          "  --protocol MODE     ghost_ndihx|ndi_hx|srt|rtmp|rtsp  (default: ghost_ndihx)\n"
          "  --url URL           Full srt:// / rtmp:// / rtsp:// (or http://…flv) URL\n"
          "\n"
          "Discovery / connect\n"
          "  --list              List sources and exit\n"
          "  --auto / --no-auto  Keep searching (default: auto)\n"
          "  --source NAME       Prefer name/url substring\n"
          "  --ip HOST           Prefer IP/host substring (also builds default SRT/RTMP URLs)\n"
          "  --any               Allow non-HX sources (NDI|HX module)\n"
          "  --discover MODE     auto|bonjour|ndi_sdk  (default: auto)\n"
          "  --find-ms N         Discovery wait ms (default 4000)\n"
          "  --rescan-ms N       Auto-search pause ms (default 3000)\n"
          "  --noframe-ms N      Reconnect if silent this long (default 8000)\n"
          "\n"
          "Efficiency / display (%s shell)\n"
          "  --bandwidth MODE    highest|lowest (NDI|HX module)\n"
          "  --max-w N --max-h N Cap letterboxed display size (0=window)\n"
          "  --fps-cap N         Cap present rate (0=uncapped)\n"
          "  --hz N              Display refresh hint (0=default)\n"
          "  --config PATH       Load key=value settings file\n"
          "  --fullscreen        Start fullscreen\n"
          "  --auto-hide         Hide chrome/cursor after idle (kiosk)\n"
          "  --stats             Start with on-screen stats HUD\n"
          "  -h, --help\n"
          "\n"
          "Keys: q/Esc quit · f fullscreen · Space pause · r rescan ·\n"
          "      c controls overlay · i stats HUD ·\n"
          "      [/] bandwidth low/high (NDI|HX) · -/= fps-cap · 0 uncapped\n",
          PRODUCT_NAME, argv0, PRODUCT_NAME);
}

static long read_rss_kb(void) {
  FILE *f = fopen("/proc/self/status", "r");
  if (!f)
    return -1;
  char line[256];
  long kb = -1;
  while (fgets(line, sizeof(line), f)) {
    if (strncmp(line, "VmRSS:", 6) == 0) {
      kb = strtol(line + 6, NULL, 10);
      break;
    }
  }
  fclose(f);
  return kb;
}

static void trim_inplace(char *s) {
  char *start = s;
  while (*start == ' ' || *start == '\t')
    start++;
  if (start != s)
    memmove(s, start, strlen(start) + 1);
  size_t n = strlen(s);
  while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r'))
    s[--n] = '\0';
}

static bool parse_boolish(const char *val) {
  return (!strcasecmp(val, "1") || !strcasecmp(val, "true") || !strcasecmp(val, "yes") ||
          !strcasecmp(val, "on"));
}

static int apply_config_kv(ViewerOptions *vo, const char *key, const char *val) {
  if (!strcmp(key, "fullscreen")) {
    vo->fullscreen = parse_boolish(val);
    return 0;
  }
  if (!strcmp(key, "stats")) {
    vo->stats = parse_boolish(val);
    return 0;
  }
  if (!strcmp(key, "auto_hide") || !strcmp(key, "autohide")) {
    vo->auto_hide = parse_boolish(val);
    return 0;
  }
  if (!strcmp(key, "auto_hide_ms") || !strcmp(key, "autohide_ms")) {
    vo->auto_hide_ms = atoi(val);
    if (vo->auto_hide_ms < 500)
      vo->auto_hide_ms = 500;
    return 0;
  }
  if (!strcmp(key, "protocol")) {
    if (!strcasecmp(val, "ghost_ndihx") || !strcasecmp(val, "ndi_hx") || !strcasecmp(val, "ndi") ||
        !strcasecmp(val, "ndihx"))
      vo->protocol = VIEWER_PROTO_NDI_HX;
    else if (!strcasecmp(val, "srt"))
      vo->protocol = VIEWER_PROTO_SRT;
    else if (!strcasecmp(val, "rtmp"))
      vo->protocol = VIEWER_PROTO_RTMP;
    else if (!strcasecmp(val, "rtsp"))
      vo->protocol = VIEWER_PROTO_RTSP;
    else {
      fprintf(stderr, "config protocol: use ghost_ndihx|srt|rtmp|rtsp\n");
      return -1;
    }
    return 0;
  }
  if (!strcmp(key, "url")) {
    snprintf(vo->srt.url, sizeof(vo->srt.url), "%s", val);
    snprintf(vo->rtmp.url, sizeof(vo->rtmp.url), "%s", val);
    snprintf(vo->rtsp.url, sizeof(vo->rtsp.url), "%s", val);
    snprintf(vo->lib.source_substr, sizeof(vo->lib.source_substr), "%s", val);
    return 0;
  }
  if (!strcmp(key, "max_w") || !strcmp(key, "max_width")) {
    vo->max_w = atoi(val);
    return 0;
  }
  if (!strcmp(key, "max_h") || !strcmp(key, "max_height")) {
    vo->max_h = atoi(val);
    return 0;
  }
  if (!strcmp(key, "fps_cap") || !strcmp(key, "fps")) {
    vo->fps_cap = atoi(val);
    return 0;
  }
  if (!strcmp(key, "hz") || !strcmp(key, "refresh")) {
    vo->hz = atoi(val);
    return 0;
  }
  if (!strcmp(key, "noframe_ms")) {
    vo->noframe_ms = atoi(val);
    return 0;
  }

  char tmpl[] = "/tmp/ndi-hx-one-XXXXXX";
  int fd = mkstemp(tmpl);
  if (fd < 0)
    return -1;
  FILE *of = fdopen(fd, "w");
  if (!of) {
    close(fd);
    unlink(tmpl);
    return -1;
  }
  fprintf(of, "%s=%s\n", key, val);
  fclose(of);
  char err[256];
  int rc = ghost_ndihx_options_load_file(&vo->lib, tmpl, err, sizeof(err));
  unlink(tmpl);
  if (rc != 0) {
    fprintf(stderr, "config key '%s': %s\n", key, err);
    return -1;
  }
  return 0;
}

static int load_config(ViewerOptions *vo, const char *path) {
  FILE *f = fopen(path, "r");
  if (!f) {
    fprintf(stderr, "Cannot open config %s\n", path);
    return -1;
  }
  char line[512];
  int lineno = 0;
  while (fgets(line, sizeof(line), f)) {
    lineno++;
    char *hash = strchr(line, '#');
    if (hash)
      *hash = '\0';
    trim_inplace(line);
    if (!line[0])
      continue;
    char *eq = strchr(line, '=');
    if (!eq) {
      fprintf(stderr, "%s:%d: expected key=value\n", path, lineno);
      fclose(f);
      return -1;
    }
    *eq = '\0';
    char *key = line;
    char *val = eq + 1;
    trim_inplace(key);
    trim_inplace(val);
    size_t vn = strlen(val);
    if (vn >= 2 && ((val[0] == '"' && val[vn - 1] == '"') ||
                    (val[0] == '\'' && val[vn - 1] == '\''))) {
      val[vn - 1] = '\0';
      val++;
    }
    if (apply_config_kv(vo, key, val) != 0) {
      fclose(f);
      return -1;
    }
  }
  fclose(f);
  return 0;
}

static void viewer_defaults(ViewerOptions *vo) {
  memset(vo, 0, sizeof(*vo));
  ghost_ndihx_options_defaults(&vo->lib);
  ghost_srt_options_defaults(&vo->srt);
  ghost_rtmp_options_defaults(&vo->rtmp);
  ghost_rtsp_options_defaults(&vo->rtsp);
  vo->protocol = VIEWER_PROTO_NDI_HX;
  vo->noframe_ms = 8000;
  vo->auto_hide = false;
  vo->auto_hide_ms = 4000;
}

static bool parse_args(int argc, char **argv, ViewerOptions *vo) {
  viewer_defaults(vo);
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--config") && i + 1 < argc) {
      vo->config_path = argv[++i];
      if (load_config(vo, vo->config_path) != 0)
        return false;
    }
  }
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--config") && i + 1 < argc) {
      i++;
    } else if (!strcmp(argv[i], "--list")) {
      vo->list_only = true;
    } else if (!strcmp(argv[i], "--fullscreen")) {
      vo->fullscreen = true;
    } else if (!strcmp(argv[i], "--auto-hide")) {
      vo->auto_hide = true;
    } else if (!strcmp(argv[i], "--stats")) {
      vo->stats = true;
    } else if (!strcmp(argv[i], "--auto")) {
      vo->lib.auto_search = true;
      vo->srt.auto_search = true;
      vo->rtmp.auto_search = true;
      vo->rtsp.auto_search = true;
    } else if (!strcmp(argv[i], "--no-auto")) {
      vo->lib.auto_search = false;
      vo->srt.auto_search = false;
      vo->rtmp.auto_search = false;
      vo->rtsp.auto_search = false;
    } else if (!strcmp(argv[i], "--any")) {
      vo->lib.prefer_hx = false;
    } else if (!strcmp(argv[i], "--source") && i + 1 < argc) {
      snprintf(vo->lib.source_substr, sizeof(vo->lib.source_substr), "%s", argv[++i]);
    } else if (!strcmp(argv[i], "--ip") && i + 1 < argc) {
      snprintf(vo->lib.ip_substr, sizeof(vo->lib.ip_substr), "%s", argv[++i]);
      snprintf(vo->srt.ip_substr, sizeof(vo->srt.ip_substr), "%s", vo->lib.ip_substr);
      snprintf(vo->rtmp.ip_substr, sizeof(vo->rtmp.ip_substr), "%s", vo->lib.ip_substr);
      snprintf(vo->rtsp.ip_substr, sizeof(vo->rtsp.ip_substr), "%s", vo->lib.ip_substr);
    } else if (!strcmp(argv[i], "--protocol") && i + 1 < argc) {
      const char *p = argv[++i];
      if (!strcasecmp(p, "ghost_ndihx") || !strcasecmp(p, "ndi_hx") || !strcasecmp(p, "ndi-hx") ||
          !strcasecmp(p, "ndi"))
        vo->protocol = VIEWER_PROTO_NDI_HX;
      else if (!strcasecmp(p, "srt"))
        vo->protocol = VIEWER_PROTO_SRT;
      else if (!strcasecmp(p, "rtmp"))
        vo->protocol = VIEWER_PROTO_RTMP;
      else if (!strcasecmp(p, "rtsp"))
        vo->protocol = VIEWER_PROTO_RTSP;
      else {
        fprintf(stderr, "Invalid --protocol (use ghost_ndihx|srt|rtmp|rtsp)\n");
        return false;
      }
    } else if (!strcmp(argv[i], "--url") && i + 1 < argc) {
      const char *u = argv[++i];
      snprintf(vo->srt.url, sizeof(vo->srt.url), "%s", u);
      snprintf(vo->rtmp.url, sizeof(vo->rtmp.url), "%s", u);
      snprintf(vo->rtsp.url, sizeof(vo->rtsp.url), "%s", u);
      snprintf(vo->lib.source_substr, sizeof(vo->lib.source_substr), "%s", u);
    } else if (!strcmp(argv[i], "--discover") && i + 1 < argc) {
      ghost_discover_backend_id_t id;
      if (!ghost_discover_backend_parse(argv[++i], &id)) {
        fprintf(stderr, "Invalid --discover (use auto|bonjour|ndi_sdk)\n");
        return false;
      }
      vo->lib.discover_backend = id;
    } else if (!strcmp(argv[i], "--find-ms") && i + 1 < argc) {
      vo->lib.find_ms = atoi(argv[++i]);
    } else if (!strcmp(argv[i], "--rescan-ms") && i + 1 < argc) {
      vo->lib.rescan_ms = atoi(argv[++i]);
    } else if (!strcmp(argv[i], "--noframe-ms") && i + 1 < argc) {
      vo->noframe_ms = atoi(argv[++i]);
    } else if (!strcmp(argv[i], "--bandwidth") && i + 1 < argc) {
      const char *v = argv[++i];
      if (!strcasecmp(v, "lowest") || !strcasecmp(v, "low"))
        vo->lib.bandwidth = GHOST_NDIHX_BW_LOWEST;
      else if (!strcasecmp(v, "highest") || !strcasecmp(v, "high") || !strcasecmp(v, "full"))
        vo->lib.bandwidth = GHOST_NDIHX_BW_HIGHEST;
      else {
        fprintf(stderr, "Invalid --bandwidth\n");
        return false;
      }
    } else if (!strcmp(argv[i], "--max-w") && i + 1 < argc) {
      vo->max_w = atoi(argv[++i]);
    } else if (!strcmp(argv[i], "--max-h") && i + 1 < argc) {
      vo->max_h = atoi(argv[++i]);
    } else if (!strcmp(argv[i], "--fps-cap") && i + 1 < argc) {
      vo->fps_cap = atoi(argv[++i]);
    } else if (!strcmp(argv[i], "--hz") && i + 1 < argc) {
      vo->hz = atoi(argv[++i]);
    } else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
      usage(argv[0]);
      exit(0);
    } else {
      fprintf(stderr, "Unknown argument: %s\n", argv[i]);
      usage(argv[0]);
      return false;
    }
  }
  if (vo->noframe_ms < 1000)
    vo->noframe_ms = 1000;
  return true;
}

static void print_sources(const ghost_ndihx_source_t *sources, int n) {
  printf("Discovered %d NDI source(s) via libghost_discover:\n", n);
  for (int i = 0; i < n; i++) {
    const char *be = sources[i].backend[0] ? sources[i].backend : "?";
    printf("  [%d] %s  url=%s%s  [%s%s]\n", i, sources[i].name[0] ? sources[i].name : "?",
           sources[i].url[0] ? sources[i].url : "?", sources[i].is_hx ? "  [HX]" : "", be,
           sources[i].via_mdns ? "+mdns" : "");
  }
}

static void compute_dst(int tex_w, int tex_h, int ww, int wh, int max_w, int max_h,
                        SDL_Rect *dst) {
  int out_w = ww;
  int out_h = wh;
  if (max_w > 0 && out_w > max_w)
    out_w = max_w;
  if (max_h > 0 && out_h > max_h)
    out_h = max_h;
  dst->x = 0;
  dst->y = 0;
  dst->w = out_w;
  dst->h = out_h;
  if (tex_w <= 0 || tex_h <= 0)
    return;
  float sx = (float)out_w / (float)tex_w;
  float sy = (float)out_h / (float)tex_h;
  float s = sx < sy ? sx : sy;
  int dw = (int)(tex_w * s);
  int dh = (int)(tex_h * s);
  dst->x = (ww - dw) / 2;
  dst->y = (wh - dh) / 2;
  dst->w = dw;
  dst->h = dh;
}

static void step_max(int *v, int delta, int min_v) {
  int n = *v + delta;
  if (n < min_v)
    n = min_v;
  if (n > 7680)
    n = 7680;
  *v = n;
}

static void refresh_ptz_caps(UiState *ui, ghost_ndihx_session_t *session) {
  ui->caps = (media_caps_t)ghost_ndihx_capabilities(session);
  ui->ptz_available = media_caps_has_ptz(ui->caps);
  ui->last_ptz_probe = SDL_GetTicks();
}

static void apply_ptz_hold(ghost_ndihx_session_t *session, HitId id, bool down) {
  if (!ghost_ndihx_ptz_supported(session))
    return;
  if (!down) {
    ghost_ndihx_ptz_stop(session);
    return;
  }
  float pan = 0, tilt = 0, zoom = 0;
  switch (id) {
  case HIT_PTZ_N:
    tilt = 0.6f;
    break;
  case HIT_PTZ_S:
    tilt = -0.6f;
    break;
  case HIT_PTZ_E:
    pan = 0.6f;
    break;
  case HIT_PTZ_W:
    pan = -0.6f;
    break;
  case HIT_PTZ_ZOOM_IN:
    zoom = 0.5f;
    break;
  case HIT_PTZ_ZOOM_OUT:
    zoom = -0.5f;
    break;
  default:
    return;
  }
  ghost_ndihx_ptz_move(session, pan, tilt, zoom, true);
}

static bool handle_hit(HitId id, ViewerOptions *vo, UiState *ui, ghost_ndihx_session_t *session,
                       SDL_Window *win, bool *fullscreen, bool *force_rescan, bool press) {
  switch (id) {
  case HIT_NONE:
    return false;
  case HIT_PANEL_BG:
    return true;
  case HIT_OPEN_CONTROLS:
    if (press)
      ui->controls_open = true;
    return true;
  case HIT_CLOSE_CONTROLS:
    if (press)
      ui->controls_open = false;
    return true;
  case HIT_BW_LOW:
    if (press) {
      vo->lib.bandwidth = GHOST_NDIHX_BW_LOWEST;
      ghost_ndihx_set_bandwidth(session, vo->lib.bandwidth);
    }
    return true;
  case HIT_BW_HIGH:
    if (press) {
      vo->lib.bandwidth = GHOST_NDIHX_BW_HIGHEST;
      ghost_ndihx_set_bandwidth(session, vo->lib.bandwidth);
    }
    return true;
  case HIT_FPS_DOWN:
    if (press) {
      if (vo->fps_cap <= 0)
        vo->fps_cap = 30;
      vo->fps_cap = vo->fps_cap > 1 ? vo->fps_cap - 1 : 1;
    }
    return true;
  case HIT_FPS_UP:
    if (press) {
      if (vo->fps_cap <= 0)
        vo->fps_cap = 30;
      vo->fps_cap++;
    }
    return true;
  case HIT_FPS_UNCAPPED:
    if (press)
      vo->fps_cap = 0;
    return true;
  case HIT_MAXW_DOWN:
    if (press)
      step_max(&vo->max_w, vo->max_w > 0 ? -64 : 1280, 0);
    return true;
  case HIT_MAXW_UP:
    if (press)
      step_max(&vo->max_w, vo->max_w > 0 ? 64 : 1280, 0);
    return true;
  case HIT_MAXH_DOWN:
    if (press)
      step_max(&vo->max_h, vo->max_h > 0 ? -64 : 720, 0);
    return true;
  case HIT_MAXH_UP:
    if (press)
      step_max(&vo->max_h, vo->max_h > 0 ? 64 : 720, 0);
    return true;
  case HIT_AUTO_TOGGLE:
    if (press) {
      vo->lib.auto_search = !vo->lib.auto_search;
      ghost_ndihx_set_auto_search(session, vo->lib.auto_search);
    }
    return true;
  case HIT_FULLSCREEN:
    if (press) {
      *fullscreen = !*fullscreen;
      SDL_SetWindowFullscreen(win, *fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    }
    return true;
  case HIT_RESCAN:
    if (press)
      *force_rescan = true;
    return true;
  case HIT_STATS_TOGGLE:
    if (press)
      ui->hud_on = !ui->hud_on;
    return true;
  case HIT_PTZ_HOME:
    if (press)
      ghost_ndihx_ptz_home(session);
    return true;
  case HIT_PTZ_STOP:
    if (press)
      ghost_ndihx_ptz_stop(session);
    return true;
  case HIT_PTZ_N:
  case HIT_PTZ_S:
  case HIT_PTZ_E:
  case HIT_PTZ_W:
  case HIT_PTZ_ZOOM_IN:
  case HIT_PTZ_ZOOM_OUT:
    if (press) {
      ui->held_ptz = id;
      apply_ptz_hold(session, id, true);
    }
    return true;
  default:
    return false;
  }
}

static int run_url_protocol_viewer(ViewerOptions *vo) {
  const char *mod_id = vo->protocol == VIEWER_PROTO_SRT
                           ? "srt"
                           : vo->protocol == VIEWER_PROTO_RTMP ? "rtmp" : "rtsp";
  const media_module_t *mod = media_find_module(mod_id);
  if (!mod) {
    fprintf(stderr, "module %s not registered\n", mod_id);
    return 1;
  }
  mod->init();

  media_open_params_t op;
  media_open_params_defaults(&op);
  if (vo->protocol == VIEWER_PROTO_SRT) {
    if (vo->lib.ip_substr[0] && !vo->srt.ip_substr[0])
      snprintf(vo->srt.ip_substr, sizeof(vo->srt.ip_substr), "%s", vo->lib.ip_substr);
    op.auto_search = vo->srt.auto_search;
    op.protocol_opts = &vo->srt;
    snprintf(op.ip_substr, sizeof(op.ip_substr), "%s", vo->srt.ip_substr);
  } else if (vo->protocol == VIEWER_PROTO_RTMP) {
    if (vo->lib.ip_substr[0] && !vo->rtmp.ip_substr[0])
      snprintf(vo->rtmp.ip_substr, sizeof(vo->rtmp.ip_substr), "%s", vo->lib.ip_substr);
    op.auto_search = vo->rtmp.auto_search;
    op.protocol_opts = &vo->rtmp;
    snprintf(op.ip_substr, sizeof(op.ip_substr), "%s", vo->rtmp.ip_substr);
  } else {
    if (vo->lib.ip_substr[0] && !vo->rtsp.ip_substr[0])
      snprintf(vo->rtsp.ip_substr, sizeof(vo->rtsp.ip_substr), "%s", vo->lib.ip_substr);
    op.auto_search = vo->rtsp.auto_search;
    op.protocol_opts = &vo->rtsp;
    snprintf(op.ip_substr, sizeof(op.ip_substr), "%s", vo->rtsp.ip_substr);
  }

  fprintf(stderr, "%s %s — multi-protocol shell (active decoder: %s, media_core %s)\n", PRODUCT_NAME,
          VIEWER_VERSION, mod_id, media_core_version());

  media_session_t *session = mod->open(&op);
  if (!session) {
    fprintf(stderr, "%s session open failed (FFmpeg / libsrt for SRT?)\n", mod_id);
    return 1;
  }

  media_source_t sources[8];
  int n = mod->discover(session, sources, 8, 1000);
  if (n < 0)
    n = 0;
  printf("Discovered %d %s source(s):\n", n, mod_id);
  for (int i = 0; i < n; i++)
    printf("  [%d] %s  url=%s\n", i, sources[i].name, sources[i].url);
  fflush(stdout);

  if (vo->list_only) {
    mod->close(session);
    mod->shutdown();
    return n > 0 ? 0 : 1;
  }

  if (mod->connect_auto(session, NULL) != 0) {
    fprintf(stderr, "No matching %s source to connect\n", mod_id);
    mod->close(session);
    mod->shutdown();
    return 1;
  }

  media_source_t connected;
  mod->connected_source(session, &connected);
  printf("Connecting to: %s (%s)\n", connected.name, connected.url);
  fflush(stdout);

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
    fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
    mod->close(session);
    mod->shutdown();
    return 1;
  }

  Uint32 win_flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
  if (vo->fullscreen)
    win_flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
  int win_w = vo->max_w > 0 ? vo->max_w : 1280;
  int win_h = vo->max_h > 0 ? vo->max_h : 720;
  SDL_Window *win = SDL_CreateWindow(connected.name[0] ? connected.name : PRODUCT_NAME,
                                     SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, win_w, win_h,
                                     win_flags);
  SDL_Renderer *ren =
      win ? SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC) : NULL;
  if (ren)
    SDL_SetHint(SDL_HINT_RENDER_VSYNC, "0");
  if (!win || !ren) {
    fprintf(stderr, "SDL window/renderer failed\n");
    if (ren)
      SDL_DestroyRenderer(ren);
    if (win)
      SDL_DestroyWindow(win);
    SDL_Quit();
    mod->close(session);
    mod->shutdown();
    return 1;
  }

  SDL_Texture *tex = NULL;
  int tex_w = 0, tex_h = 0;
  bool running = true, paused = false;
  Uint32 fps_t0 = SDL_GetTicks();
  int fps_frames = 0, fps_display = 0;

  while (running) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
      if (ev.type == SDL_QUIT)
        running = false;
      else if (ev.type == SDL_KEYDOWN) {
        SDL_Keycode k = ev.key.keysym.sym;
        if (k == SDLK_q || k == SDLK_ESCAPE)
          running = false;
        else if (k == SDLK_f)
          SDL_SetWindowFullscreen(win, (SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN_DESKTOP)
                                           ? 0
                                           : SDL_WINDOW_FULLSCREEN_DESKTOP);
        else if (k == SDLK_SPACE)
          paused = !paused;
        else if (k == SDLK_r) {
          mod->disconnect(session);
          mod->connect_auto(session, NULL);
          mod->connected_source(session, &connected);
        }
      }
    }

    media_frame_t fr;
    bool got = false;
    if (!paused)
      got = mod->capture_newest(session, &fr);
    else if (mod_id[0]) {
      /* Drain while paused when module supports it via native API */
      if (vo->protocol == VIEWER_PROTO_SRT)
        ghost_srt_drain((ghost_srt_session_t *)session);
      else if (vo->protocol == VIEWER_PROTO_RTMP)
        ghost_rtmp_drain((ghost_rtmp_session_t *)session);
      else
        ghost_rtsp_drain((ghost_rtsp_session_t *)session);
    }

    if (got && fr.data && fr.width > 0 && fr.height > 0) {
      if (!tex || tex_w != fr.width || tex_h != fr.height) {
        if (tex)
          SDL_DestroyTexture(tex);
        tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_BGRA32, SDL_TEXTUREACCESS_STREAMING, fr.width,
                                fr.height);
        tex_w = fr.width;
        tex_h = fr.height;
      }
      if (tex) {
        void *pixels;
        int pitch;
        if (SDL_LockTexture(tex, NULL, &pixels, &pitch) == 0) {
          const uint8_t *src = fr.data;
          uint8_t *dst = (uint8_t *)pixels;
          int row = fr.width * 4;
          for (int y = 0; y < fr.height; y++) {
            memcpy(dst + y * pitch, src + y * fr.stride, (size_t)row);
          }
          SDL_UnlockTexture(tex);
        }
      }
      fps_frames++;
    }

    int ww, wh;
    SDL_GetRendererOutputSize(ren, &ww, &wh);
    SDL_Rect dst;
    compute_dst(tex_w, tex_h, ww, wh, vo->max_w, vo->max_h, &dst);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);
    if (tex)
      SDL_RenderCopy(ren, tex, NULL, &dst);
    SDL_RenderPresent(ren);

    Uint32 now = SDL_GetTicks();
    if (now - fps_t0 >= 1000) {
      fps_display = fps_frames;
      fps_frames = 0;
      fps_t0 = now;
      char title[512];
      snprintf(title, sizeof(title), "%s · %s · %s  %dx%d  %d fps%s", PRODUCT_NAME, mod_id,
               connected.name[0] ? connected.name : "—", tex_w, tex_h, fps_display,
               paused ? "  [paused]" : "");
      SDL_SetWindowTitle(win, title);
    }

    if (vo->fps_cap > 0)
      SDL_Delay((Uint32)(1000 / vo->fps_cap));
  }

  if (tex)
    SDL_DestroyTexture(tex);
  SDL_DestroyRenderer(ren);
  SDL_DestroyWindow(win);
  SDL_Quit();
  mod->close(session);
  mod->shutdown();
  return 0;
}

int main(int argc, char **argv) {
  ViewerOptions vo;
  if (!parse_args(argc, argv, &vo))
    return 2;

  ghost_ndihx_register_media_module();
  ghost_srt_register_media_module();
  ghost_rtmp_register_media_module();
  ghost_rtsp_register_media_module();

  if (vo.protocol == VIEWER_PROTO_SRT || vo.protocol == VIEWER_PROTO_RTMP ||
      vo.protocol == VIEWER_PROTO_RTSP)
    return run_url_protocol_viewer(&vo);

  /* Peer-visible receiver name on the LAN */
  if (!vo.lib.recv_name[0] || !strcmp(vo.lib.recv_name, GHOST_NDIHX_DEFAULT_RECV_NAME))
    snprintf(vo.lib.recv_name, sizeof(vo.lib.recv_name), "%s", PRODUCT_NAME);

  fprintf(stderr,
          "%s %s — multi-protocol shell (active decoder: libghost_ndihx %s, media_core %s, "
          "discover=%s / libghost_discover %s)\n",
          PRODUCT_NAME, VIEWER_VERSION, ghost_ndihx_version(), media_core_version(),
          ghost_discover_backend_name(vo.lib.discover_backend), ghost_discover_version());

  ghost_ndihx_session_t *session = ghost_ndihx_session_create(&vo.lib);
  if (!session) {
    fprintf(stderr, "ghost_ndihx_session_create failed (install libndi + FFmpeg >= 7)\n");
    return 1;
  }

  ghost_ndihx_source_t sources[GHOST_NDIHX_MAX_SOURCES];
  int n = ghost_ndihx_discover(session, sources, GHOST_NDIHX_MAX_SOURCES, vo.lib.find_ms);
  if (n < 0)
    n = 0;
  print_sources(sources, n);

  if (vo.list_only) {
    ghost_ndihx_session_destroy(session);
    return n > 0 ? 0 : 1;
  }

  volatile int cancel = 0;
  if (ghost_ndihx_connect_auto(session, &cancel) != 0) {
    fprintf(stderr, "No matching source to connect\n");
    ghost_ndihx_session_destroy(session);
    return 1;
  }

  ghost_ndihx_source_t connected;
  ghost_ndihx_connected_source(session, &connected);
  printf("Connecting to: %s (%s)  bandwidth=%s\n", connected.name, connected.url,
         ghost_ndihx_bandwidth_name(vo.lib.bandwidth));
  fflush(stdout);

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
    fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
    ghost_ndihx_session_destroy(session);
    return 1;
  }

  Uint32 win_flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
  if (vo.fullscreen)
    win_flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;

  int win_w = vo.max_w > 0 ? vo.max_w : 1280;
  int win_h = vo.max_h > 0 ? vo.max_h : 720;
  if (win_w < 320)
    win_w = 320;
  if (win_h < 180)
    win_h = 180;

  SDL_Window *win =
      SDL_CreateWindow(connected.name[0] ? connected.name : PRODUCT_NAME,
                       SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, win_w, win_h, win_flags);
  if (!win) {
    fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
    SDL_Quit();
    ghost_ndihx_session_destroy(session);
    return 1;
  }

  if (vo.hz > 0) {
    SDL_DisplayMode mode;
    int display = SDL_GetWindowDisplayIndex(win);
    if (display >= 0 && SDL_GetDesktopDisplayMode(display, &mode) == 0) {
      mode.refresh_rate = vo.hz;
      SDL_SetWindowDisplayMode(win, &mode);
    }
  }

  SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
  if (!ren)
    ren = SDL_CreateRenderer(win, -1, 0);
  if (!ren) {
    fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
    SDL_DestroyWindow(win);
    SDL_Quit();
    ghost_ndihx_session_destroy(session);
    return 1;
  }
  SDL_SetHint(SDL_HINT_RENDER_VSYNC, "0");
  SDL_RenderSetVSync(ren, 0);
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");

  SDL_Texture *tex = NULL;
  int tex_w = 0, tex_h = 0;
  bool running = true;
  bool fullscreen = vo.fullscreen;
  bool paused = false;
  bool force_rescan = false;
  bool logged_first = false;
  Uint32 last_frame_ticks = SDL_GetTicks();
  Uint32 last_present = 0;
  Uint32 fps_t0 = SDL_GetTicks();
  int fps_frames = 0;
  int fps_display = 0;
  uint64_t dropped_total = 0;
  uint64_t dropped_window = 0;
  int src_n = 0, src_d = 0;

  UiState ui;
  memset(&ui, 0, sizeof(ui));
  ui.hud_on = vo.stats;
  ui.chrome_visible = true;
  ui.last_input_ticks = SDL_GetTicks();
  ui.held_ptz = HIT_NONE;
  refresh_ptz_caps(&ui, session);

  if (vo.auto_hide)
    SDL_ShowCursor(SDL_DISABLE);

  printf("Keys: q/Esc quit · f fullscreen · Space pause · r rescan · c controls · i stats · "
         "[/] bandwidth · -/= fps-cap · 0 uncapped%s\n",
         vo.auto_hide ? " · auto-hide chrome" : "");
  fflush(stdout);

  while (running) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
      if (ev.type == SDL_QUIT)
        running = false;
      else if (ev.type == SDL_KEYDOWN || ev.type == SDL_MOUSEMOTION ||
               ev.type == SDL_MOUSEBUTTONDOWN || ev.type == SDL_MOUSEBUTTONUP) {
        ui.last_input_ticks = SDL_GetTicks();
        if (vo.auto_hide) {
          ui.chrome_visible = true;
          SDL_ShowCursor(SDL_ENABLE);
        }
      }
      if (ev.type == SDL_KEYDOWN) {
        SDL_Keycode k = ev.key.keysym.sym;
        if (k == SDLK_ESCAPE || k == SDLK_q)
          running = false;
        else if (k == SDLK_f) {
          fullscreen = !fullscreen;
          SDL_SetWindowFullscreen(win, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
        } else if (k == SDLK_SPACE)
          paused = !paused;
        else if (k == SDLK_r)
          force_rescan = true;
        else if (k == SDLK_c)
          ui.controls_open = !ui.controls_open;
        else if (k == SDLK_i)
          ui.hud_on = !ui.hud_on;
        else if (k == SDLK_LEFTBRACKET) {
          vo.lib.bandwidth = GHOST_NDIHX_BW_LOWEST;
          ghost_ndihx_set_bandwidth(session, vo.lib.bandwidth);
        } else if (k == SDLK_RIGHTBRACKET) {
          vo.lib.bandwidth = GHOST_NDIHX_BW_HIGHEST;
          ghost_ndihx_set_bandwidth(session, vo.lib.bandwidth);
        } else if (k == SDLK_MINUS || k == SDLK_KP_MINUS) {
          if (vo.fps_cap <= 0)
            vo.fps_cap = fps_display > 0 ? fps_display : 30;
          vo.fps_cap = vo.fps_cap > 1 ? vo.fps_cap - 1 : 1;
        } else if (k == SDLK_EQUALS || k == SDLK_PLUS || k == SDLK_KP_PLUS) {
          if (vo.fps_cap <= 0)
            vo.fps_cap = fps_display > 0 ? fps_display : 30;
          vo.fps_cap++;
        } else if (k == SDLK_0 || k == SDLK_KP_0) {
          vo.fps_cap = 0;
        }
      } else if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT) {
        HitId id = ui_hit_at(&ui, ev.button.x, ev.button.y);
        if (id != HIT_NONE)
          handle_hit(id, &vo, &ui, session, win, &fullscreen, &force_rescan, true);
      } else if (ev.type == SDL_MOUSEBUTTONUP && ev.button.button == SDL_BUTTON_LEFT) {
        if (ui.held_ptz != HIT_NONE) {
          apply_ptz_hold(session, ui.held_ptz, false);
          ui.held_ptz = HIT_NONE;
        }
      }
    }

    if (vo.auto_hide && ui.chrome_visible) {
      Uint32 idle_ms = (Uint32)(vo.auto_hide_ms > 0 ? vo.auto_hide_ms : 4000);
      if ((SDL_GetTicks() - ui.last_input_ticks) >= idle_ms) {
        ui.chrome_visible = false;
        ui.controls_open = false;
        SDL_ShowCursor(SDL_DISABLE);
      }
    }

    Uint32 now = SDL_GetTicks();
    if (now - ui.last_ptz_probe > 1500)
      refresh_ptz_caps(&ui, session);

    if (force_rescan || (!paused && vo.lib.auto_search &&
                         (now - last_frame_ticks) > (Uint32)vo.noframe_ms)) {
      force_rescan = false;
      printf("Rescanning / reconnecting…\n");
      fflush(stdout);
      if (tex) {
        SDL_DestroyTexture(tex);
        tex = NULL;
        tex_w = tex_h = 0;
      }
      logged_first = false;
      ghost_ndihx_disconnect(session);
      ui.ptz_available = false;
      ui.caps = MEDIA_CAP_NONE;
      cancel = 0;
      while (running && ghost_ndihx_connect_auto(session, &cancel) != 0) {
        while (SDL_PollEvent(&ev)) {
          if (ev.type == SDL_QUIT ||
              (ev.type == SDL_KEYDOWN &&
               (ev.key.keysym.sym == SDLK_q || ev.key.keysym.sym == SDLK_ESCAPE))) {
            running = false;
            cancel = 1;
          }
        }
        if (!running)
          break;
        fprintf(stderr, "Auto-search: retry…\n");
      }
      if (!running)
        break;
      ghost_ndihx_connected_source(session, &connected);
      SDL_SetWindowTitle(win, connected.name);
      last_frame_ticks = SDL_GetTicks();
      refresh_ptz_caps(&ui, session);
    }

    ghost_ndihx_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    if (!paused) {
      if (ghost_ndihx_capture_newest(session, &frame)) {
        dropped_total += frame.dropped;
        dropped_window += frame.dropped;
        last_frame_ticks = SDL_GetTicks();
        src_n = frame.frame_rate_n;
        src_d = frame.frame_rate_d;

        if (!logged_first) {
          double src_fps = src_d > 0 ? (double)src_n / (double)src_d : 0.0;
          printf("First frame: %dx%d FourCC=0x%08x stride=%d src≈%.2f fps  RSS=%ld kB  "
                 "ptz=%s\n",
                 frame.width, frame.height, frame.fourcc, frame.stride, src_fps, read_rss_kb(),
                 ui.ptz_available ? "yes" : "no");
          fflush(stdout);
          logged_first = true;
        }

        if (!tex || tex_w != frame.width || tex_h != frame.height) {
          if (tex)
            SDL_DestroyTexture(tex);
          tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_BGRX32, SDL_TEXTUREACCESS_STREAMING,
                                  frame.width, frame.height);
          tex_w = frame.width;
          tex_h = frame.height;
          if (!tex) {
            fprintf(stderr, "SDL_CreateTexture: %s\n", SDL_GetError());
            running = false;
            break;
          }
        }
        /* Prefer LockTexture streaming upload (often one less internal copy than
         * UpdateTexture on accelerated backends). Fall back to UpdateTexture. */
        {
          void *pixels = NULL;
          int pitch = 0;
          if (SDL_LockTexture(tex, NULL, &pixels, &pitch) == 0 && pixels) {
            const uint8_t *src = frame.data;
            uint8_t *dst = (uint8_t *)pixels;
            if (pitch == frame.stride) {
              memcpy(dst, src, (size_t)pitch * (size_t)frame.height);
            } else {
              int row_bytes = frame.width * 4;
              if (row_bytes > frame.stride)
                row_bytes = frame.stride;
              if (row_bytes > pitch)
                row_bytes = pitch;
              for (int y = 0; y < frame.height; y++)
                memcpy(dst + (size_t)y * (size_t)pitch, src + (size_t)y * (size_t)frame.stride,
                       (size_t)row_bytes);
            }
            SDL_UnlockTexture(tex);
          } else if (SDL_UpdateTexture(tex, NULL, frame.data, frame.stride) != 0) {
            fprintf(stderr, "SDL_UpdateTexture: %s\n", SDL_GetError());
          }
        }
      } else {
        SDL_Delay(1);
      }
    } else {
      /* Keep presenting the frozen texture, but drain the NDI queue so pause
       * cannot inflate RSS (previously ~300 MiB growth while paused). */
      dropped_total += ghost_ndihx_drain(session);
      last_frame_ticks = SDL_GetTicks(); /* avoid auto-rescan while paused */
      SDL_Delay(8);
    }

    bool present_ok = true;
    if (vo.fps_cap > 0) {
      Uint32 min_delta = (Uint32)(1000 / vo.fps_cap);
      if (min_delta < 1)
        min_delta = 1;
      if (last_present != 0 && (SDL_GetTicks() - last_present) < min_delta)
        present_ok = false;
    }

    if (present_ok) {
      SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
      SDL_RenderClear(ren);
      if (tex) {
        int ww, wh;
        SDL_GetRendererOutputSize(ren, &ww, &wh);
        SDL_Rect dst;
        compute_dst(tex_w, tex_h, ww, wh, vo.max_w, vo.max_h, &dst);
        SDL_RenderCopy(ren, tex, NULL, &dst);
      }

      ui_clear_hits(&ui);
      double src_fps = src_d > 0 ? (double)src_n / (double)src_d : 0.0;
      bool show_chrome = !vo.auto_hide || ui.chrome_visible;
      if (show_chrome && ui.hud_on)
        draw_hud(ren, &vo, &connected, tex_w, tex_h, fps_display, src_fps, paused);
      if (show_chrome) {
        if (ui.controls_open)
          layout_controls(ren, &ui, &vo, &connected, src_fps);
        else
          draw_controls_chip(ren, &ui);
      }

      SDL_RenderPresent(ren);
      last_present = SDL_GetTicks();
      fps_frames++;
    }

    now = SDL_GetTicks();
    if (now - fps_t0 >= 1000) {
      fps_display = fps_frames;
      fps_frames = 0;
      fps_t0 = now;
      double src_fps = src_d > 0 ? (double)src_n / (double)src_d : 0.0;
      char title[768];
      snprintf(title, sizeof(title), "%s · %s  %dx%d  %d fps  src≈%.1f  bw=%s  drop=%llu%s%s",
               PRODUCT_NAME, connected.name[0] ? connected.name : "—", tex_w, tex_h, fps_display,
               src_fps, ghost_ndihx_bandwidth_name(vo.lib.bandwidth),
               (unsigned long long)dropped_window, paused ? "  [paused]" : "",
               ui.ptz_available ? "  [ptz]" : "");
      SDL_SetWindowTitle(win, title);
      if (vo.stats && !ui.hud_on) {
        /* Keep console stats when --stats was requested and HUD toggled off. */
        printf("stats: present=%d fps  src≈%.2f  %dx%d  bw=%s  dropped=%llu "
               "(session %llu)  RSS=%ld kB  fps_cap=%d  ptz=%d\n",
               fps_display, src_fps, tex_w, tex_h, ghost_ndihx_bandwidth_name(vo.lib.bandwidth),
               (unsigned long long)dropped_window, (unsigned long long)dropped_total,
               read_rss_kb(), vo.fps_cap, ui.ptz_available ? 1 : 0);
        fflush(stdout);
      }
      dropped_window = 0;
      (void)dropped_total;
    }
  }

  if (ui.held_ptz != HIT_NONE)
    ghost_ndihx_ptz_stop(session);

  if (tex)
    SDL_DestroyTexture(tex);
  SDL_DestroyRenderer(ren);
  SDL_DestroyWindow(win);
  SDL_Quit();
  ghost_ndihx_session_destroy(session);
  return 0;
}
