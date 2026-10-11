**Status:** deferred
**Last updated:** 2026-10-10

# Android: bundle OpenSSL so Qt has a functional TLS backend

## Problem

On Android, both the local-built and CI-built APKs spam this in logcat the moment the app starts:

```
W linker  : library "/system/lib/libcrypto.so" needed or dlopened by
            "lib/arm64/libQt6Core_arm64-v8a.so" is not accessible for the
            namespace: [name="clns-9", ...]
W qt.tlsbackend.ossl: Failed to load libssl/libcrypto.
W qt.network.ssl: No functional TLS backend was found
W qt.network.ssl: QSslSocket::connectToHostEncrypted: TLS initialization failed
```

The APK only ships the **plugin shim** `libplugins_tls_qopensslbackend_arm64-v8a.so`, not the actual `libssl_3.so` / `libcrypto_3.so` that the shim wants to dlopen at runtime. Android 7+ (API 24+) restricts apps from loading the system's `/system/lib/libcrypto.so` due to namespacing, so even though Android has its own copy, Qt cannot use it. Qt requires us to ship an Android-compatible build of OpenSSL alongside the APK.

## Why this is parked, not urgent

As of v0.9 ship date (2026-05-03):

- **YT Music works** — `OctaveMediaBridge.java` runs ytmusicapi inside a bundled Python interpreter that uses Python's own `ssl` module backed by the Android system OpenSSL via the AAR-shipped Python build. Doesn't go through `QSslSocket`.
- **OTA updates / network image fetch** — currently no in-app HTTPS calls from Qt code on Android. Album art on Android comes from local files / the Python YT Music bundle, not Qt's `QNetworkAccessManager`.
- **Phone mirror / Android Auto / OBD** — all local protocols, no TLS.

So the warnings are noisy but currently non-blocking. They become blocking the moment any Qt-side `QNetworkAccessManager` HTTPS request ships in the Android build.

## What "done" looks like

1. CI's `cpp-build-android` job downloads or builds Android-compatible OpenSSL 3.x for `arm64-v8a` (and `armeabi-v7a` if we re-enable 32-bit) and places `libssl_3.so` + `libcrypto_3.so` somewhere `androiddeployqt` will pick them up.
2. `androiddeployqt` bundles them into the APK at `lib/arm64-v8a/`.
3. On launch, logcat no longer prints `Failed to load libssl/libcrypto` or `No functional TLS backend`.
4. A QML/C++ smoke call from `QNetworkAccessManager` to `https://example.com` succeeds with a 200, written into a temporary debug log.

## Implementation options (pick one)

### Option A — KDAB / official Qt-for-Android OpenSSL prebuilds (recommended)

KDAB publishes Android-compatible OpenSSL builds that match Qt's expected layout: <https://github.com/KDAB/android_openssl>. Qt's own docs link to this repo for years.

Steps in CI:
1. `git clone --depth 1 https://github.com/KDAB/android_openssl.git`
2. Pass to CMake: `-DANDROID_EXTRA_LIBS="$PWD/android_openssl/ssl_3/arm64-v8a/libssl_3.so;$PWD/android_openssl/ssl_3/arm64-v8a/libcrypto_3.so"` — `androiddeployqt` honors `ANDROID_EXTRA_LIBS` (set as `qt_add_executable` target property `QT_ANDROID_EXTRA_LIBS` is the modern equivalent).
3. Modern Qt 6: edit `CMakeLists.txt` so the `if(ANDROID)` block adds:
   ```cmake
   set_target_properties(octave PROPERTIES
       QT_ANDROID_EXTRA_LIBS
       "${CMAKE_SOURCE_DIR}/android_openssl/ssl_3/arm64-v8a/libssl_3.so;${CMAKE_SOURCE_DIR}/android_openssl/ssl_3/arm64-v8a/libcrypto_3.so"
   )
   ```
4. Add `android_openssl/` to `.gitignore` (CI clones it; we don't vendor it).

### Option B — vendor the .so files directly

Download the two `.so` files from the KDAB repo into `android/openssl/arm64-v8a/` and check them in. Simpler CI but ~6 MB of binaries committed to the repo; harder to audit/rotate when CVEs land.

### Option C — build OpenSSL from source in CI

`./Configure android-arm64 -D__ANDROID_API__=30 ... && make`. Slowest, most flexible, never rotted. Probably overkill until we have a security policy that demands it.

## Recommendation

**Option A.** KDAB is the maintained, audited, Qt-for-Android-blessed source. Adds ~5 lines to the workflow + ~5 lines to `CMakeLists.txt`. Zero binaries in the repo. When Qt bumps OpenSSL major version, we change the path string in one place.

## Cross-references

- `TODO/android-cpp-port.md` — TLS isn't blocking the C++ port itself, but blocks any in-app HTTPS feature on Android that lands after the port. If you're working through that file and adding a network call, do this first.
- `TODO/android-search-ytmusicapi.md` — YT Music doesn't need this fix because Python ships its own SSL.

## Where to start

1. Read `CMakeLists.txt:144-160` (the `if(ANDROID)` block) — that's where `QT_ANDROID_EXTRA_LIBS` goes.
2. Read `.github/workflows/build.yml:322-450` (the `cpp-build-android` job) — add a "Fetch Android OpenSSL" step before "Configure CMake (Android arm64)".
3. Locally: `cmake -S . -B build-android -DCMAKE_TOOLCHAIN_FILE=... -DQT_ANDROID_EXTRA_LIBS="..."` then `cmake --build build-android --target apk` and verify with `unzip -l build-android-release/android-build/octave.apk | grep -i ssl` — should now show `libssl_3.so` and `libcrypto_3.so` next to the plugin shim.
4. Push, let CI build, install on phone, verify logcat is clean of `qt.tlsbackend.ossl: Failed` and `No functional TLS backend`.

## Delete this file when done.
