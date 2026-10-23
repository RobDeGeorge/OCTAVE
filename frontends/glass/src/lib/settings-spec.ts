// The settings pages, grouped like the QML app's. Each item names the
// settingsManager property it shows and the save_* slot it writes; the
// control comes from `kind`. Anything describe() lists with a save slot and
// no row here lands in "Everything else" (Settings.svelte).
export type Kind = 'toggle' | 'slider' | 'chips' | 'text' | 'number'
export interface Item {
  prop: string; slot: string; label: string; kind: Kind; hint?: string
  min?: number; max?: number; step?: number; unit?: string
  options?: { value: string | number | boolean; label: string }[] | 'themes' | 'fonts'
  /** For 'number' and 'slider': the save slot takes this type. */
  float?: boolean
}
export interface Section { title: string; items: Item[]; special?: 'themes' | 'shiftlight' | 'homeParams' | 'gestures' | 'esp32' | 'imu' | 'network' | 'power' | 'about' | 'mediaFolder' }
export interface Group { id: string; title: string; icon: string; sections: Section[] }

const opts = (...pairs: [string | number | boolean, string][]) => pairs.map(([value, label]) => ({ value, label }))

export const groups: Group[] = [
  { id: 'display', title: 'Display', icon: 'sun', sections: [
    { title: 'Layout', items: [
      { prop: 'uiScale', slot: 'save_ui_scale', label: 'UI scale', kind: 'slider', min: 0.5, max: 2, step: 0.05, float: true, hint: 'How large the bundled UI draws everything' },
      { prop: 'screenWidth', slot: 'save_screen_width', label: 'Screen width', kind: 'number', unit: 'px' },
      { prop: 'screenHeight', slot: 'save_screen_height', label: 'Screen height', kind: 'number', unit: 'px' },
      { prop: 'bottomBarOrientation', slot: 'save_bottom_bar_orientation', label: 'Nav bar', kind: 'chips', options: opts(['bottom', 'Bottom'], ['side', 'Side']) },
      { prop: 'showBottomBarMediaControls', slot: 'save_show_bottom_bar_media_controls', label: 'Media keys in the nav bar', kind: 'toggle' },
      { prop: 'settingsLayoutStyle', slot: 'save_settings_layout_style', label: 'Settings layout', kind: 'chips', options: opts(['Sidebar', 'Sidebar'], ['Tiles', 'Tiles']) },
      { prop: 'backgroundGrid', slot: 'save_background_grid', label: 'Background layout', kind: 'chips', options: opts(['Normal', 'Normal'], ['2x2', '2x2'], ['4x4', '4x4']) },
    ] },
    { title: 'Appearance', special: 'themes', items: [
      { prop: 'fontSetting', slot: 'save_font_setting', label: 'Font', kind: 'chips', options: 'fonts' },
      { prop: 'colorTransitionMs', slot: 'save_color_transition_ms', label: 'Theme transition', kind: 'slider', min: 0, max: 2000, step: 50, unit: 'ms' },
      { prop: 'songLengthTransition', slot: 'save_song_length_transition', label: 'Fade over the song', kind: 'toggle', hint: 'Album colours cross-fade for the length of the track' },
    ] },
    { title: 'Clock', items: [
      { prop: 'showClock', slot: 'save_show_clock', label: 'Show clock', kind: 'toggle' },
      { prop: 'clockFormat24Hour', slot: 'save_clock_format', label: 'Time format', kind: 'chips', options: opts([true, '24-hour'], [false, '12-hour']) },
      { prop: 'clockShowSeconds', slot: 'save_clock_show_seconds', label: 'Show seconds', kind: 'toggle' },
      { prop: 'clockSizeAuto', slot: 'save_clock_size_auto', label: 'Automatic clock size', kind: 'toggle' },
      { prop: 'clockSize', slot: 'save_clock_size', label: 'Clock size', kind: 'slider', min: 8, max: 72, step: 1, unit: 'pt' },
    ] },
  ] },
  { id: 'media', title: 'Media', icon: 'music', sections: [
    { title: 'Library and playback', special: 'mediaFolder', items: [
      { prop: 'mediaFolder', slot: 'save_media_folder', label: 'Library folder', kind: 'text', hint: 'A folder on the head unit' },
      { prop: 'startUpVolume', slot: 'save_start_volume', label: 'Startup volume', kind: 'slider', min: 0, max: 1, step: 0.01, float: true },
      { prop: 'autoPlayOnStartup', slot: 'save_auto_play_on_startup', label: 'Autoplay on startup', kind: 'toggle' },
      { prop: 'persistShuffleState', slot: 'save_persist_shuffle_state', label: 'Remember shuffle', kind: 'toggle' },
      { prop: 'musicButtonDefaultPage', slot: 'save_music_button_default_page', label: 'Music button opens', kind: 'chips', options: opts(['mediaRoom', 'Now playing'], ['mediaPlayer', 'Library']) },
      { prop: 'returnToLibraryAfterSelection', slot: 'save_return_to_library_after_selection', label: 'Back to the library after choosing', kind: 'toggle' },
      { prop: 'textScrollSpeed', slot: 'save_text_scroll_speed', label: 'Text scroll speed', kind: 'slider', min: 1000, max: 10000, step: 500, unit: 'ms' },
    ] },
    { title: 'Now playing', items: [
      { prop: 'backgroundBlurRadius', slot: 'save_background_blur_radius', label: 'Album art blur', kind: 'slider', min: 0, max: 100, step: 1 },
      { prop: 'backgroundOverlayOpacity', slot: 'save_background_overlay_opacity', label: 'Dark overlay', kind: 'slider', min: 0, max: 100, step: 1, unit: '%' },
      { prop: 'roundedAlbumArt', slot: 'save_rounded_album_art', label: 'Rounded album art', kind: 'toggle' },
      { prop: 'albumArtCornerRadius', slot: 'save_album_art_corner_radius', label: 'Corner radius', kind: 'slider', min: 2, max: 32, step: 1, unit: 'px' },
      { prop: 'showAlbumArtShadow', slot: 'save_show_album_art_shadow', label: 'Album art shadow', kind: 'toggle' },
      { prop: 'vinylRecordMode', slot: 'save_vinyl_record_mode', label: 'Vinyl record', kind: 'toggle' },
      { prop: 'showWaveformVisualizer', slot: 'save_show_waveform_visualizer', label: 'Waveform', kind: 'toggle' },
      { prop: 'albumArtTransition', slot: 'save_album_art_transition', label: 'Track transition', kind: 'chips',
        options: opts(...(['Crossfade', 'Slide', 'Vinyl Lift', 'Dissolve', 'Flip', 'Coverflow', 'Conveyor', 'Stack', 'Depth', 'Swing'] as const).map((s) => [s, s] as [string, string])) },
      { prop: 'show3DButtonTilt', slot: 'save_show_3d_button_tilt', label: '3D button tilt', kind: 'toggle' },
      { prop: 'buttonTiltDuration', slot: 'save_button_tilt_duration', label: 'Tilt duration', kind: 'slider', min: 50, max: 500, step: 10, unit: 'ms' },
      { prop: 'show3DAlbumPreview', slot: 'save_show_3d_album_preview', label: '3D album preview', kind: 'toggle' },
      { prop: 'sideCardOpacity', slot: 'save_side_card_opacity', label: 'Side card opacity', kind: 'slider', min: 0.1, max: 1, step: 0.05, float: true },
      { prop: 'sideCardAngle', slot: 'save_side_card_angle', label: 'Side card angle', kind: 'slider', min: 5, max: 60, step: 1, unit: '°' },
    ] },
  ] },
  { id: 'vehicle', title: 'Vehicle', icon: 'gauge', sections: [
    { title: 'Connection', items: [
      { prop: 'obdBluetoothPort', slot: 'save_obd_bluetooth_port', label: 'Adapter', kind: 'text', hint: 'Bluetooth address or serial port; pick one on the Vehicle page' },
      { prop: 'obdFastMode', slot: 'save_obd_fast_mode', label: 'Fast mode', kind: 'toggle', hint: 'Fewer protocol checks, quicker readings' },
      { prop: 'obdAutoReconnectAttempts', slot: 'save_obd_auto_reconnect_attempts', label: 'Reconnect attempts', kind: 'slider', min: 0, max: 20, step: 1 },
    ] },
    { title: 'Gauges', special: 'homeParams', items: [
      { prop: 'gaugeTempUnit', slot: 'save_gauge_temp_unit', label: 'Temperature', kind: 'chips', options: opts(['C', '°C'], ['F', '°F']) },
      { prop: 'gaugeSpeedUnit', slot: 'save_gauge_speed_unit', label: 'Speed', kind: 'chips', options: opts(['mph', 'mph'], ['km/h', 'km/h']) },
      { prop: 'obdCardStyleCircular', slot: 'save_obd_card_style_circular', label: 'Round parameter cards', kind: 'toggle' },
      { prop: 'fuelTankCapacity', slot: 'save_fuel_tank_capacity', label: 'Fuel tank', kind: 'number', unit: 'gal', float: true },
    ] },
    { title: 'Shift light', special: 'shiftlight', items: [] },
    { title: 'Black box', items: [
      { prop: 'blackBoxEnabled', slot: 'save_black_box_enabled', label: 'Record every drive', kind: 'toggle' },
      { prop: 'blackBoxSweepAll', slot: 'save_black_box_sweep_all', label: 'Record every PID', kind: 'toggle', hint: 'Otherwise only what is on screen' },
      { prop: 'blackBoxMaxMB', slot: 'save_black_box_max_mb', label: 'Storage cap', kind: 'slider', min: 50, max: 5000, step: 50, unit: 'MB' },
    ] },
  ] },
  { id: 'accessories', title: 'Accessories', icon: 'bolt', sections: [
    { title: 'Volume knob', special: 'esp32', items: [
      { prop: 'esp32VolumeEnabled', slot: 'save_esp32_volume_enabled', label: 'Wireless knob', kind: 'toggle' },
      { prop: 'esp32VolumeStepSize', slot: 'save_esp32_volume_step_size', label: 'Step per click', kind: 'slider', min: 1, max: 10, step: 1, float: true, unit: '%' },
      { prop: 'esp32AutoReconnect', slot: 'save_esp32_auto_reconnect', label: 'Reconnect automatically', kind: 'toggle' },
      { prop: 'esp32LedSleepEnabled', slot: 'save_esp32_led_sleep_enabled', label: 'LED sleeps when idle', kind: 'toggle' },
      { prop: 'esp32LedColorMode', slot: 'save_esp32_led_color_mode', label: 'LED colour', kind: 'chips', options: opts(['theme', 'Theme'], ['static', 'Fixed']) },
      { prop: 'esp32LedStaticColor', slot: 'save_esp32_led_static_color', label: 'Fixed colour', kind: 'text', hint: '#RRGGBB' },
    ] },
    { title: 'IMU sensor', special: 'imu', items: [
      { prop: 'imuEnabled', slot: 'save_imu_enabled', label: 'BerryIMU', kind: 'toggle' },
      { prop: 'sensorEmitRate', slot: 'save_sensor_emit_rate', label: 'Update rate', kind: 'slider', min: 10, max: 120, step: 5, unit: 'Hz' },
      { prop: 'sensorDecimalPlaces', slot: 'save_sensor_decimal_places', label: 'Decimals', kind: 'slider', min: 0, max: 3, step: 1 },
      { prop: 'sensorTempUnit', slot: 'save_sensor_temp_unit', label: 'Temperature', kind: 'chips', options: opts(['C', '°C'], ['F', '°F']) },
      { prop: 'sensorAltitudeUnit', slot: 'save_sensor_altitude_unit', label: 'Altitude', kind: 'chips', options: opts(['m', 'm'], ['ft', 'ft']) },
    ] },
    { title: 'Gesture sensor', special: 'gestures', items: [
      { prop: 'gestureSensorEnabled', slot: 'save_gesture_sensor_enabled', label: 'PAJ7620 gestures', kind: 'toggle' },
      { prop: 'gestureVolumeStep', slot: 'save_gesture_volume_step', label: 'Volume step', kind: 'slider', min: 1, max: 25, step: 1, unit: '%' },
      { prop: 'gestureCooldown', slot: 'save_gesture_cooldown', label: 'Cooldown', kind: 'slider', min: 100, max: 2000, step: 50, unit: 'ms' },
    ] },
    { title: 'Phone dock', items: [
      { prop: 'phoneMirrorEnabled', slot: 'save_phone_mirror_enabled', label: 'Phone mirror', kind: 'toggle' },
      { prop: 'androidAutoEnabled', slot: 'save_android_auto_enabled', label: 'Android Auto', kind: 'toggle' },
      { prop: 'scrcpyDisplaySize', slot: 'save_scrcpy_display_size', label: 'Mirror display', kind: 'chips', options: opts(['auto', 'Fit the screen'], ['', 'Phone screen']) },
      { prop: 'scrcpyDisplayDpi', slot: 'save_scrcpy_display_dpi', label: 'Display DPI', kind: 'number', unit: 'dpi' },
      { prop: 'scrcpyBitRateAuto', slot: 'save_scrcpy_bit_rate_auto', label: 'Automatic bit rate', kind: 'toggle' },
      { prop: 'scrcpyBitRateUsb', slot: 'save_scrcpy_bit_rate_usb', label: 'Bit rate over USB', kind: 'slider', min: 1, max: 64, step: 1, unit: 'Mb/s' },
      { prop: 'scrcpyBitRateWifi', slot: 'save_scrcpy_bit_rate_wifi', label: 'Bit rate over Wi-Fi', kind: 'slider', min: 1, max: 64, step: 1, unit: 'Mb/s' },
      { prop: 'scrcpyAutoWifi', slot: 'save_scrcpy_auto_wifi', label: 'Switch to Wi-Fi automatically', kind: 'toggle' },
      { prop: 'scrcpyWirelessAddress', slot: 'save_scrcpy_wireless_address', label: 'Wireless address', kind: 'text', hint: 'ip:port' },
      { prop: 'scrcpyPhoneScreenOff', slot: 'save_scrcpy_phone_screen_off', label: 'Phone screen off while mirroring', kind: 'toggle' },
      { prop: 'scrcpyAudioGainDb', slot: 'save_scrcpy_audio_gain_db', label: 'Phone audio gain', kind: 'slider', min: -20, max: 20, step: 1, unit: 'dB', float: true },
      { prop: 'scrcpyAudioDuckEnabled', slot: 'save_scrcpy_audio_duck_enabled', label: 'Duck music for the phone', kind: 'toggle' },
      { prop: 'scrcpyAudioDuckLevelDb', slot: 'save_scrcpy_audio_duck_level_db', label: 'Duck level', kind: 'slider', min: -40, max: 0, step: 1, unit: 'dB', float: true },
    ] },
  ] },
  { id: 'device', title: 'Device', icon: 'phone', sections: [
    { title: 'Device', items: [
      { prop: 'deviceName', slot: 'save_device_name', label: 'Name', kind: 'text' },
    ] },
    { title: 'Network and updates', special: 'network', items: [] },
    { title: 'Power', special: 'power', items: [] },
    { title: 'About', special: 'about', items: [] },
  ] },
]

export const themeNames = ['SolarizedLight', 'NeonMatrix', 'CosmicVoyager', 'TechnoRetro', 'AutumnCascade', 'QuantumNebula', 'CrimsonEmber',
  'ForestEmerald', 'ArcticChill', 'AmberConsole', 'Parchment', 'MintGarden', 'RoseQuartz', 'LavenderMist', 'Sunshine', 'SlateCloud', 'Album Art Capture']

/** camelCase -> snake_case, the way the save slots are named. */
export function snake(prop: string): string {
  return prop.replace(/([A-Z]+)/g, (m) => '_' + m.toLowerCase()).replace(/^_/, '')
}
