/**
 * @file ghost_srt.c
 * @brief SRT media_core module wrapping ffmpeg_rx.
 */
#include "ghost_srt.h"
#include "ffmpeg_rx.h"
#include "media_core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

struct ghost_srt_session {
  ghost_srt_options_t opt;
  ffmpeg_rx_session_t *rx;
  ghost_srt_source_t connected;
};

static void build_url(const ghost_srt_options_t *opt, char *out, size_t out_len) {
  if (opt->url[0]) {
    snprintf(out, out_len, "%s", opt->url);
    return;
  }
  const char *ip = opt->ip_substr[0] ? opt->ip_substr : "127.0.0.1";
  int port = opt->port > 0 ? opt->port : GHOST_SRT_DEFAULT_PORT;
  int lat = opt->latency_ms > 0 ? opt->latency_ms : 120;
  int sid = opt->stream_id >= 0 ? opt->stream_id : 0;
  /* Caller connects to camera listen mode (AIDA default). */
  snprintf(out, out_len, "srt://%s:%d?mode=caller&latency=%d&streamid=r=%d", ip, port, lat, sid);
}

void ghost_srt_options_defaults(ghost_srt_options_t *opt) {
  if (!opt)
    return;
  memset(opt, 0, sizeof(*opt));
  opt->port = GHOST_SRT_DEFAULT_PORT;
  opt->latency_ms = 120;
  opt->stream_id = 0;
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

int ghost_srt_options_load_file(ghost_srt_options_t *opt, const char *path, char *err,
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
    if (!strcmp(key, "url") || !strcmp(key, "srt_url"))
      snprintf(opt->url, sizeof(opt->url), "%s", val);
    else if (!strcmp(key, "ip"))
      snprintf(opt->ip_substr, sizeof(opt->ip_substr), "%s", val);
    else if (!strcmp(key, "port") || !strcmp(key, "srt_port"))
      opt->port = atoi(val);
    else if (!strcmp(key, "latency_ms") || !strcmp(key, "srt_latency"))
      opt->latency_ms = atoi(val);
    else if (!strcmp(key, "stream_id") || !strcmp(key, "srt_stream_id"))
      opt->stream_id = atoi(val);
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

const char *ghost_srt_version(void) { return GHOST_SRT_VERSION; }

int ghost_srt_init(void) { return ffmpeg_rx_init(); }
void ghost_srt_shutdown(void) { ffmpeg_rx_shutdown(); }

ghost_srt_session_t *ghost_srt_session_create(const ghost_srt_options_t *opt) {
  ghost_srt_session_t *s = calloc(1, sizeof(*s));
  if (!s)
    return NULL;
  if (opt)
    s->opt = *opt;
  else
    ghost_srt_options_defaults(&s->opt);

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

void ghost_srt_session_destroy(ghost_srt_session_t *session) {
  if (!session)
    return;
  ffmpeg_rx_session_destroy(session->rx);
  free(session);
}

int ghost_srt_discover(ghost_srt_session_t *session, ghost_srt_source_t *out, int cap,
                       int wait_ms) {
  (void)wait_ms;
  if (!session || !out || cap < 1)
    return -1;
  memset(&out[0], 0, sizeof(out[0]));
  build_url(&session->opt, out[0].url, sizeof(out[0].url));
  snprintf(out[0].name, sizeof(out[0].name), "SRT %s", out[0].url);
  return 1;
}

int ghost_srt_connect(ghost_srt_session_t *session, const ghost_srt_source_t *src) {
  if (!session || !src || !src->url[0])
    return -1;
  if (ffmpeg_rx_connect(session->rx, src->url) != 0)
    return -1;
  session->connected = *src;
  return 0;
}

int ghost_srt_connect_auto(ghost_srt_session_t *session, volatile const int *cancel) {
  if (!session)
    return -1;
  ghost_srt_source_t src;
  memset(&src, 0, sizeof(src));
  build_url(&session->opt, src.url, sizeof(src.url));
  snprintf(src.name, sizeof(src.name), "SRT %s", src.url);

  for (;;) {
    if (cancel && *cancel)
      return -1;
    if (ghost_srt_connect(session, &src) == 0)
      return 0;
    if (!session->opt.auto_search)
      return -1;
    usleep((useconds_t)(session->opt.rescan_ms > 0 ? session->opt.rescan_ms : 3000) * 1000);
  }
}

void ghost_srt_disconnect(ghost_srt_session_t *session) {
  if (!session)
    return;
  ffmpeg_rx_disconnect(session->rx);
  memset(&session->connected, 0, sizeof(session->connected));
}

bool ghost_srt_is_connected(const ghost_srt_session_t *session) {
  return session && ffmpeg_rx_is_connected(session->rx);
}

void ghost_srt_connected_source(const ghost_srt_session_t *session, ghost_srt_source_t *out) {
  if (!out)
    return;
  memset(out, 0, sizeof(*out));
  if (session)
    *out = session->connected;
}

bool ghost_srt_capture_newest(ghost_srt_session_t *session, ghost_srt_frame_t *out) {
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

void ghost_srt_drain(ghost_srt_session_t *session) {
  if (session)
    ffmpeg_rx_drain(session->rx);
}

/* ---- media_core ---- */

static media_session_t *mod_open(const media_open_params_t *params) {
  ghost_srt_options_t opt;
  if (params && params->protocol_opts)
    opt = *(const ghost_srt_options_t *)params->protocol_opts;
  else
    ghost_srt_options_defaults(&opt);
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
  return (media_session_t *)ghost_srt_session_create(&opt);
}

static void mod_close(media_session_t *session) {
  ghost_srt_session_destroy((ghost_srt_session_t *)session);
}

static int mod_discover(media_session_t *session, media_source_t *out, int cap, int wait_ms) {
  ghost_srt_source_t tmp[4];
  int n = ghost_srt_discover((ghost_srt_session_t *)session, tmp, 4, wait_ms);
  if (n < 0)
    return -1;
  if (n > cap)
    n = cap;
  for (int i = 0; i < n; i++) {
    memset(&out[i], 0, sizeof(out[i]));
    snprintf(out[i].name, sizeof(out[i].name), "%s", tmp[i].name);
    snprintf(out[i].url, sizeof(out[i].url), "%s", tmp[i].url);
    snprintf(out[i].tag, sizeof(out[i].tag), "%s", "srt");
    out[i].protocol = MEDIA_PROTO_SRT;
  }
  return n;
}

static int mod_connect(media_session_t *session, const media_source_t *src) {
  ghost_srt_source_t s;
  memset(&s, 0, sizeof(s));
  snprintf(s.name, sizeof(s.name), "%s", src->name);
  snprintf(s.url, sizeof(s.url), "%s", src->url);
  return ghost_srt_connect((ghost_srt_session_t *)session, &s);
}

static int mod_connect_auto(media_session_t *session, volatile const int *cancel) {
  return ghost_srt_connect_auto((ghost_srt_session_t *)session, cancel);
}

static void mod_disconnect(media_session_t *session) {
  ghost_srt_disconnect((ghost_srt_session_t *)session);
}

static bool mod_is_connected(const media_session_t *session) {
  return ghost_srt_is_connected((const ghost_srt_session_t *)session);
}

static void mod_connected_source(const media_session_t *session, media_source_t *out) {
  ghost_srt_source_t s;
  ghost_srt_connected_source((const ghost_srt_session_t *)session, &s);
  memset(out, 0, sizeof(*out));
  snprintf(out->name, sizeof(out->name), "%s", s.name);
  snprintf(out->url, sizeof(out->url), "%s", s.url);
  snprintf(out->tag, sizeof(out->tag), "%s", "srt");
  out->protocol = MEDIA_PROTO_SRT;
}

static bool mod_capture_newest(media_session_t *session, media_frame_t *out) {
  ghost_srt_frame_t fr;
  if (!ghost_srt_capture_newest((ghost_srt_session_t *)session, &fr))
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
    .id = "srt",
    .protocol = MEDIA_PROTO_SRT,
    .description = "SRT low-latency receiver (FFmpeg + libsrt)",
    .init = ghost_srt_init,
    .shutdown = ghost_srt_shutdown,
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

const media_module_t *ghost_srt_media_module(void) { return &g_mod; }
int ghost_srt_register_media_module(void) { return media_register_module(&g_mod); }
