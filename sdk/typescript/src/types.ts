// Generated from docs/api/*.json by scripts/generate-types.mjs; do not edit.
// Protocol version 3. Regenerate with `npm run generate`.

/** What `get` returns for a list-model property; the rows come from `rows` or a model subscription. */
export interface ModelSummary { model: true; count: number; roles: string[] }


/** androidAutoManager (AndroidAutoManager) */
export interface AndroidAutoManager {
  properties: {
    /** bool, constant */
    isDhuInstalled: boolean
    /** QString, constant */
    dhuPath: string
    /** bool, notify dhuEmbeddedChanged */
    isDhuEmbedded: boolean
    /** int, notify dhuWindowReady */
    dhuWindowHandle: number
    /** QObject*, constant */
    dhuCapture: unknown
  }
  methods: {
    getDhuInstallInstructions: { args: []; returns: string }
    launchDhuSeamless: { args: []; returns: boolean }
    sendDhuClick: { args: [x: number, y: number]; returns: void }
    closeDhu: { args: []; returns: void }
    cleanup: { args: []; returns: void }
  }
  signals: {
    connectionProgress: [message: string]
    error: [message: string]
    dhuWindowReady: [windowHandle: number]
    dhuEmbeddedChanged: [embedded: boolean]
  }
}

/** audioAnalyzer (AudioAnalyzer) */
export interface AudioAnalyzer {
  properties: {
    /** QVariantList, notify fftDataChanged */
    levels: unknown[]
    /** int, constant */
    numBars: number
  }
  methods: {
    setViewActive: { args: [active: boolean]; returns: void }
    setDemand: { args: [consumer: string, wanted: boolean]; returns: void }
    setPlaying: { args: [playing: boolean]; returns: void }
    setLocalFile: { args: [filePath: string]; returns: void }
    setPosition: { args: [positionMs: number]; returns: void }
    analyze_file: { args: [filePath: string]; returns: void }
    update_position: { args: [positionSeconds: number]; returns: void }
    set_active: { args: [active: boolean]; returns: void }
    get_current_levels: { args: []; returns: unknown[] }
    get_num_bars: { args: []; returns: number }
    is_analyzed: { args: []; returns: boolean }
    clear: { args: []; returns: void }
    set_live: { args: [live: boolean]; returns: void }
    update_live: { args: []; returns: void }
    feed_live_pcm: { args: [pcm: string, bufferedMs: number]; returns: void }
  }
  signals: {
    fftDataChanged: [levels: unknown[]]
    analysisComplete: []
    analysisStarted: []
  }
}

/** berryIMU (BerryIMUManager) */
export interface BerryIMUManager {
  properties: {
    /** bool, notify connectionStatusChanged */
    connected: boolean
    /** bool, notify hasTemperatureChanged */
    hasTemperature: boolean
  }
  methods: {
    getConnectionStatus: { args: []; returns: string }
    setEnabled: { args: [enabled: boolean]; returns: void }
    isEnabled: { args: []; returns: boolean }
    setEmitRate: { args: [hz: number]; returns: void }
    emitRate: { args: []; returns: number }
    setActive: { args: [active: boolean]; returns: void }
    setDemand: { args: [consumer: string, wanted: boolean]; returns: void }
    calibrateTare: { args: []; returns: void }
    resetTare: { args: []; returns: void }
    cleanup: { args: []; returns: void }
  }
  signals: {
    orientationChanged: [w: number, x: number, y: number, z: number]
    pitchChanged: [pitch: number]
    rollChanged: [roll: number]
    headingChanged: [heading: number]
    altitudeChanged: [altitude: number]
    accelMagnitudeChanged: [mag: number]
    lateralGChanged: [g: number]
    longitudinalGChanged: [g: number]
    baroTempChanged: [temp: number]
    connectionStatusChanged: [status: string]
    hasTemperatureChanged: [has: boolean]
  }
}

/** clock (Clock) */
export interface Clock {
  properties: {
    /** QString, notify timeChanged */
    time: string
  }
  methods: {
    update_time: { args: []; returns: void }
  }
  signals: {
    timeChanged: [timeStr: string]
  }
}

/** dashboardManager (DashboardManager) */
export interface DashboardManager {
  properties: {
    /** QVariantList, notify dashboardsChanged */
    dashboards: unknown[]
    /** QString, notify lastShareErrorChanged */
    lastShareError: string
    /** QString, notify lastErrorChanged */
    lastError: string
    /** QString, notify lastExportPathChanged */
    lastExportPath: string
    /** int, notify dashboardsChanged */
    hiddenBuiltInCount: number
    /** int, constant */
    supportedSchema: number
    /** QVariantMap, constant */
    widgetCatalog: Record<string, unknown>
  }
  methods: {
    saveDashboard: { args: [spec: Record<string, unknown>]; returns: string }
    deleteDashboard: { args: [id: string]; returns: boolean }
    restoreBuiltInDashboards: { args: []; returns: boolean }
    duplicateDashboard: { args: [sourceId: string, newLabel: string]; returns: string }
    exportDashboard: { args: [id: string]; returns: string }
    copyDashboardToClipboard: { args: [id: string]; returns: boolean }
    importDashboard: { args: [fileOrUrl: string]; returns: string }
    importDashboardFromClipboard: { args: []; returns: string }
    importDashboardFromText: { args: [json: string]; returns: string }
    refresh: { args: []; returns: void }
    loadDashboard: { args: [id: string]; returns: unknown }
    isBuiltIn: { args: [id: string]; returns: boolean }
    uniqueIdForLabel: { args: [label: string]; returns: string }
    normalizedSpec: { args: [spec: Record<string, unknown>]; returns: Record<string, unknown> }
    canPlace: { args: [cells: unknown[], gridColumns: number, gridRows: number, col: number, row: number, colSpan: number, rowSpan: number, exceptIndex: number]; returns: boolean }
    fitAt: { args: [cells: unknown[], gridColumns: number, gridRows: number, col: number, row: number, colSpan: number, rowSpan: number]; returns: Record<string, unknown> }
  }
  signals: {
    dashboardsChanged: []
    lastShareErrorChanged: []
    lastErrorChanged: []
    lastExportPathChanged: []
  }
}

/** diagnosticsManager (DiagnosticsManager) */
export interface DiagnosticsManager {
  properties: {
    /** QString, constant */
    logDir: string
    /** QString, constant */
    appVersion: string
    /** QString, constant */
    backendName: string
    /** QString, constant */
    deviceInfo: string
    /** QString, notify lastExportPathChanged */
    lastExportPath: string
    /** bool, constant */
    canOpenFolder: boolean
  }
  methods: {
    recentLogLines: { args: [maxLines: number] | []; returns: string }
    copyLogsToClipboard: { args: [maxLines: number] | []; returns: boolean }
    exportLogs: { args: []; returns: string }
    openLogFolder: { args: []; returns: boolean }
    licenseText: { args: [name: string]; returns: string }
  }
  signals: {
    lastExportPathChanged: []
  }
}

/** esp32VolumeManager (ESP32VolumeManager) */
export interface ESP32VolumeManager {
  properties: {
    /** bool, notify connectionStatusChanged */
    connected: boolean
    /** QString, notify connectionStatusChanged */
    connectionStatus: string
    /** QString, notify connectionDetailChanged */
    connectionDetail: string
    /** QVariantList, notify availablePortsChanged */
    availablePorts: unknown[]
  }
  methods: {
    connect_device: { args: []; returns: void }
    disconnect_device: { args: []; returns: void }
    get_available_ports: { args: []; returns: unknown[] }
    is_connected: { args: []; returns: boolean }
    get_connection_status: { args: []; returns: string }
    get_connection_detail: { args: []; returns: string }
    refresh_ports: { args: []; returns: void }
    reset_reconnect_attempts: { args: []; returns: void }
    cleanup: { args: []; returns: void }
  }
  signals: {
    connectionStatusChanged: [status: string]
    connectionDetailChanged: [detail: string]
    availablePortsChanged: [ports: unknown[]]
    volumeChangeRequested: [delta: number]
    muteToggleRequested: []
  }
}

/** fontCatalog (FontCatalog) */
export interface FontCatalog {
  properties: {
    /** QVariantList, constant */
    fonts: unknown[]
    /** QString, constant */
    symbolFamily: string
  }
  methods: {
  }
  signals: {
  }
}

