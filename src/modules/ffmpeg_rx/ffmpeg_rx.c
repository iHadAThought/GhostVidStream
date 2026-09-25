/**
 * @file ffmpeg_rx.c
 * @brief Low-latency FFmpeg demux/decode → BGRX for GhostVidStream URL modules.
 */
#include "ffmpeg_rx.h"

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct ffmpeg_rx_session {
  ffmpeg_rx_options_t opt;
  AVFormatContext *fmt;
  AVCodecContext *dec;
  struct SwsContext *sws;
  AVFrame *frame;
  AVFrame *bgr;
  AVPacket *pkt;
  int video_stream;
  bool connected;
  char connected_url[FFMPEG_RX_URL_MAX];
  uint8_t *bgr_buf;
  int bgr_size;
  int width;
  int height;
  int stride;
  int fps_n;
  int fps_d;
  uint64_t dropped_acc;
};

static int g_ff_refs;

void ffmpeg_rx_options_defaults(ffmpeg_rx_options_t *opt) {
  if (!opt)
    return;
  memset(opt, 0, sizeof(*opt));
  opt->connect_timeout_ms = 5000;
  opt->capture_wait_ms = 8;
  opt->low_latency = true;
  opt->rw_timeout_us = 5000000;
  opt->rtsp_tcp = true;
}

const char *ffmpeg_rx_version(void) { return "0.1.0"; }

int ffmpeg_rx_init(void) {
  if (g_ff_refs == 0)
    av_log_set_level(AV_LOG_ERROR);
  g_ff_refs++;
  return 0;
}

void ffmpeg_rx_shutdown(void) {
  if (g_ff_refs > 0)
    g_ff_refs--;
}

static int64_t now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void free_bgr(ffmpeg_rx_session_t *s) {
  free(s->bgr_buf);
  s->bgr_buf = NULL;
  s->bgr_size = 0;
}

static void close_codec(ffmpeg_rx_session_t *s) {
  if (s->sws) {
    sws_freeContext(s->sws);
    s->sws = NULL;
  }
  if (s->dec) {
    avcodec_free_context(&s->dec);
    s->dec = NULL;
  }
  s->video_stream = -1;
}

void ffmpeg_rx_disconnect(ffmpeg_rx_session_t *s) {
  if (!s)
    return;
  if (s->fmt) {
    avformat_close_input(&s->fmt);
    s->fmt = NULL;
  }
  close_codec(s);
  free_bgr(s);
  s->connected = false;
  s->connected_url[0] = '\0';
  s->width = s->height = s->stride = 0;
}

ffmpeg_rx_session_t *ffmpeg_rx_session_create(const ffmpeg_rx_options_t *opt) {
  ffmpeg_rx_session_t *s = calloc(1, sizeof(*s));
  if (!s)
    return NULL;
  if (opt)
    s->opt = *opt;
  else
    ffmpeg_rx_options_defaults(&s->opt);
  s->video_stream = -1;
  s->frame = av_frame_alloc();
  s->bgr = av_frame_alloc();
  s->pkt = av_packet_alloc();
  if (!s->frame || !s->bgr || !s->pkt) {
    ffmpeg_rx_session_destroy(s);
    return NULL;
  }
  return s;
}

void ffmpeg_rx_session_destroy(ffmpeg_rx_session_t *s) {
  if (!s)
    return;
  ffmpeg_rx_disconnect(s);
  av_frame_free(&s->frame);
  av_frame_free(&s->bgr);
  av_packet_free(&s->pkt);
  free(s);
}

