// Generated from docs/api/*.json by scripts/generate-types.mjs; do not edit.
// Protocol version 3. Regenerate with `npm run generate`.

/** What `get` returns for a list-model property; the rows come from `rows` or a model subscription. */
export interface ModelSummary { model: true; count: number; roles: string[] }


/** airPlayManager (AirPlayManager) */
export interface AirPlayManager {
  properties: {
    /**
     * bool, constant
     * @experimental not covered by the protocol version; may change or go in any release
     */
    available: boolean
    /**
     * bool, notify enabledChanged
     * @experimental not covered by the protocol version; may change or go in any release
     */
    enabled: boolean
    /**
     * QString, notify stateChanged
     * @experimental not covered by the protocol version; may change or go in any release
     */
    state: string
    /**
     * QString, notify stateChanged
     * @experimental not covered by the protocol version; may change or go in any release
     */
    status: string
    /**
     * QString, notify stateChanged
     * @experimental not covered by the protocol version; may change or go in any release
     */
    receiverName: string
    /**
     * QString, notify stateChanged
     * @experimental not covered by the protocol version; may change or go in any release
     */
    deviceName: string
    /**
     * QString, notify stateChanged
     * @experimental not covered by the protocol version; may change or go in any release
     */
    deviceModel: string
    /**
     * QString, notify stateChanged
     * @experimental not covered by the protocol version; may change or go in any release
     */
    pin: string
    /**
     * bool, notify stateChanged
     * @experimental not covered by the protocol version; may change or go in any release
     */
    mirroring: boolean
    /**
     * bool, notify stateChanged
     * @experimental not covered by the protocol version; may change or go in any release
     */
    audioStreaming: boolean
    /**
     * int, notify frameSizeChanged
     * @experimental not covered by the protocol version; may change or go in any release
     */
    frameWidth: number
    /**
     * int, notify frameSizeChanged
     * @experimental not covered by the protocol version; may change or go in any release
     */
    frameHeight: number
    /**
     * QString, notify mediaChanged
     * @experimental not covered by the protocol version; may change or go in any release
     */
    title: string
    /**
     * QString, notify mediaChanged
     * @experimental not covered by the protocol version; may change or go in any release
     */
    artist: string
    /**
     * QString, notify mediaChanged
     * @experimental not covered by the protocol version; may change or go in any release
     */
    album: string
    /**
     * QStringList, notify stateChanged
     * @experimental not covered by the protocol version; may change or go in any release
     */
    addresses: string[]
    /**
     * QObject*, writable, notify videoSinkChanged
     * @experimental not covered by the protocol version; may change or go in any release
     */
    videoSink: unknown
    /**
     * bool, constant
     * @experimental not covered by the protocol version; may change or go in any release
     */
    touchAvailable: boolean
    /**
     * bool, notify touchChanged
     * @experimental not covered by the protocol version; may change or go in any release
     */
    touchEnabled: boolean
    /**
     * QString, notify touchChanged
     * @experimental not covered by the protocol version; may change or go in any release
     */
    touchState: string
    /**
     * QString, notify touchChanged
     * @experimental not covered by the protocol version; may change or go in any release
     */
    touchStatus: string
    /**
     * QString, notify touchChanged
     * @experimental not covered by the protocol version; may change or go in any release
     */
    touchDevice: string
    /**
     * bool, notify touchChanged
     * @experimental not covered by the protocol version; may change or go in any release
     */
    touchPairing: boolean
    /**
     * bool, notify touchChanged
     * @experimental not covered by the protocol version; may change or go in any release
     */
    touchCalibrating: boolean
  }
  methods: {
    /** @experimental not covered by the protocol version; may change or go in any release */
    setEnabled: { args: [enabled: boolean]; returns: void }
    /** @experimental not covered by the protocol version; may change or go in any release */
    setTouchEnabled: { args: [enabled: boolean]; returns: void }
    /** @experimental not covered by the protocol version; may change or go in any release */
    pairTouch: { args: [open: boolean] | []; returns: void }
    /** @experimental not covered by the protocol version; may change or go in any release */
    injectTouch: { args: [pointerId: number, action: number, x: number, y: number]; returns: void }
    /** @experimental not covered by the protocol version; may change or go in any release */
    scroll: { args: [dx: number, dy: number]; returns: void }
    /** @experimental not covered by the protocol version; may change or go in any release */
    calibrateTouch: { args: []; returns: void }
    /** @experimental not covered by the protocol version; may change or go in any release */
    cancelCalibration: { args: []; returns: void }
    /** @experimental not covered by the protocol version; may change or go in any release */
    forgetDevices: { args: []; returns: void }
  }
  signals: {
    /** @experimental not covered by the protocol version; may change or go in any release */
    enabledChanged: []
    /** @experimental not covered by the protocol version; may change or go in any release */
    stateChanged: []
    /** @experimental not covered by the protocol version; may change or go in any release */
    mediaChanged: []
    /** @experimental not covered by the protocol version; may change or go in any release */
    frameSizeChanged: []
    /** @experimental not covered by the protocol version; may change or go in any release */
    videoSinkChanged: []
    /** @experimental not covered by the protocol version; may change or go in any release */
    touchChanged: []
    mirroringStarted: []
    audioStreamingChanged: [streaming: boolean]
  }
}

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
    /**
     * QString, notify backendChanged
     * @experimental not covered by the protocol version; may change or go in any release
     */
    backend: string
    /**
     * bool, constant
     * @experimental not covered by the protocol version; may change or go in any release
     */
    nativeAvailable: boolean
    /**
     * QString, notify nativeStateChanged
     * @experimental not covered by the protocol version; may change or go in any release
     */
    nativeState: string
    /**
     * QString, notify nativeStateChanged
     * @experimental not covered by the protocol version; may change or go in any release
     */
    nativeStatus: string
    /**
     * QString, notify nativeStateChanged
     * @experimental not covered by the protocol version; may change or go in any release
     */
    phoneName: string
    /**
     * bool, notify videoStreamingChanged
     * @experimental not covered by the protocol version; may change or go in any release
     */
    videoStreaming: boolean
    /**
     * QString, constant
     * @experimental not covered by the protocol version; may change or go in any release
     */
    certificateDir: string
    /**
     * QObject*, writable, notify videoSinkChanged
     * @experimental not covered by the protocol version; may change or go in any release
     */
    videoSink: unknown
  }
  methods: {
    getDhuInstallInstructions: { args: []; returns: string }
    launchDhuSeamless: { args: []; returns: boolean }
    sendDhuClick: { args: [x: number, y: number]; returns: void }
    closeDhu: { args: []; returns: void }
    cleanup: { args: []; returns: void }
    /** @experimental not covered by the protocol version; may change or go in any release */
    startNative: { args: []; returns: boolean }
    /** @experimental not covered by the protocol version; may change or go in any release */
    stopNative: { args: []; returns: void }
    /** @experimental not covered by the protocol version; may change or go in any release */
    injectTouch: { args: [pointerId: number, action: number, x: number, y: number]; returns: void }
    /** @experimental not covered by the protocol version; may change or go in any release */
    setViewVisible: { args: [visible: boolean]; returns: void }
  }
  signals: {
    connectionProgress: [message: string]
    error: [message: string]
    dhuWindowReady: [windowHandle: number]
    dhuEmbeddedChanged: [embedded: boolean]
    /** @experimental not covered by the protocol version; may change or go in any release */
    backendChanged: []
    /** @experimental not covered by the protocol version; may change or go in any release */
    nativeStateChanged: []
    /** @experimental not covered by the protocol version; may change or go in any release */
    videoStreamingChanged: []
    /** @experimental not covered by the protocol version; may change or go in any release */
    videoSinkChanged: []
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
    /** @deprecated use analyzeFile; goes in protocol 5 */
    analyze_file: { args: [filePath: string]; returns: void }
    /** @deprecated use updatePosition; goes in protocol 5 */
    update_position: { args: [positionSeconds: number]; returns: void }
    /** @deprecated use setActive; goes in protocol 5 */
    set_active: { args: [active: boolean]; returns: void }
    /** @deprecated use getCurrentLevels; goes in protocol 5 */
    get_current_levels: { args: []; returns: unknown[] }
    /** @deprecated use getNumBars; goes in protocol 5 */
    get_num_bars: { args: []; returns: number }
    /** @deprecated use isAnalyzed; goes in protocol 5 */
    is_analyzed: { args: []; returns: boolean }
    clear: { args: []; returns: void }
    /** @deprecated use setLive; goes in protocol 5 */
    set_live: { args: [live: boolean]; returns: void }
    /** @deprecated use updateLive; goes in protocol 5 */
    update_live: { args: []; returns: void }
    /** @deprecated use feedLivePcm; goes in protocol 5 */
    feed_live_pcm: { args: [pcm: string, bufferedMs: number]; returns: void }
    analyzeFile: { args: [filePath: string]; returns: void }
    updatePosition: { args: [positionSeconds: number]; returns: void }
    setActive: { args: [active: boolean]; returns: void }
    getCurrentLevels: { args: []; returns: unknown[] }
    getNumBars: { args: []; returns: number }
    isAnalyzed: { args: []; returns: boolean }
    setLive: { args: [live: boolean]; returns: void }
    updateLive: { args: []; returns: void }
    feedLivePcm: { args: [pcm: string, bufferedMs: number]; returns: void }
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

/**
 * busMonitor (BusMonitor)
 * @experimental not covered by the protocol version; may change or go in any release
 */
export interface BusMonitor {
  properties: {
    /** bool, notify availableChanged */
    available: boolean
    /** bool, notify activeChanged */
    active: boolean
    /** QString, notify activeChanged */
    filter: string
    /** QString, notify noteChanged */
    note: string
    /** QVariantList, notify idsChanged */
    ids: unknown[]
    /** int, notify idsChanged */
    frameCount: number
    /** int, notify idsChanged */
    framesPerSecond: number
    /** int, notify idsChanged */
    overruns: number
    /** bool, constant */
    canRecord: boolean
    /** bool, notify recordingChanged */
    recording: boolean
    /** QString, notify recordingChanged */
    logFile: string
  }
  methods: {
    start: { args: [filter: string] | []; returns: boolean }
    stop: { args: []; returns: void }
    clear: { args: []; returns: void }
    startRecording: { args: []; returns: boolean }
    stopRecording: { args: []; returns: void }
  }
  signals: {
    availableChanged: []
    activeChanged: []
    noteChanged: []
    idsChanged: []
    recordingChanged: []
  }
}

/** clock (Clock) */
export interface Clock {
  properties: {
    /** QString, notify timeChanged */
    time: string
  }
  methods: {
    /** @deprecated use updateTime; goes in protocol 5 */
    update_time: { args: []; returns: void }
    updateTime: { args: []; returns: void }
  }
  signals: {
    timeChanged: [timeStr: string]
  }
}

/**
 * customPids (CustomPids)
 * @experimental not covered by the protocol version; may change or go in any release
 */
export interface CustomPids {
  properties: {
    /** QString, constant */
    file: string
    /** bool, notify fileExistsChanged */
    fileExists: boolean
    /** QVariantList, constant */
    pids: unknown[]
    /** QStringList, constant */
    errors: string[]
  }
  methods: {
    createExample: { args: []; returns: boolean }
  }
  signals: {
    fileExistsChanged: []
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
    /**
     * QVariantMap, notify lastImportReportChanged
     * @experimental not covered by the protocol version; may change or go in any release
     */
    lastImportReport: Record<string, unknown>
    /**
     * QVariantMap, notify previewChanged
     * @experimental not covered by the protocol version; may change or go in any release
     */
    preview: Record<string, unknown>
    /**
     * QVariantList, notify widgetPresetsChanged
     * @experimental not covered by the protocol version; may change or go in any release
     */
    widgetPresets: unknown[]
    /**
     * QString, notify homeDashboardChanged
     * @experimental not covered by the protocol version; may change or go in any release
     */
    homeDashboardId: string
    /** int, notify dashboardsChanged */
    hiddenBuiltInCount: number
    /** int, constant */
    supportedSchema: number
    /** QVariantMap, constant */
    widgetCatalog: Record<string, unknown>
    /**
     * QVariantList, constant
     * @experimental not covered by the protocol version; may change or go in any release
     */
    templates: unknown[]
  }
  methods: {
    saveDashboard: { args: [spec: Record<string, unknown>]; returns: string }
    deleteDashboard: { args: [id: string]; returns: boolean }
    /** @experimental not covered by the protocol version; may change or go in any release */
    setHomeDashboard: { args: [id: string]; returns: boolean }
    restoreBuiltInDashboards: { args: []; returns: boolean }
    duplicateDashboard: { args: [sourceId: string, newLabel: string]; returns: string }
    exportDashboard: { args: [id: string]; returns: string }
    copyDashboardToClipboard: { args: [id: string]; returns: boolean }
    importDashboard: { args: [fileOrUrl: string]; returns: string }
    importDashboardFromClipboard: { args: []; returns: string }
    importDashboardFromText: { args: [json: string]; returns: string }
    /** @experimental not covered by the protocol version; may change or go in any release */
    saveWidgetPreset: { args: [preset: Record<string, unknown>]; returns: string }
    /** @experimental not covered by the protocol version; may change or go in any release */
    deleteWidgetPreset: { args: [id: string]; returns: boolean }
    /** @experimental not covered by the protocol version; may change or go in any release */
    copyWidgetPresetToClipboard: { args: [id: string]; returns: boolean }
    /** @experimental not covered by the protocol version; may change or go in any release */
    importWidgetPresetFromText: { args: [json: string]; returns: string }
    /** @experimental not covered by the protocol version; may change or go in any release */
    importWidgetPresetFromClipboard: { args: []; returns: string }
    /** @experimental not covered by the protocol version; may change or go in any release */
    previewDashboard: { args: [spec: Record<string, unknown>]; returns: boolean }
    /** @experimental not covered by the protocol version; may change or go in any release */
    endPreview: { args: []; returns: void }
    refresh: { args: []; returns: void }
    /** @experimental not covered by the protocol version; may change or go in any release */
    fromTemplate: { args: [id: string, supported: string[]]; returns: Record<string, unknown> }
    loadDashboard: { args: [id: string]; returns: unknown }
    isBuiltIn: { args: [id: string]; returns: boolean }
    uniqueIdForLabel: { args: [label: string]; returns: string }
    normalizedSpec: { args: [spec: Record<string, unknown>]; returns: Record<string, unknown> }
    canPlace: { args: [cells: unknown[], gridColumns: number, gridRows: number, col: number, row: number, colSpan: number, rowSpan: number, exceptIndex: number]; returns: boolean }
    fitAt: { args: [cells: unknown[], gridColumns: number, gridRows: number, col: number, row: number, colSpan: number, rowSpan: number]; returns: Record<string, unknown> }
    /** @experimental not covered by the protocol version; may change or go in any release */
    arrange: { args: [cells: unknown[], gridColumns: number, gridRows: number, index: number, col: number, row: number, colSpan: number, rowSpan: number]; returns: Record<string, unknown> }
    /** @experimental not covered by the protocol version; may change or go in any release */
    nearestFree: { args: [cells: unknown[], gridColumns: number, gridRows: number, col: number, row: number, colSpan: number, rowSpan: number, exceptIndex: number]; returns: Record<string, unknown> }
    /** @experimental not covered by the protocol version; may change or go in any release */
    derivePortrait: { args: [cells: unknown[], gridColumns: number, gridRows: number]; returns: Record<string, unknown> }
    /** @experimental not covered by the protocol version; may change or go in any release */
    orientedSpec: { args: [spec: Record<string, unknown>, portrait: boolean]; returns: Record<string, unknown> }
    /** @experimental not covered by the protocol version; may change or go in any release */
    rescale: { args: [cells: unknown[], fromColumns: number, fromRows: number, toColumns: number, toRows: number]; returns: Record<string, unknown> }
    /** @experimental not covered by the protocol version; may change or go in any release */
    minSpan: { args: [type: string, gridColumns: number, gridRows: number, cellWidth: number, cellHeight: number, spacing: number]; returns: Record<string, unknown> }
    /** @experimental not covered by the protocol version; may change or go in any release */
    sizeOptions: { args: [type: string, gridColumns: number, gridRows: number, cellWidth: number, cellHeight: number, spacing: number]; returns: unknown[] }
    /** @experimental not covered by the protocol version; may change or go in any release */
    thumbnailFromImage: { args: [image: unknown]; returns: string }
    /** @experimental not covered by the protocol version; may change or go in any release */
    widgetPresetText: { args: [id: string]; returns: string }
  }
  signals: {
    dashboardsChanged: []
    lastShareErrorChanged: []
    lastErrorChanged: []
    lastExportPathChanged: []
    lastImportReportChanged: []
    previewChanged: []
    widgetPresetsChanged: []
    homeDashboardChanged: []
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
    /** @deprecated use connectDevice; goes in protocol 5 */
    connect_device: { args: []; returns: void }
    /** @deprecated use disconnectDevice; goes in protocol 5 */
    disconnect_device: { args: []; returns: void }
    /** @deprecated use getAvailablePorts; goes in protocol 5 */
    get_available_ports: { args: []; returns: unknown[] }
    /** @deprecated use isConnected; goes in protocol 5 */
    is_connected: { args: []; returns: boolean }
    /** @deprecated use getConnectionStatus; goes in protocol 5 */
    get_connection_status: { args: []; returns: string }
    /** @deprecated use getConnectionDetail; goes in protocol 5 */
    get_connection_detail: { args: []; returns: string }
    /** @deprecated use refreshPorts; goes in protocol 5 */
    refresh_ports: { args: []; returns: void }
    /** @deprecated use resetReconnectAttempts; goes in protocol 5 */
    reset_reconnect_attempts: { args: []; returns: void }
    cleanup: { args: []; returns: void }
    connectDevice: { args: []; returns: void }
    disconnectDevice: { args: []; returns: void }
    getAvailablePorts: { args: []; returns: unknown[] }
    isConnected: { args: []; returns: boolean }
    getConnectionStatus: { args: []; returns: string }
    getConnectionDetail: { args: []; returns: string }
    refreshPorts: { args: []; returns: void }
    resetReconnectAttempts: { args: []; returns: void }
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
    /** int, notify trialChanged */
    trialSecondsLeft: number
  }
  methods: {
    installFromFolder: { args: [path: string]; returns: string }
    installFromZip: { args: [path: string]; returns: string }
    remove: { args: [id: string]; returns: boolean }
    setActive: { args: [id: string]; returns: boolean }
    clearForcedQml: { args: []; returns: void }
    manifestOf: { args: [id: string]; returns: Record<string, unknown> }
    tryFrontend: { args: [id: string, seconds: number]; returns: boolean }
    keepTrial: { args: []; returns: void }
    restartNow: { args: []; returns: void }
  }
  signals: {
    frontendsChanged: []
    activeChanged: []
    lastErrorChanged: []
    installed: [id: string]
    trialChanged: []
    trialExpired: []
    restartRequested: []
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
    /** @deprecated does nothing; goes in protocol 5 */
    connect_settings_manager: { args: [arg: unknown]; returns: void }
    /** @deprecated internal; goes in protocol 5 */
    _save_playback_state: { args: []; returns: void }
    /** @deprecated internal; goes in protocol 5 */
    _clear_temp_files: { args: []; returns: void }
    /** @deprecated use flushMetadataStore; goes in protocol 5 */
    flush_metadata_store: { args: []; returns: void }
    /** @deprecated use playFile; goes in protocol 5 */
    play_file: { args: [filename: string]; returns: void }
    /** @deprecated use nextTrack; goes in protocol 5 */
    next_track: { args: []; returns: void }
    /** @deprecated use previousTrack; goes in protocol 5 */
    previous_track: { args: []; returns: void }
    pause: { args: []; returns: void }
    /** @deprecated use togglePlay; goes in protocol 5 */
    toggle_play: { args: []; returns: void }
    setMuted: { args: [muted: boolean]; returns: void }
    /** @deprecated use toggleShuffle; goes in protocol 5 */
    toggle_shuffle: { args: []; returns: void }
    setVolume: { args: [volume: number]; returns: void }
    setDucking: { args: [factor: number]; returns: void }
    /** @deprecated use setPosition; goes in protocol 5 */
    set_position: { args: [positionMs: number]; returns: void }
    /** @deprecated use extractColorsFromAlbumArt; goes in protocol 5 */
    extract_colors_from_album_art: { args: [filename: string]; returns: void }
    /** @deprecated use extractColorsFromImage; goes in protocol 5 */
    extract_colors_from_image: { args: [imagePath: string]; returns: void }
    /** @deprecated use scanLibrary; goes in protocol 5 */
    scan_library: { args: [resetDisplayNames: boolean] | []; returns: void }
    /** @deprecated use setLibraryRoot; goes in protocol 5 */
    set_library_root: { args: [path: string]; returns: void }
    /** @deprecated use stripFilenames; goes in protocol 5 */
    strip_filenames: { args: []; returns: void }
    /** @deprecated use selectPlaylist; goes in protocol 5 */
    select_playlist: { args: [name: string]; returns: void }
    /** @deprecated use createPlaylist; goes in protocol 5 */
    create_playlist: { args: [name: string]; returns: boolean }
    /** @deprecated use deletePlaylist; goes in protocol 5 */
    delete_playlist: { args: [name: string]; returns: boolean }
    /** @deprecated use moveSongToPlaylist; goes in protocol 5 */
    move_song_to_playlist: { args: [filename: string, targetPlaylist: string]; returns: boolean }
    /** @deprecated use deleteSong; goes in protocol 5 */
    delete_song: { args: [filename: string]; returns: boolean }
    /** @deprecated use playFileAtPath; goes in protocol 5 */
    play_file_at_path: { args: [filePath: string]; returns: boolean }
    /** @deprecated use invalidateStatsCache; goes in protocol 5 */
    invalidate_stats_cache: { args: []; returns: void }
    /** @deprecated use getMediaFiles; goes in protocol 5 */
    get_media_files: { args: [emitSignal: boolean] | []; returns: string[] }
    /** @deprecated use getFormattedDuration; goes in protocol 5 */
    get_formatted_duration: { args: [filename: string]; returns: string }
    /** @deprecated use getBand; goes in protocol 5 */
    get_band: { args: [filename: string]; returns: string }
    /** @deprecated use getAlbum; goes in protocol 5 */
    get_album: { args: [filename: string]; returns: string }
    /** @deprecated use getAlbumArt; goes in protocol 5 */
    get_album_art: { args: [filename: string]; returns: string }
    /** @deprecated use getCurrentFile; goes in protocol 5 */
    get_current_file: { args: []; returns: string }
    /** @deprecated use getNextTrackName; goes in protocol 5 */
    get_next_track_name: { args: []; returns: string }
    /** @deprecated use getPreviousTrackName; goes in protocol 5 */
    get_previous_track_name: { args: []; returns: string }
    /** @deprecated use getNextTrackAlbumArt; goes in protocol 5 */
    get_next_track_album_art: { args: []; returns: string }
    /** @deprecated use getPreviousTrackAlbumArt; goes in protocol 5 */
    get_previous_track_album_art: { args: []; returns: string }
    /** @deprecated use getNeighborAlbumArts; goes in protocol 5 */
    get_neighbor_album_arts: { args: []; returns: string[] }
    /** @deprecated use isPlaying; goes in protocol 5 */
    is_playing: { args: []; returns: boolean }
    /** @deprecated use isPaused; goes in protocol 5 */
    is_paused: { args: []; returns: boolean }
    /** @deprecated use getDuration; goes in protocol 5 */
    get_duration: { args: []; returns: number }
    /** @deprecated use getPosition; goes in protocol 5 */
    get_position: { args: []; returns: number }
    /** @deprecated use isMuted; goes in protocol 5 */
    is_muted: { args: []; returns: boolean }
    getVolume: { args: []; returns: number }
    /** @deprecated use isShuffled; goes in protocol 5 */
    is_shuffled: { args: []; returns: boolean }
    /** @deprecated use getPlaylistNames; goes in protocol 5 */
    get_playlist_names: { args: []; returns: string[] }
    /** @deprecated use getCurrentPlaylistName; goes in protocol 5 */
    get_current_playlist_name: { args: []; returns: string }
    /** @deprecated use getCurrentSongList; goes in protocol 5 */
    get_current_song_list: { args: []; returns: string[] }
    /** @deprecated use getMovablePlaylistNames; goes in protocol 5 */
    get_movable_playlist_names: { args: []; returns: string[] }
    /** @deprecated use getSongPlaylistName; goes in protocol 5 */
    get_song_playlist_name: { args: [filename: string]; returns: string }
    /** @deprecated use getSongFilePath; goes in protocol 5 */
    get_song_file_path: { args: [filename: string]; returns: string }
    /** @deprecated use getFullFilePath; goes in protocol 5 */
    get_full_file_path: { args: [filename: string]; returns: string }
    /** @deprecated use getDisplayName; goes in protocol 5 */
    get_display_name: { args: [filename: string]; returns: string }
    /** @deprecated use getDefaultMediaDir; goes in protocol 5 */
    get_default_media_dir: { args: []; returns: string }
    /** @deprecated use getMediaFolderName; goes in protocol 5 */
    get_media_folder_name: { args: []; returns: string }
    /** @deprecated use browseMusicFolders; goes in protocol 5 */
    browse_music_folders: { args: [path: string]; returns: Record<string, unknown> }
    /** @deprecated use pickBrowserLibrary; goes in protocol 5 */
    pick_browser_library: { args: []; returns: boolean }
    /** @deprecated use getTotalDuration; goes in protocol 5 */
    get_total_duration: { args: []; returns: string }
    /** @deprecated use getAlbumCount; goes in protocol 5 */
    get_album_count: { args: []; returns: number }
    /** @deprecated use getArtistCount; goes in protocol 5 */
    get_artist_count: { args: []; returns: number }
    /** @deprecated use sortMediaFiles; goes in protocol 5 */
    sort_media_files: { args: [sortColumn: string, ascending: boolean] | [sortColumn: string]; returns: string[] }
    flushMetadataStore: { args: []; returns: void }
    playFile: { args: [filename: string]; returns: void }
    nextTrack: { args: []; returns: void }
    previousTrack: { args: []; returns: void }
    togglePlay: { args: []; returns: void }
    toggleShuffle: { args: []; returns: void }
    setPosition: { args: [positionMs: number]; returns: void }
    extractColorsFromAlbumArt: { args: [filename: string]; returns: void }
    extractColorsFromImage: { args: [imagePath: string]; returns: void }
    scanLibrary: { args: [resetDisplayNames: boolean] | []; returns: void }
    setLibraryRoot: { args: [path: string]; returns: void }
    stripFilenames: { args: []; returns: void }
    selectPlaylist: { args: [name: string]; returns: void }
    createPlaylist: { args: [name: string]; returns: boolean }
    deletePlaylist: { args: [name: string]; returns: boolean }
    moveSongToPlaylist: { args: [filename: string, targetPlaylist: string]; returns: boolean }
    deleteSong: { args: [filename: string]; returns: boolean }
    playFileAtPath: { args: [filePath: string]; returns: boolean }
    invalidateStatsCache: { args: []; returns: void }
    getMediaFiles: { args: [emitSignal: boolean] | []; returns: string[] }
    getFormattedDuration: { args: [filename: string]; returns: string }
    getBand: { args: [filename: string]; returns: string }
    getAlbum: { args: [filename: string]; returns: string }
    getAlbumArt: { args: [filename: string]; returns: string }
    getCurrentFile: { args: []; returns: string }
    getNextTrackName: { args: []; returns: string }
    getPreviousTrackName: { args: []; returns: string }
    getNextTrackAlbumArt: { args: []; returns: string }
    getPreviousTrackAlbumArt: { args: []; returns: string }
    getNeighborAlbumArts: { args: []; returns: string[] }
    isPlaying: { args: []; returns: boolean }
    isPaused: { args: []; returns: boolean }
    getDuration: { args: []; returns: number }
    getPosition: { args: []; returns: number }
    isMuted: { args: []; returns: boolean }
    isShuffled: { args: []; returns: boolean }
    getPlaylistNames: { args: []; returns: string[] }
    getCurrentPlaylistName: { args: []; returns: string }
    getCurrentSongList: { args: []; returns: string[] }
    getMovablePlaylistNames: { args: []; returns: string[] }
    getSongPlaylistName: { args: [filename: string]; returns: string }
    getSongFilePath: { args: [filename: string]; returns: string }
    getFullFilePath: { args: [filename: string]; returns: string }
    getDisplayName: { args: [filename: string]; returns: string }
    getDefaultMediaDir: { args: []; returns: string }
    getMediaFolderName: { args: []; returns: string }
    browseMusicFolders: { args: [path: string]; returns: Record<string, unknown> }
    pickBrowserLibrary: { args: []; returns: boolean }
    getTotalDuration: { args: []; returns: string }
    getAlbumCount: { args: []; returns: number }
    getArtistCount: { args: []; returns: number }
    sortMediaFiles: { args: [sortColumn: string, ascending: boolean] | [sortColumn: string]; returns: string[] }
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
    /** QString, notify releaseUrlChanged */
    releaseUrl: string
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
    /** @deprecated use shutdownDevice; goes in protocol 5 */
    shutdown_device: { args: []; returns: void }
    cleanup: { args: []; returns: void }
    shutdownDevice: { args: []; returns: void }
  }
  signals: {
    networkNameChanged: [name: string]
    isConnectedChanged: [connected: boolean]
    updateStatusChanged: [status: string]
    updateMessageChanged: [message: string]
    remoteCommitChanged: [commit: string]
    remoteCommitMsgChanged: [msg: string]
    releaseUrlChanged: [url: string]
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
    useLocal: { args: []; returns: void }
    useSpotify: { args: []; returns: void }
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
    /** bool, constant */
    canScan: boolean
    /**
     * bool, constant
     * @experimental not covered by the protocol version; may change or go in any release
     */
    canPickSerial: boolean
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
    /** @experimental not covered by the protocol version; may change or go in any release */
    pickSerialPort: { args: []; returns: void }
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
    /** bool, notify simulatedChanged */
    simulated: boolean
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
    /** @deprecated use connectObd; goes in protocol 5 */
    connect_obd: { args: []; returns: void }
    reconnect: { args: []; returns: void }
    /** @deprecated use forceConnect; goes in protocol 5 */
    force_connect: { args: []; returns: void }
    close: { args: []; returns: void }
    /** @deprecated use resetConnection; goes in protocol 5 */
    reset_connection: { args: []; returns: void }
    /** @deprecated use getConnectionStatus (or the connectionStatus property); goes in protocol 5 */
    get_connection_status: { args: []; returns: string }
    getConnectionStatus: { args: []; returns: string }
    /** @deprecated use refreshPorts; goes in protocol 5 */
    refresh_ports: { args: []; returns: void }
    /** @deprecated use getAvailablePorts; goes in protocol 5 */
    get_available_ports: { args: []; returns: unknown[] }
    scan: { args: []; returns: void }
    availableAdapters: { args: []; returns: unknown[] }
    /** @deprecated use connectToAdapter; goes in protocol 5 */
    connect_to_adapter: { args: [identifier: string]; returns: void }
    /** @deprecated use platformHint; goes in protocol 5 */
    platform_hint: { args: []; returns: string }
    /** @deprecated use scanVehicle; goes in protocol 5 */
    scan_vehicle: { args: []; returns: void }
    /** @deprecated use isScanning; goes in protocol 5 */
    is_scanning: { args: []; returns: boolean }
    /** @deprecated use getAllParameterNames; goes in protocol 5 */
    get_all_parameter_names: { args: []; returns: string[] }
    /** @deprecated use getSupportedCommands; goes in protocol 5 */
    get_supported_commands: { args: []; returns: unknown[] }
    /** @deprecated use enableScannedParameters; goes in protocol 5 */
    enable_scanned_parameters: { args: [paramNames: unknown[]]; returns: void }
    /** @deprecated use enableAllSupported; goes in protocol 5 */
    enable_all_supported: { args: []; returns: void }
    /** @deprecated use enterDiagnosticMode; goes in protocol 5 */
    enter_diagnostic_mode: { args: []; returns: void }
    /** @deprecated use exitDiagnosticMode; goes in protocol 5 */
    exit_diagnostic_mode: { args: []; returns: void }
    /** @deprecated use isDiagnosticMode; goes in protocol 5 */
    is_diagnostic_mode: { args: []; returns: boolean }
    /** @deprecated use readDtc; goes in protocol 5 */
    read_dtc: { args: []; returns: void }
    /** @deprecated use readCurrentDtc; goes in protocol 5 */
    read_current_dtc: { args: []; returns: void }
    /** @deprecated use readStatus; goes in protocol 5 */
    read_status: { args: []; returns: void }
    /** @deprecated use clearDtc; goes in protocol 5 */
    clear_dtc: { args: []; returns: void }
    /** @deprecated use readFreezeFrame; goes in protocol 5 */
    read_freeze_frame: { args: []; returns: void }
    /** @deprecated use readVin; goes in protocol 5 */
    read_vin: { args: []; returns: void }
    /** @deprecated use getDtcCodes; goes in protocol 5 */
    get_dtc_codes: { args: []; returns: unknown[] }
    /** @deprecated use getDtcCount; goes in protocol 5 */
    get_dtc_count: { args: []; returns: number }
    /** @deprecated use getMilStatus; goes in protocol 5 */
    get_mil_status: { args: []; returns: boolean }
    /** @deprecated use getFreezeFrame; goes in protocol 5 */
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
    /** @deprecated use refreshValues; goes in protocol 5 */
    refresh_values: { args: []; returns: void }
    /** @deprecated use openBluetoothSettings; goes in protocol 5 */
    open_bluetooth_settings: { args: []; returns: void }
    /** @deprecated use setTargetAddress; goes in protocol 5 */
    set_target_address: { args: [address: string]; returns: void }
    /** @deprecated use connectDirect; goes in protocol 5 */
    connect_direct: { args: [mac: string, channel: number] | [mac: string]; returns: void }
    /** @deprecated use setAutoReconnect; goes in protocol 5 */
    set_auto_reconnect: { args: [enabled: boolean]; returns: void }
    /** @deprecated use setConnectionTimeout; goes in protocol 5 */
    set_connection_timeout: { args: [timeoutSeconds: number]; returns: void }
    /** @deprecated use checkDevicePresence; goes in protocol 5 */
    check_device_presence: { args: []; returns: boolean }
    connectObd: { args: []; returns: void }
    forceConnect: { args: []; returns: void }
    resetConnection: { args: []; returns: void }
    refreshPorts: { args: []; returns: void }
    getAvailablePorts: { args: []; returns: unknown[] }
    connectToAdapter: { args: [identifier: string]; returns: void }
    platformHint: { args: []; returns: string }
    scanVehicle: { args: []; returns: void }
    isScanning: { args: []; returns: boolean }
    getAllParameterNames: { args: []; returns: string[] }
    getSupportedCommands: { args: []; returns: unknown[] }
    enableScannedParameters: { args: [paramNames: unknown[]]; returns: void }
    enableAllSupported: { args: []; returns: void }
    enterDiagnosticMode: { args: []; returns: void }
    exitDiagnosticMode: { args: []; returns: void }
    isDiagnosticMode: { args: []; returns: boolean }
    readDtc: { args: []; returns: void }
    readCurrentDtc: { args: []; returns: void }
    readStatus: { args: []; returns: void }
    clearDtc: { args: []; returns: void }
    readFreezeFrame: { args: []; returns: void }
    readVin: { args: []; returns: void }
    getDtcCodes: { args: []; returns: unknown[] }
    getDtcCount: { args: []; returns: number }
    getMilStatus: { args: []; returns: boolean }
    getFreezeFrame: { args: []; returns: unknown[] }
    refreshValues: { args: []; returns: void }
    openBluetoothSettings: { args: []; returns: void }
    setTargetAddress: { args: [address: string]; returns: void }
    connectDirect: { args: [mac: string, channel: number] | [mac: string]; returns: void }
    setAutoReconnect: { args: [enabled: boolean]; returns: void }
    setConnectionTimeout: { args: [timeoutSeconds: number]; returns: void }
    checkDevicePresence: { args: []; returns: boolean }
    isConnected: { args: []; returns: boolean }
    clearConnectionLog: { args: []; returns: void }
    /** @deprecated use isConnected (or the connected property); goes in protocol 5 */
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
    simulatedChanged: []
    simulatedAdapterRequested: []
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
    /** QStringList, constant */
    groups: string[]
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
    /** @deprecated does nothing: the bit rate is automatic; goes in protocol 5 */
    setBitRateUsb: { args: [mbps: number]; returns: void }
    /** @deprecated does nothing: the bit rate is automatic; goes in protocol 5 */
    setBitRateWifi: { args: [mbps: number]; returns: void }
    /** @deprecated does nothing: the bit rate is always adaptive; goes in protocol 5 */
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
    /** QString, writable, notify deviceNameChanged */
    deviceName: string
    /** QString, writable, notify fontSettingChanged */
    fontSetting: string
    /** QString, writable, notify themeSettingChanged */
    themeSetting: string
    /** float, writable, notify uiScaleChanged */
    uiScale: number
    /** int, writable, notify colorTransitionMsChanged */
    colorTransitionMs: number
    /** bool, writable, notify songLengthTransitionChanged */
    songLengthTransition: boolean
    /** bool, writable, notify showClockChanged */
    showClock: boolean
    /** bool, writable, notify clockFormatChanged */
    clockFormat24Hour: boolean
    /** bool, writable, notify clockShowSecondsChanged */
    clockShowSeconds: boolean
    /** int, writable, notify clockSizeChanged */
    clockSize: number
    /** bool, writable, notify clockSizeAutoChanged */
    clockSizeAuto: boolean
    /** QString, writable, notify backgroundGridChanged */
    backgroundGrid: string
    /** int, writable, notify screenWidthChanged */
    screenWidth: number
    /** int, writable, notify screenHeightChanged */
    screenHeight: number
    /** int, writable, notify backgroundBlurRadiusChanged */
    backgroundBlurRadius: number
    /** QString, writable, notify bottomBarOrientationChanged */
    bottomBarOrientation: string
    /** bool, writable, notify showBottomBarMediaControlsChanged */
    showBottomBarMediaControls: boolean
    /** QString, writable, notify settingsLayoutStyleChanged */
    settingsLayoutStyle: string
    /** QVariantMap, notify albumArtColorsChanged */
    albumArtColors: Record<string, unknown>
    /** QString, notify settingsMenuVisibilityChanged */
    settingsMenuVisibility: string
    /** float, writable, notify startUpVolumeChanged */
    startUpVolume: number
    /** int, notify currentVolumeChanged */
    currentVolume: number
    /** QString, writable, notify mediaFolderChanged */
    mediaFolder: string
    /** bool, writable, notify autoPlayOnStartupChanged */
    autoPlayOnStartup: boolean
    /** bool, writable, notify persistShuffleStateChanged */
    persistShuffleState: boolean
    /** bool, writable, notify lastShuffleStateChanged */
    lastShuffleState: boolean
    /** QString, writable, notify lastPlayedSongChanged */
    lastPlayedSong: string
    /** int, writable, notify lastPlayedPositionChanged */
    lastPlayedPosition: number
    /** QString, writable, notify lastPlayedPlaylistChanged */
    lastPlayedPlaylist: string
    /** QString, writable, notify musicButtonDefaultPageChanged */
    musicButtonDefaultPage: string
    /** bool, writable, notify returnToLibraryAfterSelectionChanged */
    returnToLibraryAfterSelection: boolean
    /** bool, writable, notify showWaveformVisualizerChanged */
    showWaveformVisualizer: boolean
    /** bool, writable, notify show3DButtonTiltChanged */
    show3DButtonTilt: boolean
    /** bool, writable, notify show3DAlbumPreviewChanged */
    show3DAlbumPreview: boolean
    /** bool, writable, notify roundedAlbumArtChanged */
    roundedAlbumArt: boolean
    /** int, writable, notify albumArtCornerRadiusChanged */
    albumArtCornerRadius: number
    /** bool, writable, notify showAlbumArtShadowChanged */
    showAlbumArtShadow: boolean
    /** bool, writable, notify vinylRecordModeChanged */
    vinylRecordMode: boolean
    /** QString, writable, notify albumArtTransitionChanged */
    albumArtTransition: string
    /** QString, writable, notify audioOutputDeviceChanged */
    audioOutputDevice: string
    /** int, writable, notify backgroundOverlayOpacityChanged */
    backgroundOverlayOpacity: number
    /** double, writable, notify sideCardOpacityChanged */
    sideCardOpacity: number
    /** int, writable, notify sideCardAngleChanged */
    sideCardAngle: number
    /** int, writable, notify buttonTiltDurationChanged */
    buttonTiltDuration: number
    /** int, writable, notify textScrollSpeedChanged */
    textScrollSpeed: number
    /** QString, writable, notify obdBluetoothPortChanged */
    obdBluetoothPort: string
    /** QVariantList, notify obdSavedAdaptersChanged */
    obdSavedAdapters: unknown[]
    /** bool, writable, notify obdFastModeChanged */
    obdFastMode: boolean
    /** bool, writable, notify obdCardStyleCircularChanged */
    obdCardStyleCircular: boolean
    /** QString, writable, notify gaugeTempUnitChanged */
    gaugeTempUnit: string
    /** QString, writable, notify gaugeSpeedUnitChanged */
    gaugeSpeedUnit: string
    /** int, writable, notify obdAutoReconnectAttemptsChanged */
    obdAutoReconnectAttempts: number
    /** float, writable, notify fuelTankCapacityChanged */
    fuelTankCapacity: number
    /** bool, writable, notify blackBoxEnabledChanged */
    blackBoxEnabled: boolean
    /** bool, writable, notify blackBoxSweepAllChanged */
    blackBoxSweepAll: boolean
    /** int, writable, notify blackBoxMaxMBChanged */
    blackBoxMaxMB: number
    /**
     * QVariantList, writable, notify homeOBDParametersChanged
     * @deprecated Home is a dashboard: dashboardManager.homeDashboardId; goes with the classic Home
     */
    homeOBDParameters: unknown[]
    /**
     * int, constant
     * @deprecated Home is a dashboard: dashboardManager.homeDashboardId; goes with the classic Home
     */
    maxHomeObdParameters: number
    /**
     * bool, writable, notify homeVehicleCardChanged
     * @deprecated Home is a dashboard: place a VehicleWidget; goes with the classic Home
     */
    homeVehicleCard: boolean
    /** QVariantList, notify customThemesChanged */
    customThemes: unknown[]
    /** QVariantMap, notify customThemesChanged */
    customThemeData: Record<string, unknown>
    /** QVariantList, constant */
    nowPlayingPresets: unknown[]
    /** QVariantList, notify directoryHistoryChanged */
    directoryHistory: unknown[]
    /** QString, writable, notify windowStateChanged */
    windowState: string
    /** QString, writable, notify lastSettingsSectionChanged */
    lastSettingsSection: string
    /** bool, writable, notify androidAutoEnabledChanged */
    androidAutoEnabled: boolean
    /** bool, writable, notify phoneMirrorEnabledChanged */
    phoneMirrorEnabled: boolean
    /** QString, writable, notify scrcpyDisplaySizeChanged */
    scrcpyDisplaySize: string
    /** double, writable, notify scrcpyAudioGainChanged */
    scrcpyAudioGain: number
    /** double, writable, notify scrcpyAudioGainChanged */
    scrcpyAudioGainDb: number
    /** double, writable, notify scrcpyAudioDuckLevelChanged */
    scrcpyAudioDuckLevelDb: number
    /** bool, writable, notify scrcpyAudioDuckEnabledChanged */
    scrcpyAudioDuckEnabled: boolean
    /** double, writable, notify scrcpyAudioDuckLevelChanged */
    scrcpyAudioDuckLevel: number
    /** bool, writable, notify scrcpyPhoneScreenOffChanged */
    scrcpyPhoneScreenOff: boolean
    /** int, writable, notify scrcpyDisplayDpiChanged */
    scrcpyDisplayDpi: number
    /** int, writable, notify scrcpyBitRateUsbChanged */
    scrcpyBitRateUsb: number
    /** int, writable, notify scrcpyBitRateWifiChanged */
    scrcpyBitRateWifi: number
    /** bool, writable, notify scrcpyAutoWifiChanged */
    scrcpyAutoWifi: boolean
    /** bool, writable, notify scrcpyBitRateAutoChanged */
    scrcpyBitRateAuto: boolean
    /** QString, writable, notify scrcpyWirelessAddressChanged */
    scrcpyWirelessAddress: string
    /** bool, writable, notify esp32VolumeEnabledChanged */
    esp32VolumeEnabled: boolean
    /** QString, writable, notify esp32VolumePortChanged */
    esp32VolumePort: string
    /** double, writable, notify esp32VolumeStepSizeChanged */
    esp32VolumeStepSize: number
    /** bool, writable, notify esp32AutoReconnectChanged */
    esp32AutoReconnect: boolean
    /** bool, writable, notify esp32LedSleepEnabledChanged */
    esp32LedSleepEnabled: boolean
    /** QString, writable, notify esp32LedColorModeChanged */
    esp32LedColorMode: string
    /** QString, writable, notify esp32LedStaticColorChanged */
    esp32LedStaticColor: string
    /** bool, writable, notify imuEnabledChanged */
    imuEnabled: boolean
    /** int, writable, notify sensorEmitRateChanged */
    sensorEmitRate: number
    /** int, writable, notify sensorDecimalPlacesChanged */
    sensorDecimalPlaces: number
    /** QString, writable, notify sensorTempUnitChanged */
    sensorTempUnit: string
    /** QString, writable, notify sensorAltitudeUnitChanged */
    sensorAltitudeUnit: string
    /** QVariantMap, notify sensorCardsChanged */
    sensorCards: Record<string, unknown>
    /** bool, writable, notify gestureSensorEnabledChanged */
    gestureSensorEnabled: boolean
    /** int, writable, notify gestureVolumeStepChanged */
    gestureVolumeStep: number
    /** int, writable, notify gestureCooldownChanged */
    gestureCooldown: number
    /** QVariantList, notify pinnedSettingsChanged */
    pinnedSettings: unknown[]
  }
  methods: {
    /** @deprecated use the deviceName property; goes in protocol 5 */
    save_device_name: { args: [name: string]; returns: void }
    /** @deprecated use the fontSetting property; goes in protocol 5 */
    save_font_setting: { args: [font: string]; returns: void }
    /** @deprecated use the themeSetting property; goes in protocol 5 */
    save_theme_setting: { args: [theme: string]; returns: void }
    /** @deprecated use the uiScale property; goes in protocol 5 */
    save_ui_scale: { args: [scale: number]; returns: void }
    /** @deprecated use the colorTransitionMs property; goes in protocol 5 */
    save_color_transition_ms: { args: [ms: number]; returns: void }
    /** @deprecated use the songLengthTransition property; goes in protocol 5 */
    save_song_length_transition: { args: [enabled: boolean]; returns: void }
    /** @deprecated use the showClock property; goes in protocol 5 */
    save_show_clock: { args: [show: boolean]; returns: void }
    /** @deprecated use the clockFormat24Hour property; goes in protocol 5 */
    save_clock_format: { args: [is24hour: boolean]; returns: void }
    /** @deprecated use the clockShowSeconds property; goes in protocol 5 */
    save_clock_show_seconds: { args: [show: boolean]; returns: void }
    /** @deprecated use the clockSize property; goes in protocol 5 */
    save_clock_size: { args: [size: number]; returns: void }
    /** @deprecated use the clockSizeAuto property; goes in protocol 5 */
    save_clock_size_auto: { args: [autoSize: boolean]; returns: void }
    /** @deprecated use the backgroundGrid property; goes in protocol 5 */
    save_background_grid: { args: [grid: string]; returns: void }
    /** @deprecated use the screenWidth property; goes in protocol 5 */
    save_screen_width: { args: [width: number]; returns: void }
    /** @deprecated use the screenHeight property; goes in protocol 5 */
    save_screen_height: { args: [height: number]; returns: void }
    /** @deprecated use the backgroundBlurRadius property; goes in protocol 5 */
    save_background_blur_radius: { args: [radius: number]; returns: void }
    /** @deprecated use the bottomBarOrientation property; goes in protocol 5 */
    save_bottom_bar_orientation: { args: [orientation: string]; returns: void }
    /** @deprecated use the showBottomBarMediaControls property; goes in protocol 5 */
    save_show_bottom_bar_media_controls: { args: [show: boolean]; returns: void }
    /** @deprecated use the settingsLayoutStyle property; goes in protocol 5 */
    save_settings_layout_style: { args: [style: string]; returns: void }
    set_album_art_colors: { args: [colorsJson: string]; returns: void }
    /** @deprecated use the startUpVolume property; goes in protocol 5 */
    save_start_volume: { args: [volume: number]; returns: void }
    setCurrentVolume: { args: [volume: number]; returns: void }
    /** @deprecated use the mediaFolder property; goes in protocol 5 */
    save_media_folder: { args: [folderPath: string]; returns: boolean }
    save_media_folder_url: { args: [url: string]; returns: boolean }
    /** @deprecated use the autoPlayOnStartup property; goes in protocol 5 */
    save_auto_play_on_startup: { args: [enabled: boolean]; returns: void }
    /** @deprecated use the persistShuffleState property; goes in protocol 5 */
    save_persist_shuffle_state: { args: [enabled: boolean]; returns: void }
    /** @deprecated use the lastShuffleState property; goes in protocol 5 */
    save_last_shuffle_state: { args: [enabled: boolean]; returns: void }
    /** @deprecated use the lastPlayedSong property; goes in protocol 5 */
    save_last_played_song: { args: [filename: string]; returns: void }
    /** @deprecated use the lastPlayedPosition property; goes in protocol 5 */
    save_last_played_position: { args: [positionMs: number]; returns: void }
    /** @deprecated use the lastPlayedPlaylist property; goes in protocol 5 */
    save_last_played_playlist: { args: [playlistName: string]; returns: void }
    save_playback_state: { args: [song: string, positionMs: number, playlist: string]; returns: void }
    /** @deprecated use the musicButtonDefaultPage property; goes in protocol 5 */
    save_music_button_default_page: { args: [page: string]; returns: void }
    /** @deprecated use the returnToLibraryAfterSelection property; goes in protocol 5 */
    save_return_to_library_after_selection: { args: [returnToLibrary: boolean]; returns: void }
    /** @deprecated use the showWaveformVisualizer property; goes in protocol 5 */
    save_show_waveform_visualizer: { args: [enabled: boolean]; returns: void }
    /** @deprecated use the show3DButtonTilt property; goes in protocol 5 */
    save_show_3d_button_tilt: { args: [enabled: boolean]; returns: void }
    /** @deprecated use the show3DAlbumPreview property; goes in protocol 5 */
    save_show_3d_album_preview: { args: [enabled: boolean]; returns: void }
    /** @deprecated use the roundedAlbumArt property; goes in protocol 5 */
    save_rounded_album_art: { args: [enabled: boolean]; returns: void }
    /** @deprecated use the albumArtCornerRadius property; goes in protocol 5 */
    save_album_art_corner_radius: { args: [radius: number]; returns: void }
    /** @deprecated use the showAlbumArtShadow property; goes in protocol 5 */
    save_show_album_art_shadow: { args: [enabled: boolean]; returns: void }
    /** @deprecated use the vinylRecordMode property; goes in protocol 5 */
    save_vinyl_record_mode: { args: [enabled: boolean]; returns: void }
    /** @deprecated use the albumArtTransition property; goes in protocol 5 */
    save_album_art_transition: { args: [transition: string]; returns: void }
    /** @deprecated use the audioOutputDevice property; goes in protocol 5 */
    save_audio_output_device: { args: [id: string]; returns: void }
    /** @deprecated use the backgroundOverlayOpacity property; goes in protocol 5 */
    save_background_overlay_opacity: { args: [opacity: number]; returns: void }
    /** @deprecated use the sideCardOpacity property; goes in protocol 5 */
    save_side_card_opacity: { args: [opacity: number]; returns: void }
    /** @deprecated use the sideCardAngle property; goes in protocol 5 */
    save_side_card_angle: { args: [angle: number]; returns: void }
    /** @deprecated use the buttonTiltDuration property; goes in protocol 5 */
    save_button_tilt_duration: { args: [duration: number]; returns: void }
    /** @deprecated use the textScrollSpeed property; goes in protocol 5 */
    save_text_scroll_speed: { args: [speed: number]; returns: void }
    /** @deprecated use the obdBluetoothPort property; goes in protocol 5 */
    save_obd_bluetooth_port: { args: [port: string]; returns: void }
    add_obd_saved_adapter: { args: [name: string, mac: string]; returns: void }
    remove_obd_saved_adapter: { args: [mac: string]; returns: void }
    /** @deprecated use the obdFastMode property; goes in protocol 5 */
    save_obd_fast_mode: { args: [enabled: boolean]; returns: void }
    /** @deprecated use the obdCardStyleCircular property; goes in protocol 5 */
    save_obd_card_style_circular: { args: [circular: boolean]; returns: void }
    /** @deprecated use the gaugeTempUnit property; goes in protocol 5 */
    save_gauge_temp_unit: { args: [unit: string]; returns: void }
    /** @deprecated use the gaugeSpeedUnit property; goes in protocol 5 */
    save_gauge_speed_unit: { args: [unit: string]; returns: void }
    /** @deprecated use the obdAutoReconnectAttempts property; goes in protocol 5 */
    save_obd_auto_reconnect_attempts: { args: [attempts: number]; returns: void }
    /** @deprecated use the fuelTankCapacity property; goes in protocol 5 */
    save_fuel_tank_capacity: { args: [capacity: number]; returns: void }
    /** @deprecated use the blackBoxEnabled property; goes in protocol 5 */
    save_black_box_enabled: { args: [enabled: boolean]; returns: void }
    /** @deprecated use the blackBoxSweepAll property; goes in protocol 5 */
    save_black_box_sweep_all: { args: [enabled: boolean]; returns: void }
    setHomeVehicleCard: { args: [enabled: boolean]; returns: void }
    /** @deprecated use the blackBoxMaxMB property; goes in protocol 5 */
    save_black_box_max_mb: { args: [megabytes: number]; returns: void }
    save_obd_parameter_enabled: { args: [parameter: string, enabled: boolean]; returns: void }
    save_supported_obd_parameters: { args: [parameters: unknown[]]; returns: void }
    /** @deprecated use the homeOBDParameters property; goes in protocol 5 */
    save_home_obd_parameters: { args: [parameters: unknown[]]; returns: void }
    add_home_obd_parameter: { args: [id: string, index: number] | [id: string]; returns: boolean }
    remove_home_obd_parameter: { args: [id: string]; returns: boolean }
    move_home_obd_parameter: { args: [from: number, to: number]; returns: boolean }
    save_to_directory_history: { args: [folderPath: string]; returns: void }
    remove_from_directory_history: { args: [folderPath: string]; returns: void }
    save_custom_theme: { args: [name: string, theme: Record<string, unknown>, albumArtSource: string] | [name: string, theme: Record<string, unknown>]; returns: void }
    delete_custom_theme: { args: [name: string]; returns: void }
    /** @deprecated use the windowState property; goes in protocol 5 */
    save_window_state: { args: [state: string]; returns: void }
    /** @deprecated use the lastSettingsSection property; goes in protocol 5 */
    set_last_settings_section: { args: [section: string]; returns: void }
    save_settings_section_visibility: { args: [section: string, visible: boolean]; returns: void }
    /** @deprecated use the androidAutoEnabled property; goes in protocol 5 */
    save_android_auto_enabled: { args: [enabled: boolean]; returns: void }
    /** @deprecated use the phoneMirrorEnabled property; goes in protocol 5 */
    save_phone_mirror_enabled: { args: [enabled: boolean]; returns: void }
    /** @deprecated use the scrcpyDisplaySize property; goes in protocol 5 */
    save_scrcpy_display_size: { args: [size: string]; returns: void }
    /** @deprecated use the scrcpyAudioGain property; goes in protocol 5 */
    save_scrcpy_audio_gain: { args: [gain: number]; returns: void }
    /** @deprecated use the scrcpyAudioDuckEnabled property; goes in protocol 5 */
    save_scrcpy_audio_duck_enabled: { args: [enabled: boolean]; returns: void }
    /** @deprecated use the scrcpyAudioDuckLevel property; goes in protocol 5 */
    save_scrcpy_audio_duck_level: { args: [level: number]; returns: void }
    /** @deprecated use the scrcpyAudioGainDb property; goes in protocol 5 */
    save_scrcpy_audio_gain_db: { args: [db: number]; returns: void }
    /** @deprecated use the scrcpyAudioDuckLevelDb property; goes in protocol 5 */
    save_scrcpy_audio_duck_level_db: { args: [db: number]; returns: void }
    /** @deprecated use the scrcpyPhoneScreenOff property; goes in protocol 5 */
    save_scrcpy_phone_screen_off: { args: [off: boolean]; returns: void }
    /** @deprecated use the scrcpyDisplayDpi property; goes in protocol 5 */
    save_scrcpy_display_dpi: { args: [dpi: number]; returns: void }
    /** @deprecated use the scrcpyBitRateUsb property; goes in protocol 5 */
    save_scrcpy_bit_rate_usb: { args: [mbps: number]; returns: void }
    /** @deprecated use the scrcpyBitRateWifi property; goes in protocol 5 */
    save_scrcpy_bit_rate_wifi: { args: [mbps: number]; returns: void }
    /** @deprecated use the scrcpyAutoWifi property; goes in protocol 5 */
    save_scrcpy_auto_wifi: { args: [enabled: boolean]; returns: void }
    /** @deprecated use the scrcpyBitRateAuto property; goes in protocol 5 */
    save_scrcpy_bit_rate_auto: { args: [enabled: boolean]; returns: void }
    /** @deprecated use the scrcpyWirelessAddress property; goes in protocol 5 */
    save_scrcpy_wireless_address: { args: [address: string]; returns: void }
    /** @deprecated use the esp32VolumeEnabled property; goes in protocol 5 */
    save_esp32_volume_enabled: { args: [enabled: boolean]; returns: void }
    /** @deprecated use the esp32VolumePort property; goes in protocol 5 */
    save_esp32_volume_port: { args: [port: string]; returns: void }
    /** @deprecated use the esp32VolumeStepSize property; goes in protocol 5 */
    save_esp32_volume_step_size: { args: [stepSize: number]; returns: void }
    /** @deprecated use the esp32AutoReconnect property; goes in protocol 5 */
    save_esp32_auto_reconnect: { args: [enabled: boolean]; returns: void }
    /** @deprecated use the esp32LedSleepEnabled property; goes in protocol 5 */
    save_esp32_led_sleep_enabled: { args: [enabled: boolean]; returns: void }
    /** @deprecated use the esp32LedColorMode property; goes in protocol 5 */
    save_esp32_led_color_mode: { args: [mode: string]; returns: void }
    /** @deprecated use the esp32LedStaticColor property; goes in protocol 5 */
    save_esp32_led_static_color: { args: [color: string]; returns: void }
    /** @deprecated use the imuEnabled property; goes in protocol 5 */
    save_imu_enabled: { args: [enabled: boolean]; returns: void }
    /** @deprecated use the sensorEmitRate property; goes in protocol 5 */
    save_sensor_emit_rate: { args: [hz: number]; returns: void }
    /** @deprecated use the sensorDecimalPlaces property; goes in protocol 5 */
    save_sensor_decimal_places: { args: [places: number]; returns: void }
    /** @deprecated use the sensorTempUnit property; goes in protocol 5 */
    save_sensor_temp_unit: { args: [unit: string]; returns: void }
    /** @deprecated use the sensorAltitudeUnit property; goes in protocol 5 */
    save_sensor_altitude_unit: { args: [unit: string]; returns: void }
    save_sensor_card_visible: { args: [card: string, visible: boolean]; returns: void }
    /** @deprecated use the gestureSensorEnabled property; goes in protocol 5 */
    save_gesture_sensor_enabled: { args: [enabled: boolean]; returns: void }
    save_gesture_action: { args: [gesture: string, action: string]; returns: void }
    save_gesture_mapping: { args: [mapping: Record<string, unknown>]; returns: void }
    reset_gesture_mapping: { args: []; returns: void }
    /** @deprecated use the gestureVolumeStep property; goes in protocol 5 */
    save_gesture_volume_step: { args: [step: number]; returns: void }
    /** @deprecated use the gestureCooldown property; goes in protocol 5 */
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
    /** @deprecated use the directoryHistory property; goes in protocol 5 */
    get_directory_history: { args: []; returns: unknown[] }
    /** @deprecated use the homeOBDParameters property; goes in protocol 5 */
    get_home_obd_parameters: { args: []; returns: unknown[] }
    /** @deprecated use the autoPlayOnStartup property; goes in protocol 5 */
    get_auto_play_on_startup: { args: []; returns: boolean }
    /** @deprecated use the persistShuffleState property; goes in protocol 5 */
    get_persist_shuffle_state: { args: []; returns: boolean }
    /** @deprecated use the lastShuffleState property; goes in protocol 5 */
    get_last_shuffle_state: { args: []; returns: boolean }
    /** @deprecated use the lastPlayedSong property; goes in protocol 5 */
    get_last_played_song: { args: []; returns: string }
    /** @deprecated use the lastPlayedPosition property; goes in protocol 5 */
    get_last_played_position: { args: []; returns: number }
    /** @deprecated use the lastPlayedPlaylist property; goes in protocol 5 */
    get_last_played_playlist: { args: []; returns: string }
    /** @deprecated use the lastSettingsSection property; goes in protocol 5 */
    get_last_settings_section: { args: []; returns: string }
    /** @deprecated use the windowState property; goes in protocol 5 */
    get_window_state: { args: []; returns: string }
    /** @deprecated use the musicButtonDefaultPage property; goes in protocol 5 */
    get_music_button_default_page: { args: []; returns: string }
    /** @deprecated use the returnToLibraryAfterSelection property; goes in protocol 5 */
    get_return_to_library_after_selection: { args: []; returns: boolean }
    flushPendingSave: { args: []; returns: void }
    /** @deprecated use the androidAutoEnabled property; goes in protocol 5 */
    get_android_auto_enabled: { args: []; returns: boolean }
    /** @deprecated use the phoneMirrorEnabled property; goes in protocol 5 */
    get_phone_mirror_enabled: { args: []; returns: boolean }
    /** @deprecated use the scrcpyDisplaySize property; goes in protocol 5 */
    get_scrcpy_display_size: { args: []; returns: string }
    /** @deprecated use the scrcpyAudioGain property; goes in protocol 5 */
    get_scrcpy_audio_gain: { args: []; returns: number }
    /** @deprecated use the scrcpyAudioDuckEnabled property; goes in protocol 5 */
    get_scrcpy_audio_duck_enabled: { args: []; returns: boolean }
    /** @deprecated use the scrcpyAudioDuckLevel property; goes in protocol 5 */
    get_scrcpy_audio_duck_level: { args: []; returns: number }
    /** @deprecated use the scrcpyPhoneScreenOff property; goes in protocol 5 */
    get_scrcpy_phone_screen_off: { args: []; returns: boolean }
    /** @deprecated use the scrcpyDisplayDpi property; goes in protocol 5 */
    get_scrcpy_display_dpi: { args: []; returns: number }
    /** @deprecated use the scrcpyBitRateUsb property; goes in protocol 5 */
    get_scrcpy_bit_rate_usb: { args: []; returns: number }
    /** @deprecated use the scrcpyBitRateWifi property; goes in protocol 5 */
    get_scrcpy_bit_rate_wifi: { args: []; returns: number }
    /** @deprecated use the scrcpyAutoWifi property; goes in protocol 5 */
    get_scrcpy_auto_wifi: { args: []; returns: boolean }
    /** @deprecated use the scrcpyBitRateAuto property; goes in protocol 5 */
    get_scrcpy_bit_rate_auto: { args: []; returns: boolean }
    /** @deprecated use the scrcpyWirelessAddress property; goes in protocol 5 */
    get_scrcpy_wireless_address: { args: []; returns: string }
    /** @deprecated use the esp32VolumeEnabled property; goes in protocol 5 */
    get_esp32_volume_enabled: { args: []; returns: boolean }
    /** @deprecated use the esp32VolumePort property; goes in protocol 5 */
    get_esp32_volume_port: { args: []; returns: string }
    /** @deprecated use the esp32VolumeStepSize property; goes in protocol 5 */
    get_esp32_volume_step_size: { args: []; returns: number }
    /** @deprecated use the esp32AutoReconnect property; goes in protocol 5 */
    get_esp32_auto_reconnect: { args: []; returns: boolean }
    /** @deprecated use the esp32LedSleepEnabled property; goes in protocol 5 */
    get_esp32_led_sleep_enabled: { args: []; returns: boolean }
    /** @deprecated use the esp32LedColorMode property; goes in protocol 5 */
    get_esp32_led_color_mode: { args: []; returns: string }
    /** @deprecated use the esp32LedStaticColor property; goes in protocol 5 */
    get_esp32_led_static_color: { args: []; returns: string }
    /** @deprecated use the showWaveformVisualizer property; goes in protocol 5 */
    get_show_waveform_visualizer: { args: []; returns: boolean }
    /** @deprecated use the imuEnabled property; goes in protocol 5 */
    get_imu_enabled: { args: []; returns: boolean }
    /** @deprecated use the gestureSensorEnabled property; goes in protocol 5 */
    get_gesture_sensor_enabled: { args: []; returns: boolean }
    get_gesture_mapping: { args: []; returns: string }
    get_gesture_mapping_dict: { args: []; returns: Record<string, unknown> }
    is_setting_pinned: { args: [settingId: string]; returns: boolean }
    get_settings_registry: { args: []; returns: string }
    get_pinned_settings_metadata: { args: []; returns: string }
    /** @deprecated use the settingsMenuVisibility property; goes in protocol 5 */
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
    /** @deprecated Home is a dashboard: dashboardManager.homeDashboardChanged; goes with the classic Home */
    homeOBDParametersChanged: []
    /** @deprecated Home is a dashboard: place a VehicleWidget; goes with the classic Home */
    homeVehicleCardChanged: [value: boolean]
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

/**
 * spotifyManager (SpotifyManager)
 * @experimental not covered by the protocol version; may change or go in any release
 */
export interface SpotifyManager {
  properties: {
    /** QString, notify stateChanged */
    state: string
    /** bool, notify stateChanged */
    signedIn: boolean
    /** QString, notify clientIdChanged */
    clientId: string
    /** bool, constant */
    builtInClientId: boolean
    /** QString, constant */
    redirectUri: string
    /** QString, notify userChanged */
    userName: string
    /** QString, notify statusChanged */
    status: string
    /** QVariantList, notify libraryChanged */
    playlists: unknown[]
    /** QVariantList, notify libraryChanged */
    albums: unknown[]
    /** bool, notify libraryLoadingChanged */
    libraryLoading: boolean
    /** QString, notify collectionChanged */
    collectionUri: string
    /** QVariantMap, notify collectionChanged */
    collection: Record<string, unknown>
    /** QVariantList, notify collectionTracksChanged */
    collectionTracks: unknown[]
    /** bool, notify collectionTracksChanged */
    collectionLoading: boolean
    /** QVariantList, notify devicesChanged */
    devices: unknown[]
    /** bool, notify trackChanged */
    hasPlayback: boolean
    /** bool, notify playingChanged */
    playing: boolean
    /** QString, notify trackChanged */
    title: string
    /** QString, notify trackChanged */
    artist: string
    /** QString, notify trackChanged */
    album: string
    /** QString, notify trackChanged */
    trackUri: string
    /** QString, notify trackChanged */
    contextUri: string
    /** QString, notify artChanged */
    artUrl: string
    /** int, notify trackChanged */
    durationMs: number
    /** int, notify positionReported */
    positionMs: number
    /** bool, notify modesChanged */
    shuffle: boolean
    /** QString, notify modesChanged */
    repeat: string
    /** QString, notify deviceChanged */
    deviceName: string
    /** int, notify deviceChanged */
    volumePercent: number
    /** bool, notify deviceChanged */
    supportsVolume: boolean
  }
  methods: {
    setClientId: { args: [clientId: string]; returns: void }
    signIn: { args: []; returns: void }
    signOut: { args: []; returns: void }
    refreshLibrary: { args: []; returns: void }
    refreshDevices: { args: []; returns: void }
    openCollection: { args: [uri: string]; returns: void }
    playCollection: { args: [uri: string, index: number] | [uri: string]; returns: void }
    togglePlay: { args: []; returns: void }
    play: { args: []; returns: void }
    pause: { args: []; returns: void }
    next: { args: []; returns: void }
    previous: { args: []; returns: void }
    seek: { args: [positionMs: number]; returns: void }
    setShuffle: { args: [on: boolean]; returns: void }
    toggleShuffle: { args: []; returns: void }
    cycleRepeat: { args: []; returns: void }
    setVolume: { args: [percent: number]; returns: void }
    transferTo: { args: [deviceId: string]; returns: void }
  }
  signals: {
    stateChanged: []
    clientIdChanged: []
    userChanged: []
    statusChanged: []
    libraryChanged: []
    libraryLoadingChanged: []
    collectionChanged: []
    collectionTracksChanged: []
    devicesChanged: []
    playingChanged: []
    trackChanged: []
    artChanged: []
    positionReported: []
    modesChanged: []
    deviceChanged: []
    playbackRequested: []
    commandFailed: [message: string]
    startedElsewhere: []
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
    requestHealthTrends: { args: [days: number] | []; returns: void }
    requestParamTrend: { args: [paramId: string, days: number] | [paramId: string]; returns: void }
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
    healthTrendsReady: [days: number, result: Record<string, unknown>]
    paramTrendReady: [paramId: string, days: number, rows: unknown[]]
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

/**
 * vehicleManager (VehicleManager)
 * @experimental not covered by the protocol version; may change or go in any release
 */
export interface VehicleManager {
  properties: {
    /** QVariantList, notify vehiclesChanged */
    vehicles: unknown[]
    /** QString, writable, notify activeChanged */
    activeId: string
    /** QVariantMap, notify activeChanged */
    active: Record<string, unknown>
    /** bool, constant */
    canImport: boolean
    /** QString, notify lastErrorChanged */
    lastError: string
  }
  methods: {
    importModel: { args: [path: string, name: string] | [path: string]; returns: string }
    remove: { args: [id: string]; returns: boolean }
    select: { args: [id: string]; returns: boolean }
    rename: { args: [id: string, name: string]; returns: boolean }
    setOrientation: { args: [id: string, yaw: number, tilt: number]; returns: boolean }
    picturePath: { args: [id: string]; returns: string }
    pictureSaved: { args: [id: string]; returns: void }
  }
  signals: {
    vehiclesChanged: []
    activeChanged: []
    lastErrorChanged: []
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
    escapeGesture: []
  }
}

/** Every object the service exposes, by its name in `hello`. */
export interface Objects {
  airPlayManager: AirPlayManager
  androidAutoManager: AndroidAutoManager
  audioAnalyzer: AudioAnalyzer
  berryIMU: BerryIMUManager
  busMonitor: BusMonitor
  clock: Clock
  customPids: CustomPids
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
  spotifyManager: SpotifyManager
  themeCatalog: ThemeCatalog
  tripRecorder: TripRecorder
  units: Units
  vehicleManager: VehicleManager
  volumeController: VolumeController
}

export const PROTOCOL_VERSION_OF_TYPES = 3