/** frontendManager (FrontendManager) */
export interface FrontendManager {
  properties: {
    /** QVariantList, notify frontendsChanged */
    frontends: unknown[]
    /** QString, notify activeChanged */
    activeId: string
    /** QString, constant */
    runningId: string
    /** bool, notify activeChanged */
    forcedQml: boolean
    /** QString, notify activeChanged */
    forcedQmlReason: string
    /** QString, notify lastErrorChanged */
    lastError: string
  }
  methods: {
    installFromFolder: { args: [path: string]; returns: string }
    installFromZip: { args: [path: string]; returns: string }
    remove: { args: [id: string]; returns: boolean }
    setActive: { args: [id: string]; returns: boolean }
    clearForcedQml: { args: []; returns: void }
    manifestOf: { args: [id: string]; returns: Record<string, unknown> }
  }
  signals: {
    frontendsChanged: []
    activeChanged: []
    lastErrorChanged: []
    installed: [id: string]
  }
}

/** gestureSensor (GestureManager) */
export interface GestureManager {
  properties: {
    /** QString, notify connectionStatusChanged */
    connectionStatus: string
    /** QStringList, constant */
    gestures: string[]
    /** QVariantList, constant */
    actions: unknown[]
  }
  methods: {
    getConnectionStatus: { args: []; returns: string }
    setGestureAction: { args: [gesture: string, action: string]; returns: void }
    getGestureAction: { args: [gesture: string]; returns: string }
    resetMappingToDefaults: { args: []; returns: void }
    setEnabled: { args: [enabled: boolean]; returns: void }
    setCooldown: { args: [ms: number]; returns: void }
    isEnabled: { args: []; returns: boolean }
    cooldownMs: { args: []; returns: number }
    cleanup: { args: []; returns: void }
  }
  signals: {
    gestureDetected: [gesture: string]
    actionTriggered: [action: string]
    connectionStatusChanged: [status: string]
  }
}

/** mediaManager (MediaManager) */
export interface MediaManager {
  properties: {
    /** QVariantList, notify audioOutputsChanged */
    audioOutputs: unknown[]
    /** QString, notify audioOutputsChanged */
    currentAudioOutput: string
  }
  methods: {
    connect_settings_manager: { args: [arg: unknown]; returns: void }
    _save_playback_state: { args: []; returns: void }
    _clear_temp_files: { args: []; returns: void }
    flush_metadata_store: { args: []; returns: void }
    play_file: { args: [filename: string]; returns: void }
    next_track: { args: []; returns: void }
    previous_track: { args: []; returns: void }
    pause: { args: []; returns: void }
    toggle_play: { args: []; returns: void }
    setMuted: { args: [muted: boolean]; returns: void }
    toggle_shuffle: { args: []; returns: void }
    setVolume: { args: [volume: number]; returns: void }
    setDucking: { args: [factor: number]; returns: void }
    set_position: { args: [positionMs: number]; returns: void }
    extract_colors_from_album_art: { args: [filename: string]; returns: void }
    extract_colors_from_image: { args: [imagePath: string]; returns: void }
    scan_library: { args: [resetDisplayNames: boolean] | []; returns: void }
    set_library_root: { args: [path: string]; returns: void }
    strip_filenames: { args: []; returns: void }
    select_playlist: { args: [name: string]; returns: void }
    create_playlist: { args: [name: string]; returns: boolean }
    delete_playlist: { args: [name: string]; returns: boolean }
    move_song_to_playlist: { args: [filename: string, targetPlaylist: string]; returns: boolean }
    delete_song: { args: [filename: string]; returns: boolean }
    play_file_at_path: { args: [filePath: string]; returns: boolean }
    invalidate_stats_cache: { args: []; returns: void }
    get_media_files: { args: [emitSignal: boolean] | []; returns: string[] }
    get_formatted_duration: { args: [filename: string]; returns: string }
    get_band: { args: [filename: string]; returns: string }
    get_album: { args: [filename: string]; returns: string }
    get_album_art: { args: [filename: string]; returns: string }
    get_current_file: { args: []; returns: string }
    get_next_track_name: { args: []; returns: string }
    get_previous_track_name: { args: []; returns: string }
    get_next_track_album_art: { args: []; returns: string }
    get_previous_track_album_art: { args: []; returns: string }
    get_neighbor_album_arts: { args: []; returns: string[] }
    is_playing: { args: []; returns: boolean }
    is_paused: { args: []; returns: boolean }
    get_duration: { args: []; returns: number }
    get_position: { args: []; returns: number }
    is_muted: { args: []; returns: boolean }
    getVolume: { args: []; returns: number }
    is_shuffled: { args: []; returns: boolean }
    get_playlist_names: { args: []; returns: string[] }
    get_current_playlist_name: { args: []; returns: string }
    get_current_song_list: { args: []; returns: string[] }
    get_movable_playlist_names: { args: []; returns: string[] }
    get_song_playlist_name: { args: [filename: string]; returns: string }
    get_song_file_path: { args: [filename: string]; returns: string }
    get_full_file_path: { args: [filename: string]; returns: string }
    get_display_name: { args: [filename: string]; returns: string }
    get_default_media_dir: { args: []; returns: string }
    get_media_folder_name: { args: []; returns: string }
    browse_music_folders: { args: [path: string]; returns: Record<string, unknown> }
    get_total_duration: { args: []; returns: string }
    get_album_count: { args: []; returns: number }
    get_artist_count: { args: []; returns: number }
    sort_media_files: { args: [sortColumn: string, ascending: boolean] | [sortColumn: string]; returns: string[] }
  }
  signals: {
    playbackStateChanged: [state: number]
    playStateChanged: [playing: boolean]
    currentMediaChanged: [filename: string]
    mediaListChanged: [files: string[]]
    durationChanged: [durationMs: number]
    positionChanged: [positionMs: number]
    metadataChanged: [title: string, artist: string, album: string]
    durationFormatChanged: [formatted: string]
    volumeChanged: [volume: number]
    shuffleStateChanged: [shuffled: boolean]
    totalDurationChanged: [formatted: string]
    albumCountChanged: [count: number]
    artistCountChanged: [count: number]
    playlistsChanged: []
    currentPlaylistChanged: [name: string]
    scanProgress: [message: string]
    albumColorsExtracted: [jsonColors: string]
    songDeleted: [filename: string]
    audioOutputsChanged: []
  }
}

/** networkManager (NetworkManager) */
export interface NetworkManager {
  properties: {
    /** QString, notify networkNameChanged */
    networkName: string
    /** bool, notify isConnectedChanged */
    isConnected: boolean
    /** QString, notify updateStatusChanged */
    updateStatus: string
    /** QString, notify updateMessageChanged */
    updateMessage: string
    /** QString, notify remoteCommitChanged */
    remoteCommit: string
    /** QString, notify remoteCommitMsgChanged */
    remoteCommitMsg: string
    /** bool, notify workingTreeDirtyChanged */
    workingTreeDirty: boolean
    /** QString, notify selfUpdateStatusChanged */
    selfUpdateStatus: string
    /** QString, notify selfUpdateMessageChanged */
    selfUpdateMessage: string
    /** bool, notify canSelfUpdateChanged */
    canSelfUpdate: boolean
  }
  methods: {
    refreshNetwork: { args: []; returns: void }
    checkForUpdates: { args: []; returns: void }
    applySelfUpdate: { args: []; returns: void }
    restartApp: { args: []; returns: void }
    shutdown_device: { args: []; returns: void }
    cleanup: { args: []; returns: void }
  }
  signals: {
    networkNameChanged: [name: string]
    isConnectedChanged: [connected: boolean]
    updateStatusChanged: [status: string]
    updateMessageChanged: [message: string]
    remoteCommitChanged: [commit: string]
    remoteCommitMsgChanged: [msg: string]
    workingTreeDirtyChanged: [dirty: boolean]
    selfUpdateStatusChanged: [status: string]
    selfUpdateMessageChanged: [message: string]
    canSelfUpdateChanged: [canUpdate: boolean]
  }
}

/** nowPlaying (NowPlaying) */
export interface NowPlaying {
  properties: {
    /** QString, notify sourceChanged */
    source: string
    /** bool, notify playingChanged */
    playing: boolean
    /** bool, notify trackInfoChanged */
    hasTrack: boolean
    /** QString, notify trackInfoChanged */
    title: string
    /** QString, notify trackInfoChanged */
    artist: string
    /** QString, notify trackInfoChanged */
    album: string
    /** QString, notify artUrlChanged */
    artUrl: string
    /** int, notify artUrlChanged */
    artRevision: number
    /** int, notify durationChanged */
    durationMs: number
    /** int, notify positionChanged */
    positionMs: number
    /** bool, notify capabilitiesChanged */
    canSeek: boolean
    /** bool, notify capabilitiesChanged */
    canShuffle: boolean
    /** bool, notify shuffledChanged */
    shuffled: boolean
  }
  methods: {
    togglePlay: { args: []; returns: void }
    next: { args: []; returns: void }
    previous: { args: []; returns: void }
    seek: { args: [positionMs: number]; returns: void }
    toggleShuffle: { args: []; returns: void }
    extractArtColors: { args: []; returns: void }
  }
  signals: {
    sourceChanged: []
    playingChanged: []
    trackInfoChanged: []
    artUrlChanged: []
    durationChanged: []
    positionChanged: []
    capabilitiesChanged: []
    shuffledChanged: []
    trackChanged: []
  }
}