static void apply_low_latency(AVDictionary **opts, const ffmpeg_rx_options_t *o, const char *url) {
  char buf[32];
  bool is_rtmp = url && !strncmp(url, "rtmp", 4);
  bool is_rtsp = url && !strncmp(url, "rtsp", 4);
  bool is_srt = url && !strncmp(url, "srt", 3);

  /* RTMP: skip socket timeout knobs (some builds map them to listen_timeout).
   * SRT: skip generic "timeout" / rw_timeout — Ubuntu libav + libsrt treat them
   * as listen/accept and fail caller open immediately; Mac Homebrew is tolerant. */
  if (o->rw_timeout_us > 0 && !is_rtmp && !is_srt) {
    snprintf(buf, sizeof(buf), "%d", o->rw_timeout_us);
    av_dict_set(opts, "rw_timeout", buf, 0);
    av_dict_set(opts, "stimeout", buf, 0); /* RTSP */
  }
  if (o->connect_timeout_ms > 0 && !is_rtmp && !is_srt) {
    snprintf(buf, sizeof(buf), "%d000", o->connect_timeout_ms);
    av_dict_set(opts, "timeout", buf, 0);
  }
  if (o->low_latency) {
    av_dict_set(opts, "fflags", "nobuffer", 0);
    /* Mild probe — tiny probesize breaks live SRT/RTMP/RTSP; HEVC needs more. */
    av_dict_set(opts, "analyzeduration", "2000000", 0);
    av_dict_set(opts, "probesize", "1000000", 0);
    if (!is_rtmp) {
      av_dict_set(opts, "flags", "low_delay", 0);
      av_dict_set(opts, "max_delay", "0", 0);
    }
  }
  if (is_rtmp)
    av_dict_set(opts, "rtmp_live", "live", 0);
  if (is_rtsp && o->rtsp_tcp)
    av_dict_set(opts, "rtsp_transport", "tcp", 0);
  if (is_srt) {
    /* Prefer caller mode when URL omitted query; AIDA listen is the peer. */
    av_dict_set(opts, "mode", "caller", 0);
    av_dict_set(opts, "transtype", "live", 0);
  }
}
static int open_decoder(ffmpeg_rx_session_t *s) {
  const AVStream *st = s->fmt->streams[s->video_stream];
  const AVCodec *codec = avcodec_find_decoder(st->codecpar->codec_id);
  if (!codec)
    return -1;
  s->dec = avcodec_alloc_context3(codec);
  if (!s->dec)
    return -1;
  if (avcodec_parameters_to_context(s->dec, st->codecpar) < 0)
    return -1;
  s->dec->flags |= AV_CODEC_FLAG_LOW_DELAY;
  s->dec->flags2 |= AV_CODEC_FLAG2_FAST;
  if (avcodec_open2(s->dec, codec, NULL) < 0)
    return -1;

  if (st->avg_frame_rate.den > 0) {
    s->fps_n = st->avg_frame_rate.num;
    s->fps_d = st->avg_frame_rate.den;
  } else if (st->r_frame_rate.den > 0) {
    s->fps_n = st->r_frame_rate.num;
    s->fps_d = st->r_frame_rate.den;
  } else {
    s->fps_n = 30;
    s->fps_d = 1;
  }
  return 0;
}

