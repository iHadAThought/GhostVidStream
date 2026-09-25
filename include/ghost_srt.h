/**
 * @file ghost_srt.h
 * @brief libghost_srt — SRT receive API (first-class GhostVidStream decoder plugin).
 *
 * UI-free SRT library for Linux aarch64 + x86_64 (+ macOS for lab). GhostVidStream
 * is the multi-protocol shell; this header is the **SRT** module. On AIDA cameras,
 * SRT sits beside RTSP in the UI — treat it as a sibling first-class path to NDI|HX.
 *
 * Hosts may:
 *   A) Call `ghost_srt_*` directly (this header), or
 *   B) Use protocol-agnostic `media_core.h` after `ghost_srt_register_media_module()`.
 *
 * Typical native embed flow:
 *   1. ghost_srt_init()
 *   2. ghost_srt_options_defaults(&opt);  set url or ip/port/stream_id
 *   3. session = ghost_srt_session_create(&opt)
 *   4. ghost_srt_connect_auto(session)
 *   5. loop: ghost_srt_capture_newest(session, &frame)  // BGRX newest-frame
 *   6. ghost_srt_session_destroy(session); ghost_srt_shutdown()
 *
 * Threading: one session is not thread-safe. Use one session per thread, or
 * external locking around capture/connect/disconnect.
 *
 * Dependencies: FFmpeg with libsrt (--enable-libsrt), libsrt runtime.
 * No PTZ over SRT. See docs/embed-srt.md / docs/srt-rtmp-rtsp-encode-compatibility.md.
 */
#ifndef GHOST_SRT_H
#define GHOST_SRT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GHOST_SRT_VERSION "0.1.0"
#define GHOST_SRT_DEFAULT_PORT 1600
#define GHOST_SRT_URL_MAX 512

typedef struct ghost_srt_session ghost_srt_session_t;

typedef struct ghost_srt_options {
  char url[GHOST_SRT_URL_MAX]; /**< Full srt:// URL (preferred). */
  char ip_substr[128];         /**< If url empty: build listen/caller URL from IP. */
  int port;                    /**< Default 1600 (AIDA listen). */
  int latency_ms;              /**< SRT latency. Default 120. */
  int stream_id;               /**< streamid=r=N — 0=main, 1=sub on AIDA. */
  bool auto_search;            /**< Retry connect until success. */
  int find_ms;
  int rescan_ms;
  int capture_wait_ms;
  bool low_latency;
  int connect_timeout_ms;
} ghost_srt_options_t;

typedef struct ghost_srt_source {
  char name[256];
  char url[GHOST_SRT_URL_MAX];
} ghost_srt_source_t;

typedef struct ghost_srt_frame {
  const uint8_t *data;
  int width;
  int height;
  int stride;
  uint32_t fourcc;
  int frame_rate_n;
  int frame_rate_d;
  uint64_t dropped;
} ghost_srt_frame_t;

int ghost_srt_init(void);
void ghost_srt_shutdown(void);
void ghost_srt_options_defaults(ghost_srt_options_t *opt);
int ghost_srt_options_load_file(ghost_srt_options_t *opt, const char *path, char *err, size_t err_len);

ghost_srt_session_t *ghost_srt_session_create(const ghost_srt_options_t *opt);
void ghost_srt_session_destroy(ghost_srt_session_t *session);

int ghost_srt_discover(ghost_srt_session_t *session, ghost_srt_source_t *out, int cap, int wait_ms);
int ghost_srt_connect(ghost_srt_session_t *session, const ghost_srt_source_t *src);
int ghost_srt_connect_auto(ghost_srt_session_t *session, volatile const int *cancel);
void ghost_srt_disconnect(ghost_srt_session_t *session);
bool ghost_srt_is_connected(const ghost_srt_session_t *session);
void ghost_srt_connected_source(const ghost_srt_session_t *session, ghost_srt_source_t *out);

bool ghost_srt_capture_newest(ghost_srt_session_t *session, ghost_srt_frame_t *out);
void ghost_srt_drain(ghost_srt_session_t *session);

const char *ghost_srt_version(void);

struct media_module;
const struct media_module *ghost_srt_media_module(void);
int ghost_srt_register_media_module(void);

#ifdef __cplusplus
}
#endif

#endif /* GHOST_SRT_H */
