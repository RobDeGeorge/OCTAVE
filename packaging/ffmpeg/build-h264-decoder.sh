#!/usr/bin/env bash
# Build the FFmpeg that release builds link: static libavcodec + libavutil with
# the H.264 decoder and nothing else (no encoders, no external codec libraries,
# no GPL parts), so every desktop package carries an LGPL-2.1-or-later FFmpeg
# of a few hundred KB instead of a distro / Homebrew build with x264, x265,
# xvid and a dozen codec libraries.
#
# Static on purpose: Qt's FFmpeg media backend brings its own FFmpeg (linked
# into the plugin on Linux x86_64, libavcodec.61.dylib on macOS, the distro's
# on aarch64), and a second shared copy could clash with it by file name.
#
# Usage:   bash packaging/ffmpeg/build-h264-decoder.sh <install-prefix>
# Then:    cmake ... -DOCTAVE_FFMPEG_ROOT=<install-prefix>
# Needs:   a C compiler, make, curl, gpg; nasm on x86_64 (else the decoder
#          builds without its SIMD code and is several times slower).
# Env:     FFMPEG_EXTRA_CONFIGURE  extra ./configure flags (e.g. --disable-x86asm
#                                   for a local test build without nasm)
#
# The source tarball is pinned by SHA-256 and checked against FFmpeg's release
# signing key. Bump FFMPEG_VERSION + FFMPEG_SHA256 together, and the version
# in NOTICE.
set -euo pipefail

FFMPEG_VERSION="9.0.2"
FFMPEG_SHA256="8c3850283eb25fa026482078a04051e0be17347b09ef81a0849bec15a96e002e"
# FFmpeg release signing key <ffmpeg-devel@ffmpeg.org>
FFMPEG_KEY_FPR="FCF986EA15E6E293A5644F10B4322F04D67658D8"

PREFIX="${1:?usage: build-h264-decoder.sh <install-prefix>}"
mkdir -p "$PREFIX"
PREFIX="$(cd "$PREFIX" && pwd)"
WORK="${FFMPEG_WORK_DIR:-$PREFIX/.work}"
mkdir -p "$WORK"
cd "$WORK"

TARBALL="ffmpeg-${FFMPEG_VERSION}.tar.xz"
URL="https://ffmpeg.org/releases/${TARBALL}"

fetch() { curl -sSfL --retry 5 --retry-all-errors --connect-timeout 20 -o "$2" "$1"; }

sha256() {
    if command -v sha256sum >/dev/null; then sha256sum "$1" | cut -d' ' -f1
    else shasum -a 256 "$1" | cut -d' ' -f1; fi
}

if [ ! -s "$TARBALL" ] || [ "$(sha256 "$TARBALL")" != "$FFMPEG_SHA256" ]; then
    echo "==> Downloading $URL"
    fetch "$URL" "$TARBALL"
    fetch "$URL.asc" "$TARBALL.asc"
fi
GOT="$(sha256 "$TARBALL")"
if [ "$GOT" != "$FFMPEG_SHA256" ]; then
    echo "error: $TARBALL SHA-256 is $GOT, expected $FFMPEG_SHA256" >&2
    exit 1
fi
# The hash pins the bytes; the signature says they are FFmpeg's. Skip only when
# gpg is missing (the hash still holds), never on a bad signature.
if command -v gpg >/dev/null && [ -s "$TARBALL.asc" ]; then
    export GNUPGHOME="$WORK/gnupg"
    mkdir -p -m 700 "$GNUPGHOME"
    [ -s ffmpeg-devel.asc ] || fetch https://ffmpeg.org/ffmpeg-devel.asc ffmpeg-devel.asc
    gpg -q --import ffmpeg-devel.asc 2>/dev/null
    gpg --status-fd 1 --verify "$TARBALL.asc" "$TARBALL" 2>/dev/null \
        | grep -q "VALIDSIG $FFMPEG_KEY_FPR" \
        || { echo "error: $TARBALL is not signed by the FFmpeg release key" >&2; exit 1; }
    echo "==> Signature OK ($FFMPEG_KEY_FPR)"
fi

rm -rf "ffmpeg-${FFMPEG_VERSION}"
tar xf "$TARBALL"
cd "ffmpeg-${FFMPEG_VERSION}"

# No --enable-gpl / --enable-version3 / --enable-nonfree: the result is
# LGPL-2.1-or-later, which src/main.cpp logs under --smoke and CI checks.
# --disable-autodetect keeps out every system library configure would
# otherwise pick up (zlib, iconv, VA-API, VideoToolbox, ...), so the static
# libraries need nothing beyond libm and pthreads.
# shellcheck disable=SC2086
./configure \
    --prefix="$PREFIX" \
    --enable-static --disable-shared --enable-pic \
    --disable-autodetect \
    --disable-everything \
    --disable-programs --disable-doc --disable-network \
    --disable-avformat --disable-avdevice --disable-avfilter \
    --disable-swscale --disable-swresample \
    --enable-decoder=h264 \
    ${FFMPEG_EXTRA_CONFIGURE:-}

JOBS="$(getconf _NPROCESSORS_ONLN 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"
make -j"$JOBS"
make install

echo "==> Installed static LGPL libavcodec + libavutil (H.264 decoder only) to $PREFIX"
