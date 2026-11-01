/*
 * video_esplus.c - moonlight-common-c DECODER_RENDERER_CALLBACKS backed by the
 * Tizen 5.0 esplusplayer (HW video). The native equivalent of the WASM fork's
 * wasm/wasmplayer.cpp (EMSS ElementaryMediaTrack.AppendPacket).
 *
 * moonlight-common-c delivers each frame as a chain of LENTRY buffers already in
 * Annex-B (start-code prefixed SPS/PPS/VPS/picture NALUs). esplusplayer accepts
 * raw Annex-B H.264 (proven by the esppplay prototype), so submitDecodeUnit just
 * concatenates the chain and submits one ES packet with the frame's PTS.
 *
 * Latency note: as measured with esppplay, packets queued before
 * prepare_async_done stay as constant latency. We start prepare_async in
 * VidDecStart and let the first decode units feed the preroll; the player
 * reports prepare_async_done after a few frames, then we start().
 */
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "Limelight.h"
#include "esplusplayer_tv50.h"

/* Minimal EFL decls (opaque), same as esppplay.c. */
int ecore_init(void);
int ecore_shutdown(void);
int ecore_wl2_init(void);
int ecore_wl2_shutdown(void);
void *ecore_wl2_display_connect(const char *name);
void ecore_wl2_display_disconnect(void *d);
void *ecore_wl2_window_new(void *d, void *parent, int x, int y, int w, int h);
void ecore_wl2_window_show(void *w);
void ecore_wl2_window_alpha_set(void *w, unsigned char alpha);
void ecore_wl2_window_free(void *w);
void ecore_wl2_display_flush(void *d);

#define VLOG(...) fprintf(stderr, "[video_esplus] " __VA_ARGS__)

static esplusplayer_handle s_pl;
static void *s_disp, *s_win;
static volatile int s_prepared, s_prepare_ok, s_feed_ready;
static int s_width, s_height, s_fps;
static uint8_t *s_scratch;      /* reused frame assembly buffer */
static size_t s_scratch_cap;
static uint64_t s_base_pts_ms;  /* first frame's presentationTimeMs */

static void on_prepared(int ok) { s_prepare_ok = ok; s_prepared = 1; VLOG("prepare_async_done=%d\n", ok); }
static void on_ready(uint32_t st) { (void)st; s_feed_ready = 1; }
static void on_error(uint32_t e) { VLOG("player error 0x%x\n", e); }
static void on_buffer(uint32_t st, uint32_t s) { (void)st; if (s) VLOG("OVERRUN\n"); }

static int ensure_scratch(size_t need) {
  if (need <= s_scratch_cap) return 0;
  size_t cap = s_scratch_cap ? s_scratch_cap : 65536;
  while (cap < need) cap *= 2;
  uint8_t *p = realloc(s_scratch, cap);
  if (!p) return -1;
  s_scratch = p; s_scratch_cap = cap;
  return 0;
}

