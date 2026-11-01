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

## Pozostałe zależności do rozwiązania (krytyczna ścieżka M1)

1. **mbedTLS** dla `PlatformCrypto.c` - ~20 funkcji z modułów cipher/ctr_drbg/
   entropy. mbedTLS to przenośny C, buduje się GCC 6.2. Wbudować źródło.
2. **libgamestream** (parowanie/HTTP) - OpenSSL 1.0 + libcurl. TV ma `.so`,
   brak nagłówków. Opcje: (a) nagłówki z zrzutu źródeł OpenSSL 1.0.x + linkowanie
   do `tv-libs`, (b) port libgamestream na mbedTLS (wariant mbedTLS istnieje w
   moonlight-embedded - do sprawdzenia), by uniknąć OpenSSL.

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
