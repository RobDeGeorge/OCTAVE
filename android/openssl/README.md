# OpenSSL for the Android build

Prebuilt OpenSSL 3.1.8 (`libcrypto_3.so`, `libssl_3.so`, arm64-v8a) from
KDAB's [android_openssl](https://github.com/KDAB/android_openssl) repository
(`ssl_3/arm64-v8a`, Apache-2.0 licensed OpenSSL, see `LICENSE.txt`).

Qt for Android ships only the OpenSSL *TLS plugin*, not OpenSSL itself. Without
these two libraries in the APK every `QSslSocket` connection fails with
"No functional TLS backend was found" (Qt logs it once at startup), which
silently killed Spotify, the self-update check and any other Qt-side HTTPS on
the phone (the YouTube Music search and downloads survived only because they go
through the Java bridge). `CMakeLists.txt` adds them via
`QT_ANDROID_EXTRA_LIBS` so androiddeployqt bundles them, and `main.cpp` logs
`QSslSocket::supportsSsl()` at startup so a missing library shows up in the log.

To update: download the matching `ssl_3/<abi>/` files from the repository for
every ABI in `QT_ANDROID_ABIS` and replace these.