static int VidDecSetup(int videoFormat, int width, int height, int redrawRate, void *context, int drFlags) {
  (void)context; (void)drFlags;
  s_width = width; s_height = height; s_fps = redrawRate > 0 ? redrawRate : 60;
  uint32_t mime;
  if (videoFormat & VIDEO_FORMAT_MASK_H264) mime = ESPLUSPLAYER_VIDEO_MIME_TYPE_H264;
  else if (videoFormat & VIDEO_FORMAT_MASK_H265) mime = ESPLUSPLAYER_VIDEO_MIME_TYPE_HEVC;
  else { VLOG("unsupported videoFormat 0x%x\n", videoFormat); return -1; }

  ecore_init();
  ecore_wl2_init();
  s_disp = ecore_wl2_display_connect(NULL);
  if (!s_disp) { VLOG("display_connect failed (XDG_RUNTIME_DIR/WAYLAND_DISPLAY?)\n"); return -1; }
  s_win = ecore_wl2_window_new(s_disp, NULL, 0, 0, width, height);
  if (!s_win) { VLOG("window_new failed\n"); return -1; }
  ecore_wl2_window_alpha_set(s_win, 1);
  ecore_wl2_window_show(s_win);
  ecore_wl2_display_flush(s_disp);

  s_pl = esplusplayer_create();
  if (!s_pl) { VLOG("esplusplayer_create failed\n"); return -1; }
  esplusplayer_set_error_cb(s_pl, on_error);
  esplusplayer_set_buffer_status_cb(s_pl, on_buffer);
  esplusplayer_set_ready_to_prepare_cb(s_pl, on_ready);
  esplusplayer_set_prepare_async_done_cb(s_pl, on_prepared);
  if (!esplusplayer_open(s_pl)) { VLOG("open failed\n"); return -1; }
  if (!esplusplayer_set_ecore_display(s_pl, ESPLUSPLAYER_DISPLAY_TYPE_OVERLAY, s_win, 0, 0, width, height))
    { VLOG("set_ecore_display failed\n"); return -1; }
  esplusplayer_video_stream_info vi = {NULL, 0, mime, (uint32_t)width, (uint32_t)height,
                                       (uint32_t)width, (uint32_t)height, (uint32_t)s_fps, 1};
  if (!esplusplayer_add_video_stream_info(s_pl, &vi)) { VLOG("add_video_stream_info failed\n"); return -1; }
  VLOG("setup %dx%d@%d fmt=0x%x\n", width, height, s_fps, videoFormat);
  return 0;
}

static void VidDecStart(void) {
  s_base_pts_ms = 0;
  if (!esplusplayer_prepare_async(s_pl)) VLOG("prepare_async failed\n");
  /* player is started from the first submitted frames completing preroll (see
     VidDecSubmit); nothing to block on here. */
}

static void VidDecStop(void) { if (s_pl) esplusplayer_stop(s_pl); }

static void VidDecCleanup(void) {
  if (s_pl) { esplusplayer_close(s_pl); s_pl = NULL; }
  if (s_win) { ecore_wl2_window_free(s_win); s_win = NULL; }
  if (s_disp) { ecore_wl2_display_disconnect(s_disp); s_disp = NULL; }
  ecore_wl2_shutdown(); ecore_shutdown();
  free(s_scratch); s_scratch = NULL; s_scratch_cap = 0;
}

static int VidDecSubmit(PDECODE_UNIT du) {
  /* Assemble the Annex-B buffer chain into one contiguous packet. */
  if (ensure_scratch((size_t)du->fullLength)) return DR_NEED_IDR;
  size_t off = 0;
  for (PLENTRY e = du->bufferList; e; e = e->next) {
    memcpy(s_scratch + off, e->data, (size_t)e->length);
    off += (size_t)e->length;
  }
  if (s_base_pts_ms == 0) s_base_pts_ms = du->presentationTimeMs;
  uint64_t pts_ms = du->presentationTimeMs - s_base_pts_ms;

  esplusplayer_es_packet pkt = {ESPLUSPLAYER_STREAM_TYPE_VIDEO, (char *)s_scratch, (uint32_t)off,
                                pts_ms * 1000000ull, (1000000000ull / s_fps)};
  int r = esplusplayer_submit_packet(s_pl, &pkt);

  /* Once prepare completes, start playback exactly once. */
  if (s_prepared && s_prepare_ok == 1) {
    static int started;
    if (!started) { started = 1; esplusplayer_start(s_pl); VLOG("started\n"); }
  }

  if (r == ESPLUSPLAYER_SUBMIT_STATUS_SUCCESS || r == ESPLUSPLAYER_SUBMIT_STATUS_FULL
      || r == ESPLUSPLAYER_SUBMIT_STATUS_NOT_PREPARED)
    return DR_OK;
  VLOG("submit_packet failed %d\n", r);
  return DR_NEED_IDR;
}

void VidEsplus_FillCallbacks(PDECODER_RENDERER_CALLBACKS cb) {
  LiInitializeVideoCallbacks(cb);
  cb->setup = VidDecSetup;
  cb->start = VidDecStart;
  cb->stop = VidDecStop;
  cb->cleanup = VidDecCleanup;
  cb->submitDecodeUnit = VidDecSubmit;
  cb->capabilities = CAPABILITY_DIRECT_SUBMIT;
}
