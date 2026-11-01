# Backport moonlight-tizen -> Tizen 5.0 natywny

Cel: szybki natywny klient Moonlight na Tizen 5.0 (armv7l softfp, glibc 2.24,
GCC 6.2), wideo przez `/usr/lib/libesplusplayer.so` (ABI w `esplusplayer_tv50.h`,
zmierzone opóźnienie 2-3 klatki). Źródło: brightcraft/moonlight-tizen (WASM/EMSS).

## Co jest wspólne (zweryfikowane w drzewie źródeł)

| Warstwa | Pliki | Uwaga |
|---|---|---|
| Rdzeń strumienia | `moonlight-common-c/src/*.c` (Connection, RTSP, Control/Video/Audio, depacketizer, ENet, Reed-Solomon) | czysty C, przenośny |
| Krypto rdzenia | `moonlight-common-c/src/PlatformCrypto.c` | **mbedTLS** (cipher + ctr_drbg + entropy, AES-GCM) |
| Parowanie/HTTP | `libgamestream/{http,pairing,mkcert}.c` | **libcurl + OpenSSL 1.0** |
| Audio dekod | `wasm/auddec.cpp` | opus_multistream_decode; backend EMSS lub WebAudio |
| Wideo (EMSS) | `wasm/wasmplayer.cpp` | `ElementaryMediaTrack.AppendPacket` - odpowiednik `esplusplayer_submit_packet` |
| Wejście | `wasm/input.cpp`, `wasm/gamepad.cpp` | cienkie nakładki na API moonlight-common-c |
| UI/sterowanie | `wasm/platform/*.js`, `index.html` | Tizen WebAPI |

Tylko ~6 plików nakładek platformy trzeba przepisać. Rdzeń, libgamestream,
h264bitstream, opus, enet są wspólne.

## Biblioteki na Tizen 5.0 (sprawdzone w tv-libs)

Są: `libcrypto.so.1.0.0`, `libssl`, `libcurl.so.4.5.0`, PulseAudio, ALSA,
libevdev, libinput, Ecore_Wl2, GLES/EGL, `libesplusplayer.so`.
Brak: libopus, libenet (oba wbudowane w repo - OK).
Brak nagłówków dev w sysroot dla openssl/curl/mbedtls - trzeba dostarczyć.

## Milestone 1 krok 1 - WYNIK (2026-11-01)

`native/build-core.sh`: rdzeń (moonlight-common-c + enet + reedsolomon),
**27/27 plików kompiluje się GCC 6.2 na sysroot, 0 błędów** (2 nieszkodliwe
ostrzeżenia strict-aliasing w enet/unix.c, CMSG_DATA - są też w upstream).
Potrzebne były tylko 2 drobne atrapy dla zbędnych odwołań Emscripten w
`Platform.h` (żaden plik .c rdzenia nie woła API emscripten):
- `#include <emscripten.h>` w gałęzi POSIX (linia 46) - zbędny,
- makro `Limelog` woła `emscripten_log` - port przekieruje na
  `ListenerCallbacks.logMessage`.

**Największe ryzyko z planu (czy GCC 6.2 zbuduje rdzeń) - ZDJĘTE.**

## Milestone 1 krok 2 - WYNIK (2026-11-01)

Skrypty: `native/fetch-backport-deps.sh` (pobiera zależności), `native/build-deps-lib.sh`
(buduje i linkuje). Dobrano wersje pod biblioteki TV: TV ma OpenSSL **1.0.2o**
i libcurl **7.59.0**.

- **mbedTLS 2.28.8 LTS**: 96/96 plików kompiluje GCC 6.2, archiwum statyczne.
- **PlatformCrypto.c** (`-DUSE_MBEDTLS`, API 2.x): kompiluje się na mbedTLS.
- **libgamestream** (http/pairing/mkcert): kompiluje się na nagłówkach OpenSSL
  1.0.2o + curl 7.59 (opensslconf.h ręczny dla 32-bit ARM, `OPENSSL_NO_KRB5`).
