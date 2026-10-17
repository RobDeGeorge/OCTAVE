#!/bin/sh
# 4-finger tap: close the focused window, or open a terminal if the workspace is empty.
windows=$(hyprctl activeworkspace -j | jq '.windows')
if [ "${windows:-0}" -gt 0 ]; then
    hyprctl dispatch killactive
else
    hyprctl dispatch exec kitty
fi
