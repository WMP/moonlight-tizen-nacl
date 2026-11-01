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
#include <time.h>
#include <unistd.h>

#include "Limelight.h"

extern int ml_cert_init(const char *dir);
extern void VidEsplus_FillCallbacks(PDECODER_RENDERER_CALLBACKS cb);
extern void AudEsplus_FillCallbacks(PAUDIO_RENDERER_CALLBACKS cb);  /* audio_opus.c */
extern int input_init(void);      /* input_evdev.c */
extern void input_shutdown(void);
/* gs_client.c: serverinfo -> pair -> launch -> RTSP url; fills sc->remoteInputAes*
 * and si->serverInfoAppVersion/GfeVersion/rtspSessionUrl. */
extern int gs_orchestrate(const char *host, unsigned short httpPort, unsigned short httpsPort,
                          int appId, const char *pin, STREAM_CONFIGURATION *sc, SERVER_INFORMATION *si,
                          const char *datadir);

static volatile sig_atomic_t g_quit;
static void on_sig(int s) { (void)s; g_quit = 1; }

static void cl_stageFailed(int stage, int errorCode) { fprintf(stderr, "[ml] stage %d FAILED err=%d\n", stage, errorCode); }
/* When the host drops the stream (e.g. "lack of video traffic"), quit so main
   runs LiStopConnection -> the video sink's cleanup releases the HW overlay.
   Without this the client hangs holding the overlay and blocks the TV's own
   broadcast video until another app cycles the plane. */
static void cl_connTerminated(int errorCode) { fprintf(stderr, "[ml] connection terminated err=%d\n", errorCode); g_quit = 1; }
static void cl_logMessage(const char *format, ...) {
  va_list a; va_start(a, format); vfprintf(stderr, format, a); va_end(a);
}

int main(int argc, char **argv) {
  if (argc < 3) {
    fprintf(stderr, "usage: %s <host-ip> <appid> [width=1920] [height=1080] [fps=60] [bitrate_kbps=20000]\n"
                    "  env: MOONLIGHT_PIN (4 digits, for first-time pairing), MOONLIGHT_DATADIR (default /tmp)\n", argv[0]);
    return 2;
  }
  const char *host = argv[1];
  int appId = atoi(argv[2]);
  int width = argc > 3 ? atoi(argv[3]) : 1920;
  int height = argc > 4 ? atoi(argv[4]) : 1080;
  int fps = argc > 5 ? atoi(argv[5]) : 60;
  int bitrate = argc > 6 ? atoi(argv[6]) : 20000;
  const char *pin = getenv("MOONLIGHT_PIN");
  const char *datadir = getenv("MOONLIGHT_DATADIR"); if (!datadir) datadir = "/tmp";
  const unsigned short httpPort = 47989, httpsPort = 47984;

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

  if (gs_orchestrate(host, httpPort, httpsPort, appId, pin, &sc, &si, datadir) != 0) {
    fprintf(stderr, "[ml] GameStream orchestration failed; cannot start stream.\n");
    return 3;
  }
  si.serverCodecModeSupport = SCM_H264;

  DECODER_RENDERER_CALLBACKS dr;
  VidEsplus_FillCallbacks(&dr);

  AUDIO_RENDERER_CALLBACKS ar;
  AudEsplus_FillCallbacks(&ar);

  CONNECTION_LISTENER_CALLBACKS cl;
  LiInitializeConnectionCallbacks(&cl);
  cl.stageFailed = cl_stageFailed;
  cl.connectionTerminated = cl_connTerminated;
  cl.logMessage = cl_logMessage;

  fprintf(stderr, "[ml] LiStartConnection %dx%d@%d %dkbps -> %s\n", width, height, fps, bitrate, host);
  int err = LiStartConnection(&si, &sc, &cl, &dr, &ar, NULL, 0, NULL, 0);
  if (err != 0) { fprintf(stderr, "[ml] LiStartConnection failed: %d\n", err); return 1; }

  if (input_init() != 0) fprintf(stderr, "[ml] input disabled (no evdev access)\n");

  fprintf(stderr, "[ml] streaming; Ctrl+C to stop\n");
  /* poll g_quit (set by SIGINT/SIGTERM or connectionTerminated); pause() would
     not wake on a flag set from a callback thread. */
  while (!g_quit) { struct timespec ts = {0, 200000000}; nanosleep(&ts, NULL); }
  fprintf(stderr, "[ml] stopping, releasing overlay\n");
  input_shutdown();
  LiStopConnection();   /* tears down stages -> video sink cleanup -> esplus_close */
  return 0;
}
