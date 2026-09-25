/**
 * @file ghost_rtmp.c
 * @brief RTMP / HTTP-FLV media_core module wrapping ffmpeg_rx.
 */
#include "ghost_rtmp.h"
#include "ffmpeg_rx.h"
#include "media_core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

struct ghost_rtmp_session {
  ghost_rtmp_options_t opt;
  ffmpeg_rx_session_t *rx;
  ghost_rtmp_source_t connected;
};

static void build_url(const ghost_rtmp_options_t *opt, char *out, size_t out_len) {
  if (opt->url[0]) {
    snprintf(out, out_len, "%s", opt->url);
    return;
  }
  const char *ip = opt->ip_substr[0] ? opt->ip_substr : "127.0.0.1";
  const char *app = opt->app[0] ? opt->app : "app";
  const char *stream = opt->stream[0] ? opt->stream : "rtmpstream0";
  if (opt->prefer_flv) {
    snprintf(out, out_len, "http://%s:8080/%s/%s.flv", ip, app, stream);
    return;
  }
  int port = opt->port > 0 ? opt->port : GHOST_RTMP_DEFAULT_PORT;
  snprintf(out, out_len, "rtmp://%s:%d/%s/%s", ip, port, app, stream);
}

void ghost_rtmp_options_defaults(ghost_rtmp_options_t *opt) {
  if (!opt)
    return;
  memset(opt, 0, sizeof(*opt));
  opt->port = GHOST_RTMP_DEFAULT_PORT;
  snprintf(opt->app, sizeof(opt->app), "%s", "app");
  snprintf(opt->stream, sizeof(opt->stream), "%s", "rtmpstream0");
  opt->prefer_flv = false;
  opt->auto_search = true;
  opt->find_ms = 4000;
  opt->rescan_ms = 3000;
  opt->capture_wait_ms = 8;
  opt->low_latency = true;
  opt->connect_timeout_ms = 5000;
}

static int parse_bool(const char *v, bool *out) {
  if (!v || !out)
    return -1;
  if (!strcasecmp(v, "1") || !strcasecmp(v, "true") || !strcasecmp(v, "yes") ||
      !strcasecmp(v, "on")) {
    *out = true;
    return 0;
  }
  if (!strcasecmp(v, "0") || !strcasecmp(v, "false") || !strcasecmp(v, "no") ||
      !strcasecmp(v, "off")) {
    *out = false;
    return 0;
  }
  return -1;
}

int ghost_rtmp_options_load_file(ghost_rtmp_options_t *opt, const char *path, char *err,
                                 size_t err_len) {
  if (!opt || !path)
    return -1;
  FILE *f = fopen(path, "r");
  if (!f) {
    if (err && err_len)
      snprintf(err, err_len, "cannot open %s", path);
    return -1;
  }
  char line[512];
  while (fgets(line, sizeof(line), f)) {
    char *p = line;
    while (*p == ' ' || *p == '\t')
      p++;
    if (*p == '#' || *p == '\n' || !*p)
      continue;
    char *eq = strchr(p, '=');
    if (!eq)
      continue;
    *eq = '\0';
    char *key = p;
    char *val = eq + 1;
    char *nl = strpbrk(val, "\r\n");
    if (nl)
      *nl = '\0';
    while (key[0] && (key[strlen(key) - 1] == ' ' || key[strlen(key) - 1] == '\t'))
      key[strlen(key) - 1] = '\0';
    if (!strcmp(key, "url") || !strcmp(key, "rtmp_url"))
      snprintf(opt->url, sizeof(opt->url), "%s", val);
    else if (!strcmp(key, "ip"))
      snprintf(opt->ip_substr, sizeof(opt->ip_substr), "%s", val);
    else if (!strcmp(key, "port") || !strcmp(key, "rtmp_port"))
      opt->port = atoi(val);
    else if (!strcmp(key, "app"))
      snprintf(opt->app, sizeof(opt->app), "%s", val);
    else if (!strcmp(key, "stream") || !strcmp(key, "rtmp_stream"))
      snprintf(opt->stream, sizeof(opt->stream), "%s", val);
    else if (!strcmp(key, "prefer_flv") || !strcmp(key, "http_flv"))
      parse_bool(val, &opt->prefer_flv);
    else if (!strcmp(key, "auto") || !strcmp(key, "auto_search"))
      parse_bool(val, &opt->auto_search);
    else if (!strcmp(key, "find_ms"))
      opt->find_ms = atoi(val);
    else if (!strcmp(key, "rescan_ms"))
      opt->rescan_ms = atoi(val);
    else if (!strcmp(key, "capture_wait_ms"))
      opt->capture_wait_ms = atoi(val);
    else if (!strcmp(key, "low_latency"))
      parse_bool(val, &opt->low_latency);
  }
  fclose(f);
  return 0;
}

