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
#   FAKE_ADB_SERIALNO   what getprop ro.serialno answers (default R5CTFAKE)
#   FAKE_ADB_WLAN_IP    the phone's Wi-Fi address in `ip -4 -o addr show` (none when unset)
#   FAKE_ADB_WIFI_STATE file holding the adb_wifi_enabled setting (0 when missing);
#                       `settings put` writes it unless FAKE_ADB_WIFI_REFUSED is set
#   FAKE_ADB_MDNS       file listed by `adb mdns services` while that setting is 1
#   FAKE_ADB_CAR_JOIN   file `cmd wifi connect-network` writes FAKE_ADB_CAR_IP to; while it
#                       exists the phone's Wi-Fi address is its contents (it joined the car's Wi-Fi)
#   FAKE_ADB_WIFI_NO_R  `cmd wifi connect-network` refuses the -r option (an older Android)
#   FAKE_ADB_WATCHER    file holding the version of the Wi-Fi watcher that runs on the phone
#   FAKE_ADB_CONNECT_ONLY  the one address `adb connect` reaches (any when unset)

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
mdns)
    echo "List of discovered mdns services"
    [ -n "$FAKE_ADB_MDNS" ] && [ "$(cat "$FAKE_ADB_WIFI_STATE" 2>/dev/null)" = 1 ] && cat "$FAKE_ADB_MDNS"
    ;;
connect)
    if [ -n "$FAKE_ADB_CONNECT_ONLY" ] && [ "$2" != "$FAKE_ADB_CONNECT_ONLY" ]; then
        echo "failed to connect to '$2': No route to host"
    else
        echo "connected to $2"
    fi
    ;;
logcat)
    # Following a reattached server's log: stays open like the real one
    exec sleep 600
    ;;
shell)
    case "$*" in
    *app_process*)
        [ "$FAKE_ADB_SLOW" = shell ] && sleep 0.5
        [ -n "$FAKE_ADB_SHELL_PID" ] && echo $$ > "$FAKE_ADB_SHELL_PID"
        exec sleep 600
        ;;
    *"settings get global adb_wifi_enabled"*) cat "$FAKE_ADB_WIFI_STATE" 2>/dev/null || echo 0 ;;
    *"settings put global adb_wifi_enabled"*)
        [ -z "$FAKE_ADB_WIFI_REFUSED" ] && [ -n "$FAKE_ADB_WIFI_STATE" ] && echo "$6" > "$FAKE_ADB_WIFI_STATE" ;;
    *octave-wifi-watch*)
        # The Wi-Fi watcher: FAKE_ADB_WATCHER holds the version that "runs"
        case "$*" in
        *"echo running"*)
            v=$(cat "$FAKE_ADB_WATCHER" 2>/dev/null)
            case "$*" in *"= '$v' ]"*) [ -n "$v" ] && echo running || echo stopped ;; *) echo stopped ;; esac ;;
        *"echo started"*)
            echo "$*" | sed -n "s/.*echo '\([0-9a-f]*\)' > .*/\1/p" > "$FAKE_ADB_WATCHER"; echo started ;;
        *"echo removed"*) rm -f "$FAKE_ADB_WATCHER"; echo removed ;;
        esac
        ;;
    *"cmd wifi connect-network"*)
        case "$*" in
        *"-r persistent"*) [ -n "$FAKE_ADB_WIFI_NO_R" ] && { echo "Unknown option: -r"; exit 255; } ;;
        esac
        [ -n "$FAKE_ADB_CAR_JOIN" ] && echo "$FAKE_ADB_CAR_IP" > "$FAKE_ADB_CAR_JOIN"
        echo "Connection initiated"
        ;;
    *"ip -4 -o addr show"*)
        echo "1: lo    inet 127.0.0.1/8 scope host lo"
        if [ -n "$FAKE_ADB_CAR_JOIN" ] && [ -f "$FAKE_ADB_CAR_JOIN" ]; then
            FAKE_ADB_WLAN_IP=$(cat "$FAKE_ADB_CAR_JOIN")
        fi
        [ -n "$FAKE_ADB_WLAN_IP" ] && echo "30: wlan0    inet $FAKE_ADB_WLAN_IP/24 brd 192.168.1.255 scope global wlan0" ;;
    *ro.build.version.sdk*) echo "${FAKE_ADB_SDK:-34}" ;;
    *ro.product.model*) echo "SM-TEST" ;;
    *ro.serialno*) echo "${FAKE_ADB_SERIALNO:-R5CTFAKE}" ;;
    esac
    ;;
esac
exit 0
