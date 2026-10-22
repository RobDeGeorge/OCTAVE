# SQLite amalgamation

`sqlite3.c` / `sqlite3.h` are the unmodified SQLite 3.54.1 amalgamation
(`sqlite-amalgamation-3540100.zip` from https://sqlite.org/download.html,
SHA3-256 `92281c86e2957f9b9305890f9ac04204caa7b677cc868dc326d942689df28e30`).
SQLite is in the public domain (https://sqlite.org/copyright.html).

It is compiled into the app as the `octave_sqlite` static library
(`CMakeLists.txt`) and used by the black box trip store
(`src/managers/tripstore.cpp`). Compiling it in, instead of going through
Qt Sql, means no platform needs a SQL driver plugin at runtime: the first
in-car build on the Orange Pi (Ubuntu) had `libQt6Sql` but not the separate
`libqt6sql6-sqlite` plugin package, so the recorder silently stored nothing.

To update: download the new amalgamation zip, verify the hash published on
the download page, replace the two files, and update this note.
