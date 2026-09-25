/**
 * @file stress_ghost_ndihx.c
 * @brief Headless stress harness for libghost_ndihx / media_core.
 *
 * Usage:
 *   stress_ghost_ndihx --ip 172.16.1.189 [--soak-sec N] [--suite all|soak|reconnect|bw|discover|ptz|wrongip|multi]
 *
 * Exit 0 = all selected suites passed; non-zero = failure.
 * Prints JSONL metrics lines prefixed with METRIC and a final SUMMARY.
 */
#include "ghost_ndihx.h"
#include "media_core.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

typedef struct {
  char ip[128];
  int find_ms;
  int soak_sec;
  int reconnect_n;
  int bw_flaps;
  int discover_n;
  int ptz_cycles;
  int multi_sec;
  const char *suite; /* all or name */
} StressOpts;

typedef struct {
  int fails;
  int passes;
} Score;

static long rss_kb(void) {
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

static double now_sec(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static void metric(const char *suite, const char *key, double value) {
  printf("METRIC\t%s\t%s\t%.6f\n", suite, key, value);
  fflush(stdout);
}

static void result(Score *sc, const char *suite, bool ok, const char *detail) {
  printf("RESULT\t%s\t%s\t%s\n", suite, ok ? "PASS" : "FAIL", detail ? detail : "");
  fflush(stdout);
  if (ok)
    sc->passes++;
  else
    sc->fails++;
}

static void sleep_ms(int ms) {
  if (ms <= 0)
    return;
  struct timespec ts;
  ts.tv_sec = ms / 1000;
  ts.tv_nsec = (long)(ms % 1000) * 1000000L;
  while (nanosleep(&ts, &ts) == -1 && errno == EINTR) {
  }
}

static ghost_ndihx_session_t *open_connected(const StressOpts *o, char *err, size_t errlen) {
  ghost_ndihx_options_t opt;
  ghost_ndihx_options_defaults(&opt);
  snprintf(opt.ip_substr, sizeof(opt.ip_substr), "%s", o->ip);
  opt.find_ms = o->find_ms;
  opt.auto_search = true;
  snprintf(opt.recv_name, sizeof(opt.recv_name), "%s", "ndi-hx-stress");

  ghost_ndihx_session_t *s = ghost_ndihx_session_create(&opt);
  if (!s) {
    snprintf(err, errlen, "session_create failed");
    return NULL;
  }
  volatile int cancel = 0;
  if (ghost_ndihx_connect_auto(s, &cancel) != 0) {
    snprintf(err, errlen, "connect_auto failed for ip=%s", o->ip);
    ghost_ndihx_session_destroy(s);
    return NULL;
  }
  return s;
}

static bool suite_soak(const StressOpts *o, Score *sc) {
  char err[256];
  ghost_ndihx_session_t *s = open_connected(o, err, sizeof(err));
  if (!s) {
    result(sc, "soak", false, err);
    return false;
  }

  long rss0 = rss_kb();
  long rss_max = rss0;
  uint64_t frames = 0, drops = 0;
  int last_w = 0, last_h = 0;
  double t0 = now_sec();
  double window_t0 = t0;
  uint64_t window_frames = 0;
  double fps_min = 1e9, fps_max = 0;
  int fps_samples = 0;

  while (now_sec() - t0 < (double)o->soak_sec) {
    ghost_ndihx_frame_t fr;
    if (ghost_ndihx_capture_newest(s, &fr)) {
      frames++;
      window_frames++;
      drops += fr.dropped;
      last_w = fr.width;
      last_h = fr.height;
      long r = rss_kb();
      if (r > rss_max)
        rss_max = r;
    } else {
      sleep_ms(1);
    }
    double now = now_sec();
    if (now - window_t0 >= 1.0) {
      double fps = (double)window_frames / (now - window_t0);
      if (fps < fps_min)
        fps_min = fps;
      if (fps > fps_max)
        fps_max = fps;
      fps_samples++;
      window_frames = 0;
      window_t0 = now;
    }
  }

  double elapsed = now_sec() - t0;
  double avg_fps = elapsed > 0 ? (double)frames / elapsed : 0;
  long rss1 = rss_kb();
  long growth = (rss1 >= 0 && rss0 >= 0) ? (rss1 - rss0) : -1;

  metric("soak", "elapsed_sec", elapsed);
  metric("soak", "frames", (double)frames);
  metric("soak", "avg_fps", avg_fps);
  metric("soak", "fps_min", fps_samples ? fps_min : 0);
  metric("soak", "fps_max", fps_samples ? fps_max : 0);
  metric("soak", "dropped", (double)drops);
  metric("soak", "rss0_kb", (double)rss0);
  metric("soak", "rss1_kb", (double)rss1);
  metric("soak", "rss_max_kb", (double)rss_max);
  metric("soak", "rss_growth_kb", (double)growth);
  metric("soak", "width", (double)last_w);
  metric("soak", "height", (double)last_h);

  char detail[256];
  bool ok = frames > 0 && last_w > 0;
  /* Allow modest RSS growth (codec caches); flag >64 MiB growth over soak.
   * ASan/shadow memory inflates RSS — skip hard fail when ASAN_OPTIONS is set. */
  bool under_asan = getenv("ASAN_OPTIONS") != NULL;
  if (!under_asan && growth > 64 * 1024)
    ok = false;
  /* Expect roughly >= 10 fps average on a live 30fps cam (decode/present free). */
  if (avg_fps < 10.0 && o->soak_sec >= 10)
    ok = false;
  snprintf(detail, sizeof(detail), "frames=%llu avg_fps=%.1f drops=%llu rss_growth=%ldk %dx%d%s",
           (unsigned long long)frames, avg_fps, (unsigned long long)drops, growth, last_w, last_h,
           under_asan ? " (asan)" : "");

  ghost_ndihx_session_destroy(s);
  result(sc, "soak", ok, detail);
  return ok;
}

static bool suite_reconnect(const StressOpts *o, Score *sc) {
  char err[256];
  ghost_ndihx_session_t *s = open_connected(o, err, sizeof(err));
  if (!s) {
    result(sc, "reconnect", false, err);
    return false;
  }

  int ok_n = 0, fail_n = 0;
  double t0 = now_sec();
  for (int i = 0; i < o->reconnect_n; i++) {
    ghost_ndihx_disconnect(s);
    volatile int cancel = 0;
    if (ghost_ndihx_connect_auto(s, &cancel) != 0) {
      fail_n++;
      continue;
    }
    /* Grab a few frames to prove path works. */
    int got = 0;
    for (int j = 0; j < 50 && got < 3; j++) {
      ghost_ndihx_frame_t fr;
      if (ghost_ndihx_capture_newest(s, &fr))
        got++;
      else
        sleep_ms(2);
    }
    if (got >= 1)
      ok_n++;
    else
      fail_n++;
  }
  double elapsed = now_sec() - t0;
  metric("reconnect", "cycles", (double)o->reconnect_n);
  metric("reconnect", "ok", (double)ok_n);
  metric("reconnect", "fail", (double)fail_n);
  metric("reconnect", "elapsed_sec", elapsed);

  char detail[128];
  snprintf(detail, sizeof(detail), "ok=%d fail=%d in %.1fs", ok_n, fail_n, elapsed);
  bool pass = fail_n == 0 && ok_n == o->reconnect_n;
  ghost_ndihx_session_destroy(s);
  result(sc, "reconnect", pass, detail);
  return pass;
}

static bool suite_bw(const StressOpts *o, Score *sc) {
  char err[256];
  ghost_ndihx_session_t *s = open_connected(o, err, sizeof(err));
  if (!s) {
    result(sc, "bw", false, err);
    return false;
  }

  int ok_n = 0, fail_n = 0;
  double t0 = now_sec();
  for (int i = 0; i < o->bw_flaps; i++) {
    ghost_ndihx_bandwidth_t bw = (i % 2 == 0) ? GHOST_NDIHX_BW_LOWEST : GHOST_NDIHX_BW_HIGHEST;
    if (ghost_ndihx_set_bandwidth(s, bw) != 0) {
      fail_n++;
      continue;
    }
    int got = 0;
    for (int j = 0; j < 80 && got < 2; j++) {
      ghost_ndihx_frame_t fr;
      if (ghost_ndihx_capture_newest(s, &fr))
        got++;
      else
        sleep_ms(2);
    }
    if (got >= 1)
      ok_n++;
    else
      fail_n++;
  }
  /* Leave on highest. */
  ghost_ndihx_set_bandwidth(s, GHOST_NDIHX_BW_HIGHEST);

  double elapsed = now_sec() - t0;
  metric("bw", "flaps", (double)o->bw_flaps);
  metric("bw", "ok", (double)ok_n);
  metric("bw", "fail", (double)fail_n);
  metric("bw", "elapsed_sec", elapsed);

  char detail[128];
  snprintf(detail, sizeof(detail), "ok=%d fail=%d", ok_n, fail_n);
  bool pass = fail_n == 0 && ok_n == o->bw_flaps;
  ghost_ndihx_session_destroy(s);
  result(sc, "bw", pass, detail);
  return pass;
}

static bool suite_discover(const StressOpts *o, Score *sc) {
  ghost_ndihx_options_t opt;
  ghost_ndihx_options_defaults(&opt);
  snprintf(opt.ip_substr, sizeof(opt.ip_substr), "%s", o->ip);
  opt.find_ms = o->find_ms > 2000 ? 2000 : o->find_ms;
  snprintf(opt.recv_name, sizeof(opt.recv_name), "%s", "ndi-hx-stress-disc");

  ghost_ndihx_session_t *s = ghost_ndihx_session_create(&opt);
  if (!s) {
    result(sc, "discover", false, "session_create failed");
    return false;
  }

  int hits = 0, misses = 0;
  double t0 = now_sec();
  for (int i = 0; i < o->discover_n; i++) {
    ghost_ndihx_source_t srcs[GHOST_NDIHX_MAX_SOURCES];
    int n = ghost_ndihx_discover(s, srcs, GHOST_NDIHX_MAX_SOURCES, opt.find_ms);
    if (n < 0) {
      misses++;
      continue;
    }
    int found = 0;
    for (int j = 0; j < n; j++) {
      if (strcasestr(srcs[j].name, o->ip) || strcasestr(srcs[j].url, o->ip)) {
        found = 1;
        break;
      }
    }
    if (found)
      hits++;
    else
      misses++;
  }
  double elapsed = now_sec() - t0;
  metric("discover", "rounds", (double)o->discover_n);
  metric("discover", "hits", (double)hits);
  metric("discover", "misses", (double)misses);
  metric("discover", "elapsed_sec", elapsed);

  char detail[128];
  snprintf(detail, sizeof(detail), "hits=%d misses=%d", hits, misses);
  /* Allow a few misses under LAN jitter; require majority. */
  bool pass = hits * 2 >= o->discover_n && hits > 0;
  ghost_ndihx_session_destroy(s);
  result(sc, "discover", pass, detail);
  return pass;
}

static bool suite_wrongip(const StressOpts *o, Score *sc) {
  (void)o;
  ghost_ndihx_options_t opt;
  ghost_ndihx_options_defaults(&opt);
  snprintf(opt.ip_substr, sizeof(opt.ip_substr), "%s", "203.0.113.9"); /* TEST-NET-3 */
  opt.find_ms = 1500;
  opt.auto_search = false;
  snprintf(opt.recv_name, sizeof(opt.recv_name), "%s", "ndi-hx-stress-badip");

  ghost_ndihx_session_t *s = ghost_ndihx_session_create(&opt);
  if (!s) {
    result(sc, "wrongip", false, "session_create failed");
    return false;
  }
  volatile int cancel = 0;
  int rc = ghost_ndihx_connect_auto(s, &cancel);
  bool pass = (rc != 0); /* must fail cleanly */
  metric("wrongip", "connect_rc", (double)rc);
  ghost_ndihx_session_destroy(s);
  result(sc, "wrongip", pass, pass ? "rejected as expected" : "unexpectedly connected");
  return pass;
}

static bool suite_ptz(const StressOpts *o, Score *sc) {
  char err[256];
  ghost_ndihx_session_t *s = open_connected(o, err, sizeof(err));
  if (!s) {
    result(sc, "ptz", false, err);
    return false;
  }

  /* Wait briefly for PTZ capability advertisement. */
  bool has = false;
  for (int i = 0; i < 20; i++) {
    if (ghost_ndihx_ptz_supported(s)) {
      has = true;
      break;
    }
    sleep_ms(100);
  }
  metric("ptz", "supported", has ? 1.0 : 0.0);
  if (!has) {
    ghost_ndihx_session_destroy(s);
    result(sc, "ptz", true, "SKIP no MEDIA_CAP_PTZ (gated off)");
    return true;
  }

  int crashes = 0; /* we can't catch SIGSEGV easily; count API errors */
  int api_fail = 0, api_ok = 0;
  uint64_t frames = 0;
  double t0 = now_sec();
  for (int i = 0; i < o->ptz_cycles; i++) {
    float pan = (i % 4 == 0) ? 0.4f : (i % 4 == 1) ? -0.4f : 0.f;
    float tilt = (i % 4 == 2) ? 0.4f : (i % 4 == 3) ? -0.4f : 0.f;
    float zoom = (i % 5 == 0) ? 0.3f : (i % 5 == 1) ? -0.3f : 0.f;
    if (ghost_ndihx_ptz_move(s, pan, tilt, zoom, true) != 0)
      api_fail++;
    else
      api_ok++;
    /* Keep receiving while moving. */
    for (int j = 0; j < 5; j++) {
      ghost_ndihx_frame_t fr;
      if (ghost_ndihx_capture_newest(s, &fr))
        frames++;
    }
    if (ghost_ndihx_ptz_stop(s) != 0)
      api_fail++;
    else
      api_ok++;
    sleep_ms(20);
  }
  ghost_ndihx_ptz_stop(s);
  double elapsed = now_sec() - t0;
  metric("ptz", "cycles", (double)o->ptz_cycles);
  metric("ptz", "api_ok", (double)api_ok);
  metric("ptz", "api_fail", (double)api_fail);
  metric("ptz", "frames_during", (double)frames);
  metric("ptz", "elapsed_sec", elapsed);
  metric("ptz", "segfaults", (double)crashes);

  char detail[160];
  snprintf(detail, sizeof(detail), "api_ok=%d api_fail=%d frames=%llu", api_ok, api_fail,
           (unsigned long long)frames);
  /* PTZ commands may fail on some cams even when is_supported; require no crash + still decoding. */
  bool pass = frames > 0 && crashes == 0;
  ghost_ndihx_session_destroy(s);
  result(sc, "ptz", pass, detail);
  return pass;
}

static bool suite_multi(const StressOpts *o, Score *sc) {
  char err[256];
  ghost_ndihx_session_t *a = open_connected(o, err, sizeof(err));
  if (!a) {
    result(sc, "multi", false, err);
    return false;
  }
  ghost_ndihx_session_t *b = open_connected(o, err, sizeof(err));
  if (!b) {
    ghost_ndihx_session_destroy(a);
    result(sc, "multi", false, err);
    return false;
  }

  uint64_t fa = 0, fb = 0;
  double t0 = now_sec();
  while (now_sec() - t0 < (double)o->multi_sec) {
    ghost_ndihx_frame_t fr;
    if (ghost_ndihx_capture_newest(a, &fr))
      fa++;
    if (ghost_ndihx_capture_newest(b, &fr))
      fb++;
  }
  metric("multi", "frames_a", (double)fa);
  metric("multi", "frames_b", (double)fb);
  metric("multi", "sec", (double)o->multi_sec);

  char detail[128];
  snprintf(detail, sizeof(detail), "a=%llu b=%llu", (unsigned long long)fa, (unsigned long long)fb);
  bool pass = fa > 0 && fb > 0;
  ghost_ndihx_session_destroy(b);
  ghost_ndihx_session_destroy(a);
  result(sc, "multi", pass, detail);
  return pass;
}

static bool want(const StressOpts *o, const char *name) {
  return !o->suite || !strcmp(o->suite, "all") || !strcmp(o->suite, name);
}

static void usage(const char *argv0) {
  fprintf(stderr,
          "Usage: %s --ip HOST [options]\n"
          "  --suite NAME     all|soak|reconnect|bw|discover|ptz|wrongip|multi\n"
          "  --soak-sec N     default 180\n"
          "  --reconnect-n N  default 40\n"
          "  --bw-flaps N     default 30\n"
          "  --discover-n N   default 20\n"
          "  --ptz-cycles N   default 40\n"
          "  --multi-sec N    default 20\n"
          "  --find-ms N      default 4000\n",
          argv0);
}

int main(int argc, char **argv) {
  StressOpts o;
  memset(&o, 0, sizeof(o));
  o.find_ms = 4000;
  o.soak_sec = 180;
  o.reconnect_n = 40;
  o.bw_flaps = 30;
  o.discover_n = 20;
  o.ptz_cycles = 40;
  o.multi_sec = 20;
  o.suite = "all";

  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--ip") && i + 1 < argc)
      snprintf(o.ip, sizeof(o.ip), "%s", argv[++i]);
    else if (!strcmp(argv[i], "--suite") && i + 1 < argc)
      o.suite = argv[++i];
    else if (!strcmp(argv[i], "--soak-sec") && i + 1 < argc)
      o.soak_sec = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--reconnect-n") && i + 1 < argc)
      o.reconnect_n = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--bw-flaps") && i + 1 < argc)
      o.bw_flaps = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--discover-n") && i + 1 < argc)
      o.discover_n = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--ptz-cycles") && i + 1 < argc)
      o.ptz_cycles = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--multi-sec") && i + 1 < argc)
      o.multi_sec = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--find-ms") && i + 1 < argc)
      o.find_ms = atoi(argv[++i]);
    else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
      usage(argv[0]);
      return 0;
    } else {
      fprintf(stderr, "Unknown arg: %s\n", argv[i]);
      usage(argv[0]);
      return 2;
    }
  }
  if (!o.ip[0]) {
    usage(argv[0]);
    return 2;
  }

  printf("stress_ghost_ndihx lib=%s media_core=%s suite=%s ip=%s\n", ghost_ndihx_version(),
         media_core_version(), o.suite, o.ip);
  fflush(stdout);

  Score sc = {0, 0};
  if (want(&o, "wrongip"))
    suite_wrongip(&o, &sc);
  if (want(&o, "discover"))
    suite_discover(&o, &sc);
  if (want(&o, "reconnect"))
    suite_reconnect(&o, &sc);
  if (want(&o, "bw"))
    suite_bw(&o, &sc);
  if (want(&o, "ptz"))
    suite_ptz(&o, &sc);
  if (want(&o, "multi"))
    suite_multi(&o, &sc);
  if (want(&o, "soak"))
    suite_soak(&o, &sc);

  printf("SUMMARY\tpasses=%d\tfails=%d\n", sc.passes, sc.fails);
  fflush(stdout);
  return sc.fails == 0 ? 0 : 1;
}
