# Copies frontend/ into the WebAssembly build's filesystem image (run by the
# octave_wasm_fs target in CMakeLists.txt). The README screenshots, the asset
# sources and the unused full-size Jeep model stay out: ~45 MB of the 74 MB.
#   cmake -DSRC=<repo>/frontend -DDEST=<build>/wasm_fs -DSTAMP=<file> -P stage_frontend.cmake
file(REMOVE_RECURSE "${DEST}/frontend")
file(COPY "${SRC}" DESTINATION "${DEST}"
     PATTERN "assets/readme" EXCLUDE
     PATTERN "assets/-src" EXCLUDE
     PATTERN "jeep_tj_2003.glb" EXCLUDE)
file(TOUCH "${STAMP}")