int ffmpeg_rx_connect(ffmpeg_rx_session_t *s, const char *url) {
  if (!s || !url || !*url)
    return -1;
  ffmpeg_rx_disconnect(s);

  AVDictionary *opts = NULL;
  apply_low_latency(&opts, &s->opt, url);

  s->fmt = avformat_alloc_context();
  if (!s->fmt) {
    av_dict_free(&opts);
    return -1;
  }
  if (s->opt.low_latency) {
    s->fmt->flags |= AVFMT_FLAG_NOBUFFER | AVFMT_FLAG_FLUSH_PACKETS;
    s->fmt->max_delay = 0;
  }

  int err = avformat_open_input(&s->fmt, url, NULL, &opts);
  av_dict_free(&opts);
  if (err < 0) {
    s->fmt = NULL;
    return -1;
  }

  if (avformat_find_stream_info(s->fmt, NULL) < 0) {
    ffmpeg_rx_disconnect(s);
    return -1;
  }

  s->video_stream = av_find_best_stream(s->fmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
  if (s->video_stream < 0 || open_decoder(s) < 0) {
    ffmpeg_rx_disconnect(s);
    return -1;
  }

  snprintf(s->connected_url, sizeof(s->connected_url), "%s", url);
  snprintf(s->opt.url, sizeof(s->opt.url), "%s", url);
  s->connected = true;
  return 0;
}

bool ffmpeg_rx_is_connected(const ffmpeg_rx_session_t *s) {
  return s && s->connected && s->fmt && s->dec;
}

void ffmpeg_rx_connected_url(const ffmpeg_rx_session_t *s, char *out, size_t out_len) {
  if (!out || out_len == 0)
    return;
  out[0] = '\0';
  if (s && s->connected_url[0])
    snprintf(out, out_len, "%s", s->connected_url);
}

static int ensure_bgr(ffmpeg_rx_session_t *s, int w, int h) {
  int need = av_image_get_buffer_size(AV_PIX_FMT_BGRA, w, h, 1);
  if (need <= 0)
    return -1;
  if (!s->bgr_buf || s->bgr_size < need || s->width != w || s->height != h) {
    free_bgr(s);
    s->bgr_buf = (uint8_t *)av_malloc((size_t)need);
    if (!s->bgr_buf)
      return -1;
    s->bgr_size = need;
    s->width = w;
    s->height = h;
    s->stride = w * 4;
    if (s->sws) {
      sws_freeContext(s->sws);
      s->sws = NULL;
    }
  }
  return 0;
}

static bool convert_frame(ffmpeg_rx_session_t *s, AVFrame *src) {
  if (ensure_bgr(s, src->width, src->height) < 0)
    return false;
  s->sws = sws_getCachedContext(s->sws, src->width, src->height, (enum AVPixelFormat)src->format,
                                src->width, src->height, AV_PIX_FMT_BGRA, SWS_BILINEAR, NULL, NULL,
                                NULL);
  if (!s->sws)
    return false;
  uint8_t *dst[4] = {s->bgr_buf, NULL, NULL, NULL};
  int dst_ls[4] = {s->stride, 0, 0, 0};
  sws_scale(s->sws, (const uint8_t *const *)src->data, src->linesize, 0, src->height, dst, dst_ls);
  return true;
}

static int decode_packet(ffmpeg_rx_session_t *s, AVPacket *pkt, bool *got_pic) {
  *got_pic = false;
  if (pkt && pkt->stream_index != s->video_stream)
    return 0;
  int ret = avcodec_send_packet(s->dec, pkt);
  if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF)
    return ret;
  for (;;) {
    ret = avcodec_receive_frame(s->dec, s->frame);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
      return 0;
    if (ret < 0)
      return ret;
    if (*got_pic)
      s->dropped_acc++;
    if (!convert_frame(s, s->frame))
      return -1;
    *got_pic = true;
    av_frame_unref(s->frame);
  }
}

bool ffmpeg_rx_capture_newest(ffmpeg_rx_session_t *s, ffmpeg_rx_frame_t *out) {
  if (!ffmpeg_rx_is_connected(s) || !out)
    return false;

  bool got = false;
  int64_t deadline = now_ms() + (s->opt.capture_wait_ms > 0 ? s->opt.capture_wait_ms : 0);

  do {
    int ret = av_read_frame(s->fmt, s->pkt);
    if (ret >= 0) {
      bool pic = false;
      decode_packet(s, s->pkt, &pic);
      av_packet_unref(s->pkt);
      if (pic)
        got = true;
      /* Keep draining while more packets are immediately available. */
      for (;;) {
        ret = av_read_frame(s->fmt, s->pkt);
        if (ret < 0)
          break;
        pic = false;
        decode_packet(s, s->pkt, &pic);
        av_packet_unref(s->pkt);
        if (pic)
          got = true;
        /* Limit drain burst */
        if (got && now_ms() > deadline)
          break;
      }
      break;
    }
    if (ret == AVERROR(EAGAIN)) {
      /* spin briefly */
    } else if (ret == AVERROR_EOF) {
      break;
    } else {
      break;
    }
  } while (now_ms() <= deadline);

  if (!got || !s->bgr_buf)
    return false;

  memset(out, 0, sizeof(*out));
  out->data = s->bgr_buf;
  out->width = s->width;
  out->height = s->height;
  out->stride = s->stride;
  out->fourcc = FFMPEG_RX_FOURCC_BGRX;
  out->frame_rate_n = s->fps_n;
  out->frame_rate_d = s->fps_d;
  out->dropped = s->dropped_acc;
  s->dropped_acc = 0;
  return true;
}

void ffmpeg_rx_drain(ffmpeg_rx_session_t *s) {
  if (!ffmpeg_rx_is_connected(s))
    return;
  /* Flush decoder + discard packets briefly. */
  for (int i = 0; i < 64; i++) {
    int ret = av_read_frame(s->fmt, s->pkt);
    if (ret < 0)
      break;
    if (s->pkt->stream_index == s->video_stream)
      s->dropped_acc++;
    av_packet_unref(s->pkt);
  }
  avcodec_flush_buffers(s->dec);
}
