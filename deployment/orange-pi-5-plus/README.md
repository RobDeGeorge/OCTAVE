# Orange Pi 5 Plus head unit: reference config

These are the config files from the OCTAVE head unit in the author's 2003 Jeep TJ: an Orange Pi 5 Plus driving a 10.1-inch 1280x800 touchscreen. The 3D-printed mounts for this box are in `hardware/2003-jeep-tj/`.

The files are copied from the running box and are meant as a reference. The user on the box is `roxy`, so change the user name, paths and audio device names to match your own setup before you use them.

## Layout

The folders mirror where each file lives on the box:

```
deployment/orange-pi-5-plus/
├── home/      → /home/<user>/  (desktop session, launcher scripts, user systemd units)
├── etc/       → /etc/          (system services, udev rules, hotspot, reliability)
└── boot/      → /boot/         (Armbian boot env)
```

Secrets and identifiers are replaced with placeholders: `<REDACTED>` for the hotspot password, `<AP-MAC>`, `<PHONE-MAC>`, `XX:XX:XX:XX:XX:XX` for the OBD adapter, `<ADB-PORT>` and `<ROOT-UUID>`. Fill in your own.

## Hardware

| Part | What's in the Jeep |
|---|---|
| Board | Orange Pi 5 Plus, RK3588 (4x Cortex-A76 @ 2.4 GHz + 4x Cortex-A55 @ 1.8 GHz), 16 GB RAM |
| Storage | 500 GB NVMe SSD, boots from NVMe (u-boot on SPI flash) |
| OS | Armbian 26.8 (Ubuntu 26.04), mainline kernel 6.18 (`current-rockchip64`) |
| GPU | Mali-G610 MC4 on the mainline `panthor` driver, Mesa 26 (GLES 3.1). No vendor blob |
| Display | 10.1-inch 1280x800 (Chi Mei PMX-G101) over HDMI (the board's second HDMI port, `HDMI-A-2`), no rotation, scale 1 |
| Touch | USB ILITEK multi-touch controller (`222a:0001`) |
| Audio | ES8388 codec on the board's 3.5 mm jack, PipeWire + WirePlumber |
| Wi-Fi / BT | Onboard RTL8852BE (`rtw89`). Wi-Fi also hosts the `OCTAVE-Jeep` hotspot for phone mirroring |
| Volume knob | ESP32 rotary encoder on UART `/dev/ttyS0` |
| OBD-II | Bluetooth ELM327-type adapter, opened over RFCOMM by OCTAVE (`rfcomm://<MAC>`) |
| Cooling | PWM fan under kernel thermal control |

## Software stack

- **Session:** GDM autologin → Hyprland 0.53.3. Waybar and hyprpaper run as systemd user services bound to `hyprland-session.target`.
- **OCTAVE:** the C++ build in `~/octave-cpp/build`, started by `exec-once = ~/.local/bin/octave-start.sh` in `hyprland.conf`. The launcher routes audio to the analog jack and restarts OCTAVE if it crashes. It stops after 5 crashes in 3 minutes, and a clean exit ends it.
- **Touch gestures:** the [hyprgrass](https://github.com/horriblename/hyprgrass) Hyprland plugin, built from source for Hyprland 0.53.3 and loaded from `~/.local/lib/hyprland/libhyprgrass.so`. Rebuild it whenever Hyprland is upgraded, or Hyprland may refuse to load it.
- **On-screen keyboard:** `~/.local/bin/osk`, a small GTK 3 keyboard toggled from the Waybar keyboard icon. It needs `python3-gi` and `wtype`.
- **Other packages the configs call:** kitty, wofi, nautilus, chromium, pavucontrol, pamixer, playerctl, grim, slurp, wl-clipboard, jq, libnotify, nm-applet, polkit-gnome, and a VictorMono Nerd Font for Waybar.

## Files

| Path on the box | What it does |
|---|---|
| `~/.config/hypr/hyprland.conf` | Hyprland config: monitor, autostart (including OCTAVE), keybinds, touch gestures, on-screen keyboard window rules |
| `~/.config/hypr/hyprpaper.conf` | Wallpaper |
| `~/.config/hypr/scripts/tap4.sh` | 4-finger tap: closes the focused window, or opens a terminal on an empty workspace |
| `~/.config/waybar/config`, `style.css` | 34 px top bar with volume, network, CPU/GPU temps and the keyboard toggle |
| `~/.config/systemd/user/hyprland-session.target` | Lets Waybar and hyprpaper start with the Hyprland session |
| `~/.local/bin/octave-start.sh` | Boot launcher: audio routing, single instance, crash-restart loop. Logs to `~/.local/state/octave/console.log` |
| `~/.local/bin/octave-rebuild-start.sh` | Menu entry "OCTAVE (Rebuild)": rebuilds `~/octave-cpp`, then starts or restarts OCTAVE on the new binary |
| `~/.local/bin/osk`, `osk-toggle` | On-screen keyboard and its Waybar toggle |
| `~/.config/OCTAVE/settingsConfigure.example.json` | The in-car OCTAVE settings only (dashboard, ESP32 port, OBD, phone mirror, window size). Not a full settings file: merge these keys into your own |
| `/etc/gdm3/custom.conf` | GDM autologin |
| `/etc/systemd/system/ap0-iface.service` | Creates the virtual `ap0` hotspot interface next to the Wi-Fi station, with a fixed MAC so Android keeps trusting it for wireless debugging |
| `/etc/systemd/system/hostapd-ap0.service`, `/etc/hostapd/ap0.conf` | The `OCTAVE-Jeep` 5 GHz WPA2 hotspot. The AP has to share a channel with the Wi-Fi the Pi joins at home |
| `/etc/systemd/system/dnsmasq-ap0.service`, `/etc/dnsmasq-ap0.conf` | DHCP only, with no gateway or DNS, so the phone keeps mobile data for internet. Gives the phone a fixed lease |
| `/etc/udev/rules.d/85-ap0-nm-unmanaged.rules` | Keeps NetworkManager off `ap0` |
| `/etc/systemd/system.conf.d/10-hw-watchdog.conf` | Hardware watchdog: the board resets itself if the kernel locks up |
| `/etc/sysctl.d/90-panic-reboot.conf` | Turns oopses and RCU stalls into a panic, and reboots 10 s after a panic |
| `/etc/systemd/journald.conf.d/10-persistent.conf` | Keeps the journal on disk so a freeze leaves evidence |
| `/etc/default/armbian-ramlog` | Turns off Armbian's RAM logging (same reason) |
| `/etc/systemd/logind.conf.d/no-sleep.conf` | Never suspend. `sleep.target` and `suspend.target` are also masked (`systemctl mask sleep.target suspend.target`) |
| `/boot/armbianEnv.txt` | Armbian boot env: device tree, `cma=256M` |

## Known gaps

- **No I2C overlay is enabled** in `armbianEnv.txt` (there's no `overlays=` line), so `/dev/i2c-2` doesn't exist and the BerryIMU and gesture sensor can't be read. Add the RK3588 I2C overlay for the bus the sensors are wired to before relying on them.