/** obdAdapterFinder (OBDAdapterFinder) */
export interface OBDAdapterFinder {
  properties: {
    /** bool, constant */
    supported: boolean
    /** bool, notify bluetoothOnChanged */
    bluetoothOn: boolean
    /** bool, notify scanningChanged */
    scanning: boolean
    /** QVariantList, notify devicesChanged */
    devices: unknown[]
    /** QString, notify busyChanged */
    busyAddress: string
    /** QString, notify statusChanged */
    status: string
    /** bool, notify statusChanged */
    statusIsError: boolean
  }
  methods: {
    startScan: { args: []; returns: void }
    stopScan: { args: []; returns: void }
    connectAdapter: { args: [address: string]; returns: void }
    cancel: { args: []; returns: void }
    powerOn: { args: []; returns: void }
  }
  signals: {
    bluetoothOnChanged: []
    scanningChanged: []
    devicesChanged: []
    busyChanged: []
    statusChanged: []
  }
}
export type OBDManager_ConnectionState = "Disconnected" | "Connecting" | "Connected" | "NoVehicle" | "Failed"

/** obdManager (OBDManager) */
export interface OBDManager {
  properties: {
    /** bool, notify connectionStatusChanged */
    connected: boolean
    /** QString, notify connectionStatusChanged */
    connectionStatus: string
    /** OBDManager::ConnectionState, notify connectionStatusChanged */
    connectionState: "Disconnected" | "Connecting" | "Connected" | "NoVehicle" | "Failed"
    /** QString, notify connectionStatusDetailChanged */
    connectionDetail: string
    /** int, notify connectionProgressChanged */
    connectionProgress: number
    /** QStringList, notify connectionLogChanged */
    connectionLog: string[]
    /** QVariantList, notify availableAdaptersChanged */
    availableAdapters: unknown[]
    /** QString, notify vinChanged */
    vin: string
    /** QString, notify vinChanged */
    vehicleMake: string
    /** int, notify vinChanged */
    vehicleModelYear: number
    /** QStringList, notify vehicleSupportedParametersChanged */
    vehicleSupportedParameters: string[]
    /** bool, notify vehicleSupportedParametersChanged */
    vehicleScanComplete: boolean
  }
  methods: {
    connect_obd: { args: []; returns: void }
    reconnect: { args: []; returns: void }
    force_connect: { args: []; returns: void }
    close: { args: []; returns: void }
    reset_connection: { args: []; returns: void }
    get_connection_status: { args: []; returns: string }
    getConnectionStatus: { args: []; returns: string }
    refresh_ports: { args: []; returns: void }
    get_available_ports: { args: []; returns: unknown[] }
    scan: { args: []; returns: void }
    availableAdapters: { args: []; returns: unknown[] }
    connect_to_adapter: { args: [identifier: string]; returns: void }
    platform_hint: { args: []; returns: string }
    scan_vehicle: { args: []; returns: void }
    is_scanning: { args: []; returns: boolean }
    get_all_parameter_names: { args: []; returns: string[] }
    get_supported_commands: { args: []; returns: unknown[] }
    enable_scanned_parameters: { args: [paramNames: unknown[]]; returns: void }
    enable_all_supported: { args: []; returns: void }
    enter_diagnostic_mode: { args: []; returns: void }
    exit_diagnostic_mode: { args: []; returns: void }
    is_diagnostic_mode: { args: []; returns: boolean }
    read_dtc: { args: []; returns: void }
    read_current_dtc: { args: []; returns: void }
    read_status: { args: []; returns: void }
    clear_dtc: { args: []; returns: void }
    read_freeze_frame: { args: []; returns: void }
    read_vin: { args: []; returns: void }
    get_dtc_codes: { args: []; returns: unknown[] }
    get_dtc_count: { args: []; returns: number }
    get_mil_status: { args: []; returns: boolean }
    get_freeze_frame: { args: []; returns: unknown[] }
    coolantTemp: { args: []; returns: number }
    voltage: { args: []; returns: number }
    engineLoad: { args: []; returns: number }
    throttlePosition: { args: []; returns: number }
    intakeTemp: { args: []; returns: number }
    timingAdvance: { args: []; returns: number }
    massAirFlow: { args: []; returns: number }
    speedMPH: { args: []; returns: number }
    rpm: { args: []; returns: number }
    airFuelRatio: { args: []; returns: number }
    fuelLevel: { args: []; returns: number }
    intakeManifoldPressure: { args: []; returns: number }
    shortTermFuelTrim: { args: []; returns: number }
    longTermFuelTrim: { args: []; returns: number }
    oxygenSensorVoltage: { args: []; returns: number }
    fuelPressure: { args: []; returns: number }
    engineOilTemp: { args: []; returns: number }
    ignitionTiming: { args: []; returns: number }
    refresh_values: { args: []; returns: void }
    open_bluetooth_settings: { args: []; returns: void }
    set_target_address: { args: [address: string]; returns: void }
    connect_direct: { args: [mac: string, channel: number] | [mac: string]; returns: void }
    set_auto_reconnect: { args: [enabled: boolean]; returns: void }
    set_connection_timeout: { args: [timeoutSeconds: number]; returns: void }
    check_device_presence: { args: []; returns: boolean }
    isConnected: { args: []; returns: boolean }
    clearConnectionLog: { args: []; returns: void }
    is_connected: { args: []; returns: boolean }
    setParameterDemand: { args: [consumer: string, paramIds: string[]]; returns: void }
    setBackgroundDemand: { args: [consumer: string, paramIds: string[]]; returns: void }
  }
  signals: {
    coolantTempChanged: [value: number]
    voltageChanged: [value: number]
    engineLoadChanged: [value: number]
    throttlePositionChanged: [value: number]
    intakeAirTempChanged: [value: number]
    timingAdvanceChanged: [value: number]
    massAirFlowChanged: [value: number]
    speedMPHChanged: [value: number]
    rpmChanged: [value: number]
    airFuelRatioChanged: [value: number]
    fuelLevelChanged: [value: number]
    intakeManifoldPressureChanged: [value: number]
    shortTermFuelTrimChanged: [value: number]
    longTermFuelTrimChanged: [value: number]
    oxygenSensorVoltageChanged: [value: number]
    fuelPressureChanged: [value: number]
    engineOilTempChanged: [value: number]
    ignitionTimingChanged: [value: number]
    runTimeChanged: [value: number]
    distanceWithMILChanged: [value: number]
    fuelRailPressureChanged: [value: number]
    fuelRailPressureDirectChanged: [value: number]
    barometricPressureChanged: [value: number]
    ambientAirTempChanged: [value: number]
    relativeThrottlePosChanged: [value: number]
    absoluteThrottlePosBChanged: [value: number]
    acceleratorPosChanged: [value: number]
    catalystTempB1S1Changed: [value: number]
    catalystTempB1S2Changed: [value: number]
    evapVaporPressureChanged: [value: number]
    shortFuelTrim2Changed: [value: number]
    longFuelTrim2Changed: [value: number]
    o2SensorB1S2Changed: [value: number]
    o2SensorB2S1Changed: [value: number]
    o2SensorB2S2Changed: [value: number]
    distanceSinceCodesCleared: [value: number]
    warmupsSinceCodesCleared: [value: number]
    absoluteLoadChanged: [value: number]
    commandedEGRChanged: [value: number]
    egrErrorChanged: [value: number]
    ethanoPercentChanged: [value: number]
    o2SensorB1S3Changed: [value: number]
    o2SensorB1S4Changed: [value: number]
    o2SensorB2S3Changed: [value: number]
    o2SensorB2S4Changed: [value: number]
    o2S1WRVoltageChanged: [value: number]
    o2S2WRVoltageChanged: [value: number]
    o2S3WRVoltageChanged: [value: number]
    o2S4WRVoltageChanged: [value: number]
    o2S5WRVoltageChanged: [value: number]
    o2S6WRVoltageChanged: [value: number]
    o2S7WRVoltageChanged: [value: number]
    o2S8WRVoltageChanged: [value: number]
    o2S1WRCurrentChanged: [value: number]
    o2S2WRCurrentChanged: [value: number]
    o2S3WRCurrentChanged: [value: number]
    o2S4WRCurrentChanged: [value: number]
    o2S5WRCurrentChanged: [value: number]
    o2S6WRCurrentChanged: [value: number]
    o2S7WRCurrentChanged: [value: number]
    o2S8WRCurrentChanged: [value: number]
    catalystTempB2S1Changed: [value: number]
    catalystTempB2S2Changed: [value: number]
    throttlePosCChanged: [value: number]
    acceleratorPosEChanged: [value: number]
    acceleratorPosFChanged: [value: number]
    throttleActuatorChanged: [value: number]
    evaporativePurgeChanged: [value: number]
    fuelRailPressureAbsChanged: [value: number]
    fuelInjectTimingChanged: [value: number]
    fuelRateChanged: [value: number]
    runTimeMILChanged: [value: number]
    timeSinceDTCClearedChanged: [value: number]
    maxMAFChanged: [value: number]
    fuelTypeChanged: [value: number]
    evapVaporPressureAbsChanged: [value: number]
    evapVaporPressureAltChanged: [value: number]
    shortO2TrimB1Changed: [value: number]
    longO2TrimB1Changed: [value: number]
    shortO2TrimB2Changed: [value: number]
    longO2TrimB2Changed: [value: number]
    relativeAccelPosChanged: [value: number]
    hybridBatteryRemainingChanged: [value: number]
    elmVoltageChanged: [value: number]
    connectionStatusChanged: [status: string]
    connectionLostUnexpectedly: [reason: string]
    connectionStatusDetailChanged: [detail: string]
    connectionProgressChanged: [progress: number]
    devicePresenceChanged: [present: boolean]
    availablePortsChanged: [ports: unknown[]]
    availableAdaptersChanged: [adapters: unknown[]]
    supportedCommandsChanged: [commands: unknown[]]
    scanProgressChanged: [progress: number, message: string]
    scanCompleteChanged: [supportedParams: unknown[]]
    scanOutputChanged: [output: string]
    vehicleSupportedParametersChanged: []
    dtcCodesChanged: [codes: unknown[]]
    dtcCountChanged: [count: number]
    milStatusChanged: [status: boolean]
    dtcClearResult: [success: boolean, message: string]
    freezeFrameChanged: [data: unknown[]]
    obdParameterChanged: [paramId: string, value: number]
    parameterSampled: [paramId: string, value: number]
    vinChanged: [vin: string]
    connectionLogLineAppended: [line: string]
    connectionLogChanged: []
  }
}

