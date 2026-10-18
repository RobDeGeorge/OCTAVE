#!/bin/sh
# Stand-in for adb in the phone-mirror tests (tests/cpp/tst_scrcpyclient.cpp,
# tst_phonemirror.cpp). No phone, no adb server. Behaviour comes from the
# environment:
#   FAKE_ADB_LOG        append each call's arguments, one line per call
#   FAKE_ADB_DEVICES    file whose contents follow the `adb devices` header
#   FAKE_ADB_GATE       while this file exists, `adb devices` blocks (a hung adb)
#   FAKE_ADB_SLOW       "push" or "shell": that step takes 0.5 s
#   FAKE_ADB_SHELL_PID  the server shell writes its pid here, then waits to be killed
#   FAKE_ADB_SDK        what getprop ro.build.version.sdk answers (default 34)

[ -n "$FAKE_ADB_LOG" ] && echo "$*" >> "$FAKE_ADB_LOG"
if [ "$1" = "-s" ]; then shift 2; fi

case "$1" in
version)
    echo "Android Debug Bridge version 1.0.41"
    echo "Version 35.0.2-12147458"
    ;;
devices)
    if [ -n "$FAKE_ADB_GATE" ]; then
        while [ -e "$FAKE_ADB_GATE" ]; do sleep 0.02; done
    fi
    echo "List of devices attached"
    [ -n "$FAKE_ADB_DEVICES" ] && [ -f "$FAKE_ADB_DEVICES" ] && cat "$FAKE_ADB_DEVICES"
    ;;
push)
    [ "$FAKE_ADB_SLOW" = push ] && sleep 0.5
    ;;
shell)
    case "$*" in
    *app_process*)
        [ "$FAKE_ADB_SLOW" = shell ] && sleep 0.5
        [ -n "$FAKE_ADB_SHELL_PID" ] && echo $$ > "$FAKE_ADB_SHELL_PID"
        exec sleep 600
        ;;
    *ro.build.version.sdk*) echo "${FAKE_ADB_SDK:-34}" ;;
    *ro.product.model*) echo "SM-TEST" ;;
    esac
    ;;
esac
exit 0