- **Test linkowania**: cały łańcuch (rdzeń + mbedTLS statycznie + libgamestream)
  linkuje się do ARM ELF przeciw prawdziwym `tv-libs` libssl.so.1.0.0 /
  libcrypto.so.1.0.0 / libcurl.so.4. **Wszystkie symbole krypto/curl rozwiązane.**
  Żaden symbol glibc > 2.24. Nierozwiązane zostają tylko globale aplikacji
  (`g_Cert`, `g_PrivateKey`, `g_UniqueId`, `g_CertHex`) - dostarcza je natywny `main`.

**Ryzyko zależności/linkowania z planu - ZDJĘTE.** Nie trzeba portować
libgamestream na mbedTLS; OpenSSL 1.0.2o z TV wystarcza.

## Milestone 1 krok 3 (część) - WYNIK (2026-11-01)

Sink wideo: `native/mlclient/video_esplus.c` - implementacja
`DECODER_RENDERER_CALLBACKS` nad esplusplayer (natywny odpowiednik
`wasm/wasmplayer.cpp`). `submitDecodeUnit` skleja łańcuch LENTRY (Annex-B)
w jeden pakiet ES i wysyła z PTS z `presentationTimeMs`. Skrypt:
`native/mlclient/build-video-path.sh`.

- Kompiluje się 0 ostrzeżeń.
- **Cały natywny tor wideo linkuje się w jeden ARM ELF**: rdzeń + PlatformCrypto
  (mbedTLS) + libgamestream (OpenSSL/curl) + sink + esplusplayer + ecore, przeciw
  prawdziwym `tv-libs`. NEEDED: libesplusplayer, libecore*, libssl, libcrypto,
  libcurl. Żaden symbol glibc > 2.24. `--allow-shlib-undefined` potrzebny dla
  przechodnich zależności esplusplayer tylko-z-TV (jak w `build-esppplay.sh`).

## Milestone 1 krok 3b (część) - WYNIK (2026-11-01)

- `native/mlclient/cert_init.c`: generuje/ładuje cert klienta (PEM), ustawia
  globale `g_Cert/g_PrivateKey/g_UniqueId/g_CertHex`, curl init. Natywny
  odpowiednik `http.cpp` MakeCert/LoadCert/HttpInit.
  **Przetestowane pod qemu-arm** na OpenSSL 1.0.2 z TV: run 1 tworzy ważny X.509
  (CN=NVIDIA GameStream Client, 10 lat), zapisuje client.pem/key/uniqueid;
  run 2 wczytuje istniejący (ta sama uniqueid). Cert poprawny wg `openssl x509`.
- `native/mlclient/main.c`: SZKIELET. Wiąże cert init + STREAM_CONFIGURATION +
  sink wideo + zaślepki audio/listener + `LiStartConnection`. Buduje się i
  linkuje w pełny klient ELF (`native/mlclient/build-client.sh`), brak symboli
  glibc > 2.24.

Jeszcze NIE zrobione: `gs_orchestrate()` w main.c (GET serverinfo, parowanie,
launch, URL RTSP) - do portu z moonlight-embedded `client.c`. Binarka robi cert
init i zatrzymuje się na tym szwie. Pełny test wymaga hosta Sunshine w LAN i
włączonego TV.

Uwaga do uruchamiania: klienta (z esplusplayer) nie da się uruchomić pod qemu -
esplusplayer ciąga `libopen-debugmenu.so` (lib tylko-z-TV). Testować na TV.

## Dalej

- M1 krok 3b cd.: port orkiestracji HTTP/parowania z moonlight-embedded `client.c`
  do `gs_orchestrate()`.
- M1 krok 4: `LiStartConnection` -> obraz na ekranie z /tmp, test z hostem Sunshine.

Niewiadome do potwierdzenia: odpowiednik EMSS `LatencyMode::kUltraLow` w
esplusplayer (brak `set_low_latency_mode` w eksportach - szukać w ustawieniach
bufora/display); synchronizacja A/V przy dwóch ścieżkach esplusplayer.
