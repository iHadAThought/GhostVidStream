/**
 * @file ghost_rtsp.h
 * @brief libghost_rtsp — RTSP/RTP receive API (first-class GhostVidStream plugin).
 *
 * UI-free RTSP library. On AIDA, RTSP sits above SRT in the camera UI — both are
 * first-class; this module matches the libghost_ndihx / libghost_srt API bar.
 *
 * Typical embed flow mirrors ghost_srt. Default AIDA URL:
 *   rtsp://IP:554/stream/main   (sub: …/stream/sub)
 * TCP interleaved transport is preferred by default (tcp_transport=true).
 *
 * Threading: one session is not thread-safe.
 * Dependencies: FFmpeg. No PTZ in this module (VISCA remains separate).
 * See docs/embed-rtsp.md.
 */
#ifndef GHOST_RTSP_H
#define GHOST_RTSP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GHOST_RTSP_VERSION "0.1.0"
#define GHOST_RTSP_DEFAULT_PORT 554
#define GHOST_RTSP_URL_MAX 512

typedef struct ghost_rtsp_session ghost_rtsp_session_t;

typedef struct ghost_rtsp_options {
  char url[GHOST_RTSP_URL_MAX]; /**< Full rtsp:// URL (preferred). */
  char ip_substr[128];          /**< If url empty: build from IP + path. */
  int port;                     /**< Default 554. */
  char path[128];               /**< Default "stream/main". */
  bool tcp_transport;           /**< Prefer RTSP over TCP. Default true. */
  bool auto_search;
  int find_ms;
  int rescan_ms;
  int capture_wait_ms;
  bool low_latency;
  int connect_timeout_ms;
} ghost_rtsp_options_t;

typedef struct ghost_rtsp_source {
  char name[256];
  char url[GHOST_RTSP_URL_MAX];
} ghost_rtsp_source_t;

typedef struct ghost_rtsp_frame {
  const uint8_t *data;
  int width;
  int height;
  int stride;
  uint32_t fourcc;
  int frame_rate_n;
  int frame_rate_d;
  uint64_t dropped;
} ghost_rtsp_frame_t;

int ghost_rtsp_init(void);
void ghost_rtsp_shutdown(void);
void ghost_rtsp_options_defaults(ghost_rtsp_options_t *opt);
int ghost_rtsp_options_load_file(ghost_rtsp_options_t *opt, const char *path, char *err,
                                 size_t err_len);

ghost_rtsp_session_t *ghost_rtsp_session_create(const ghost_rtsp_options_t *opt);
void ghost_rtsp_session_destroy(ghost_rtsp_session_t *session);

int ghost_rtsp_discover(ghost_rtsp_session_t *session, ghost_rtsp_source_t *out, int cap,
                        int wait_ms);
int ghost_rtsp_connect(ghost_rtsp_session_t *session, const ghost_rtsp_source_t *src);
int ghost_rtsp_connect_auto(ghost_rtsp_session_t *session, volatile const int *cancel);
void ghost_rtsp_disconnect(ghost_rtsp_session_t *session);
bool ghost_rtsp_is_connected(const ghost_rtsp_session_t *session);
void ghost_rtsp_connected_source(const ghost_rtsp_session_t *session, ghost_rtsp_source_t *out);

bool ghost_rtsp_capture_newest(ghost_rtsp_session_t *session, ghost_rtsp_frame_t *out);
void ghost_rtsp_drain(ghost_rtsp_session_t *session);

const char *ghost_rtsp_version(void);

struct media_module;
const struct media_module *ghost_rtsp_media_module(void);
int ghost_rtsp_register_media_module(void);

#ifdef __cplusplus
}
#endif

#endif /* GHOST_RTSP_H */