const char *ghost_rtmp_version(void) { return GHOST_RTMP_VERSION; }

int ghost_rtmp_init(void) { return ffmpeg_rx_init(); }
void ghost_rtmp_shutdown(void) { ffmpeg_rx_shutdown(); }

ghost_rtmp_session_t *ghost_rtmp_session_create(const ghost_rtmp_options_t *opt) {
  ghost_rtmp_session_t *s = calloc(1, sizeof(*s));
  if (!s)
    return NULL;
  if (opt)
    s->opt = *opt;
  else
    ghost_rtmp_options_defaults(&s->opt);

  ffmpeg_rx_options_t rxo;
  ffmpeg_rx_options_defaults(&rxo);
  rxo.capture_wait_ms = s->opt.capture_wait_ms;
  rxo.low_latency = s->opt.low_latency;
  rxo.connect_timeout_ms = s->opt.connect_timeout_ms;
  build_url(&s->opt, rxo.url, sizeof(rxo.url));
  s->rx = ffmpeg_rx_session_create(&rxo);
  if (!s->rx) {
    free(s);
    return NULL;
  }
  return s;
}

void ghost_rtmp_session_destroy(ghost_rtmp_session_t *session) {
  if (!session)
    return;
  ffmpeg_rx_session_destroy(session->rx);
  free(session);
}

int ghost_rtmp_discover(ghost_rtmp_session_t *session, ghost_rtmp_source_t *out, int cap,
                        int wait_ms) {
  (void)wait_ms;
  if (!session || !out || cap < 1)
    return -1;
  memset(&out[0], 0, sizeof(out[0]));
  build_url(&session->opt, out[0].url, sizeof(out[0].url));
  snprintf(out[0].name, sizeof(out[0].name), "RTMP %.240s", out[0].url);
  return 1;
}

int ghost_rtmp_connect(ghost_rtmp_session_t *session, const ghost_rtmp_source_t *src) {
  if (!session || !src || !src->url[0])
    return -1;
  if (ffmpeg_rx_connect(session->rx, src->url) != 0)
    return -1;
  session->connected = *src;
  return 0;
}

int ghost_rtmp_connect_auto(ghost_rtmp_session_t *session, volatile const int *cancel) {
  if (!session)
    return -1;
  ghost_rtmp_source_t src;
  memset(&src, 0, sizeof(src));
  build_url(&session->opt, src.url, sizeof(src.url));
  snprintf(src.name, sizeof(src.name), "RTMP %.240s", src.url);
  for (;;) {
    if (cancel && *cancel)
      return -1;
    if (ghost_rtmp_connect(session, &src) == 0)
      return 0;
    if (!session->opt.auto_search)
      return -1;
    usleep((useconds_t)(session->opt.rescan_ms > 0 ? session->opt.rescan_ms : 3000) * 1000);
  }
}

void ghost_rtmp_disconnect(ghost_rtmp_session_t *session) {
  if (!session)
    return;
  ffmpeg_rx_disconnect(session->rx);
  memset(&session->connected, 0, sizeof(session->connected));
}

bool ghost_rtmp_is_connected(const ghost_rtmp_session_t *session) {
  return session && ffmpeg_rx_is_connected(session->rx);
}

void ghost_rtmp_connected_source(const ghost_rtmp_session_t *session, ghost_rtmp_source_t *out) {
  if (!out)
    return;
  memset(out, 0, sizeof(*out));
  if (session)
    *out = session->connected;
}

bool ghost_rtmp_capture_newest(ghost_rtmp_session_t *session, ghost_rtmp_frame_t *out) {
  if (!session || !out)
    return false;
  ffmpeg_rx_frame_t fr;
  if (!ffmpeg_rx_capture_newest(session->rx, &fr))
    return false;
  memset(out, 0, sizeof(*out));
  out->data = fr.data;
  out->width = fr.width;
  out->height = fr.height;
  out->stride = fr.stride;
  out->fourcc = fr.fourcc;
  out->frame_rate_n = fr.frame_rate_n;
  out->frame_rate_d = fr.frame_rate_d;
  out->dropped = fr.dropped;
  return true;
}

void ghost_rtmp_drain(ghost_rtmp_session_t *session) {
  if (session)
    ffmpeg_rx_drain(session->rx);
}

