# No-root install on a Samsung TV — the dev-certificate path

Install the native Moonlight engine on a **stock (unrooted)** Tizen 5.0 TV, in
**Developer Mode**, with a Samsung certificate bound to that TV. No root, no store.

Why this shape (verified, see `../BACKPORT.md` + memory):
- A retail TV installs only **Web (.wgt)** and **.NET (.tpk)** apps; there is no
  native-app SDK/rootstrap for TV, so the installable unit is a **.NET host .tpk**.
- Our C engine ships inside it as **`libmlengine.so`** and is driven over the stable
  C ABI (`mlengine.h`) by P/Invoke — the host is `dotnet-host/`.
- The TV **rejects** unsigned / generic / tizen-public-distributor signatures; it
  needs a **Samsung distributor cert bound to the TV's DUID** (per-TV, dev mode).

> Scope: this covers **your own + a few known TVs** (each TV's DUID registered). Free
> distribution to anyone needs the Samsung Store + Seller partner status — out of
> scope, and Samsung may reject a GPL GameStream client.

## 0. Prerequisites
- `libmlengine.so` built for the TV: `native/mlclient/build-engine-so.sh` →
  copy `engine-build/libmlengine.so` to `dotnet-host/lib/`.
- .NET SDK + Tizen workload (`dotnet workload install tizen`) **or** Tizen Studio
  CLI with the .NET/`dotnet` packaging support. Not installed locally yet — do this
  in the CI image or a dev box.
- A **Samsung account** (free) for the certificate.

## 1. Enable Developer Mode on the TV
1. Apps → type `12345` on the remote → **Developer mode** ON.
2. Enter your **host PC IP** (the machine that will run `sdb`/`tizen install`).
3. Reboot the TV. It now accepts `sdb connect <TV-IP>`.

## 2. Get the TV DUID
- `sdb connect <TV-IP>` then `sdb devices` → or in Tizen Studio **Device Manager**.
- The **DUID** (Device Unique ID) is shown per connected TV; copy it.

## 3. Create the Samsung certificate profile (bound to the DUID)
Tizen Studio **Certificate Manager** → new **Samsung** certificate:
1. **Author** certificate (your Samsung account).
2. **Distributor** certificate, privilege level **Public**, with the **DUID(s)** of
   the target TV(s) entered. (This is what makes the TV accept the app; a Tizen
   distributor cert will NOT work.)
3. This yields a security **profile** (author.p12 + distributor.p12 + profile.xml).
   Headless alternative: the `Apps2Samsung` tooling can mint the same per-DUID cert.

## 4. Build the .tpk
From `dotnet-host/` (after copying the engine .so into `lib/`):
```
dotnet build -c Release                 # or: tizen package -t tpk
```
This produces `MoonlightHost.tpk` containing `MoonlightHost.dll` + `lib/libmlengine.so`.

> **TFM/runtime:** match the TV. Tizen 5.0 runs an older .NET (Tizen.NET 5.x). Before
> building, inspect the **working** on-device app and copy its target:
> `sdb pull /opt/usr/apps/dev.moonlight.tvshell` and read its `tizen-manifest.xml`
> (api-version, type) + the `.dll` runtime. The `.csproj`/manifest here are
> provisional until matched to it.

## 5. Sign with the Samsung profile
```
tizen cli-config "profiles.path=<...>/profiles.xml"
tizen package -t tpk -s <SamsungProfileName> -- MoonlightHost.tpk
```

## 6. Install + run
```
sdb connect <TV-IP>
tizen install -n MoonlightHost.tpk -t <TV-IP>     # or: sdb install MoonlightHost.tpk
sdb shell app_launcher -s org.moonlight.host.app
sdb dlog MLHOST                                    # watch ml_engine_start rc + engine logs
```

## 7. The in-memory .so caveat (test, then fix if needed)
On some 2018–2020 sets the TV **blocks native code loaded from the app folder**. If
`ml_engine_start` fails to load `libmlengine.so` from `lib/`, switch to the
community workaround: read the `.so` bytes and `dlopen` it from an in-memory fd
(`memfd_create`), as nuvio-native-legacy does. Only add this if the direct load is
actually blocked — flutter-tizen ships its engine .so from disk, so it may work.

## Open items / verify against the device
- Exact TFM + Tizen.NET version (from `dev.moonlight.tvshell`).
- Whether direct `dlopen` from `lib/` works, or the memfd path is required.
- Privilege set the engine actually needs (internet + display confirmed necessary;
  trim the rest).
- Whether the overlay/esplusplayer behaves the same under the .NET-hosted sandbox as
  under root (it should — same SMACK app domain).