/** paramStore (ParameterStore) */
export interface ParameterStore {
  properties: {
  }
  methods: {
    ingest: { args: [id: string, value: number]; returns: void }
    get: { args: [id: string]; returns: unknown }
    snapshot: { args: []; returns: Record<string, unknown> }
  }
  signals: {
    changed: [ids: string[]]
  }
}

/** parameterCatalog (ParameterCatalog) */
export interface ParameterCatalog {
  properties: {
    /** QVariantList, constant */
    parameters: unknown[]
    /** QVariantList, notify displayParametersChanged */
    displayParameters: unknown[]
  }
  methods: {
    toDisplay: { args: [id: string, value: unknown]; returns: unknown }
    contains: { args: [id: string]; returns: boolean }
    info: { args: [id: string]; returns: Record<string, unknown> }
  }
  signals: {
    displayParametersChanged: []
  }
}

/** phoneMirrorManager (PhoneMirrorManager) */
export interface PhoneMirrorManager {
  properties: {
    /** QString, constant */
    adbPath: string
    /** bool, constant */
    nativeAvailable: boolean
    /** QString, constant */
    serverVersion: string
    /** bool, notify isRunningChanged */
    isRunning: boolean
    /** QString, notify displaySizeChanged */
    displaySize: string
    /** QString, notify frameSizeChanged */
    activeDisplaySize: string
    /** QObject*, writable, notify videoSinkChanged */
    videoSink: unknown
    /** int, notify frameSizeChanged */
    frameWidth: number
    /** int, notify frameSizeChanged */
    frameHeight: number
    /** bool, notify audioActiveChanged */
    audioActive: boolean
    /** bool, notify audioPlayingChanged */
    audioPlaying: boolean
    /** bool, notify phoneAsleepChanged */
    phoneAsleep: boolean
    /** bool, notify phoneInUseChanged */
    phoneInUse: boolean
    /** QString, notify wirelessAddressChanged */
    wirelessAddress: string
    /** QString, notify connectionTypeChanged */
    connectionType: string
    /** bool, notify wirelessBusyChanged */
    wirelessBusy: boolean
    /** QStringList, notify qrPairingChanged */
    qrPairingRows: string[]
    /** bool, notify qrPairingChanged */
    qrPairingActive: boolean
    /** int, notify currentBitRateChanged */
    currentBitRate: number
    /** bool, notify mediaChanged */
    mediaAvailable: boolean
    /** bool, notify mediaChanged */
    mediaPlaying: boolean
    /** QString, notify mediaChanged */
    mediaTitle: string
    /** QString, notify mediaChanged */
    mediaArtist: string
    /** QString, notify mediaChanged */
    mediaAlbum: string
    /** QString, notify mediaChanged */
    mediaApp: string
    /** int, notify mediaChanged */
    mediaDuration: number
    /** bool, notify mediaChanged */
    mediaCanSeek: boolean
    /** QString, notify mediaArtChanged */
    mediaArtUrl: string
    /** bool, notify mediaActiveChanged */
    mediaActive: boolean
    /** QVariantList, notify mediaQueueChanged */
    mediaQueue: unknown[]
    /** qlonglong, notify mediaQueueChanged */
    mediaQueueActiveId: number
    /** QString, notify mediaQueueChanged */
    mediaQueueTitle: string
    /** QString, notify deviceStateChanged */
    deviceState: string
    /** QString, notify lastErrorKindChanged */
    lastErrorKind: string
    /** QString, notify sessionStateChanged */
    sessionState: string
    /** QString, notify deviceStateChanged */
    deviceHint: string
    /** QVariantList, notify deviceStateChanged */
    devices: unknown[]
    /** QString, notify mirrorNoticeChanged */
    mirrorNotice: string
  }
  methods: {
    setVolume: { args: [volume: number]; returns: void }
    setMuted: { args: [muted: boolean]; returns: void }
    setAudioGain: { args: [gain: number]; returns: void }
    setAudioDuckEnabled: { args: [enabled: boolean]; returns: void }
    setAudioDuckLevel: { args: [level: number]; returns: void }
    setAudioOutputDevice: { args: [id: string]; returns: void }
    setDisplaySize: { args: [size: string]; returns: void }
    setViewSize: { args: [width: number, height: number]; returns: void }
    setDisplayDpi: { args: [dpi: number]; returns: void }
    setBitRateUsb: { args: [mbps: number]; returns: void }
    setBitRateWifi: { args: [mbps: number]; returns: void }
    setBitRateAuto: { args: [enabled: boolean]; returns: void }
    setAutoWifi: { args: [enabled: boolean]; returns: void }
    setPhoneScreenOff: { args: [off: boolean]; returns: void }
    wakePhone: { args: []; returns: void }
    resumeMirroring: { args: []; returns: void }
    environmentOk: { args: []; returns: boolean }
    getInstallInstructions: { args: []; returns: string }
    getDeviceState: { args: []; returns: string }
    refreshDeviceState: { args: []; returns: void }
    setViewActive: { args: [active: boolean]; returns: void }
    describeDeviceState: { args: [state: string]; returns: string }
    hasConnectedDevice: { args: []; returns: boolean }
    selectDevice: { args: [serial: string]; returns: void }
    dismissNotice: { args: []; returns: void }
    injectTouch: { args: [pointerId: number, action: number, relX: number, relY: number]; returns: void }
    injectKey: { args: [keycode: number]; returns: void }
    pressHome: { args: []; returns: void }
    pressBack: { args: []; returns: void }
    pressAppSwitch: { args: []; returns: void }
    startScrcpy: { args: []; returns: void }
    stopScrcpy: { args: []; returns: void }
    cleanup: { args: []; returns: void }
    setWirelessAddress: { args: [address: string]; returns: void }
    forgetWireless: { args: []; returns: void }
    startQrPairing: { args: []; returns: void }
    cancelQrPairing: { args: []; returns: void }
    mediaPosition: { args: []; returns: number }
    mediaPlayPause: { args: []; returns: void }
    mediaPlay: { args: []; returns: void }
    mediaPause: { args: []; returns: void }
    mediaNext: { args: []; returns: void }
    mediaPrevious: { args: []; returns: void }
    mediaSeek: { args: [positionMs: number]; returns: void }
    mediaSkipToQueueItem: { args: [id: number]; returns: void }
    releaseMedia: { args: [pausePhone: boolean]; returns: void }
  }
  signals: {
    scrcpyStarted: [handle: number]
    scrcpyStopped: []
    scrcpyError: [message: string]
    lastErrorKindChanged: []
    sessionStateChanged: []
    isRunningChanged: []
    displaySizeChanged: []
    frameSizeChanged: [width: number, height: number]
    frameReady: []
    videoSinkChanged: []
    audioActiveChanged: [active: boolean]
    audioPlayingChanged: [playing: boolean]
    audioPlayed: [pcm: string, bufferedMs: number]
    phoneAsleepChanged: [asleep: boolean]
    phoneInUseChanged: [inUse: boolean]
    duckingChanged: [factor: number]
    wirelessAddressChanged: [address: string]
    connectionTypeChanged: []
    wirelessBusyChanged: []
    wirelessResult: [ok: boolean, message: string]
    qrPairingChanged: []
    currentBitRateChanged: []
    mediaChanged: []
    mediaTrackChanged: []
    mediaArtChanged: []
    mediaQueueChanged: []
    mediaActiveChanged: [active: boolean]
    deviceStateChanged: []
    mirrorNoticeChanged: []
  }
}