static media_session_t *mod_open(const media_open_params_t *params) {
  ghost_rtmp_options_t opt;
  if (params && params->protocol_opts)
    opt = *(const ghost_rtmp_options_t *)params->protocol_opts;
  else
    ghost_rtmp_options_defaults(&opt);
  if (params) {
    if (params->source_substr[0] && !opt.url[0])
      snprintf(opt.url, sizeof(opt.url), "%s", params->source_substr);
    if (params->ip_substr[0])
      snprintf(opt.ip_substr, sizeof(opt.ip_substr), "%s", params->ip_substr);
    opt.auto_search = params->auto_search;
    if (params->find_ms > 0)
      opt.find_ms = params->find_ms;
    if (params->rescan_ms > 0)
      opt.rescan_ms = params->rescan_ms;
  }
  return (media_session_t *)ghost_rtmp_session_create(&opt);
}

static void mod_close(media_session_t *session) {
  ghost_rtmp_session_destroy((ghost_rtmp_session_t *)session);
}

static int mod_discover(media_session_t *session, media_source_t *out, int cap, int wait_ms) {
  ghost_rtmp_source_t tmp[4];
  int n = ghost_rtmp_discover((ghost_rtmp_session_t *)session, tmp, 4, wait_ms);
  if (n < 0)
    return -1;
  if (n > cap)
    n = cap;
  for (int i = 0; i < n; i++) {
    memset(&out[i], 0, sizeof(out[i]));
    snprintf(out[i].name, sizeof(out[i].name), "%s", tmp[i].name);
    snprintf(out[i].url, sizeof(out[i].url), "%.255s", tmp[i].url);
    snprintf(out[i].tag, sizeof(out[i].tag), "%s", "rtmp");
    out[i].protocol = MEDIA_PROTO_RTMP;
  }
  return n;
}

static int mod_connect(media_session_t *session, const media_source_t *src) {
  ghost_rtmp_source_t s;
  memset(&s, 0, sizeof(s));
  snprintf(s.name, sizeof(s.name), "%s", src->name);
  snprintf(s.url, sizeof(s.url), "%s", src->url);
  return ghost_rtmp_connect((ghost_rtmp_session_t *)session, &s);
}

static int mod_connect_auto(media_session_t *session, volatile const int *cancel) {
  return ghost_rtmp_connect_auto((ghost_rtmp_session_t *)session, cancel);
}

static void mod_disconnect(media_session_t *session) {
  ghost_rtmp_disconnect((ghost_rtmp_session_t *)session);
}

static bool mod_is_connected(const media_session_t *session) {
  return ghost_rtmp_is_connected((const ghost_rtmp_session_t *)session);
}

static void mod_connected_source(const media_session_t *session, media_source_t *out) {
  ghost_rtmp_source_t s;
  ghost_rtmp_connected_source((const ghost_rtmp_session_t *)session, &s);
  memset(out, 0, sizeof(*out));
  snprintf(out->name, sizeof(out->name), "%s", s.name);
  snprintf(out->url, sizeof(out->url), "%.255s", s.url);
  snprintf(out->tag, sizeof(out->tag), "%s", "rtmp");
  out->protocol = MEDIA_PROTO_RTMP;
}

static bool mod_capture_newest(media_session_t *session, media_frame_t *out) {
  ghost_rtmp_frame_t fr;
  if (!ghost_rtmp_capture_newest((ghost_rtmp_session_t *)session, &fr))
    return false;
  memset(out, 0, sizeof(*out));
  out->data = fr.data;
  out->width = fr.width;
  out->height = fr.height;
  out->stride = fr.stride;
  out->fourcc = fr.fourcc;
  out->frame_rate_n = fr.frame_rate_n;
  out->frame_rate_d = fr.frame_rate_d;
  out->dropped = fr.dropped;
  return true;
}

static media_caps_t mod_capabilities(const media_session_t *session) {
  (void)session;
  return MEDIA_CAP_NONE;
}

static const media_module_t g_mod = {
    .id = "rtmp",
    .protocol = MEDIA_PROTO_RTMP,
    .description = "RTMP / HTTP-FLV low-latency receiver (FFmpeg)",
    .init = ghost_rtmp_init,
    .shutdown = ghost_rtmp_shutdown,
    .open = mod_open,
    .close = mod_close,
    .discover = mod_discover,
    .connect = mod_connect,
    .connect_auto = mod_connect_auto,
    .disconnect = mod_disconnect,
    .is_connected = mod_is_connected,
    .connected_source = mod_connected_source,
    .capture_newest = mod_capture_newest,
    .capabilities = mod_capabilities,
};

const media_module_t *ghost_rtmp_media_module(void) { return &g_mod; }
int ghost_rtmp_register_media_module(void) { return media_register_module(&g_mod); }
