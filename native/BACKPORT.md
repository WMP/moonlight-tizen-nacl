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

## Dalej

- M1 krok 2: zbudować mbedTLS + libgamestream, natywny `main` robi `gs_pair`
  i HTTP do PC (Sunshine).
- M1 krok 3: `DECODER_RENDERER_CALLBACKS.submitDecodeUnit` -> `esplusplayer_submit_packet`
  (użyć `split_aus` i okna z `esppplay.c`). esplusplayer przyjmuje Annex-B
  (potwierdzone prototypem). Audio i wejście zaślepione.
- M1 krok 4: `LiStartConnection` -> obraz na ekranie, uruchamiane z /tmp.

Niewiadome do potwierdzenia: odpowiednik EMSS `LatencyMode::kUltraLow` w
esplusplayer (brak `set_low_latency_mode` w eksportach - szukać w ustawieniach
bufora/display); synchronizacja A/V przy dwóch ścieżkach esplusplayer.
