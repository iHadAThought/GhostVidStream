/**
 * @file ghost_rtmp.h
 * @brief libghost_rtmp — RTMP / HTTP-FLV receive API (first-class GhostVidStream plugin).
 *
 * UI-free RTMP pull library. Same lifecycle bar as libghost_ndihx / libghost_srt.
 * Hosts may call `ghost_rtmp_*` directly or register with `media_core`.
 *
 * Typical embed flow mirrors ghost_srt (init → options → session → connect_auto →
 * capture_newest BGRX → destroy). Default AIDA pull URL:
 *   rtmp://IP:1935/app/rtmpstream0
 * Optional HTTP-FLV: prefer_flv → http://IP:8080/app/rtmpstream0.flv
 *
 * Threading: one session is not thread-safe.
 * Dependencies: FFmpeg (RTMP demux). No PTZ. See docs/embed-rtmp.md.
 */
#ifndef GHOST_RTMP_H
#define GHOST_RTMP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GHOST_RTMP_VERSION "0.1.0"
#define GHOST_RTMP_DEFAULT_PORT 1935
#define GHOST_RTMP_URL_MAX 512

typedef struct ghost_rtmp_session ghost_rtmp_session_t;

typedef struct ghost_rtmp_options {
  char url[GHOST_RTMP_URL_MAX];
  char ip_substr[128];
  int port;          /**< Default 1935. */
  char app[64];      /**< Default "app". */
  char stream[64];   /**< Default "rtmpstream0" (main). */
  bool prefer_flv;   /**< Use http://IP:8080/app/stream.flv instead of rtmp:// */
  bool auto_search;
  int find_ms;
  int rescan_ms;
  int capture_wait_ms;
  bool low_latency;
  int connect_timeout_ms;
} ghost_rtmp_options_t;

typedef struct ghost_rtmp_source {
  char name[256];
  char url[GHOST_RTMP_URL_MAX];
} ghost_rtmp_source_t;

typedef struct ghost_rtmp_frame {
  const uint8_t *data;
  int width;
  int height;
  int stride;
  uint32_t fourcc;
  int frame_rate_n;
  int frame_rate_d;
  uint64_t dropped;
} ghost_rtmp_frame_t;

int ghost_rtmp_init(void);
void ghost_rtmp_shutdown(void);
void ghost_rtmp_options_defaults(ghost_rtmp_options_t *opt);
int ghost_rtmp_options_load_file(ghost_rtmp_options_t *opt, const char *path, char *err,
                                 size_t err_len);

ghost_rtmp_session_t *ghost_rtmp_session_create(const ghost_rtmp_options_t *opt);
void ghost_rtmp_session_destroy(ghost_rtmp_session_t *session);

int ghost_rtmp_discover(ghost_rtmp_session_t *session, ghost_rtmp_source_t *out, int cap,
                        int wait_ms);
int ghost_rtmp_connect(ghost_rtmp_session_t *session, const ghost_rtmp_source_t *src);
int ghost_rtmp_connect_auto(ghost_rtmp_session_t *session, volatile const int *cancel);
void ghost_rtmp_disconnect(ghost_rtmp_session_t *session);
bool ghost_rtmp_is_connected(const ghost_rtmp_session_t *session);
void ghost_rtmp_connected_source(const ghost_rtmp_session_t *session, ghost_rtmp_source_t *out);

bool ghost_rtmp_capture_newest(ghost_rtmp_session_t *session, ghost_rtmp_frame_t *out);
void ghost_rtmp_drain(ghost_rtmp_session_t *session);

const char *ghost_rtmp_version(void);

struct media_module;
const struct media_module *ghost_rtmp_media_module(void);
int ghost_rtmp_register_media_module(void);

#ifdef __cplusplus
}
#endif

#endif /* GHOST_RTMP_H */