/** settingsManager (SettingsManager) */
export interface SettingsManager {
  properties: {
    /** QString, notify deviceNameChanged */
    deviceName: string
    /** QString, notify fontSettingChanged */
    fontSetting: string
    /** QString, notify themeSettingChanged */
    themeSetting: string
    /** float, notify uiScaleChanged */
    uiScale: number
    /** int, notify colorTransitionMsChanged */
    colorTransitionMs: number
    /** bool, notify songLengthTransitionChanged */
    songLengthTransition: boolean
    /** bool, notify showClockChanged */
    showClock: boolean
    /** bool, notify clockFormatChanged */
    clockFormat24Hour: boolean
    /** bool, notify clockShowSecondsChanged */
    clockShowSeconds: boolean
    /** int, notify clockSizeChanged */
    clockSize: number
    /** bool, notify clockSizeAutoChanged */
    clockSizeAuto: boolean
    /** QString, notify backgroundGridChanged */
    backgroundGrid: string
    /** int, notify screenWidthChanged */
    screenWidth: number
    /** int, notify screenHeightChanged */
    screenHeight: number
    /** int, notify backgroundBlurRadiusChanged */
    backgroundBlurRadius: number
    /** QString, notify bottomBarOrientationChanged */
    bottomBarOrientation: string
    /** bool, notify showBottomBarMediaControlsChanged */
    showBottomBarMediaControls: boolean
    /** QString, notify settingsLayoutStyleChanged */
    settingsLayoutStyle: string
    /** QVariantMap, notify albumArtColorsChanged */
    albumArtColors: Record<string, unknown>
    /** QString, notify settingsMenuVisibilityChanged */
    settingsMenuVisibility: string
    /** float, notify startUpVolumeChanged */
    startUpVolume: number
    /** int, notify currentVolumeChanged */
    currentVolume: number
    /** QString, notify mediaFolderChanged */
    mediaFolder: string
    /** bool, notify autoPlayOnStartupChanged */
    autoPlayOnStartup: boolean
    /** bool, notify persistShuffleStateChanged */
    persistShuffleState: boolean
    /** bool, notify lastShuffleStateChanged */
    lastShuffleState: boolean
    /** QString, notify lastPlayedSongChanged */
    lastPlayedSong: string
    /** int, notify lastPlayedPositionChanged */
    lastPlayedPosition: number
    /** QString, notify lastPlayedPlaylistChanged */
    lastPlayedPlaylist: string
    /** QString, notify musicButtonDefaultPageChanged */
    musicButtonDefaultPage: string
    /** bool, notify returnToLibraryAfterSelectionChanged */
    returnToLibraryAfterSelection: boolean
    /** bool, notify showWaveformVisualizerChanged */
    showWaveformVisualizer: boolean
    /** bool, notify show3DButtonTiltChanged */
    show3DButtonTilt: boolean
    /** bool, notify show3DAlbumPreviewChanged */
    show3DAlbumPreview: boolean
    /** bool, notify roundedAlbumArtChanged */
    roundedAlbumArt: boolean
    /** int, notify albumArtCornerRadiusChanged */
    albumArtCornerRadius: number
    /** bool, notify showAlbumArtShadowChanged */
    showAlbumArtShadow: boolean
    /** bool, notify vinylRecordModeChanged */
    vinylRecordMode: boolean
    /** QString, notify albumArtTransitionChanged */
    albumArtTransition: string
    /** QString, notify audioOutputDeviceChanged */
    audioOutputDevice: string
    /** int, notify backgroundOverlayOpacityChanged */
    backgroundOverlayOpacity: number
    /** double, notify sideCardOpacityChanged */
    sideCardOpacity: number
    /** int, notify sideCardAngleChanged */
    sideCardAngle: number
    /** int, notify buttonTiltDurationChanged */
    buttonTiltDuration: number
    /** int, notify textScrollSpeedChanged */
    textScrollSpeed: number
    /** QString, notify obdBluetoothPortChanged */
    obdBluetoothPort: string
    /** QVariantList, notify obdSavedAdaptersChanged */
    obdSavedAdapters: unknown[]
    /** bool, notify obdFastModeChanged */
    obdFastMode: boolean
    /** bool, notify obdCardStyleCircularChanged */
    obdCardStyleCircular: boolean
    /** QString, notify gaugeTempUnitChanged */
    gaugeTempUnit: string
    /** QString, notify gaugeSpeedUnitChanged */
    gaugeSpeedUnit: string
    /** int, notify obdAutoReconnectAttemptsChanged */
    obdAutoReconnectAttempts: number
    /** float, notify fuelTankCapacityChanged */
    fuelTankCapacity: number
    /** bool, notify blackBoxEnabledChanged */
    blackBoxEnabled: boolean
    /** bool, notify blackBoxSweepAllChanged */
    blackBoxSweepAll: boolean
    /** int, notify blackBoxMaxMBChanged */
    blackBoxMaxMB: number
    /** QVariantList, notify homeOBDParametersChanged */
    homeOBDParameters: unknown[]
    /** int, constant */
    maxHomeObdParameters: number
    /** QVariantList, notify customThemesChanged */
    customThemes: unknown[]
    /** QVariantMap, notify customThemesChanged */
    customThemeData: Record<string, unknown>
    /** QVariantList, constant */
    nowPlayingPresets: unknown[]
    /** QVariantList, notify directoryHistoryChanged */
    directoryHistory: unknown[]
    /** QString, notify windowStateChanged */
    windowState: string
    /** QString, notify lastSettingsSectionChanged */
    lastSettingsSection: string
    /** bool, notify androidAutoEnabledChanged */
    androidAutoEnabled: boolean
    /** bool, notify phoneMirrorEnabledChanged */
    phoneMirrorEnabled: boolean
    /** QString, notify scrcpyDisplaySizeChanged */
    scrcpyDisplaySize: string
    /** double, notify scrcpyAudioGainChanged */
    scrcpyAudioGain: number
    /** double, notify scrcpyAudioGainChanged */
    scrcpyAudioGainDb: number
    /** double, notify scrcpyAudioDuckLevelChanged */
    scrcpyAudioDuckLevelDb: number
    /** bool, notify scrcpyAudioDuckEnabledChanged */
    scrcpyAudioDuckEnabled: boolean
    /** double, notify scrcpyAudioDuckLevelChanged */
    scrcpyAudioDuckLevel: number
    /** bool, notify scrcpyPhoneScreenOffChanged */
    scrcpyPhoneScreenOff: boolean
    /** int, notify scrcpyDisplayDpiChanged */
    scrcpyDisplayDpi: number
    /** int, notify scrcpyBitRateUsbChanged */
    scrcpyBitRateUsb: number
    /** int, notify scrcpyBitRateWifiChanged */
    scrcpyBitRateWifi: number
    /** bool, notify scrcpyAutoWifiChanged */
    scrcpyAutoWifi: boolean
    /** bool, notify scrcpyBitRateAutoChanged */
    scrcpyBitRateAuto: boolean
    /** QString, notify scrcpyWirelessAddressChanged */
    scrcpyWirelessAddress: string
    /** bool, notify esp32VolumeEnabledChanged */
    esp32VolumeEnabled: boolean
    /** QString, notify esp32VolumePortChanged */
    esp32VolumePort: string
    /** double, notify esp32VolumeStepSizeChanged */
    esp32VolumeStepSize: number
    /** bool, notify esp32AutoReconnectChanged */
    esp32AutoReconnect: boolean
    /** bool, notify esp32LedSleepEnabledChanged */
    esp32LedSleepEnabled: boolean
    /** QString, notify esp32LedColorModeChanged */
    esp32LedColorMode: string
    /** QString, notify esp32LedStaticColorChanged */
    esp32LedStaticColor: string
    /** bool, notify imuEnabledChanged */
    imuEnabled: boolean
    /** int, notify sensorEmitRateChanged */
    sensorEmitRate: number
    /** int, notify sensorDecimalPlacesChanged */
    sensorDecimalPlaces: number
    /** QString, notify sensorTempUnitChanged */
    sensorTempUnit: string
    /** QString, notify sensorAltitudeUnitChanged */
    sensorAltitudeUnit: string
    /** QVariantMap, notify sensorCardsChanged */
    sensorCards: Record<string, unknown>
    /** bool, notify gestureSensorEnabledChanged */
    gestureSensorEnabled: boolean
    /** int, notify gestureVolumeStepChanged */
    gestureVolumeStep: number
    /** int, notify gestureCooldownChanged */
    gestureCooldown: number
    /** QVariantList, notify pinnedSettingsChanged */
    pinnedSettings: unknown[]
  }
  methods: {
    save_device_name: { args: [name: string]; returns: void }
    save_font_setting: { args: [font: string]; returns: void }
    save_theme_setting: { args: [theme: string]; returns: void }
    save_ui_scale: { args: [scale: number]; returns: void }
    save_color_transition_ms: { args: [ms: number]; returns: void }
    save_song_length_transition: { args: [enabled: boolean]; returns: void }
    save_show_clock: { args: [show: boolean]; returns: void }
    save_clock_format: { args: [is24hour: boolean]; returns: void }
    save_clock_show_seconds: { args: [show: boolean]; returns: void }
    save_clock_size: { args: [size: number]; returns: void }
    save_clock_size_auto: { args: [autoSize: boolean]; returns: void }
    save_background_grid: { args: [grid: string]; returns: void }
    save_screen_width: { args: [width: number]; returns: void }
    save_screen_height: { args: [height: number]; returns: void }
    save_background_blur_radius: { args: [radius: number]; returns: void }
    save_bottom_bar_orientation: { args: [orientation: string]; returns: void }
    save_show_bottom_bar_media_controls: { args: [show: boolean]; returns: void }
    save_settings_layout_style: { args: [style: string]; returns: void }
    set_album_art_colors: { args: [colorsJson: string]; returns: void }
    save_start_volume: { args: [volume: number]; returns: void }
    setCurrentVolume: { args: [volume: number]; returns: void }
    save_media_folder: { args: [folderPath: string]; returns: boolean }
    save_media_folder_url: { args: [url: string]; returns: boolean }
    save_auto_play_on_startup: { args: [enabled: boolean]; returns: void }
    save_persist_shuffle_state: { args: [enabled: boolean]; returns: void }
    save_last_shuffle_state: { args: [enabled: boolean]; returns: void }
    save_last_played_song: { args: [filename: string]; returns: void }
    save_last_played_position: { args: [positionMs: number]; returns: void }
    save_last_played_playlist: { args: [playlistName: string]; returns: void }
    save_playback_state: { args: [song: string, positionMs: number, playlist: string]; returns: void }
    save_music_button_default_page: { args: [page: string]; returns: void }
    save_return_to_library_after_selection: { args: [returnToLibrary: boolean]; returns: void }
    save_show_waveform_visualizer: { args: [enabled: boolean]; returns: void }
    save_show_3d_button_tilt: { args: [enabled: boolean]; returns: void }
    save_show_3d_album_preview: { args: [enabled: boolean]; returns: void }
    save_rounded_album_art: { args: [enabled: boolean]; returns: void }
    save_album_art_corner_radius: { args: [radius: number]; returns: void }
    save_show_album_art_shadow: { args: [enabled: boolean]; returns: void }
    save_vinyl_record_mode: { args: [enabled: boolean]; returns: void }
    save_album_art_transition: { args: [transition: string]; returns: void }
    save_audio_output_device: { args: [id: string]; returns: void }
    save_background_overlay_opacity: { args: [opacity: number]; returns: void }
    save_side_card_opacity: { args: [opacity: number]; returns: void }
    save_side_card_angle: { args: [angle: number]; returns: void }
    save_button_tilt_duration: { args: [duration: number]; returns: void }
    save_text_scroll_speed: { args: [speed: number]; returns: void }
    save_obd_bluetooth_port: { args: [port: string]; returns: void }
    add_obd_saved_adapter: { args: [name: string, mac: string]; returns: void }
    remove_obd_saved_adapter: { args: [mac: string]; returns: void }
    save_obd_fast_mode: { args: [enabled: boolean]; returns: void }
    save_obd_card_style_circular: { args: [circular: boolean]; returns: void }
    save_gauge_temp_unit: { args: [unit: string]; returns: void }
    save_gauge_speed_unit: { args: [unit: string]; returns: void }
    save_obd_auto_reconnect_attempts: { args: [attempts: number]; returns: void }
    save_fuel_tank_capacity: { args: [capacity: number]; returns: void }
    save_black_box_enabled: { args: [enabled: boolean]; returns: void }
    save_black_box_sweep_all: { args: [enabled: boolean]; returns: void }
    save_black_box_max_mb: { args: [megabytes: number]; returns: void }
    save_obd_parameter_enabled: { args: [parameter: string, enabled: boolean]; returns: void }
    save_supported_obd_parameters: { args: [parameters: unknown[]]; returns: void }
    save_home_obd_parameters: { args: [parameters: unknown[]]; returns: void }
    add_home_obd_parameter: { args: [id: string, index: number] | [id: string]; returns: boolean }
    remove_home_obd_parameter: { args: [id: string]; returns: boolean }
    move_home_obd_parameter: { args: [from: number, to: number]; returns: boolean }
    save_to_directory_history: { args: [folderPath: string]; returns: void }
    remove_from_directory_history: { args: [folderPath: string]; returns: void }
    save_custom_theme: { args: [name: string, theme: Record<string, unknown>, albumArtSource: string] | [name: string, theme: Record<string, unknown>]; returns: void }
    delete_custom_theme: { args: [name: string]; returns: void }
    save_window_state: { args: [state: string]; returns: void }
    set_last_settings_section: { args: [section: string]; returns: void }
    save_settings_section_visibility: { args: [section: string, visible: boolean]; returns: void }
    save_android_auto_enabled: { args: [enabled: boolean]; returns: void }
    save_phone_mirror_enabled: { args: [enabled: boolean]; returns: void }
    save_scrcpy_display_size: { args: [size: string]; returns: void }
    save_scrcpy_audio_gain: { args: [gain: number]; returns: void }
    save_scrcpy_audio_duck_enabled: { args: [enabled: boolean]; returns: void }
    save_scrcpy_audio_duck_level: { args: [level: number]; returns: void }
    save_scrcpy_audio_gain_db: { args: [db: number]; returns: void }
    save_scrcpy_audio_duck_level_db: { args: [db: number]; returns: void }
    save_scrcpy_phone_screen_off: { args: [off: boolean]; returns: void }
    save_scrcpy_display_dpi: { args: [dpi: number]; returns: void }
    save_scrcpy_bit_rate_usb: { args: [mbps: number]; returns: void }
    save_scrcpy_bit_rate_wifi: { args: [mbps: number]; returns: void }
    save_scrcpy_auto_wifi: { args: [enabled: boolean]; returns: void }
    save_scrcpy_bit_rate_auto: { args: [enabled: boolean]; returns: void }
    save_scrcpy_wireless_address: { args: [address: string]; returns: void }
    save_esp32_volume_enabled: { args: [enabled: boolean]; returns: void }
    save_esp32_volume_port: { args: [port: string]; returns: void }
    save_esp32_volume_step_size: { args: [stepSize: number]; returns: void }
    save_esp32_auto_reconnect: { args: [enabled: boolean]; returns: void }
    save_esp32_led_sleep_enabled: { args: [enabled: boolean]; returns: void }
    save_esp32_led_color_mode: { args: [mode: string]; returns: void }
    save_esp32_led_static_color: { args: [color: string]; returns: void }
    save_imu_enabled: { args: [enabled: boolean]; returns: void }
    save_sensor_emit_rate: { args: [hz: number]; returns: void }
    save_sensor_decimal_places: { args: [places: number]; returns: void }
    save_sensor_temp_unit: { args: [unit: string]; returns: void }
    save_sensor_altitude_unit: { args: [unit: string]; returns: void }
    save_sensor_card_visible: { args: [card: string, visible: boolean]; returns: void }
    save_gesture_sensor_enabled: { args: [enabled: boolean]; returns: void }
    save_gesture_action: { args: [gesture: string, action: string]; returns: void }
    save_gesture_mapping: { args: [mapping: Record<string, unknown>]; returns: void }
    reset_gesture_mapping: { args: []; returns: void }
    save_gesture_volume_step: { args: [step: number]; returns: void }
    save_gesture_cooldown: { args: [ms: number]; returns: void }
    toggle_pin_setting: { args: [settingId: string]; returns: void }
    reset_to_defaults: { args: []; returns: void }
    save_setting: { args: [key: string, value: unknown]; returns: void }
    apply_settings: { args: [values: Record<string, unknown>]; returns: Record<string, unknown> }
    applyNowPlayingPreset: { args: [label: string]; returns: boolean }
    get_setting: { args: [key: string]; returns: unknown }
    get_setting_with_default: { args: [key: string, defaultValue: unknown]; returns: unknown }
    get_obd_parameter_enabled: { args: [parameter: string, defaultVal: boolean] | [parameter: string]; returns: boolean }
    get_supported_obd_parameters: { args: []; returns: unknown[] }
    get_directory_history: { args: []; returns: unknown[] }
    get_home_obd_parameters: { args: []; returns: unknown[] }
    get_auto_play_on_startup: { args: []; returns: boolean }
    get_persist_shuffle_state: { args: []; returns: boolean }
    get_last_shuffle_state: { args: []; returns: boolean }
    get_last_played_song: { args: []; returns: string }
    get_last_played_position: { args: []; returns: number }
    get_last_played_playlist: { args: []; returns: string }
    get_last_settings_section: { args: []; returns: string }
    get_window_state: { args: []; returns: string }
    get_music_button_default_page: { args: []; returns: string }
    get_return_to_library_after_selection: { args: []; returns: boolean }
    flushPendingSave: { args: []; returns: void }
    get_android_auto_enabled: { args: []; returns: boolean }
    get_phone_mirror_enabled: { args: []; returns: boolean }
    get_scrcpy_display_size: { args: []; returns: string }
    get_scrcpy_audio_gain: { args: []; returns: number }
    get_scrcpy_audio_duck_enabled: { args: []; returns: boolean }
    get_scrcpy_audio_duck_level: { args: []; returns: number }
    get_scrcpy_phone_screen_off: { args: []; returns: boolean }
    get_scrcpy_display_dpi: { args: []; returns: number }
    get_scrcpy_bit_rate_usb: { args: []; returns: number }
    get_scrcpy_bit_rate_wifi: { args: []; returns: number }
    get_scrcpy_auto_wifi: { args: []; returns: boolean }
    get_scrcpy_bit_rate_auto: { args: []; returns: boolean }
    get_scrcpy_wireless_address: { args: []; returns: string }
    get_esp32_volume_enabled: { args: []; returns: boolean }
    get_esp32_volume_port: { args: []; returns: string }
    get_esp32_volume_step_size: { args: []; returns: number }
    get_esp32_auto_reconnect: { args: []; returns: boolean }
    get_esp32_led_sleep_enabled: { args: []; returns: boolean }
    get_esp32_led_color_mode: { args: []; returns: string }
    get_esp32_led_static_color: { args: []; returns: string }
    get_show_waveform_visualizer: { args: []; returns: boolean }
    get_imu_enabled: { args: []; returns: boolean }
    get_gesture_sensor_enabled: { args: []; returns: boolean }
    get_gesture_mapping: { args: []; returns: string }
    get_gesture_mapping_dict: { args: []; returns: Record<string, unknown> }
    is_setting_pinned: { args: [settingId: string]; returns: boolean }
    get_settings_registry: { args: []; returns: string }
    get_pinned_settings_metadata: { args: []; returns: string }
    get_settings_menu_visibility: { args: []; returns: string }
    is_settings_section_visible: { args: [section: string]; returns: boolean }
    get_git_commit_hash: { args: []; returns: string }
    get_git_commit_date: { args: []; returns: string }
    get_git_commit_message: { args: []; returns: string }
  }
  signals: {
    deviceNameChanged: [value: string]
    fontSettingChanged: [value: string]
    themeSettingChanged: [value: string]
    uiScaleChanged: [value: number]
    colorTransitionMsChanged: [value: number]
    songLengthTransitionChanged: [value: boolean]
    showClockChanged: [value: boolean]
    clockFormatChanged: [value: boolean]
    clockShowSecondsChanged: [value: boolean]
    clockSizeChanged: [value: number]
    clockSizeAutoChanged: [value: boolean]
    backgroundGridChanged: [value: string]
    screenWidthChanged: [value: number]
    screenHeightChanged: [value: number]
    backgroundBlurRadiusChanged: [value: number]
    bottomBarOrientationChanged: [value: string]
    showBottomBarMediaControlsChanged: [value: boolean]
    settingsLayoutStyleChanged: [value: string]
    albumArtColorsChanged: []
    settingsMenuVisibilityChanged: []
    startUpVolumeChanged: [value: string]
    currentVolumeChanged: [value: number]
    mediaFolderChanged: [value: string]
    autoPlayOnStartupChanged: [value: boolean]
    persistShuffleStateChanged: [value: boolean]
    lastShuffleStateChanged: [value: boolean]
    lastPlayedSongChanged: [value: string]
    lastPlayedPositionChanged: [value: number]
    lastPlayedPlaylistChanged: [value: string]
    musicButtonDefaultPageChanged: [value: string]
    returnToLibraryAfterSelectionChanged: [value: boolean]
    showWaveformVisualizerChanged: [value: boolean]
    show3DButtonTiltChanged: [value: boolean]
    show3DAlbumPreviewChanged: [value: boolean]
    roundedAlbumArtChanged: [value: boolean]
    albumArtCornerRadiusChanged: [value: number]
    showAlbumArtShadowChanged: [value: boolean]
    vinylRecordModeChanged: [value: boolean]
    albumArtTransitionChanged: [value: string]
    audioOutputDeviceChanged: [value: string]
    backgroundOverlayOpacityChanged: [value: number]
    sideCardOpacityChanged: [value: number]
    sideCardAngleChanged: [value: number]
    buttonTiltDurationChanged: [value: number]
    textScrollSpeedChanged: [value: number]
    obdBluetoothPortChanged: [value: string]
    obdSavedAdaptersChanged: []
    obdFastModeChanged: [value: boolean]
    obdCardStyleCircularChanged: [value: boolean]
    gaugeTempUnitChanged: []
    gaugeSpeedUnitChanged: []
    obdAutoReconnectAttemptsChanged: [value: number]
    fuelTankCapacityChanged: [value: number]
    blackBoxEnabledChanged: [value: boolean]
    blackBoxSweepAllChanged: [value: boolean]
    blackBoxMaxMBChanged: [value: number]
    obdParametersChanged: []
    homeOBDParametersChanged: []
    supportedOBDParametersChanged: []
    directoryHistoryChanged: []
    customThemesChanged: []
    windowStateChanged: [value: string]
    lastSettingsSectionChanged: [value: string]
    androidAutoEnabledChanged: [value: boolean]
    phoneMirrorEnabledChanged: [value: boolean]
    scrcpyDisplaySizeChanged: [value: string]
    scrcpyAudioGainChanged: [value: number]
    scrcpyAudioDuckEnabledChanged: [value: boolean]
    scrcpyAudioDuckLevelChanged: [value: number]
    scrcpyPhoneScreenOffChanged: [value: boolean]
    scrcpyDisplayDpiChanged: [value: number]
    scrcpyBitRateUsbChanged: [value: number]
    scrcpyBitRateWifiChanged: [value: number]
    scrcpyAutoWifiChanged: [value: boolean]
    scrcpyBitRateAutoChanged: [value: boolean]
    scrcpyWirelessAddressChanged: [value: string]
    esp32VolumeEnabledChanged: [value: boolean]
    esp32VolumePortChanged: [value: string]
    esp32VolumeStepSizeChanged: [value: number]
    esp32AutoReconnectChanged: [value: boolean]
    esp32LedSleepEnabledChanged: [value: boolean]
    esp32LedColorModeChanged: [value: string]
    esp32LedStaticColorChanged: [value: string]
    imuEnabledChanged: [value: boolean]
    sensorEmitRateChanged: [value: number]
    sensorDecimalPlacesChanged: [value: number]
    sensorTempUnitChanged: [value: string]
    sensorAltitudeUnitChanged: [value: string]
    sensorCardsChanged: []
    gestureSensorEnabledChanged: [value: boolean]
    gestureMappingChanged: []
    gestureVolumeStepChanged: [value: number]
    gestureCooldownChanged: [value: number]
    pinnedSettingsChanged: []
    genericSettingChanged: [key: string]
  }
}

