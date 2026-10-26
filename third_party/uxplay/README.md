# UxPlay AirPlay library

The AirPlay receiver library (`lib/` of UxPlay 1.74, https://github.com/FDH2/UxPlay,
commit `82ad05ce34ec862851fa6ab1240f50f58fb7fa5a`, 2026-10-25), unmodified:

- `*.c` / `*.h`: the RAOP / AirPlay server (RTSP over `llhttp`, pairing,
  FairPlay setup, the mirror and audio RTP streams, NTP timing). LGPL-2.1-or-later
  (Juho Vähä-Herttua, dsafa22, F. Duncanh and others; see each file's header).
- `mdnsd/`: UxPlay's own minimal mDNS responder, so no Avahi or Bonjour is
  needed. LGPL-2.1-or-later.
- `playfair/`: the FairPlay handshake. GPL-3.0 (`playfair/LICENSE.md`).
- `llhttp/`: the HTTP parser. MIT (`llhttp/LICENSE-MIT`).
- `LICENSE`: UxPlay's GPL-3.0, which covers the whole as distributed.

Not copied: UxPlay's GStreamer renderers, its `uxplay.cpp` front end, and
`lib/dns_sd/` (the Avahi / Bonjour variant of the service registration).

It is compiled into the app as the `octave_uxplay` static library
(`CMakeLists.txt`, `OCTAVE_HAVE_AIRPLAY`) and driven by
`src/airplay/airplayreceiver.cpp`, which supplies what UxPlay's renderers did:
H.264 into OCTAVE's shared decoder (`src/util/h264decoder`), ALAC / AAC
audio through libavcodec into Qt Multimedia. Dependencies: OpenSSL (libcrypto)
and libplist 2.x.

To update: copy `lib/` of a newer UxPlay over these files (keep the list above),
drop the subdirectory `CMakeLists.txt` files, rebuild, and update the commit here.
