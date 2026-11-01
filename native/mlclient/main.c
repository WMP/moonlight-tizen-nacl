/*
 * main.c - native Moonlight client for Tizen 5.0 (SKELETON).
 *
 * Status: wires client cert init + stream config + the esplusplayer video sink
 * + stub audio/connection callbacks + LiStartConnection, and compiles/links into
 * the full client ELF. The GameStream HTTP orchestration (serverinfo GET, pairing,
 * app launch, RTSP session URL) is NOT implemented yet - see gs_orchestrate()
 * below; it must be ported from moonlight-embedded client.c. Until then this
 * binary performs cert init and then stops at the orchestration seam.
 *
 * Build: native/mlclient/build-client.sh
 */
#define _GNU_SOURCE
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "Limelight.h"

extern int ml_cert_init(const char *dir);
extern void VidEsplus_FillCallbacks(PDECODER_RENDERER_CALLBACKS cb);
/* fork libgamestream pairing: returns the server cert DER as a hex string. */
extern int gs_pair(int serverMajorVersion, const char *address, unsigned short httpPort,
                   const char *pin, char **server_cert_der_string);

static void cl_stageFailed(int stage, int errorCode) { fprintf(stderr, "[ml] stage %d FAILED err=%d\n", stage, errorCode); }
static void cl_connTerminated(int errorCode) { fprintf(stderr, "[ml] connection terminated err=%d\n", errorCode); }
static void cl_logMessage(const char *format, ...) {
  va_list a; va_start(a, format); vfprintf(stderr, format, a); va_end(a);
}

/* --- audio: stubbed (video-first milestone). Returning success keeps the
   connection alive; samples are dropped until the audio sink is written. --- */
static int ar_init(int audioConfig, const POPUS_MULTISTREAM_CONFIGURATION cfg, void *ctx, int flags) {
  (void)audioConfig; (void)cfg; (void)ctx; (void)flags; return 0;
}
static void ar_cleanup(void) {}
static void ar_decodePlay(char *sampleData, int sampleLength) { (void)sampleData; (void)sampleLength; }

/*
 * TODO (M1 step 3b): port from moonlight-embedded client.c.
 * Must: GET https://<host>:47984/serverinfo (and :47989 http), read app version /
 * GFE version / pair state / current game; if unpaired call gs_pair(); POST
 * /launch?... (or /resume) to start the app; read the RTSP session URL from the
 * launch response. Fills host/appVersion/gfeVersion/rtspUrl for SERVER_INFORMATION.
 * Returns 0 on success.
 */
static int gs_orchestrate(const char *host, SERVER_INFORMATION *si,
                          char **appVersion, char **gfeVersion, char **rtspUrl) {
  (void)host; (void)si; (void)appVersion; (void)gfeVersion; (void)rtspUrl;
  fprintf(stderr, "[ml] gs_orchestrate: NOT IMPLEMENTED - port serverinfo/pair/launch/RTSP from moonlight-embedded client.c\n");
  return -1;
}

static volatile sig_atomic_t g_quit;
static void on_sig(int s) { (void)s; g_quit = 1; LiStopConnection(); }

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: %s <host-ip> [width=1920] [height=1080] [fps=60] [bitrate_kbps=20000] [data-dir=/tmp]\n", argv[0]);
    return 2;
  }
  const char *host = argv[1];
  int width = argc > 2 ? atoi(argv[2]) : 1920;
  int height = argc > 3 ? atoi(argv[3]) : 1080;
  int fps = argc > 4 ? atoi(argv[4]) : 60;
  int bitrate = argc > 5 ? atoi(argv[5]) : 20000;
  const char *datadir = argc > 6 ? argv[6] : "/tmp";

  signal(SIGINT, on_sig);
  signal(SIGTERM, on_sig);

  if (ml_cert_init(datadir) != 0) { fprintf(stderr, "[ml] cert init failed\n"); return 1; }

  STREAM_CONFIGURATION sc;
  LiInitializeStreamConfiguration(&sc);
  sc.width = width; sc.height = height; sc.fps = fps;
  sc.bitrate = bitrate;
  sc.packetSize = 1024;
  sc.supportedVideoFormats = VIDEO_FORMAT_H264;  /* HEVC later */
  sc.audioConfiguration = AUDIO_CONFIGURATION_STEREO;

  SERVER_INFORMATION si;
  LiInitializeServerInformation(&si);
  si.address = host;

  char *appVersion = NULL, *gfeVersion = NULL, *rtspUrl = NULL;
  if (gs_orchestrate(host, &si, &appVersion, &gfeVersion, &rtspUrl) != 0) {
    fprintf(stderr, "[ml] orchestration not ready; stopping before LiStartConnection.\n");
    return 3;
  }
  si.serverInfoAppVersion = appVersion;
  si.serverInfoGfeVersion = gfeVersion;
  si.rtspSessionUrl = rtspUrl;
  si.serverCodecModeSupport = SCM_H264;

  DECODER_RENDERER_CALLBACKS dr;
  VidEsplus_FillCallbacks(&dr);

  AUDIO_RENDERER_CALLBACKS ar;
  LiInitializeAudioCallbacks(&ar);
  ar.init = ar_init; ar.cleanup = ar_cleanup; ar.decodeAndPlaySample = ar_decodePlay;
  ar.capabilities = CAPABILITY_DIRECT_SUBMIT;

  CONNECTION_LISTENER_CALLBACKS cl;
  LiInitializeConnectionCallbacks(&cl);
  cl.stageFailed = cl_stageFailed;
  cl.connectionTerminated = cl_connTerminated;
  cl.logMessage = cl_logMessage;

  fprintf(stderr, "[ml] LiStartConnection %dx%d@%d %dkbps -> %s\n", width, height, fps, bitrate, host);
  int err = LiStartConnection(&si, &sc, &cl, &dr, &ar, NULL, 0, NULL, 0);
  if (err != 0) { fprintf(stderr, "[ml] LiStartConnection failed: %d\n", err); return 1; }

  fprintf(stderr, "[ml] streaming; Ctrl+C to stop\n");
  while (!g_quit) pause();
  LiStopConnection();
  return 0;
}