/** shiftLightManager (ShiftLightManager) */
export interface ShiftLightManager {
  properties: {
    /** bool, notify settingsChanged */
    enabled: boolean
    /** bool, notify settingsChanged */
    showOnHomeCard: boolean
    /** int, notify settingsChanged */
    maxRpm: number
    /** double, notify settingsChanged */
    fullScreenFlashOpacity: number
    /** double, notify settingsChanged */
    lampSize: number
    /** double, notify settingsChanged */
    glowSize: number
    /** double, notify settingsChanged */
    glowIntensity: number
    /** int, notify settingsChanged */
    colorTransitionSpeed: number
    /** bool, notify settingsChanged */
    pulseEnabled: boolean
    /** QVariantList, notify flagsChanged */
    flags: unknown[]
    /** double, notify rpmChanged */
    rpm: number
    /** bool, notify activeFlagChanged */
    active: boolean
    /** int, notify activeFlagChanged */
    activeIndex: number
    /** QVariantMap, notify activeFlagChanged */
    activeFlag: Record<string, unknown>
    /** bool, notify litChanged */
    lit: boolean
    /** bool, writable, notify suppressedChanged */
    suppressed: boolean
  }
  methods: {
    setRpm: { args: [rpm: number]; returns: void }
    setFlags: { args: [flags: unknown[]]; returns: void }
  }
  signals: {
    settingsChanged: []
    flagsChanged: []
    rpmChanged: []
    activeFlagChanged: []
    litChanged: []
    suppressedChanged: []
  }
}

