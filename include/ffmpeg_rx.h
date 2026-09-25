/**
 * @file ffmpeg_rx.h
 * @brief Shared low-latency FFmpeg URL receive helper (SRT / RTMP / RTSP / HTTP-FLV).
 *
 * Internal to GhostVidStream protocol modules. Decodes to BGRX (4 bytes/pixel).
 * Not a public stable API — prefer ghost_srt.h / ghost_rtmp.h.
 */
#ifndef FFMPEG_RX_H
#define FFMPEG_RX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FFMPEG_RX_URL_MAX 512
#define FFMPEG_RX_FOURCC_BGRX 0x58524742u /* 'BGRX' little-endian tag */

typedef struct ffmpeg_rx_options {
  char url[FFMPEG_RX_URL_MAX];
  int connect_timeout_ms; /**< Open timeout hint (ms). Default 5000. */
  int capture_wait_ms;    /**< Wait inside capture before returning false. Default 8. */
  bool low_latency;       /**< nobuffer / low_delay / tiny probe. Default true. */
  int rw_timeout_us;      /**< Network read/write timeout (µs). Default 5_000_000. */
  bool rtsp_tcp;          /**< Prefer RTSP interleaved TCP. Default true when URL is rtsp. */
} ffmpeg_rx_options_t;

typedef struct ffmpeg_rx_frame {
  const uint8_t *data;
  int width;
  int height;
  int stride;
  uint32_t fourcc;
  int frame_rate_n;
  int frame_rate_d;
  uint64_t dropped;
} ffmpeg_rx_frame_t;

typedef struct ffmpeg_rx_session ffmpeg_rx_session_t;

void ffmpeg_rx_options_defaults(ffmpeg_rx_options_t *opt);

/** 0 = FFmpeg libs usable. */
int ffmpeg_rx_init(void);
void ffmpeg_rx_shutdown(void);

ffmpeg_rx_session_t *ffmpeg_rx_session_create(const ffmpeg_rx_options_t *opt);
void ffmpeg_rx_session_destroy(ffmpeg_rx_session_t *s);

int ffmpeg_rx_connect(ffmpeg_rx_session_t *s, const char *url);
void ffmpeg_rx_disconnect(ffmpeg_rx_session_t *s);
bool ffmpeg_rx_is_connected(const ffmpeg_rx_session_t *s);
void ffmpeg_rx_connected_url(const ffmpeg_rx_session_t *s, char *out, size_t out_len);

/** Drain to newest decoded video frame; convert to BGRX. */
bool ffmpeg_rx_capture_newest(ffmpeg_rx_session_t *s, ffmpeg_rx_frame_t *out);

/** Discard queued compressed/decoded video without presenting. */
void ffmpeg_rx_drain(ffmpeg_rx_session_t *s);

const char *ffmpeg_rx_version(void);

#ifdef __cplusplus
}
#endif

#endif /* FFMPEG_RX_H */
