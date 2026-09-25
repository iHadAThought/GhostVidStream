/**
 * @file stress_url_rx.c
 * @brief Connect / first-frame / soak / reconnect tests for SRT and RTMP modules.
 *
 * Usage:
 *   ./stress_url_rx --protocol srt --ip 172.16.1.189
 *   ./stress_url_rx --protocol rtmp --ip 172.16.1.189 --soak 30
 *   ./stress_url_rx --protocol rtmp --url rtmp://172.16.1.189:1935/app/rtmpstream0
 */
#include "ghost_rtmp.h"
#include "ghost_rtsp.h"
#include "ghost_srt.h"
#include "media_core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

typedef enum { PROTO_SRT, PROTO_RTMP, PROTO_RTSP } Proto;

static int64_t now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void usage(const char *a0) {
  fprintf(stderr,
          "Usage: %s --protocol srt|rtmp|rtsp [--ip HOST] [--url URL] [--soak SEC] [--reconnect N]\n",
          a0);
}

int main(int argc, char **argv) {
  Proto proto = PROTO_SRT;
  char ip[128] = "172.16.1.189";
  char url[512] = "";
  int soak_s = 15;
  int reconnect_n = 3;

  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--protocol") && i + 1 < argc) {
      const char *p = argv[++i];
      if (!strcasecmp(p, "srt"))
        proto = PROTO_SRT;
      else if (!strcasecmp(p, "rtmp"))
        proto = PROTO_RTMP;
      else if (!strcasecmp(p, "rtsp"))
        proto = PROTO_RTSP;
      else {
        usage(argv[0]);
        return 2;
      }
    } else if (!strcmp(argv[i], "--ip") && i + 1 < argc) {
      snprintf(ip, sizeof(ip), "%s", argv[++i]);
    } else if (!strcmp(argv[i], "--url") && i + 1 < argc) {
      snprintf(url, sizeof(url), "%s", argv[++i]);
    } else if (!strcmp(argv[i], "--soak") && i + 1 < argc) {
      soak_s = atoi(argv[++i]);
    } else if (!strcmp(argv[i], "--reconnect") && i + 1 < argc) {
      reconnect_n = atoi(argv[++i]);
    } else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
      usage(argv[0]);
      return 0;
    } else {
      usage(argv[0]);
      return 2;
    }
  }

  ghost_srt_register_media_module();
  ghost_rtmp_register_media_module();
  ghost_rtsp_register_media_module();

  const char *mod_id = proto == PROTO_SRT ? "srt" : proto == PROTO_RTMP ? "rtmp" : "rtsp";
  const media_module_t *mod = media_find_module(mod_id);
  if (!mod) {
    fprintf(stderr, "module %s not registered\n", mod_id);
    return 1;
  }
  mod->init();

  media_open_params_t op;
  media_open_params_defaults(&op);
  snprintf(op.ip_substr, sizeof(op.ip_substr), "%s", ip);
  op.auto_search = false;
  if (url[0])
    snprintf(op.source_substr, sizeof(op.source_substr), "%s", url);

  ghost_srt_options_t srt_opt;
  ghost_rtmp_options_t rtmp_opt;
  ghost_rtsp_options_t rtsp_opt;
  if (proto == PROTO_SRT) {
    ghost_srt_options_defaults(&srt_opt);
    snprintf(srt_opt.ip_substr, sizeof(srt_opt.ip_substr), "%s", ip);
    if (url[0])
      snprintf(srt_opt.url, sizeof(srt_opt.url), "%s", url);
    srt_opt.auto_search = false;
    op.protocol_opts = &srt_opt;
  } else if (proto == PROTO_RTMP) {
    ghost_rtmp_options_defaults(&rtmp_opt);
    snprintf(rtmp_opt.ip_substr, sizeof(rtmp_opt.ip_substr), "%s", ip);
    if (url[0])
      snprintf(rtmp_opt.url, sizeof(rtmp_opt.url), "%s", url);
    rtmp_opt.auto_search = false;
    op.protocol_opts = &rtmp_opt;
  } else {
    ghost_rtsp_options_defaults(&rtsp_opt);
    snprintf(rtsp_opt.ip_substr, sizeof(rtsp_opt.ip_substr), "%s", ip);
    if (url[0])
      snprintf(rtsp_opt.url, sizeof(rtsp_opt.url), "%s", url);
    rtsp_opt.auto_search = false;
    op.protocol_opts = &rtsp_opt;
  }

  int fails = 0;

  /* Connect + first frame */
  {
    media_session_t *s = mod->open(&op);
    if (!s) {
      fprintf(stderr, "FAIL open\n");
      return 1;
    }
    if (mod->connect_auto(s, NULL) != 0) {
      fprintf(stderr, "FAIL connect_auto\n");
      fails++;
      mod->close(s);
      return 1;
    }
    media_source_t src;
    mod->connected_source(s, &src);
    printf("CONNECTED %s url=%s\n", mod_id, src.url);

    media_frame_t fr;
    int64_t t0 = now_ms();
    bool got = false;
    while (now_ms() - t0 < 8000) {
      if (mod->capture_newest(s, &fr)) {
        got = true;
        break;
      }
      usleep(2000);
    }
    if (!got) {
      printf("FAIL first_frame\n");
      fails++;
    } else {
      printf("PASS first_frame %dx%d fps~%d/%d\n", fr.width, fr.height, fr.frame_rate_n,
             fr.frame_rate_d > 0 ? fr.frame_rate_d : 1);
    }

    /* Soak */
    int64_t soak_end = now_ms() + soak_s * 1000LL;
    uint64_t frames = 0, drops = 0;
    while (now_ms() < soak_end) {
      if (mod->capture_newest(s, &fr)) {
        frames++;
        drops += fr.dropped;
      } else {
        usleep(1000);
      }
    }
    double secs = soak_s > 0 ? (double)soak_s : 1.0;
    printf("%s soak %ds frames=%llu ~%.1f fps drops=%llu\n", frames > 0 ? "PASS" : "FAIL", soak_s,
           (unsigned long long)frames, frames / secs, (unsigned long long)drops);
    if (frames == 0)
      fails++;

    /* Reconnect */
    int ok = 0;
    for (int i = 0; i < reconnect_n; i++) {
      mod->disconnect(s);
      usleep(200000);
      if (mod->connect_auto(s, NULL) != 0)
        continue;
      int64_t d = now_ms() + 5000;
      while (now_ms() < d) {
        if (mod->capture_newest(s, &fr)) {
          ok++;
          break;
        }
        usleep(2000);
      }
    }
    printf("%s reconnect %d/%d\n", ok == reconnect_n ? "PASS" : "FAIL", ok, reconnect_n);
    if (ok != reconnect_n)
      fails++;

    /* Idle disconnect RSS-ish: just disconnect cleanly */
    mod->disconnect(s);
    printf("PASS disconnect\n");
    mod->close(s);
  }

  mod->shutdown();
  printf("RESULT %s fails=%d\n", fails == 0 ? "PASS" : "FAIL", fails);
  return fails == 0 ? 0 : 1;
}