/**
 * sim (Simulator)
 * @experimental not covered by the protocol version; may change or go in any release
 */
export interface Simulator {
  properties: {
    /** bool, notify activeChanged */
    active: boolean
  }
  methods: {
    setThrottle: { args: [percent: number]; returns: boolean }
    setBrake: { args: [percent: number]; returns: boolean }
    rev: { args: [intensity: number]; returns: boolean }
    setEngineOn: { args: [on: boolean]; returns: boolean }
    toggleEngine: { args: []; returns: boolean }
    setGear: { args: [gear: number]; returns: boolean }
    upshift: { args: []; returns: boolean }
    downshift: { args: []; returns: boolean }
    setSpeed: { args: [mph: number]; returns: boolean }
    setRpm: { args: [rpm: number]; returns: boolean }
    setRealistic: { args: [on: boolean]; returns: boolean }
    scenario: { args: [name: string]; returns: boolean }
    setDropouts: { args: [on: boolean]; returns: boolean }
    dropoutNow: { args: []; returns: boolean }
    setFailureRate: { args: [rate: number]; returns: boolean }
    setLinkUp: { args: [up: boolean]; returns: boolean }
    setImu: { args: [on: boolean]; returns: boolean }
    setVideo: { args: [on: boolean]; returns: boolean }
    pairingCode: { args: []; returns: Record<string, unknown> }
    status: { args: []; returns: Record<string, unknown> }
  }
  signals: {
    activeChanged: []
  }
}

/** themeCatalog (ThemeCatalog) */
export interface ThemeCatalog {
  properties: {
    /** QVariantMap, constant */
    themes: Record<string, unknown>
    /** QStringList, constant */
    themeNames: string[]
    /** QStringList, constant */
    lightThemeNames: string[]
    /** QStringList, constant */
    darkThemeNames: string[]
    /** QString, notify activeThemeChanged */
    activeThemeName: string
    /** QVariantMap, notify activeThemeChanged */
    activeTheme: Record<string, unknown>
  }
  methods: {
  }
  signals: {
    activeThemeChanged: []
  }
}

/** tripRecorder (TripRecorder) */
export interface TripRecorder {
  properties: {
    /** bool, notify enabledChanged */
    enabled: boolean
    /** bool, notify storeReadyChanged */
    storeReady: boolean
    /** bool, notify recordingChanged */
    recording: boolean
    /** qlonglong, notify recordingChanged */
    currentTripId: number
    /** qlonglong, notify recordingChanged */
    currentTripStartMs: number
    /** int, notify currentSampleCountChanged */
    currentSampleCount: number
    /** int, notify currentTripElapsedSChanged */
    currentTripElapsedS: number
    /** QVariantList, notify tripsChanged */
    trips: unknown[]
    /** QString, notify historyDirChanged */
    historyDir: string
    /** QString, notify historyDirChanged */
    tripsDir: string
    /** qlonglong, notify storageBytesChanged */
    storageBytes: number
    /** bool, constant */
    canOpenFolder: boolean
    /** QString, notify lastExportPathChanged */
    lastExportPath: string
    /** QString, notify lastErrorChanged */
    lastError: string
  }
  methods: {
    recordSample: { args: [paramId: string, value: number]; returns: void }
    setConnected: { args: [connected: boolean]; returns: void }
    setVehicleParameters: { args: [ids: string[]]; returns: void }
    setVehicleIdentity: { args: [vin: string, make: string, modelYear: number]; returns: void }
    noteEvent: { args: [kind: string, detail: string]; returns: void }
    setEnabled: { args: [enabled: boolean]; returns: void }
    endTripNow: { args: []; returns: void }
    refresh: { args: []; returns: void }
    requestDetail: { args: [id: number]; returns: void }
    requestSeries: { args: [id: number, paramId: string, maxPoints: number, fromS: number, toS: number] | [id: number, paramId: string, maxPoints: number, fromS: number] | [id: number, paramId: string, maxPoints: number] | [id: number, paramId: string]; returns: void }
    deleteTrip: { args: [id: number]; returns: void }
    clearHistory: { args: []; returns: void }
    exportTrip: { args: [id: number]; returns: void }
    openHistoryFolder: { args: []; returns: boolean }
  }
  signals: {
    enabledChanged: [enabled: boolean]
    storeReadyChanged: []
    recordingChanged: []
    currentSampleCountChanged: []
    currentTripElapsedSChanged: []
    liveTripChanged: [id: number]
    tripsChanged: []
    historyDirChanged: []
    storageBytesChanged: []
    lastExportPathChanged: []
    lastErrorChanged: []
    tripStarted: [id: number]
    tripEnded: [id: number, summary: Record<string, unknown>]
    detailReady: [id: number, detail: Record<string, unknown>]
    seriesReady: [id: number, paramId: string, points: unknown[]]
    exportFinished: [id: number, path: string]
  }
}

/** units (Units) */
export interface Units {
  properties: {
  }
  methods: {
    formatDuration: { args: [ms: number]; returns: string }
    formatSeconds: { args: [s: number]; returns: string }
    cardinal: { args: [headingDeg: number]; returns: string }
    cToF: { args: [c: number]; returns: number }
    mToFt: { args: [m: number]; returns: number }
    mphToKph: { args: [mph: number]; returns: number }
    linearToDb: { args: [x: number]; returns: number }
    dbToLinear: { args: [db: number]; returns: number }
  }
  signals: {
  }
}

/** volumeController (VolumeController) */
export interface VolumeController {
  properties: {
    /** int, notify percentChanged */
    percent: number
    /** bool, writable, notify mutedChanged */
    muted: boolean
    /** bool, notify effectivelyMutedChanged */
    effectivelyMuted: boolean
  }
  methods: {
    applyVolume: { args: [percent: number]; returns: void }
    setMuted: { args: [muted: boolean]; returns: void }
    toggleMute: { args: []; returns: void }
    toLinear: { args: [percent: number]; returns: number }
    toPercent: { args: [linear: number]; returns: number }
  }
  signals: {
    volumeApplied: [percent: number, linear: number]
    percentChanged: [percent: number]
    mutedChanged: [muted: boolean]
    effectivelyMutedChanged: []
  }
}

/** Every object the service exposes, by its name in `hello`. */
export interface Objects {
  androidAutoManager: AndroidAutoManager
  audioAnalyzer: AudioAnalyzer
  berryIMU: BerryIMUManager
  clock: Clock
  dashboardManager: DashboardManager
  diagnosticsManager: DiagnosticsManager
  esp32VolumeManager: ESP32VolumeManager
  fontCatalog: FontCatalog
  frontendManager: FrontendManager
  gestureSensor: GestureManager
  mediaManager: MediaManager
  networkManager: NetworkManager
  nowPlaying: NowPlaying
  obdAdapterFinder: OBDAdapterFinder
  obdManager: OBDManager
  paramStore: ParameterStore
  parameterCatalog: ParameterCatalog
  phoneMirrorManager: PhoneMirrorManager
  settingsManager: SettingsManager
  shiftLightManager: ShiftLightManager
  sim: Simulator
  themeCatalog: ThemeCatalog
  tripRecorder: TripRecorder
  units: Units
  volumeController: VolumeController
}

export const PROTOCOL_VERSION_OF_TYPES = 3
