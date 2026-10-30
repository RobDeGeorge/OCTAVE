// Code generated from docs/api/<object>.json by internal/gen; DO NOT EDIT.
// Regenerate with `go generate ./...` after `octave --dump-api docs/api`.

package octave

// ObjectNames is every service object, as hello lists them.
var ObjectNames = []string{"airPlayManager", "androidAutoManager", "audioAnalyzer", "berryIMU", "clock", "dashboardManager", "diagnosticsManager", "esp32VolumeManager", "fontCatalog", "frontendManager", "gestureSensor", "mediaManager", "networkManager", "nowPlaying", "obdAdapterFinder", "obdManager", "paramStore", "parameterCatalog", "phoneMirrorManager", "settingsManager", "shiftLightManager", "sim", "themeCatalog", "tripRecorder", "units", "volumeController"}

// AirPlayManager is the service object "airPlayManager" (C++ AirPlayManager); NewAirPlayManager(c) binds it to a client.
type AirPlayManager struct{ c *Client }

// NewAirPlayManager binds "airPlayManager" to c.
func NewAirPlayManager(c *Client) AirPlayManager { return AirPlayManager{c} }

// Name is the object's name on the wire.
func (AirPlayManager) Name() string { return "airPlayManager" }

// GetAvailable reads the property "available" (bool).
//
// Experimental: not covered by the protocol version; may change or go in any release.
func (o AirPlayManager) GetAvailable() (v bool, err error) {
	err = o.c.Get("airPlayManager", "available", &v)
	return
}

// GetEnabled reads the property "enabled" (bool).
//
// Experimental: not covered by the protocol version; may change or go in any release.
func (o AirPlayManager) GetEnabled() (v bool, err error) {
	err = o.c.Get("airPlayManager", "enabled", &v)
	return
}

// GetState reads the property "state" (QString).
//
// Experimental: not covered by the protocol version; may change or go in any release.
func (o AirPlayManager) GetState() (v string, err error) {
	err = o.c.Get("airPlayManager", "state", &v)
	return
}

// GetStatus reads the property "status" (QString).
//
// Experimental: not covered by the protocol version; may change or go in any release.
func (o AirPlayManager) GetStatus() (v string, err error) {
	err = o.c.Get("airPlayManager", "status", &v)
	return
}

// GetReceiverName reads the property "receiverName" (QString).
//
// Experimental: not covered by the protocol version; may change or go in any release.
func (o AirPlayManager) GetReceiverName() (v string, err error) {
	err = o.c.Get("airPlayManager", "receiverName", &v)
	return
}

// GetDeviceName reads the property "deviceName" (QString).
//
// Experimental: not covered by the protocol version; may change or go in any release.
func (o AirPlayManager) GetDeviceName() (v string, err error) {
	err = o.c.Get("airPlayManager", "deviceName", &v)
	return
}

// GetDeviceModel reads the property "deviceModel" (QString).
//
// Experimental: not covered by the protocol version; may change or go in any release.
func (o AirPlayManager) GetDeviceModel() (v string, err error) {
	err = o.c.Get("airPlayManager", "deviceModel", &v)
	return
}

// GetPin reads the property "pin" (QString).
//
// Experimental: not covered by the protocol version; may change or go in any release.
func (o AirPlayManager) GetPin() (v string, err error) {
	err = o.c.Get("airPlayManager", "pin", &v)
	return
}

// GetMirroring reads the property "mirroring" (bool).
//
// Experimental: not covered by the protocol version; may change or go in any release.
func (o AirPlayManager) GetMirroring() (v bool, err error) {
	err = o.c.Get("airPlayManager", "mirroring", &v)
	return
}

// GetAudioStreaming reads the property "audioStreaming" (bool).
//
// Experimental: not covered by the protocol version; may change or go in any release.
func (o AirPlayManager) GetAudioStreaming() (v bool, err error) {
	err = o.c.Get("airPlayManager", "audioStreaming", &v)
	return
}

// GetFrameWidth reads the property "frameWidth" (int).
//
// Experimental: not covered by the protocol version; may change or go in any release.
func (o AirPlayManager) GetFrameWidth() (v int, err error) {
	err = o.c.Get("airPlayManager", "frameWidth", &v)
	return
}

// GetFrameHeight reads the property "frameHeight" (int).
//
// Experimental: not covered by the protocol version; may change or go in any release.
func (o AirPlayManager) GetFrameHeight() (v int, err error) {
	err = o.c.Get("airPlayManager", "frameHeight", &v)
	return
}

// GetTitle reads the property "title" (QString).
//
// Experimental: not covered by the protocol version; may change or go in any release.
func (o AirPlayManager) GetTitle() (v string, err error) {
	err = o.c.Get("airPlayManager", "title", &v)
	return
}

// GetArtist reads the property "artist" (QString).
//
// Experimental: not covered by the protocol version; may change or go in any release.
func (o AirPlayManager) GetArtist() (v string, err error) {
	err = o.c.Get("airPlayManager", "artist", &v)
	return
}

// GetAlbum reads the property "album" (QString).
//
// Experimental: not covered by the protocol version; may change or go in any release.
func (o AirPlayManager) GetAlbum() (v string, err error) {
	err = o.c.Get("airPlayManager", "album", &v)
	return
}

// GetAddresses reads the property "addresses" (QStringList).
//
// Experimental: not covered by the protocol version; may change or go in any release.
func (o AirPlayManager) GetAddresses() (v []string, err error) {
	err = o.c.Get("airPlayManager", "addresses", &v)
	return
}

// GetVideoSink reads the property "videoSink" (QObject*, writable).
//
// Experimental: not covered by the protocol version; may change or go in any release.
func (o AirPlayManager) GetVideoSink() (v any, err error) {
	err = o.c.Get("airPlayManager", "videoSink", &v)
	return
}

// SetVideoSink writes the property "videoSink" and returns the value read back.
//
// Experimental: not covered by the protocol version; may change or go in any release.
func (o AirPlayManager) SetVideoSink(value any) (v any, err error) {
	err = o.c.Set("airPlayManager", "videoSink", value, &v)
	return
}

// GetTouchAvailable reads the property "touchAvailable" (bool).
//
// Experimental: not covered by the protocol version; may change or go in any release.
func (o AirPlayManager) GetTouchAvailable() (v bool, err error) {
	err = o.c.Get("airPlayManager", "touchAvailable", &v)
	return
}

// GetTouchEnabled reads the property "touchEnabled" (bool).
//
// Experimental: not covered by the protocol version; may change or go in any release.
func (o AirPlayManager) GetTouchEnabled() (v bool, err error) {
	err = o.c.Get("airPlayManager", "touchEnabled", &v)
	return
}

// GetTouchState reads the property "touchState" (QString).
//
// Experimental: not covered by the protocol version; may change or go in any release.
func (o AirPlayManager) GetTouchState() (v string, err error) {
	err = o.c.Get("airPlayManager", "touchState", &v)
	return
}

// GetTouchStatus reads the property "touchStatus" (QString).
//
// Experimental: not covered by the protocol version; may change or go in any release.
func (o AirPlayManager) GetTouchStatus() (v string, err error) {
	err = o.c.Get("airPlayManager", "touchStatus", &v)
	return
}

// GetTouchDevice reads the property "touchDevice" (QString).
//
// Experimental: not covered by the protocol version; may change or go in any release.
func (o AirPlayManager) GetTouchDevice() (v string, err error) {
	err = o.c.Get("airPlayManager", "touchDevice", &v)
	return
}

// GetTouchPairing reads the property "touchPairing" (bool).
//
// Experimental: not covered by the protocol version; may change or go in any release.
func (o AirPlayManager) GetTouchPairing() (v bool, err error) {
	err = o.c.Get("airPlayManager", "touchPairing", &v)
	return
}

// GetTouchCalibrating reads the property "touchCalibrating" (bool).
//
// Experimental: not covered by the protocol version; may change or go in any release.
func (o AirPlayManager) GetTouchCalibrating() (v bool, err error) {
	err = o.c.Get("airPlayManager", "touchCalibrating", &v)
	return
}

// SetEnabled calls setEnabled(bool).
//
// Experimental: not covered by the protocol version; may change or go in any release.
func (o AirPlayManager) SetEnabled(enabled bool) error {
	return o.c.Call("airPlayManager", "setEnabled", nil, enabled)
}

// SetTouchEnabled calls setTouchEnabled(bool).
//
// Experimental: not covered by the protocol version; may change or go in any release.
func (o AirPlayManager) SetTouchEnabled(enabled bool) error {
	return o.c.Call("airPlayManager", "setTouchEnabled", nil, enabled)
}

// PairTouch1 calls pairTouch(bool).
//
// Experimental: not covered by the protocol version; may change or go in any release.
func (o AirPlayManager) PairTouch1(open bool) error {
	return o.c.Call("airPlayManager", "pairTouch", nil, open)
}

// PairTouch0 calls pairTouch().
//
// Experimental: not covered by the protocol version; may change or go in any release.
func (o AirPlayManager) PairTouch0() error {
	return o.c.Call("airPlayManager", "pairTouch", nil)
}

// InjectTouch calls injectTouch(int, int, double, double).
//
// Experimental: not covered by the protocol version; may change or go in any release.
func (o AirPlayManager) InjectTouch(pointerId int, action int, x float64, y float64) error {
	return o.c.Call("airPlayManager", "injectTouch", nil, pointerId, action, x, y)
}

// Scroll calls scroll(int, int).
//
// Experimental: not covered by the protocol version; may change or go in any release.
func (o AirPlayManager) Scroll(dx int, dy int) error {
	return o.c.Call("airPlayManager", "scroll", nil, dx, dy)
}

// CalibrateTouch calls calibrateTouch().
//
// Experimental: not covered by the protocol version; may change or go in any release.
func (o AirPlayManager) CalibrateTouch() error {
	return o.c.Call("airPlayManager", "calibrateTouch", nil)
}

// CancelCalibration calls cancelCalibration().
//
// Experimental: not covered by the protocol version; may change or go in any release.
func (o AirPlayManager) CancelCalibration() error {
	return o.c.Call("airPlayManager", "cancelCalibration", nil)
}

// ForgetDevices calls forgetDevices().
//
// Experimental: not covered by the protocol version; may change or go in any release.
func (o AirPlayManager) ForgetDevices() error {
	return o.c.Call("airPlayManager", "forgetDevices", nil)
}

// AndroidAutoManager is the service object "androidAutoManager" (C++ AndroidAutoManager); NewAndroidAutoManager(c) binds it to a client.
type AndroidAutoManager struct{ c *Client }

// NewAndroidAutoManager binds "androidAutoManager" to c.
func NewAndroidAutoManager(c *Client) AndroidAutoManager { return AndroidAutoManager{c} }

// Name is the object's name on the wire.
func (AndroidAutoManager) Name() string { return "androidAutoManager" }

// GetIsDhuInstalled reads the property "isDhuInstalled" (bool).
func (o AndroidAutoManager) GetIsDhuInstalled() (v bool, err error) {
	err = o.c.Get("androidAutoManager", "isDhuInstalled", &v)
	return
}

// GetDhuPath reads the property "dhuPath" (QString).
func (o AndroidAutoManager) GetDhuPath() (v string, err error) {
	err = o.c.Get("androidAutoManager", "dhuPath", &v)
	return
}

// GetIsDhuEmbedded reads the property "isDhuEmbedded" (bool).
func (o AndroidAutoManager) GetIsDhuEmbedded() (v bool, err error) {
	err = o.c.Get("androidAutoManager", "isDhuEmbedded", &v)
	return
}

// GetDhuWindowHandle reads the property "dhuWindowHandle" (int).
func (o AndroidAutoManager) GetDhuWindowHandle() (v int, err error) {
	err = o.c.Get("androidAutoManager", "dhuWindowHandle", &v)
	return
}

// GetDhuCapture reads the property "dhuCapture" (QObject*).
func (o AndroidAutoManager) GetDhuCapture() (v any, err error) {
	err = o.c.Get("androidAutoManager", "dhuCapture", &v)
	return
}

// GetBackend reads the property "backend" (QString).
//
// Experimental: not covered by the protocol version; may change or go in any release.
func (o AndroidAutoManager) GetBackend() (v string, err error) {
	err = o.c.Get("androidAutoManager", "backend", &v)
	return
}

// GetNativeAvailable reads the property "nativeAvailable" (bool).
//
// Experimental: not covered by the protocol version; may change or go in any release.
func (o AndroidAutoManager) GetNativeAvailable() (v bool, err error) {
	err = o.c.Get("androidAutoManager", "nativeAvailable", &v)
	return
}

// GetNativeState reads the property "nativeState" (QString).
//
// Experimental: not covered by the protocol version; may change or go in any release.
func (o AndroidAutoManager) GetNativeState() (v string, err error) {
	err = o.c.Get("androidAutoManager", "nativeState", &v)
	return
}

// GetNativeStatus reads the property "nativeStatus" (QString).
//
// Experimental: not covered by the protocol version; may change or go in any release.
func (o AndroidAutoManager) GetNativeStatus() (v string, err error) {
	err = o.c.Get("androidAutoManager", "nativeStatus", &v)
	return
}

// GetPhoneName reads the property "phoneName" (QString).
//
// Experimental: not covered by the protocol version; may change or go in any release.
func (o AndroidAutoManager) GetPhoneName() (v string, err error) {
	err = o.c.Get("androidAutoManager", "phoneName", &v)
	return
}

// GetVideoStreaming reads the property "videoStreaming" (bool).
//
// Experimental: not covered by the protocol version; may change or go in any release.
func (o AndroidAutoManager) GetVideoStreaming() (v bool, err error) {
	err = o.c.Get("androidAutoManager", "videoStreaming", &v)
	return
}

// GetCertificateDir reads the property "certificateDir" (QString).
//
// Experimental: not covered by the protocol version; may change or go in any release.
func (o AndroidAutoManager) GetCertificateDir() (v string, err error) {
	err = o.c.Get("androidAutoManager", "certificateDir", &v)
	return
}

// GetVideoSink reads the property "videoSink" (QObject*, writable).
//
// Experimental: not covered by the protocol version; may change or go in any release.
func (o AndroidAutoManager) GetVideoSink() (v any, err error) {
	err = o.c.Get("androidAutoManager", "videoSink", &v)
	return
}

// SetVideoSink writes the property "videoSink" and returns the value read back.
//
// Experimental: not covered by the protocol version; may change or go in any release.
func (o AndroidAutoManager) SetVideoSink(value any) (v any, err error) {
	err = o.c.Set("androidAutoManager", "videoSink", value, &v)
	return
}

// GetDhuInstallInstructions calls getDhuInstallInstructions() -> QString.
func (o AndroidAutoManager) GetDhuInstallInstructions() (v string, err error) {
	err = o.c.Call("androidAutoManager", "getDhuInstallInstructions", &v)
	return
}

// LaunchDhuSeamless calls launchDhuSeamless() -> bool.
func (o AndroidAutoManager) LaunchDhuSeamless() (v bool, err error) {
	err = o.c.Call("androidAutoManager", "launchDhuSeamless", &v)
	return
}

// SendDhuClick calls sendDhuClick(int, int).
func (o AndroidAutoManager) SendDhuClick(x int, y int) error {
	return o.c.Call("androidAutoManager", "sendDhuClick", nil, x, y)
}

// CloseDhu calls closeDhu().
func (o AndroidAutoManager) CloseDhu() error {
	return o.c.Call("androidAutoManager", "closeDhu", nil)
}

// Cleanup calls cleanup().
func (o AndroidAutoManager) Cleanup() error {
	return o.c.Call("androidAutoManager", "cleanup", nil)
}

// StartNative calls startNative() -> bool.
//
// Experimental: not covered by the protocol version; may change or go in any release.
func (o AndroidAutoManager) StartNative() (v bool, err error) {
	err = o.c.Call("androidAutoManager", "startNative", &v)
	return
}

// StopNative calls stopNative().
//
// Experimental: not covered by the protocol version; may change or go in any release.
func (o AndroidAutoManager) StopNative() error {
	return o.c.Call("androidAutoManager", "stopNative", nil)
}

// InjectTouch calls injectTouch(int, int, double, double).
//
// Experimental: not covered by the protocol version; may change or go in any release.
func (o AndroidAutoManager) InjectTouch(pointerId int, action int, x float64, y float64) error {
	return o.c.Call("androidAutoManager", "injectTouch", nil, pointerId, action, x, y)
}

// SetViewVisible calls setViewVisible(bool).
//
// Experimental: not covered by the protocol version; may change or go in any release.
func (o AndroidAutoManager) SetViewVisible(visible bool) error {
	return o.c.Call("androidAutoManager", "setViewVisible", nil, visible)
}

// AudioAnalyzer is the service object "audioAnalyzer" (C++ AudioAnalyzer); NewAudioAnalyzer(c) binds it to a client.
type AudioAnalyzer struct{ c *Client }

// NewAudioAnalyzer binds "audioAnalyzer" to c.
func NewAudioAnalyzer(c *Client) AudioAnalyzer { return AudioAnalyzer{c} }

// Name is the object's name on the wire.
func (AudioAnalyzer) Name() string { return "audioAnalyzer" }

// GetLevels reads the property "levels" (QVariantList).
func (o AudioAnalyzer) GetLevels() (v []any, err error) {
	err = o.c.Get("audioAnalyzer", "levels", &v)
	return
}

// GetNumBars reads the property "numBars" (int).
func (o AudioAnalyzer) GetNumBars() (v int, err error) {
	err = o.c.Get("audioAnalyzer", "numBars", &v)
	return
}

// SetViewActive calls setViewActive(bool).
func (o AudioAnalyzer) SetViewActive(active bool) error {
	return o.c.Call("audioAnalyzer", "setViewActive", nil, active)
}

// SetDemand calls setDemand(QString, bool).
func (o AudioAnalyzer) SetDemand(consumer string, wanted bool) error {
	return o.c.Call("audioAnalyzer", "setDemand", nil, consumer, wanted)
}

// SetPlaying calls setPlaying(bool).
func (o AudioAnalyzer) SetPlaying(playing bool) error {
	return o.c.Call("audioAnalyzer", "setPlaying", nil, playing)
}

// SetLocalFile calls setLocalFile(QString).
func (o AudioAnalyzer) SetLocalFile(filePath string) error {
	return o.c.Call("audioAnalyzer", "setLocalFile", nil, filePath)
}

// SetPosition calls setPosition(qlonglong).
func (o AudioAnalyzer) SetPosition(positionMs int64) error {
	return o.c.Call("audioAnalyzer", "setPosition", nil, positionMs)
}

// AnalyzeFile calls analyze_file(QString).
func (o AudioAnalyzer) AnalyzeFile(filePath string) error {
	return o.c.Call("audioAnalyzer", "analyze_file", nil, filePath)
}

// UpdatePosition calls update_position(float).
func (o AudioAnalyzer) UpdatePosition(positionSeconds float64) error {
	return o.c.Call("audioAnalyzer", "update_position", nil, positionSeconds)
}

// SetActive calls set_active(bool).
func (o AudioAnalyzer) SetActive(active bool) error {
	return o.c.Call("audioAnalyzer", "set_active", nil, active)
}

// GetCurrentLevels calls get_current_levels() -> QVariantList.
func (o AudioAnalyzer) GetCurrentLevels() (v []any, err error) {
	err = o.c.Call("audioAnalyzer", "get_current_levels", &v)
	return
}

// GetNumBarsCall calls get_num_bars() -> int.
func (o AudioAnalyzer) GetNumBarsCall() (v int, err error) {
	err = o.c.Call("audioAnalyzer", "get_num_bars", &v)
	return
}

// IsAnalyzed calls is_analyzed() -> bool.
func (o AudioAnalyzer) IsAnalyzed() (v bool, err error) {
	err = o.c.Call("audioAnalyzer", "is_analyzed", &v)
	return
}

// Clear calls clear().
func (o AudioAnalyzer) Clear() error {
	return o.c.Call("audioAnalyzer", "clear", nil)
}

// SetLive calls set_live(bool).
func (o AudioAnalyzer) SetLive(live bool) error {
	return o.c.Call("audioAnalyzer", "set_live", nil, live)
}

// UpdateLive calls update_live().
func (o AudioAnalyzer) UpdateLive() error {
	return o.c.Call("audioAnalyzer", "update_live", nil)
}

// FeedLivePcm calls feed_live_pcm(QByteArray, int).
func (o AudioAnalyzer) FeedLivePcm(pcm string, bufferedMs int) error {
	return o.c.Call("audioAnalyzer", "feed_live_pcm", nil, pcm, bufferedMs)
}

// BerryIMU is the service object "berryIMU" (C++ BerryIMUManager); NewBerryIMU(c) binds it to a client.
type BerryIMU struct{ c *Client }

// NewBerryIMU binds "berryIMU" to c.
func NewBerryIMU(c *Client) BerryIMU { return BerryIMU{c} }

// Name is the object's name on the wire.
func (BerryIMU) Name() string { return "berryIMU" }

// GetConnected reads the property "connected" (bool).
func (o BerryIMU) GetConnected() (v bool, err error) {
	err = o.c.Get("berryIMU", "connected", &v)
	return
}

// GetHasTemperature reads the property "hasTemperature" (bool).
func (o BerryIMU) GetHasTemperature() (v bool, err error) {
	err = o.c.Get("berryIMU", "hasTemperature", &v)
	return
}

// GetConnectionStatus calls getConnectionStatus() -> QString.
func (o BerryIMU) GetConnectionStatus() (v string, err error) {
	err = o.c.Call("berryIMU", "getConnectionStatus", &v)
	return
}

// SetEnabled calls setEnabled(bool).
func (o BerryIMU) SetEnabled(enabled bool) error {
	return o.c.Call("berryIMU", "setEnabled", nil, enabled)
}

// IsEnabled calls isEnabled() -> bool.
func (o BerryIMU) IsEnabled() (v bool, err error) {
	err = o.c.Call("berryIMU", "isEnabled", &v)
	return
}

// SetEmitRate calls setEmitRate(int).
func (o BerryIMU) SetEmitRate(hz int) error {
	return o.c.Call("berryIMU", "setEmitRate", nil, hz)
}

// EmitRate calls emitRate() -> int.
func (o BerryIMU) EmitRate() (v int, err error) {
	err = o.c.Call("berryIMU", "emitRate", &v)
	return
}

// SetActive calls setActive(bool).
func (o BerryIMU) SetActive(active bool) error {
	return o.c.Call("berryIMU", "setActive", nil, active)
}

// SetDemand calls setDemand(QString, bool).
func (o BerryIMU) SetDemand(consumer string, wanted bool) error {
	return o.c.Call("berryIMU", "setDemand", nil, consumer, wanted)
}

// CalibrateTare calls calibrateTare().
func (o BerryIMU) CalibrateTare() error {
	return o.c.Call("berryIMU", "calibrateTare", nil)
}

// ResetTare calls resetTare().
func (o BerryIMU) ResetTare() error {
	return o.c.Call("berryIMU", "resetTare", nil)
}

// Cleanup calls cleanup().
func (o BerryIMU) Cleanup() error {
	return o.c.Call("berryIMU", "cleanup", nil)
}

// Clock is the service object "clock" (C++ Clock); NewClock(c) binds it to a client.
type Clock struct{ c *Client }

// NewClock binds "clock" to c.
func NewClock(c *Client) Clock { return Clock{c} }

// Name is the object's name on the wire.
func (Clock) Name() string { return "clock" }

// GetTime reads the property "time" (QString).
func (o Clock) GetTime() (v string, err error) {
	err = o.c.Get("clock", "time", &v)
	return
}

// UpdateTime calls update_time().
func (o Clock) UpdateTime() error {
	return o.c.Call("clock", "update_time", nil)
}

// DashboardManager is the service object "dashboardManager" (C++ DashboardManager); NewDashboardManager(c) binds it to a client.
type DashboardManager struct{ c *Client }

// NewDashboardManager binds "dashboardManager" to c.
func NewDashboardManager(c *Client) DashboardManager { return DashboardManager{c} }

// Name is the object's name on the wire.
func (DashboardManager) Name() string { return "dashboardManager" }

// GetDashboards reads the property "dashboards" (QVariantList).
func (o DashboardManager) GetDashboards() (v []any, err error) {
	err = o.c.Get("dashboardManager", "dashboards", &v)
	return
}

// GetLastShareError reads the property "lastShareError" (QString).
func (o DashboardManager) GetLastShareError() (v string, err error) {
	err = o.c.Get("dashboardManager", "lastShareError", &v)
	return
}

// GetLastError reads the property "lastError" (QString).
func (o DashboardManager) GetLastError() (v string, err error) {
	err = o.c.Get("dashboardManager", "lastError", &v)
	return
}

// GetLastExportPath reads the property "lastExportPath" (QString).
func (o DashboardManager) GetLastExportPath() (v string, err error) {
	err = o.c.Get("dashboardManager", "lastExportPath", &v)
	return
}

// GetHiddenBuiltInCount reads the property "hiddenBuiltInCount" (int).
func (o DashboardManager) GetHiddenBuiltInCount() (v int, err error) {
	err = o.c.Get("dashboardManager", "hiddenBuiltInCount", &v)
	return
}

// GetSupportedSchema reads the property "supportedSchema" (int).
func (o DashboardManager) GetSupportedSchema() (v int, err error) {
	err = o.c.Get("dashboardManager", "supportedSchema", &v)
	return
}

// GetWidgetCatalog reads the property "widgetCatalog" (QVariantMap).
func (o DashboardManager) GetWidgetCatalog() (v map[string]any, err error) {
	err = o.c.Get("dashboardManager", "widgetCatalog", &v)
	return
}

// SaveDashboard calls saveDashboard(QVariantMap) -> QString.
func (o DashboardManager) SaveDashboard(spec map[string]any) (v string, err error) {
	err = o.c.Call("dashboardManager", "saveDashboard", &v, spec)
	return
}

// DeleteDashboard calls deleteDashboard(QString) -> bool.
func (o DashboardManager) DeleteDashboard(id string) (v bool, err error) {
	err = o.c.Call("dashboardManager", "deleteDashboard", &v, id)
	return
}

// RestoreBuiltInDashboards calls restoreBuiltInDashboards() -> bool.
func (o DashboardManager) RestoreBuiltInDashboards() (v bool, err error) {
	err = o.c.Call("dashboardManager", "restoreBuiltInDashboards", &v)
	return
}

// DuplicateDashboard calls duplicateDashboard(QString, QString) -> QString.
func (o DashboardManager) DuplicateDashboard(sourceId string, newLabel string) (v string, err error) {
	err = o.c.Call("dashboardManager", "duplicateDashboard", &v, sourceId, newLabel)
	return
}

// ExportDashboard calls exportDashboard(QString) -> QString.
func (o DashboardManager) ExportDashboard(id string) (v string, err error) {
	err = o.c.Call("dashboardManager", "exportDashboard", &v, id)
	return
}

// CopyDashboardToClipboard calls copyDashboardToClipboard(QString) -> bool.
func (o DashboardManager) CopyDashboardToClipboard(id string) (v bool, err error) {
	err = o.c.Call("dashboardManager", "copyDashboardToClipboard", &v, id)
	return
}

// ImportDashboard calls importDashboard(QString) -> QString.
func (o DashboardManager) ImportDashboard(fileOrUrl string) (v string, err error) {
	err = o.c.Call("dashboardManager", "importDashboard", &v, fileOrUrl)
	return
}

// ImportDashboardFromClipboard calls importDashboardFromClipboard() -> QString.
func (o DashboardManager) ImportDashboardFromClipboard() (v string, err error) {
	err = o.c.Call("dashboardManager", "importDashboardFromClipboard", &v)
	return
}

// ImportDashboardFromText calls importDashboardFromText(QString) -> QString.
func (o DashboardManager) ImportDashboardFromText(json string) (v string, err error) {
	err = o.c.Call("dashboardManager", "importDashboardFromText", &v, json)
	return
}

// Refresh calls refresh().
func (o DashboardManager) Refresh() error {
	return o.c.Call("dashboardManager", "refresh", nil)
}

// LoadDashboard calls loadDashboard(QString) -> QVariant.
func (o DashboardManager) LoadDashboard(id string) (v any, err error) {
	err = o.c.Call("dashboardManager", "loadDashboard", &v, id)
	return
}

// IsBuiltIn calls isBuiltIn(QString) -> bool.
func (o DashboardManager) IsBuiltIn(id string) (v bool, err error) {
	err = o.c.Call("dashboardManager", "isBuiltIn", &v, id)
	return
}

// UniqueIdForLabel calls uniqueIdForLabel(QString) -> QString.
func (o DashboardManager) UniqueIdForLabel(label string) (v string, err error) {
	err = o.c.Call("dashboardManager", "uniqueIdForLabel", &v, label)
	return
}

// NormalizedSpec calls normalizedSpec(QVariantMap) -> QVariantMap.
func (o DashboardManager) NormalizedSpec(spec map[string]any) (v map[string]any, err error) {
	err = o.c.Call("dashboardManager", "normalizedSpec", &v, spec)
	return
}

// CanPlace calls canPlace(QVariantList, int, int, int, int, int, int, int) -> bool.
func (o DashboardManager) CanPlace(cells []any, gridColumns int, gridRows int, col int, row int, colSpan int, rowSpan int, exceptIndex int) (v bool, err error) {
	err = o.c.Call("dashboardManager", "canPlace", &v, cells, gridColumns, gridRows, col, row, colSpan, rowSpan, exceptIndex)
	return
}

// FitAt calls fitAt(QVariantList, int, int, int, int, int, int) -> QVariantMap.
func (o DashboardManager) FitAt(cells []any, gridColumns int, gridRows int, col int, row int, colSpan int, rowSpan int) (v map[string]any, err error) {
	err = o.c.Call("dashboardManager", "fitAt", &v, cells, gridColumns, gridRows, col, row, colSpan, rowSpan)
	return
}

// Arrange calls arrange(QVariantList, int, int, int, int, int, int, int) -> QVariantMap.
//
// Experimental: not covered by the protocol version; may change or go in any release.
func (o DashboardManager) Arrange(cells []any, gridColumns int, gridRows int, index int, col int, row int, colSpan int, rowSpan int) (v map[string]any, err error) {
	err = o.c.Call("dashboardManager", "arrange", &v, cells, gridColumns, gridRows, index, col, row, colSpan, rowSpan)
	return
}

// MinSpan calls minSpan(QString, int, int, double, double, double) -> QVariantMap.
//
// Experimental: not covered by the protocol version; may change or go in any release.
func (o DashboardManager) MinSpan(type_ string, gridColumns int, gridRows int, cellWidth float64, cellHeight float64, spacing float64) (v map[string]any, err error) {
	err = o.c.Call("dashboardManager", "minSpan", &v, type_, gridColumns, gridRows, cellWidth, cellHeight, spacing)
	return
}

// SizeOptions calls sizeOptions(QString, int, int, double, double, double) -> QVariantList.
//
// Experimental: not covered by the protocol version; may change or go in any release.
func (o DashboardManager) SizeOptions(type_ string, gridColumns int, gridRows int, cellWidth float64, cellHeight float64, spacing float64) (v []any, err error) {
	err = o.c.Call("dashboardManager", "sizeOptions", &v, type_, gridColumns, gridRows, cellWidth, cellHeight, spacing)
	return
}

// DiagnosticsManager is the service object "diagnosticsManager" (C++ DiagnosticsManager); NewDiagnosticsManager(c) binds it to a client.
type DiagnosticsManager struct{ c *Client }

// NewDiagnosticsManager binds "diagnosticsManager" to c.
func NewDiagnosticsManager(c *Client) DiagnosticsManager { return DiagnosticsManager{c} }

// Name is the object's name on the wire.
func (DiagnosticsManager) Name() string { return "diagnosticsManager" }

// GetLogDir reads the property "logDir" (QString).
func (o DiagnosticsManager) GetLogDir() (v string, err error) {
	err = o.c.Get("diagnosticsManager", "logDir", &v)
	return
}

// GetAppVersion reads the property "appVersion" (QString).
func (o DiagnosticsManager) GetAppVersion() (v string, err error) {
	err = o.c.Get("diagnosticsManager", "appVersion", &v)
	return
}

// GetBackendName reads the property "backendName" (QString).
func (o DiagnosticsManager) GetBackendName() (v string, err error) {
	err = o.c.Get("diagnosticsManager", "backendName", &v)
	return
}

// GetDeviceInfo reads the property "deviceInfo" (QString).
func (o DiagnosticsManager) GetDeviceInfo() (v string, err error) {
	err = o.c.Get("diagnosticsManager", "deviceInfo", &v)
	return
}

// GetLastExportPath reads the property "lastExportPath" (QString).
func (o DiagnosticsManager) GetLastExportPath() (v string, err error) {
	err = o.c.Get("diagnosticsManager", "lastExportPath", &v)
	return
}

// GetCanOpenFolder reads the property "canOpenFolder" (bool).
func (o DiagnosticsManager) GetCanOpenFolder() (v bool, err error) {
	err = o.c.Get("diagnosticsManager", "canOpenFolder", &v)
	return
}

// RecentLogLines1 calls recentLogLines(int) -> QString.
func (o DiagnosticsManager) RecentLogLines1(maxLines int) (v string, err error) {
	err = o.c.Call("diagnosticsManager", "recentLogLines", &v, maxLines)
	return
}

// RecentLogLines0 calls recentLogLines() -> QString.
func (o DiagnosticsManager) RecentLogLines0() (v string, err error) {
	err = o.c.Call("diagnosticsManager", "recentLogLines", &v)
	return
}

// CopyLogsToClipboard1 calls copyLogsToClipboard(int) -> bool.
func (o DiagnosticsManager) CopyLogsToClipboard1(maxLines int) (v bool, err error) {
	err = o.c.Call("diagnosticsManager", "copyLogsToClipboard", &v, maxLines)
	return
}

// CopyLogsToClipboard0 calls copyLogsToClipboard() -> bool.
func (o DiagnosticsManager) CopyLogsToClipboard0() (v bool, err error) {
	err = o.c.Call("diagnosticsManager", "copyLogsToClipboard", &v)
	return
}

// ExportLogs calls exportLogs() -> QString.
func (o DiagnosticsManager) ExportLogs() (v string, err error) {
	err = o.c.Call("diagnosticsManager", "exportLogs", &v)
	return
}

// OpenLogFolder calls openLogFolder() -> bool.
func (o DiagnosticsManager) OpenLogFolder() (v bool, err error) {
	err = o.c.Call("diagnosticsManager", "openLogFolder", &v)
	return
}

// LicenseText calls licenseText(QString) -> QString.
func (o DiagnosticsManager) LicenseText(name string) (v string, err error) {
	err = o.c.Call("diagnosticsManager", "licenseText", &v, name)
	return
}

// Esp32VolumeManager is the service object "esp32VolumeManager" (C++ ESP32VolumeManager); NewEsp32VolumeManager(c) binds it to a client.
type Esp32VolumeManager struct{ c *Client }

// NewEsp32VolumeManager binds "esp32VolumeManager" to c.
func NewEsp32VolumeManager(c *Client) Esp32VolumeManager { return Esp32VolumeManager{c} }

// Name is the object's name on the wire.
func (Esp32VolumeManager) Name() string { return "esp32VolumeManager" }

// GetConnected reads the property "connected" (bool).
func (o Esp32VolumeManager) GetConnected() (v bool, err error) {
	err = o.c.Get("esp32VolumeManager", "connected", &v)
	return
}

// GetConnectionStatus reads the property "connectionStatus" (QString).
func (o Esp32VolumeManager) GetConnectionStatus() (v string, err error) {
	err = o.c.Get("esp32VolumeManager", "connectionStatus", &v)
	return
}

// GetConnectionDetail reads the property "connectionDetail" (QString).
func (o Esp32VolumeManager) GetConnectionDetail() (v string, err error) {
	err = o.c.Get("esp32VolumeManager", "connectionDetail", &v)
	return
}

// GetAvailablePorts reads the property "availablePorts" (QVariantList).
func (o Esp32VolumeManager) GetAvailablePorts() (v []any, err error) {
	err = o.c.Get("esp32VolumeManager", "availablePorts", &v)
	return
}

// ConnectDevice calls connect_device().
func (o Esp32VolumeManager) ConnectDevice() error {
	return o.c.Call("esp32VolumeManager", "connect_device", nil)
}

// DisconnectDevice calls disconnect_device().
func (o Esp32VolumeManager) DisconnectDevice() error {
	return o.c.Call("esp32VolumeManager", "disconnect_device", nil)
}

// GetAvailablePortsCall calls get_available_ports() -> QVariantList.
func (o Esp32VolumeManager) GetAvailablePortsCall() (v []any, err error) {
	err = o.c.Call("esp32VolumeManager", "get_available_ports", &v)
	return
}

// IsConnected calls is_connected() -> bool.
func (o Esp32VolumeManager) IsConnected() (v bool, err error) {
	err = o.c.Call("esp32VolumeManager", "is_connected", &v)
	return
}

// GetConnectionStatusCall calls get_connection_status() -> QString.
func (o Esp32VolumeManager) GetConnectionStatusCall() (v string, err error) {
	err = o.c.Call("esp32VolumeManager", "get_connection_status", &v)
	return
}

// GetConnectionDetailCall calls get_connection_detail() -> QString.
func (o Esp32VolumeManager) GetConnectionDetailCall() (v string, err error) {
	err = o.c.Call("esp32VolumeManager", "get_connection_detail", &v)
	return
}

// RefreshPorts calls refresh_ports().
func (o Esp32VolumeManager) RefreshPorts() error {
	return o.c.Call("esp32VolumeManager", "refresh_ports", nil)
}

// ResetReconnectAttempts calls reset_reconnect_attempts().
func (o Esp32VolumeManager) ResetReconnectAttempts() error {
	return o.c.Call("esp32VolumeManager", "reset_reconnect_attempts", nil)
}

// Cleanup calls cleanup().
func (o Esp32VolumeManager) Cleanup() error {
	return o.c.Call("esp32VolumeManager", "cleanup", nil)
}

// FontCatalog is the service object "fontCatalog" (C++ FontCatalog); NewFontCatalog(c) binds it to a client.
type FontCatalog struct{ c *Client }

// NewFontCatalog binds "fontCatalog" to c.
func NewFontCatalog(c *Client) FontCatalog { return FontCatalog{c} }

// Name is the object's name on the wire.
func (FontCatalog) Name() string { return "fontCatalog" }

// GetFonts reads the property "fonts" (QVariantList).
func (o FontCatalog) GetFonts() (v []any, err error) {
	err = o.c.Get("fontCatalog", "fonts", &v)
	return
}

// GetSymbolFamily reads the property "symbolFamily" (QString).
func (o FontCatalog) GetSymbolFamily() (v string, err error) {
	err = o.c.Get("fontCatalog", "symbolFamily", &v)
	return
}

// FrontendManager is the service object "frontendManager" (C++ FrontendManager); NewFrontendManager(c) binds it to a client.
type FrontendManager struct{ c *Client }

// NewFrontendManager binds "frontendManager" to c.
func NewFrontendManager(c *Client) FrontendManager { return FrontendManager{c} }

// Name is the object's name on the wire.
func (FrontendManager) Name() string { return "frontendManager" }

// GetFrontends reads the property "frontends" (QVariantList).
func (o FrontendManager) GetFrontends() (v []any, err error) {
	err = o.c.Get("frontendManager", "frontends", &v)
	return
}

// GetActiveId reads the property "activeId" (QString).
func (o FrontendManager) GetActiveId() (v string, err error) {
	err = o.c.Get("frontendManager", "activeId", &v)
	return
}

// GetRunningId reads the property "runningId" (QString).
func (o FrontendManager) GetRunningId() (v string, err error) {
	err = o.c.Get("frontendManager", "runningId", &v)
	return
}

// GetForcedQml reads the property "forcedQml" (bool).
func (o FrontendManager) GetForcedQml() (v bool, err error) {
	err = o.c.Get("frontendManager", "forcedQml", &v)
	return
}

// GetForcedQmlReason reads the property "forcedQmlReason" (QString).
func (o FrontendManager) GetForcedQmlReason() (v string, err error) {
	err = o.c.Get("frontendManager", "forcedQmlReason", &v)
	return
}

// GetLastError reads the property "lastError" (QString).
func (o FrontendManager) GetLastError() (v string, err error) {
	err = o.c.Get("frontendManager", "lastError", &v)
	return
}

// InstallFromFolder calls installFromFolder(QString) -> QString.
func (o FrontendManager) InstallFromFolder(path string) (v string, err error) {
	err = o.c.Call("frontendManager", "installFromFolder", &v, path)
	return
}

// InstallFromZip calls installFromZip(QString) -> QString.
func (o FrontendManager) InstallFromZip(path string) (v string, err error) {
	err = o.c.Call("frontendManager", "installFromZip", &v, path)
	return
}

// Remove calls remove(QString) -> bool.
func (o FrontendManager) Remove(id string) (v bool, err error) {
	err = o.c.Call("frontendManager", "remove", &v, id)
	return
}

// SetActive calls setActive(QString) -> bool.
func (o FrontendManager) SetActive(id string) (v bool, err error) {
	err = o.c.Call("frontendManager", "setActive", &v, id)
	return
}

// ClearForcedQml calls clearForcedQml().
func (o FrontendManager) ClearForcedQml() error {
	return o.c.Call("frontendManager", "clearForcedQml", nil)
}

// ManifestOf calls manifestOf(QString) -> QVariantMap.
func (o FrontendManager) ManifestOf(id string) (v map[string]any, err error) {
	err = o.c.Call("frontendManager", "manifestOf", &v, id)
	return
}

// GestureSensor is the service object "gestureSensor" (C++ GestureManager); NewGestureSensor(c) binds it to a client.
type GestureSensor struct{ c *Client }

// NewGestureSensor binds "gestureSensor" to c.
func NewGestureSensor(c *Client) GestureSensor { return GestureSensor{c} }

// Name is the object's name on the wire.
func (GestureSensor) Name() string { return "gestureSensor" }

// GetConnectionStatus reads the property "connectionStatus" (QString).
func (o GestureSensor) GetConnectionStatus() (v string, err error) {
	err = o.c.Get("gestureSensor", "connectionStatus", &v)
	return
}

// GetGestures reads the property "gestures" (QStringList).
func (o GestureSensor) GetGestures() (v []string, err error) {
	err = o.c.Get("gestureSensor", "gestures", &v)
	return
}

// GetActions reads the property "actions" (QVariantList).
func (o GestureSensor) GetActions() (v []any, err error) {
	err = o.c.Get("gestureSensor", "actions", &v)
	return
}

// GetConnectionStatusCall calls getConnectionStatus() -> QString.
func (o GestureSensor) GetConnectionStatusCall() (v string, err error) {
	err = o.c.Call("gestureSensor", "getConnectionStatus", &v)
	return
}

// SetGestureAction calls setGestureAction(QString, QString).
func (o GestureSensor) SetGestureAction(gesture string, action string) error {
	return o.c.Call("gestureSensor", "setGestureAction", nil, gesture, action)
}

// GetGestureAction calls getGestureAction(QString) -> QString.
func (o GestureSensor) GetGestureAction(gesture string) (v string, err error) {
	err = o.c.Call("gestureSensor", "getGestureAction", &v, gesture)
	return
}

// ResetMappingToDefaults calls resetMappingToDefaults().
func (o GestureSensor) ResetMappingToDefaults() error {
	return o.c.Call("gestureSensor", "resetMappingToDefaults", nil)
}

// SetEnabled calls setEnabled(bool).
func (o GestureSensor) SetEnabled(enabled bool) error {
	return o.c.Call("gestureSensor", "setEnabled", nil, enabled)
}

// SetCooldown calls setCooldown(int).
func (o GestureSensor) SetCooldown(ms int) error {
	return o.c.Call("gestureSensor", "setCooldown", nil, ms)
}

// IsEnabled calls isEnabled() -> bool.
func (o GestureSensor) IsEnabled() (v bool, err error) {
	err = o.c.Call("gestureSensor", "isEnabled", &v)
	return
}

// CooldownMs calls cooldownMs() -> int.
func (o GestureSensor) CooldownMs() (v int, err error) {
	err = o.c.Call("gestureSensor", "cooldownMs", &v)
	return
}

// Cleanup calls cleanup().
func (o GestureSensor) Cleanup() error {
	return o.c.Call("gestureSensor", "cleanup", nil)
}

// MediaManager is the service object "mediaManager" (C++ MediaManager); NewMediaManager(c) binds it to a client.
type MediaManager struct{ c *Client }

// NewMediaManager binds "mediaManager" to c.
func NewMediaManager(c *Client) MediaManager { return MediaManager{c} }

// Name is the object's name on the wire.
func (MediaManager) Name() string { return "mediaManager" }

// GetAudioOutputs reads the property "audioOutputs" (QVariantList).
func (o MediaManager) GetAudioOutputs() (v []any, err error) {
	err = o.c.Get("mediaManager", "audioOutputs", &v)
	return
}

// GetCurrentAudioOutput reads the property "currentAudioOutput" (QString).
func (o MediaManager) GetCurrentAudioOutput() (v string, err error) {
	err = o.c.Get("mediaManager", "currentAudioOutput", &v)
	return
}

// ConnectSettingsManager calls connect_settings_manager(QObject*).
func (o MediaManager) ConnectSettingsManager(arg0 any) error {
	return o.c.Call("mediaManager", "connect_settings_manager", nil, arg0)
}

// SavePlaybackState calls _save_playback_state().
func (o MediaManager) SavePlaybackState() error {
	return o.c.Call("mediaManager", "_save_playback_state", nil)
}

// ClearTempFiles calls _clear_temp_files().
func (o MediaManager) ClearTempFiles() error {
	return o.c.Call("mediaManager", "_clear_temp_files", nil)
}

// FlushMetadataStore calls flush_metadata_store().
func (o MediaManager) FlushMetadataStore() error {
	return o.c.Call("mediaManager", "flush_metadata_store", nil)
}

// PlayFile calls play_file(QString).
func (o MediaManager) PlayFile(filename string) error {
	return o.c.Call("mediaManager", "play_file", nil, filename)
}

// NextTrack calls next_track().
func (o MediaManager) NextTrack() error {
	return o.c.Call("mediaManager", "next_track", nil)
}

// PreviousTrack calls previous_track().
func (o MediaManager) PreviousTrack() error {
	return o.c.Call("mediaManager", "previous_track", nil)
}

// Pause calls pause().
func (o MediaManager) Pause() error {
	return o.c.Call("mediaManager", "pause", nil)
}

// TogglePlay calls toggle_play().
func (o MediaManager) TogglePlay() error {
	return o.c.Call("mediaManager", "toggle_play", nil)
}

// SetMuted calls setMuted(bool).
func (o MediaManager) SetMuted(muted bool) error {
	return o.c.Call("mediaManager", "setMuted", nil, muted)
}

// ToggleShuffle calls toggle_shuffle().
func (o MediaManager) ToggleShuffle() error {
	return o.c.Call("mediaManager", "toggle_shuffle", nil)
}

// SetVolume calls setVolume(float).
func (o MediaManager) SetVolume(volume float64) error {
	return o.c.Call("mediaManager", "setVolume", nil, volume)
}

// SetDucking calls setDucking(float).
func (o MediaManager) SetDucking(factor float64) error {
	return o.c.Call("mediaManager", "setDucking", nil, factor)
}

// SetPosition calls set_position(int).
func (o MediaManager) SetPosition(positionMs int) error {
	return o.c.Call("mediaManager", "set_position", nil, positionMs)
}

// ExtractColorsFromAlbumArt calls extract_colors_from_album_art(QString).
func (o MediaManager) ExtractColorsFromAlbumArt(filename string) error {
	return o.c.Call("mediaManager", "extract_colors_from_album_art", nil, filename)
}

// ExtractColorsFromImage calls extract_colors_from_image(QString).
func (o MediaManager) ExtractColorsFromImage(imagePath string) error {
	return o.c.Call("mediaManager", "extract_colors_from_image", nil, imagePath)
}

// ScanLibrary1 calls scan_library(bool).
func (o MediaManager) ScanLibrary1(resetDisplayNames bool) error {
	return o.c.Call("mediaManager", "scan_library", nil, resetDisplayNames)
}

// ScanLibrary0 calls scan_library().
func (o MediaManager) ScanLibrary0() error {
	return o.c.Call("mediaManager", "scan_library", nil)
}

// SetLibraryRoot calls set_library_root(QString).
func (o MediaManager) SetLibraryRoot(path string) error {
	return o.c.Call("mediaManager", "set_library_root", nil, path)
}

// StripFilenames calls strip_filenames().
func (o MediaManager) StripFilenames() error {
	return o.c.Call("mediaManager", "strip_filenames", nil)
}

// SelectPlaylist calls select_playlist(QString).
func (o MediaManager) SelectPlaylist(name string) error {
	return o.c.Call("mediaManager", "select_playlist", nil, name)
}

// CreatePlaylist calls create_playlist(QString) -> bool.
func (o MediaManager) CreatePlaylist(name string) (v bool, err error) {
	err = o.c.Call("mediaManager", "create_playlist", &v, name)
	return
}

// DeletePlaylist calls delete_playlist(QString) -> bool.
func (o MediaManager) DeletePlaylist(name string) (v bool, err error) {
	err = o.c.Call("mediaManager", "delete_playlist", &v, name)
	return
}

// MoveSongToPlaylist calls move_song_to_playlist(QString, QString) -> bool.
func (o MediaManager) MoveSongToPlaylist(filename string, targetPlaylist string) (v bool, err error) {
	err = o.c.Call("mediaManager", "move_song_to_playlist", &v, filename, targetPlaylist)
	return
}

// DeleteSong calls delete_song(QString) -> bool.
func (o MediaManager) DeleteSong(filename string) (v bool, err error) {
	err = o.c.Call("mediaManager", "delete_song", &v, filename)
	return
}

// PlayFileAtPath calls play_file_at_path(QString) -> bool.
func (o MediaManager) PlayFileAtPath(filePath string) (v bool, err error) {
	err = o.c.Call("mediaManager", "play_file_at_path", &v, filePath)
	return
}

// InvalidateStatsCache calls invalidate_stats_cache().
func (o MediaManager) InvalidateStatsCache() error {
	return o.c.Call("mediaManager", "invalidate_stats_cache", nil)
}

// GetMediaFiles1 calls get_media_files(bool) -> QStringList.
func (o MediaManager) GetMediaFiles1(emitSignal bool) (v []string, err error) {
	err = o.c.Call("mediaManager", "get_media_files", &v, emitSignal)
	return
}

// GetMediaFiles0 calls get_media_files() -> QStringList.
func (o MediaManager) GetMediaFiles0() (v []string, err error) {
	err = o.c.Call("mediaManager", "get_media_files", &v)
	return
}

// GetFormattedDuration calls get_formatted_duration(QString) -> QString.
func (o MediaManager) GetFormattedDuration(filename string) (v string, err error) {
	err = o.c.Call("mediaManager", "get_formatted_duration", &v, filename)
	return
}

// GetBand calls get_band(QString) -> QString.
func (o MediaManager) GetBand(filename string) (v string, err error) {
	err = o.c.Call("mediaManager", "get_band", &v, filename)
	return
}

// GetAlbum calls get_album(QString) -> QString.
func (o MediaManager) GetAlbum(filename string) (v string, err error) {
	err = o.c.Call("mediaManager", "get_album", &v, filename)
	return
}

// GetAlbumArt calls get_album_art(QString) -> QString.
func (o MediaManager) GetAlbumArt(filename string) (v string, err error) {
	err = o.c.Call("mediaManager", "get_album_art", &v, filename)
	return
}

// GetCurrentFile calls get_current_file() -> QString.
func (o MediaManager) GetCurrentFile() (v string, err error) {
	err = o.c.Call("mediaManager", "get_current_file", &v)
	return
}

// GetNextTrackName calls get_next_track_name() -> QString.
func (o MediaManager) GetNextTrackName() (v string, err error) {
	err = o.c.Call("mediaManager", "get_next_track_name", &v)
	return
}

// GetPreviousTrackName calls get_previous_track_name() -> QString.
func (o MediaManager) GetPreviousTrackName() (v string, err error) {
	err = o.c.Call("mediaManager", "get_previous_track_name", &v)
	return
}

// GetNextTrackAlbumArt calls get_next_track_album_art() -> QString.
func (o MediaManager) GetNextTrackAlbumArt() (v string, err error) {
	err = o.c.Call("mediaManager", "get_next_track_album_art", &v)
	return
}

// GetPreviousTrackAlbumArt calls get_previous_track_album_art() -> QString.
func (o MediaManager) GetPreviousTrackAlbumArt() (v string, err error) {
	err = o.c.Call("mediaManager", "get_previous_track_album_art", &v)
	return
}

// GetNeighborAlbumArts calls get_neighbor_album_arts() -> QStringList.
func (o MediaManager) GetNeighborAlbumArts() (v []string, err error) {
	err = o.c.Call("mediaManager", "get_neighbor_album_arts", &v)
	return
}

// IsPlaying calls is_playing() -> bool.
func (o MediaManager) IsPlaying() (v bool, err error) {
	err = o.c.Call("mediaManager", "is_playing", &v)
	return
}

// IsPaused calls is_paused() -> bool.
func (o MediaManager) IsPaused() (v bool, err error) {
	err = o.c.Call("mediaManager", "is_paused", &v)
	return
}

// GetDuration calls get_duration() -> float.
func (o MediaManager) GetDuration() (v float64, err error) {
	err = o.c.Call("mediaManager", "get_duration", &v)
	return
}

// GetPosition calls get_position() -> float.
func (o MediaManager) GetPosition() (v float64, err error) {
	err = o.c.Call("mediaManager", "get_position", &v)
	return
}

// IsMuted calls is_muted() -> bool.
func (o MediaManager) IsMuted() (v bool, err error) {
	err = o.c.Call("mediaManager", "is_muted", &v)
	return
}

// GetVolume calls getVolume() -> float.
func (o MediaManager) GetVolume() (v float64, err error) {
	err = o.c.Call("mediaManager", "getVolume", &v)
	return
}

// IsShuffled calls is_shuffled() -> bool.
func (o MediaManager) IsShuffled() (v bool, err error) {
	err = o.c.Call("mediaManager", "is_shuffled", &v)
	return
}

// GetPlaylistNames calls get_playlist_names() -> QStringList.
func (o MediaManager) GetPlaylistNames() (v []string, err error) {
	err = o.c.Call("mediaManager", "get_playlist_names", &v)
	return
}

// GetCurrentPlaylistName calls get_current_playlist_name() -> QString.
func (o MediaManager) GetCurrentPlaylistName() (v string, err error) {
	err = o.c.Call("mediaManager", "get_current_playlist_name", &v)
	return
}

// GetCurrentSongList calls get_current_song_list() -> QStringList.
func (o MediaManager) GetCurrentSongList() (v []string, err error) {
	err = o.c.Call("mediaManager", "get_current_song_list", &v)
	return
}

// GetMovablePlaylistNames calls get_movable_playlist_names() -> QStringList.
func (o MediaManager) GetMovablePlaylistNames() (v []string, err error) {
	err = o.c.Call("mediaManager", "get_movable_playlist_names", &v)
	return
}

// GetSongPlaylistName calls get_song_playlist_name(QString) -> QString.
func (o MediaManager) GetSongPlaylistName(filename string) (v string, err error) {
	err = o.c.Call("mediaManager", "get_song_playlist_name", &v, filename)
	return
}

// GetSongFilePath calls get_song_file_path(QString) -> QString.
func (o MediaManager) GetSongFilePath(filename string) (v string, err error) {
	err = o.c.Call("mediaManager", "get_song_file_path", &v, filename)
	return
}

// GetFullFilePath calls get_full_file_path(QString) -> QString.
func (o MediaManager) GetFullFilePath(filename string) (v string, err error) {
	err = o.c.Call("mediaManager", "get_full_file_path", &v, filename)
	return
}

// GetDisplayName calls get_display_name(QString) -> QString.
func (o MediaManager) GetDisplayName(filename string) (v string, err error) {
	err = o.c.Call("mediaManager", "get_display_name", &v, filename)
	return
}

// GetDefaultMediaDir calls get_default_media_dir() -> QString.
func (o MediaManager) GetDefaultMediaDir() (v string, err error) {
	err = o.c.Call("mediaManager", "get_default_media_dir", &v)
	return
}

// GetMediaFolderName calls get_media_folder_name() -> QString.
func (o MediaManager) GetMediaFolderName() (v string, err error) {
	err = o.c.Call("mediaManager", "get_media_folder_name", &v)
	return
}

// BrowseMusicFolders calls browse_music_folders(QString) -> QVariantMap.
func (o MediaManager) BrowseMusicFolders(path string) (v map[string]any, err error) {
	err = o.c.Call("mediaManager", "browse_music_folders", &v, path)
	return
}

// PickBrowserLibrary calls pick_browser_library() -> bool.
func (o MediaManager) PickBrowserLibrary() (v bool, err error) {
	err = o.c.Call("mediaManager", "pick_browser_library", &v)
	return
}

// GetTotalDuration calls get_total_duration() -> QString.
func (o MediaManager) GetTotalDuration() (v string, err error) {
	err = o.c.Call("mediaManager", "get_total_duration", &v)
	return
}

// GetAlbumCount calls get_album_count() -> int.
func (o MediaManager) GetAlbumCount() (v int, err error) {
	err = o.c.Call("mediaManager", "get_album_count", &v)
	return
}

// GetArtistCount calls get_artist_count() -> int.
func (o MediaManager) GetArtistCount() (v int, err error) {
	err = o.c.Call("mediaManager", "get_artist_count", &v)
	return
}

// SortMediaFiles2 calls sort_media_files(QString, bool) -> QStringList.
func (o MediaManager) SortMediaFiles2(sortColumn string, ascending bool) (v []string, err error) {
	err = o.c.Call("mediaManager", "sort_media_files", &v, sortColumn, ascending)
	return
}

// SortMediaFiles1 calls sort_media_files(QString) -> QStringList.
func (o MediaManager) SortMediaFiles1(sortColumn string) (v []string, err error) {
	err = o.c.Call("mediaManager", "sort_media_files", &v, sortColumn)
	return
}

// NetworkManager is the service object "networkManager" (C++ NetworkManager); NewNetworkManager(c) binds it to a client.
type NetworkManager struct{ c *Client }

// NewNetworkManager binds "networkManager" to c.
func NewNetworkManager(c *Client) NetworkManager { return NetworkManager{c} }

// Name is the object's name on the wire.
func (NetworkManager) Name() string { return "networkManager" }

// GetNetworkName reads the property "networkName" (QString).
func (o NetworkManager) GetNetworkName() (v string, err error) {
	err = o.c.Get("networkManager", "networkName", &v)
	return
}

// GetIsConnected reads the property "isConnected" (bool).
func (o NetworkManager) GetIsConnected() (v bool, err error) {
	err = o.c.Get("networkManager", "isConnected", &v)
	return
}

// GetUpdateStatus reads the property "updateStatus" (QString).
func (o NetworkManager) GetUpdateStatus() (v string, err error) {
	err = o.c.Get("networkManager", "updateStatus", &v)
	return
}

// GetUpdateMessage reads the property "updateMessage" (QString).
func (o NetworkManager) GetUpdateMessage() (v string, err error) {
	err = o.c.Get("networkManager", "updateMessage", &v)
	return
}

// GetRemoteCommit reads the property "remoteCommit" (QString).
func (o NetworkManager) GetRemoteCommit() (v string, err error) {
	err = o.c.Get("networkManager", "remoteCommit", &v)
	return
}

// GetRemoteCommitMsg reads the property "remoteCommitMsg" (QString).
func (o NetworkManager) GetRemoteCommitMsg() (v string, err error) {
	err = o.c.Get("networkManager", "remoteCommitMsg", &v)
	return
}

// GetWorkingTreeDirty reads the property "workingTreeDirty" (bool).
func (o NetworkManager) GetWorkingTreeDirty() (v bool, err error) {
	err = o.c.Get("networkManager", "workingTreeDirty", &v)
	return
}

// GetSelfUpdateStatus reads the property "selfUpdateStatus" (QString).
func (o NetworkManager) GetSelfUpdateStatus() (v string, err error) {
	err = o.c.Get("networkManager", "selfUpdateStatus", &v)
	return
}

// GetSelfUpdateMessage reads the property "selfUpdateMessage" (QString).
func (o NetworkManager) GetSelfUpdateMessage() (v string, err error) {
	err = o.c.Get("networkManager", "selfUpdateMessage", &v)
	return
}

// GetCanSelfUpdate reads the property "canSelfUpdate" (bool).
func (o NetworkManager) GetCanSelfUpdate() (v bool, err error) {
	err = o.c.Get("networkManager", "canSelfUpdate", &v)
	return
}

// RefreshNetwork calls refreshNetwork().
func (o NetworkManager) RefreshNetwork() error {
	return o.c.Call("networkManager", "refreshNetwork", nil)
}

// CheckForUpdates calls checkForUpdates().
func (o NetworkManager) CheckForUpdates() error {
	return o.c.Call("networkManager", "checkForUpdates", nil)
}

// ApplySelfUpdate calls applySelfUpdate().
func (o NetworkManager) ApplySelfUpdate() error {
	return o.c.Call("networkManager", "applySelfUpdate", nil)
}

// RestartApp calls restartApp().
func (o NetworkManager) RestartApp() error {
	return o.c.Call("networkManager", "restartApp", nil)
}

// ShutdownDevice calls shutdown_device().
func (o NetworkManager) ShutdownDevice() error {
	return o.c.Call("networkManager", "shutdown_device", nil)
}

// Cleanup calls cleanup().
func (o NetworkManager) Cleanup() error {
	return o.c.Call("networkManager", "cleanup", nil)
}

// NowPlaying is the service object "nowPlaying" (C++ NowPlaying); NewNowPlaying(c) binds it to a client.
type NowPlaying struct{ c *Client }

// NewNowPlaying binds "nowPlaying" to c.
func NewNowPlaying(c *Client) NowPlaying { return NowPlaying{c} }

// Name is the object's name on the wire.
func (NowPlaying) Name() string { return "nowPlaying" }

// GetSource reads the property "source" (QString).
func (o NowPlaying) GetSource() (v string, err error) {
	err = o.c.Get("nowPlaying", "source", &v)
	return
}

// GetPlaying reads the property "playing" (bool).
func (o NowPlaying) GetPlaying() (v bool, err error) {
	err = o.c.Get("nowPlaying", "playing", &v)
	return
}

// GetHasTrack reads the property "hasTrack" (bool).
func (o NowPlaying) GetHasTrack() (v bool, err error) {
	err = o.c.Get("nowPlaying", "hasTrack", &v)
	return
}

// GetTitle reads the property "title" (QString).
func (o NowPlaying) GetTitle() (v string, err error) {
	err = o.c.Get("nowPlaying", "title", &v)
	return
}

// GetArtist reads the property "artist" (QString).
func (o NowPlaying) GetArtist() (v string, err error) {
	err = o.c.Get("nowPlaying", "artist", &v)
	return
}

// GetAlbum reads the property "album" (QString).
func (o NowPlaying) GetAlbum() (v string, err error) {
	err = o.c.Get("nowPlaying", "album", &v)
	return
}

// GetArtUrl reads the property "artUrl" (QString).
func (o NowPlaying) GetArtUrl() (v string, err error) {
	err = o.c.Get("nowPlaying", "artUrl", &v)
	return
}

// GetArtRevision reads the property "artRevision" (int).
func (o NowPlaying) GetArtRevision() (v int, err error) {
	err = o.c.Get("nowPlaying", "artRevision", &v)
	return
}

// GetDurationMs reads the property "durationMs" (int).
func (o NowPlaying) GetDurationMs() (v int, err error) {
	err = o.c.Get("nowPlaying", "durationMs", &v)
	return
}

// GetPositionMs reads the property "positionMs" (int).
func (o NowPlaying) GetPositionMs() (v int, err error) {
	err = o.c.Get("nowPlaying", "positionMs", &v)
	return
}

// GetCanSeek reads the property "canSeek" (bool).
func (o NowPlaying) GetCanSeek() (v bool, err error) {
	err = o.c.Get("nowPlaying", "canSeek", &v)
	return
}

// GetCanShuffle reads the property "canShuffle" (bool).
func (o NowPlaying) GetCanShuffle() (v bool, err error) {
	err = o.c.Get("nowPlaying", "canShuffle", &v)
	return
}

// GetShuffled reads the property "shuffled" (bool).
func (o NowPlaying) GetShuffled() (v bool, err error) {
	err = o.c.Get("nowPlaying", "shuffled", &v)
	return
}

// TogglePlay calls togglePlay().
func (o NowPlaying) TogglePlay() error {
	return o.c.Call("nowPlaying", "togglePlay", nil)
}

// Next calls next().
func (o NowPlaying) Next() error {
	return o.c.Call("nowPlaying", "next", nil)
}

// Previous calls previous().
func (o NowPlaying) Previous() error {
	return o.c.Call("nowPlaying", "previous", nil)
}

// Seek calls seek(int).
func (o NowPlaying) Seek(positionMs int) error {
	return o.c.Call("nowPlaying", "seek", nil, positionMs)
}

// ToggleShuffle calls toggleShuffle().
func (o NowPlaying) ToggleShuffle() error {
	return o.c.Call("nowPlaying", "toggleShuffle", nil)
}

// UseLocal calls useLocal().
func (o NowPlaying) UseLocal() error {
	return o.c.Call("nowPlaying", "useLocal", nil)
}

// ExtractArtColors calls extractArtColors().
func (o NowPlaying) ExtractArtColors() error {
	return o.c.Call("nowPlaying", "extractArtColors", nil)
}

// ObdAdapterFinder is the service object "obdAdapterFinder" (C++ OBDAdapterFinder); NewObdAdapterFinder(c) binds it to a client.
type ObdAdapterFinder struct{ c *Client }

// NewObdAdapterFinder binds "obdAdapterFinder" to c.
func NewObdAdapterFinder(c *Client) ObdAdapterFinder { return ObdAdapterFinder{c} }

// Name is the object's name on the wire.
func (ObdAdapterFinder) Name() string { return "obdAdapterFinder" }

// GetSupported reads the property "supported" (bool).
func (o ObdAdapterFinder) GetSupported() (v bool, err error) {
	err = o.c.Get("obdAdapterFinder", "supported", &v)
	return
}

// GetCanScan reads the property "canScan" (bool).
func (o ObdAdapterFinder) GetCanScan() (v bool, err error) {
	err = o.c.Get("obdAdapterFinder", "canScan", &v)
	return
}

// GetBluetoothOn reads the property "bluetoothOn" (bool).
func (o ObdAdapterFinder) GetBluetoothOn() (v bool, err error) {
	err = o.c.Get("obdAdapterFinder", "bluetoothOn", &v)
	return
}

// GetScanning reads the property "scanning" (bool).
func (o ObdAdapterFinder) GetScanning() (v bool, err error) {
	err = o.c.Get("obdAdapterFinder", "scanning", &v)
	return
}

// GetDevices reads the property "devices" (QVariantList).
func (o ObdAdapterFinder) GetDevices() (v []any, err error) {
	err = o.c.Get("obdAdapterFinder", "devices", &v)
	return
}

// GetBusyAddress reads the property "busyAddress" (QString).
func (o ObdAdapterFinder) GetBusyAddress() (v string, err error) {
	err = o.c.Get("obdAdapterFinder", "busyAddress", &v)
	return
}

// GetStatus reads the property "status" (QString).
func (o ObdAdapterFinder) GetStatus() (v string, err error) {
	err = o.c.Get("obdAdapterFinder", "status", &v)
	return
}

// GetStatusIsError reads the property "statusIsError" (bool).
func (o ObdAdapterFinder) GetStatusIsError() (v bool, err error) {
	err = o.c.Get("obdAdapterFinder", "statusIsError", &v)
	return
}

// StartScan calls startScan().
func (o ObdAdapterFinder) StartScan() error {
	return o.c.Call("obdAdapterFinder", "startScan", nil)
}

// StopScan calls stopScan().
func (o ObdAdapterFinder) StopScan() error {
	return o.c.Call("obdAdapterFinder", "stopScan", nil)
}

// ConnectAdapter calls connectAdapter(QString).
func (o ObdAdapterFinder) ConnectAdapter(address string) error {
	return o.c.Call("obdAdapterFinder", "connectAdapter", nil, address)
}

// Cancel calls cancel().
func (o ObdAdapterFinder) Cancel() error {
	return o.c.Call("obdAdapterFinder", "cancel", nil)
}

// PowerOn calls powerOn().
func (o ObdAdapterFinder) PowerOn() error {
	return o.c.Call("obdAdapterFinder", "powerOn", nil)
}

// ObdManager is the service object "obdManager" (C++ OBDManager); NewObdManager(c) binds it to a client.
type ObdManager struct{ c *Client }

// NewObdManager binds "obdManager" to c.
func NewObdManager(c *Client) ObdManager { return ObdManager{c} }

// Name is the object's name on the wire.
func (ObdManager) Name() string { return "obdManager" }

// GetConnected reads the property "connected" (bool).
func (o ObdManager) GetConnected() (v bool, err error) {
	err = o.c.Get("obdManager", "connected", &v)
	return
}

// GetSimulated reads the property "simulated" (bool).
func (o ObdManager) GetSimulated() (v bool, err error) {
	err = o.c.Get("obdManager", "simulated", &v)
	return
}

// GetConnectionStatus reads the property "connectionStatus" (QString).
func (o ObdManager) GetConnectionStatus() (v string, err error) {
	err = o.c.Get("obdManager", "connectionStatus", &v)
	return
}

// GetConnectionState reads the property "connectionState" (OBDManager::ConnectionState).
func (o ObdManager) GetConnectionState() (v any, err error) {
	err = o.c.Get("obdManager", "connectionState", &v)
	return
}

// GetConnectionDetail reads the property "connectionDetail" (QString).
func (o ObdManager) GetConnectionDetail() (v string, err error) {
	err = o.c.Get("obdManager", "connectionDetail", &v)
	return
}

// GetConnectionProgress reads the property "connectionProgress" (int).
func (o ObdManager) GetConnectionProgress() (v int, err error) {
	err = o.c.Get("obdManager", "connectionProgress", &v)
	return
}

// GetConnectionLog reads the property "connectionLog" (QStringList).
func (o ObdManager) GetConnectionLog() (v []string, err error) {
	err = o.c.Get("obdManager", "connectionLog", &v)
	return
}

// GetAvailableAdapters reads the property "availableAdapters" (QVariantList).
func (o ObdManager) GetAvailableAdapters() (v []any, err error) {
	err = o.c.Get("obdManager", "availableAdapters", &v)
	return
}

// GetVin reads the property "vin" (QString).
func (o ObdManager) GetVin() (v string, err error) {
	err = o.c.Get("obdManager", "vin", &v)
	return
}

// GetVehicleMake reads the property "vehicleMake" (QString).
func (o ObdManager) GetVehicleMake() (v string, err error) {
	err = o.c.Get("obdManager", "vehicleMake", &v)
	return
}

// GetVehicleModelYear reads the property "vehicleModelYear" (int).
func (o ObdManager) GetVehicleModelYear() (v int, err error) {
	err = o.c.Get("obdManager", "vehicleModelYear", &v)
	return
}

// GetVehicleSupportedParameters reads the property "vehicleSupportedParameters" (QStringList).
func (o ObdManager) GetVehicleSupportedParameters() (v []string, err error) {
	err = o.c.Get("obdManager", "vehicleSupportedParameters", &v)
	return
}

// GetVehicleScanComplete reads the property "vehicleScanComplete" (bool).
func (o ObdManager) GetVehicleScanComplete() (v bool, err error) {
	err = o.c.Get("obdManager", "vehicleScanComplete", &v)
	return
}

// ConnectObd calls connect_obd().
func (o ObdManager) ConnectObd() error {
	return o.c.Call("obdManager", "connect_obd", nil)
}

// Reconnect calls reconnect().
func (o ObdManager) Reconnect() error {
	return o.c.Call("obdManager", "reconnect", nil)
}

// ForceConnect calls force_connect().
func (o ObdManager) ForceConnect() error {
	return o.c.Call("obdManager", "force_connect", nil)
}

// Close calls close().
func (o ObdManager) Close() error {
	return o.c.Call("obdManager", "close", nil)
}

// ResetConnection calls reset_connection().
func (o ObdManager) ResetConnection() error {
	return o.c.Call("obdManager", "reset_connection", nil)
}

// GetConnectionStatusCall calls get_connection_status() -> QString.
func (o ObdManager) GetConnectionStatusCall() (v string, err error) {
	err = o.c.Call("obdManager", "get_connection_status", &v)
	return
}

// GetConnectionStatusCallCall calls getConnectionStatus() -> QString.
func (o ObdManager) GetConnectionStatusCallCall() (v string, err error) {
	err = o.c.Call("obdManager", "getConnectionStatus", &v)
	return
}

// RefreshPorts calls refresh_ports().
func (o ObdManager) RefreshPorts() error {
	return o.c.Call("obdManager", "refresh_ports", nil)
}

// GetAvailablePorts calls get_available_ports() -> QVariantList.
func (o ObdManager) GetAvailablePorts() (v []any, err error) {
	err = o.c.Call("obdManager", "get_available_ports", &v)
	return
}

// Scan calls scan().
func (o ObdManager) Scan() error {
	return o.c.Call("obdManager", "scan", nil)
}

// AvailableAdapters calls availableAdapters() -> QVariantList.
func (o ObdManager) AvailableAdapters() (v []any, err error) {
	err = o.c.Call("obdManager", "availableAdapters", &v)
	return
}

// ConnectToAdapter calls connect_to_adapter(QString).
func (o ObdManager) ConnectToAdapter(identifier string) error {
	return o.c.Call("obdManager", "connect_to_adapter", nil, identifier)
}

// PlatformHint calls platform_hint() -> QString.
func (o ObdManager) PlatformHint() (v string, err error) {
	err = o.c.Call("obdManager", "platform_hint", &v)
	return
}

// ScanVehicle calls scan_vehicle().
func (o ObdManager) ScanVehicle() error {
	return o.c.Call("obdManager", "scan_vehicle", nil)
}

// IsScanning calls is_scanning() -> bool.
func (o ObdManager) IsScanning() (v bool, err error) {
	err = o.c.Call("obdManager", "is_scanning", &v)
	return
}

// GetAllParameterNames calls get_all_parameter_names() -> QStringList.
func (o ObdManager) GetAllParameterNames() (v []string, err error) {
	err = o.c.Call("obdManager", "get_all_parameter_names", &v)
	return
}

// GetSupportedCommands calls get_supported_commands() -> QVariantList.
func (o ObdManager) GetSupportedCommands() (v []any, err error) {
	err = o.c.Call("obdManager", "get_supported_commands", &v)
	return
}

// EnableScannedParameters calls enable_scanned_parameters(QVariantList).
func (o ObdManager) EnableScannedParameters(paramNames []any) error {
	return o.c.Call("obdManager", "enable_scanned_parameters", nil, paramNames)
}

// EnableAllSupported calls enable_all_supported().
func (o ObdManager) EnableAllSupported() error {
	return o.c.Call("obdManager", "enable_all_supported", nil)
}

// EnterDiagnosticMode calls enter_diagnostic_mode().
func (o ObdManager) EnterDiagnosticMode() error {
	return o.c.Call("obdManager", "enter_diagnostic_mode", nil)
}

// ExitDiagnosticMode calls exit_diagnostic_mode().
func (o ObdManager) ExitDiagnosticMode() error {
	return o.c.Call("obdManager", "exit_diagnostic_mode", nil)
}

// IsDiagnosticMode calls is_diagnostic_mode() -> bool.
func (o ObdManager) IsDiagnosticMode() (v bool, err error) {
	err = o.c.Call("obdManager", "is_diagnostic_mode", &v)
	return
}

// ReadDtc calls read_dtc().
func (o ObdManager) ReadDtc() error {
	return o.c.Call("obdManager", "read_dtc", nil)
}

// ReadCurrentDtc calls read_current_dtc().
func (o ObdManager) ReadCurrentDtc() error {
	return o.c.Call("obdManager", "read_current_dtc", nil)
}

// ReadStatus calls read_status().
func (o ObdManager) ReadStatus() error {
	return o.c.Call("obdManager", "read_status", nil)
}

// ClearDtc calls clear_dtc().
func (o ObdManager) ClearDtc() error {
	return o.c.Call("obdManager", "clear_dtc", nil)
}

// ReadFreezeFrame calls read_freeze_frame().
func (o ObdManager) ReadFreezeFrame() error {
	return o.c.Call("obdManager", "read_freeze_frame", nil)
}

// ReadVin calls read_vin().
func (o ObdManager) ReadVin() error {
	return o.c.Call("obdManager", "read_vin", nil)
}

// GetDtcCodes calls get_dtc_codes() -> QVariantList.
func (o ObdManager) GetDtcCodes() (v []any, err error) {
	err = o.c.Call("obdManager", "get_dtc_codes", &v)
	return
}

// GetDtcCount calls get_dtc_count() -> int.
func (o ObdManager) GetDtcCount() (v int, err error) {
	err = o.c.Call("obdManager", "get_dtc_count", &v)
	return
}

// GetMilStatus calls get_mil_status() -> bool.
func (o ObdManager) GetMilStatus() (v bool, err error) {
	err = o.c.Call("obdManager", "get_mil_status", &v)
	return
}

// GetFreezeFrame calls get_freeze_frame() -> QVariantList.
func (o ObdManager) GetFreezeFrame() (v []any, err error) {
	err = o.c.Call("obdManager", "get_freeze_frame", &v)
	return
}

// CoolantTemp calls coolantTemp() -> float.
func (o ObdManager) CoolantTemp() (v float64, err error) {
	err = o.c.Call("obdManager", "coolantTemp", &v)
	return
}

// Voltage calls voltage() -> float.
func (o ObdManager) Voltage() (v float64, err error) {
	err = o.c.Call("obdManager", "voltage", &v)
	return
}

// EngineLoad calls engineLoad() -> float.
func (o ObdManager) EngineLoad() (v float64, err error) {
	err = o.c.Call("obdManager", "engineLoad", &v)
	return
}

// ThrottlePosition calls throttlePosition() -> float.
func (o ObdManager) ThrottlePosition() (v float64, err error) {
	err = o.c.Call("obdManager", "throttlePosition", &v)
	return
}

// IntakeTemp calls intakeTemp() -> float.
func (o ObdManager) IntakeTemp() (v float64, err error) {
	err = o.c.Call("obdManager", "intakeTemp", &v)
	return
}

// TimingAdvance calls timingAdvance() -> float.
func (o ObdManager) TimingAdvance() (v float64, err error) {
	err = o.c.Call("obdManager", "timingAdvance", &v)
	return
}

// MassAirFlow calls massAirFlow() -> float.
func (o ObdManager) MassAirFlow() (v float64, err error) {
	err = o.c.Call("obdManager", "massAirFlow", &v)
	return
}

// SpeedMPH calls speedMPH() -> float.
func (o ObdManager) SpeedMPH() (v float64, err error) {
	err = o.c.Call("obdManager", "speedMPH", &v)
	return
}

// Rpm calls rpm() -> float.
func (o ObdManager) Rpm() (v float64, err error) {
	err = o.c.Call("obdManager", "rpm", &v)
	return
}

// AirFuelRatio calls airFuelRatio() -> float.
func (o ObdManager) AirFuelRatio() (v float64, err error) {
	err = o.c.Call("obdManager", "airFuelRatio", &v)
	return
}

// FuelLevel calls fuelLevel() -> float.
func (o ObdManager) FuelLevel() (v float64, err error) {
	err = o.c.Call("obdManager", "fuelLevel", &v)
	return
}

// IntakeManifoldPressure calls intakeManifoldPressure() -> float.
func (o ObdManager) IntakeManifoldPressure() (v float64, err error) {
	err = o.c.Call("obdManager", "intakeManifoldPressure", &v)
	return
}

// ShortTermFuelTrim calls shortTermFuelTrim() -> float.
func (o ObdManager) ShortTermFuelTrim() (v float64, err error) {
	err = o.c.Call("obdManager", "shortTermFuelTrim", &v)
	return
}

// LongTermFuelTrim calls longTermFuelTrim() -> float.
func (o ObdManager) LongTermFuelTrim() (v float64, err error) {
	err = o.c.Call("obdManager", "longTermFuelTrim", &v)
	return
}

// OxygenSensorVoltage calls oxygenSensorVoltage() -> float.
func (o ObdManager) OxygenSensorVoltage() (v float64, err error) {
	err = o.c.Call("obdManager", "oxygenSensorVoltage", &v)
	return
}

// FuelPressure calls fuelPressure() -> float.
func (o ObdManager) FuelPressure() (v float64, err error) {
	err = o.c.Call("obdManager", "fuelPressure", &v)
	return
}

// EngineOilTemp calls engineOilTemp() -> float.
func (o ObdManager) EngineOilTemp() (v float64, err error) {
	err = o.c.Call("obdManager", "engineOilTemp", &v)
	return
}

// IgnitionTiming calls ignitionTiming() -> float.
func (o ObdManager) IgnitionTiming() (v float64, err error) {
	err = o.c.Call("obdManager", "ignitionTiming", &v)
	return
}

// RefreshValues calls refresh_values().
func (o ObdManager) RefreshValues() error {
	return o.c.Call("obdManager", "refresh_values", nil)
}

// OpenBluetoothSettings calls open_bluetooth_settings().
func (o ObdManager) OpenBluetoothSettings() error {
	return o.c.Call("obdManager", "open_bluetooth_settings", nil)
}

// SetTargetAddress calls set_target_address(QString).
func (o ObdManager) SetTargetAddress(address string) error {
	return o.c.Call("obdManager", "set_target_address", nil, address)
}

// ConnectDirect2 calls connect_direct(QString, int).
func (o ObdManager) ConnectDirect2(mac string, channel int) error {
	return o.c.Call("obdManager", "connect_direct", nil, mac, channel)
}

// ConnectDirect1 calls connect_direct(QString).
func (o ObdManager) ConnectDirect1(mac string) error {
	return o.c.Call("obdManager", "connect_direct", nil, mac)
}

// SetAutoReconnect calls set_auto_reconnect(bool).
func (o ObdManager) SetAutoReconnect(enabled bool) error {
	return o.c.Call("obdManager", "set_auto_reconnect", nil, enabled)
}

// SetConnectionTimeout calls set_connection_timeout(int).
func (o ObdManager) SetConnectionTimeout(timeoutSeconds int) error {
	return o.c.Call("obdManager", "set_connection_timeout", nil, timeoutSeconds)
}

// CheckDevicePresence calls check_device_presence() -> bool.
func (o ObdManager) CheckDevicePresence() (v bool, err error) {
	err = o.c.Call("obdManager", "check_device_presence", &v)
	return
}

// IsConnected calls isConnected() -> bool.
func (o ObdManager) IsConnected() (v bool, err error) {
	err = o.c.Call("obdManager", "isConnected", &v)
	return
}

// ClearConnectionLog calls clearConnectionLog().
func (o ObdManager) ClearConnectionLog() error {
	return o.c.Call("obdManager", "clearConnectionLog", nil)
}

// IsConnectedCall calls is_connected() -> bool.
func (o ObdManager) IsConnectedCall() (v bool, err error) {
	err = o.c.Call("obdManager", "is_connected", &v)
	return
}

// SetParameterDemand calls setParameterDemand(QString, QStringList).
func (o ObdManager) SetParameterDemand(consumer string, paramIds []string) error {
	return o.c.Call("obdManager", "setParameterDemand", nil, consumer, paramIds)
}

// SetBackgroundDemand calls setBackgroundDemand(QString, QStringList).
func (o ObdManager) SetBackgroundDemand(consumer string, paramIds []string) error {
	return o.c.Call("obdManager", "setBackgroundDemand", nil, consumer, paramIds)
}

// ParamStore is the service object "paramStore" (C++ ParameterStore); NewParamStore(c) binds it to a client.
type ParamStore struct{ c *Client }

// NewParamStore binds "paramStore" to c.
func NewParamStore(c *Client) ParamStore { return ParamStore{c} }

// Name is the object's name on the wire.
func (ParamStore) Name() string { return "paramStore" }

// Ingest calls ingest(QString, float).
func (o ParamStore) Ingest(id string, value float64) error {
	return o.c.Call("paramStore", "ingest", nil, id, value)
}

// Get calls get(QString) -> QVariant.
func (o ParamStore) Get(id string) (v any, err error) {
	err = o.c.Call("paramStore", "get", &v, id)
	return
}

// Snapshot calls snapshot() -> QVariantMap.
func (o ParamStore) Snapshot() (v map[string]any, err error) {
	err = o.c.Call("paramStore", "snapshot", &v)
	return
}

// ParameterCatalog is the service object "parameterCatalog" (C++ ParameterCatalog); NewParameterCatalog(c) binds it to a client.
type ParameterCatalog struct{ c *Client }

// NewParameterCatalog binds "parameterCatalog" to c.
func NewParameterCatalog(c *Client) ParameterCatalog { return ParameterCatalog{c} }

// Name is the object's name on the wire.
func (ParameterCatalog) Name() string { return "parameterCatalog" }

// GetParameters reads the property "parameters" (QVariantList).
func (o ParameterCatalog) GetParameters() (v []any, err error) {
	err = o.c.Get("parameterCatalog", "parameters", &v)
	return
}

// GetDisplayParameters reads the property "displayParameters" (QVariantList).
func (o ParameterCatalog) GetDisplayParameters() (v []any, err error) {
	err = o.c.Get("parameterCatalog", "displayParameters", &v)
	return
}

// ToDisplay calls toDisplay(QString, QVariant) -> QVariant.
func (o ParameterCatalog) ToDisplay(id string, value any) (v any, err error) {
	err = o.c.Call("parameterCatalog", "toDisplay", &v, id, value)
	return
}

// Contains calls contains(QString) -> bool.
func (o ParameterCatalog) Contains(id string) (v bool, err error) {
	err = o.c.Call("parameterCatalog", "contains", &v, id)
	return
}

// Info calls info(QString) -> QVariantMap.
func (o ParameterCatalog) Info(id string) (v map[string]any, err error) {
	err = o.c.Call("parameterCatalog", "info", &v, id)
	return
}

// PhoneMirrorManager is the service object "phoneMirrorManager" (C++ PhoneMirrorManager); NewPhoneMirrorManager(c) binds it to a client.
type PhoneMirrorManager struct{ c *Client }

// NewPhoneMirrorManager binds "phoneMirrorManager" to c.
func NewPhoneMirrorManager(c *Client) PhoneMirrorManager { return PhoneMirrorManager{c} }

// Name is the object's name on the wire.
func (PhoneMirrorManager) Name() string { return "phoneMirrorManager" }

// GetAdbPath reads the property "adbPath" (QString).
func (o PhoneMirrorManager) GetAdbPath() (v string, err error) {
	err = o.c.Get("phoneMirrorManager", "adbPath", &v)
	return
}

// GetNativeAvailable reads the property "nativeAvailable" (bool).
func (o PhoneMirrorManager) GetNativeAvailable() (v bool, err error) {
	err = o.c.Get("phoneMirrorManager", "nativeAvailable", &v)
	return
}

// GetServerVersion reads the property "serverVersion" (QString).
func (o PhoneMirrorManager) GetServerVersion() (v string, err error) {
	err = o.c.Get("phoneMirrorManager", "serverVersion", &v)
	return
}

// GetIsRunning reads the property "isRunning" (bool).
func (o PhoneMirrorManager) GetIsRunning() (v bool, err error) {
	err = o.c.Get("phoneMirrorManager", "isRunning", &v)
	return
}

// GetDisplaySize reads the property "displaySize" (QString).
func (o PhoneMirrorManager) GetDisplaySize() (v string, err error) {
	err = o.c.Get("phoneMirrorManager", "displaySize", &v)
	return
}

// GetActiveDisplaySize reads the property "activeDisplaySize" (QString).
func (o PhoneMirrorManager) GetActiveDisplaySize() (v string, err error) {
	err = o.c.Get("phoneMirrorManager", "activeDisplaySize", &v)
	return
}

// GetVideoSink reads the property "videoSink" (QObject*, writable).
func (o PhoneMirrorManager) GetVideoSink() (v any, err error) {
	err = o.c.Get("phoneMirrorManager", "videoSink", &v)
	return
}

// SetVideoSink writes the property "videoSink" and returns the value read back.
func (o PhoneMirrorManager) SetVideoSink(value any) (v any, err error) {
	err = o.c.Set("phoneMirrorManager", "videoSink", value, &v)
	return
}

// GetFrameWidth reads the property "frameWidth" (int).
func (o PhoneMirrorManager) GetFrameWidth() (v int, err error) {
	err = o.c.Get("phoneMirrorManager", "frameWidth", &v)
	return
}

// GetFrameHeight reads the property "frameHeight" (int).
func (o PhoneMirrorManager) GetFrameHeight() (v int, err error) {
	err = o.c.Get("phoneMirrorManager", "frameHeight", &v)
	return
}

// GetAudioActive reads the property "audioActive" (bool).
func (o PhoneMirrorManager) GetAudioActive() (v bool, err error) {
	err = o.c.Get("phoneMirrorManager", "audioActive", &v)
	return
}

// GetAudioPlaying reads the property "audioPlaying" (bool).
func (o PhoneMirrorManager) GetAudioPlaying() (v bool, err error) {
	err = o.c.Get("phoneMirrorManager", "audioPlaying", &v)
	return
}

// GetPhoneAsleep reads the property "phoneAsleep" (bool).
func (o PhoneMirrorManager) GetPhoneAsleep() (v bool, err error) {
	err = o.c.Get("phoneMirrorManager", "phoneAsleep", &v)
	return
}

// GetPhoneInUse reads the property "phoneInUse" (bool).
func (o PhoneMirrorManager) GetPhoneInUse() (v bool, err error) {
	err = o.c.Get("phoneMirrorManager", "phoneInUse", &v)
	return
}

// GetWirelessAddress reads the property "wirelessAddress" (QString).
func (o PhoneMirrorManager) GetWirelessAddress() (v string, err error) {
	err = o.c.Get("phoneMirrorManager", "wirelessAddress", &v)
	return
}

// GetConnectionType reads the property "connectionType" (QString).
func (o PhoneMirrorManager) GetConnectionType() (v string, err error) {
	err = o.c.Get("phoneMirrorManager", "connectionType", &v)
	return
}

// GetWirelessBusy reads the property "wirelessBusy" (bool).
func (o PhoneMirrorManager) GetWirelessBusy() (v bool, err error) {
	err = o.c.Get("phoneMirrorManager", "wirelessBusy", &v)
	return
}

// GetQrPairingRows reads the property "qrPairingRows" (QStringList).
func (o PhoneMirrorManager) GetQrPairingRows() (v []string, err error) {
	err = o.c.Get("phoneMirrorManager", "qrPairingRows", &v)
	return
}

// GetQrPairingActive reads the property "qrPairingActive" (bool).
func (o PhoneMirrorManager) GetQrPairingActive() (v bool, err error) {
	err = o.c.Get("phoneMirrorManager", "qrPairingActive", &v)
	return
}

// GetCurrentBitRate reads the property "currentBitRate" (int).
func (o PhoneMirrorManager) GetCurrentBitRate() (v int, err error) {
	err = o.c.Get("phoneMirrorManager", "currentBitRate", &v)
	return
}

// GetMediaAvailable reads the property "mediaAvailable" (bool).
func (o PhoneMirrorManager) GetMediaAvailable() (v bool, err error) {
	err = o.c.Get("phoneMirrorManager", "mediaAvailable", &v)
	return
}

// GetMediaPlaying reads the property "mediaPlaying" (bool).
func (o PhoneMirrorManager) GetMediaPlaying() (v bool, err error) {
	err = o.c.Get("phoneMirrorManager", "mediaPlaying", &v)
	return
}

// GetMediaTitle reads the property "mediaTitle" (QString).
func (o PhoneMirrorManager) GetMediaTitle() (v string, err error) {
	err = o.c.Get("phoneMirrorManager", "mediaTitle", &v)
	return
}

// GetMediaArtist reads the property "mediaArtist" (QString).
func (o PhoneMirrorManager) GetMediaArtist() (v string, err error) {
	err = o.c.Get("phoneMirrorManager", "mediaArtist", &v)
	return
}

// GetMediaAlbum reads the property "mediaAlbum" (QString).
func (o PhoneMirrorManager) GetMediaAlbum() (v string, err error) {
	err = o.c.Get("phoneMirrorManager", "mediaAlbum", &v)
	return
}

// GetMediaApp reads the property "mediaApp" (QString).
func (o PhoneMirrorManager) GetMediaApp() (v string, err error) {
	err = o.c.Get("phoneMirrorManager", "mediaApp", &v)
	return
}

// GetMediaDuration reads the property "mediaDuration" (int).
func (o PhoneMirrorManager) GetMediaDuration() (v int, err error) {
	err = o.c.Get("phoneMirrorManager", "mediaDuration", &v)
	return
}

// GetMediaCanSeek reads the property "mediaCanSeek" (bool).
func (o PhoneMirrorManager) GetMediaCanSeek() (v bool, err error) {
	err = o.c.Get("phoneMirrorManager", "mediaCanSeek", &v)
	return
}

// GetMediaArtUrl reads the property "mediaArtUrl" (QString).
func (o PhoneMirrorManager) GetMediaArtUrl() (v string, err error) {
	err = o.c.Get("phoneMirrorManager", "mediaArtUrl", &v)
	return
}

// GetMediaActive reads the property "mediaActive" (bool).
func (o PhoneMirrorManager) GetMediaActive() (v bool, err error) {
	err = o.c.Get("phoneMirrorManager", "mediaActive", &v)
	return
}

// GetMediaQueue reads the property "mediaQueue" (QVariantList).
func (o PhoneMirrorManager) GetMediaQueue() (v []any, err error) {
	err = o.c.Get("phoneMirrorManager", "mediaQueue", &v)
	return
}

// GetMediaQueueActiveId reads the property "mediaQueueActiveId" (qlonglong).
func (o PhoneMirrorManager) GetMediaQueueActiveId() (v int64, err error) {
	err = o.c.Get("phoneMirrorManager", "mediaQueueActiveId", &v)
	return
}

// GetMediaQueueTitle reads the property "mediaQueueTitle" (QString).
func (o PhoneMirrorManager) GetMediaQueueTitle() (v string, err error) {
	err = o.c.Get("phoneMirrorManager", "mediaQueueTitle", &v)
	return
}

// GetDeviceState reads the property "deviceState" (QString).
func (o PhoneMirrorManager) GetDeviceState() (v string, err error) {
	err = o.c.Get("phoneMirrorManager", "deviceState", &v)
	return
}

// GetLastErrorKind reads the property "lastErrorKind" (QString).
func (o PhoneMirrorManager) GetLastErrorKind() (v string, err error) {
	err = o.c.Get("phoneMirrorManager", "lastErrorKind", &v)
	return
}

// GetSessionState reads the property "sessionState" (QString).
func (o PhoneMirrorManager) GetSessionState() (v string, err error) {
	err = o.c.Get("phoneMirrorManager", "sessionState", &v)
	return
}

// GetDeviceHint reads the property "deviceHint" (QString).
func (o PhoneMirrorManager) GetDeviceHint() (v string, err error) {
	err = o.c.Get("phoneMirrorManager", "deviceHint", &v)
	return
}

// GetDevices reads the property "devices" (QVariantList).
func (o PhoneMirrorManager) GetDevices() (v []any, err error) {
	err = o.c.Get("phoneMirrorManager", "devices", &v)
	return
}

// GetMirrorNotice reads the property "mirrorNotice" (QString).
func (o PhoneMirrorManager) GetMirrorNotice() (v string, err error) {
	err = o.c.Get("phoneMirrorManager", "mirrorNotice", &v)
	return
}

// SetVolume calls setVolume(float).
func (o PhoneMirrorManager) SetVolume(volume float64) error {
	return o.c.Call("phoneMirrorManager", "setVolume", nil, volume)
}

// SetMuted calls setMuted(bool).
func (o PhoneMirrorManager) SetMuted(muted bool) error {
	return o.c.Call("phoneMirrorManager", "setMuted", nil, muted)
}

// SetAudioGain calls setAudioGain(float).
func (o PhoneMirrorManager) SetAudioGain(gain float64) error {
	return o.c.Call("phoneMirrorManager", "setAudioGain", nil, gain)
}

// SetAudioDuckEnabled calls setAudioDuckEnabled(bool).
func (o PhoneMirrorManager) SetAudioDuckEnabled(enabled bool) error {
	return o.c.Call("phoneMirrorManager", "setAudioDuckEnabled", nil, enabled)
}

// SetAudioDuckLevel calls setAudioDuckLevel(float).
func (o PhoneMirrorManager) SetAudioDuckLevel(level float64) error {
	return o.c.Call("phoneMirrorManager", "setAudioDuckLevel", nil, level)
}

// SetAudioOutputDevice calls setAudioOutputDevice(QString).
func (o PhoneMirrorManager) SetAudioOutputDevice(id string) error {
	return o.c.Call("phoneMirrorManager", "setAudioOutputDevice", nil, id)
}

// SetDisplaySize calls setDisplaySize(QString).
func (o PhoneMirrorManager) SetDisplaySize(size string) error {
	return o.c.Call("phoneMirrorManager", "setDisplaySize", nil, size)
}

// SetViewSize calls setViewSize(int, int).
func (o PhoneMirrorManager) SetViewSize(width int, height int) error {
	return o.c.Call("phoneMirrorManager", "setViewSize", nil, width, height)
}

// SetDisplayDpi calls setDisplayDpi(int).
func (o PhoneMirrorManager) SetDisplayDpi(dpi int) error {
	return o.c.Call("phoneMirrorManager", "setDisplayDpi", nil, dpi)
}

// SetBitRateUsb calls setBitRateUsb(int).
func (o PhoneMirrorManager) SetBitRateUsb(mbps int) error {
	return o.c.Call("phoneMirrorManager", "setBitRateUsb", nil, mbps)
}

// SetBitRateWifi calls setBitRateWifi(int).
func (o PhoneMirrorManager) SetBitRateWifi(mbps int) error {
	return o.c.Call("phoneMirrorManager", "setBitRateWifi", nil, mbps)
}

// SetBitRateAuto calls setBitRateAuto(bool).
func (o PhoneMirrorManager) SetBitRateAuto(enabled bool) error {
	return o.c.Call("phoneMirrorManager", "setBitRateAuto", nil, enabled)
}

// SetAutoWifi calls setAutoWifi(bool).
func (o PhoneMirrorManager) SetAutoWifi(enabled bool) error {
	return o.c.Call("phoneMirrorManager", "setAutoWifi", nil, enabled)
}

// SetPhoneScreenOff calls setPhoneScreenOff(bool).
func (o PhoneMirrorManager) SetPhoneScreenOff(off bool) error {
	return o.c.Call("phoneMirrorManager", "setPhoneScreenOff", nil, off)
}

// WakePhone calls wakePhone().
func (o PhoneMirrorManager) WakePhone() error {
	return o.c.Call("phoneMirrorManager", "wakePhone", nil)
}

// ResumeMirroring calls resumeMirroring().
func (o PhoneMirrorManager) ResumeMirroring() error {
	return o.c.Call("phoneMirrorManager", "resumeMirroring", nil)
}

// EnvironmentOk calls environmentOk() -> bool.
func (o PhoneMirrorManager) EnvironmentOk() (v bool, err error) {
	err = o.c.Call("phoneMirrorManager", "environmentOk", &v)
	return
}

// GetInstallInstructions calls getInstallInstructions() -> QString.
func (o PhoneMirrorManager) GetInstallInstructions() (v string, err error) {
	err = o.c.Call("phoneMirrorManager", "getInstallInstructions", &v)
	return
}

// GetDeviceStateCall calls getDeviceState() -> QString.
func (o PhoneMirrorManager) GetDeviceStateCall() (v string, err error) {
	err = o.c.Call("phoneMirrorManager", "getDeviceState", &v)
	return
}

// RefreshDeviceState calls refreshDeviceState().
func (o PhoneMirrorManager) RefreshDeviceState() error {
	return o.c.Call("phoneMirrorManager", "refreshDeviceState", nil)
}

// SetViewActive calls setViewActive(bool).
func (o PhoneMirrorManager) SetViewActive(active bool) error {
	return o.c.Call("phoneMirrorManager", "setViewActive", nil, active)
}

// DescribeDeviceState calls describeDeviceState(QString) -> QString.
func (o PhoneMirrorManager) DescribeDeviceState(state string) (v string, err error) {
	err = o.c.Call("phoneMirrorManager", "describeDeviceState", &v, state)
	return
}

// HasConnectedDevice calls hasConnectedDevice() -> bool.
func (o PhoneMirrorManager) HasConnectedDevice() (v bool, err error) {
	err = o.c.Call("phoneMirrorManager", "hasConnectedDevice", &v)
	return
}

// SelectDevice calls selectDevice(QString).
func (o PhoneMirrorManager) SelectDevice(serial string) error {
	return o.c.Call("phoneMirrorManager", "selectDevice", nil, serial)
}

// DismissNotice calls dismissNotice().
func (o PhoneMirrorManager) DismissNotice() error {
	return o.c.Call("phoneMirrorManager", "dismissNotice", nil)
}

// InjectTouch calls injectTouch(int, int, float, float).
func (o PhoneMirrorManager) InjectTouch(pointerId int, action int, relX float64, relY float64) error {
	return o.c.Call("phoneMirrorManager", "injectTouch", nil, pointerId, action, relX, relY)
}

// InjectKey calls injectKey(int).
func (o PhoneMirrorManager) InjectKey(keycode int) error {
	return o.c.Call("phoneMirrorManager", "injectKey", nil, keycode)
}

// PressHome calls pressHome().
func (o PhoneMirrorManager) PressHome() error {
	return o.c.Call("phoneMirrorManager", "pressHome", nil)
}

// PressBack calls pressBack().
func (o PhoneMirrorManager) PressBack() error {
	return o.c.Call("phoneMirrorManager", "pressBack", nil)
}

// PressAppSwitch calls pressAppSwitch().
func (o PhoneMirrorManager) PressAppSwitch() error {
	return o.c.Call("phoneMirrorManager", "pressAppSwitch", nil)
}

// StartScrcpy calls startScrcpy().
func (o PhoneMirrorManager) StartScrcpy() error {
	return o.c.Call("phoneMirrorManager", "startScrcpy", nil)
}

// StopScrcpy calls stopScrcpy().
func (o PhoneMirrorManager) StopScrcpy() error {
	return o.c.Call("phoneMirrorManager", "stopScrcpy", nil)
}

// Cleanup calls cleanup().
func (o PhoneMirrorManager) Cleanup() error {
	return o.c.Call("phoneMirrorManager", "cleanup", nil)
}

// SetWirelessAddress calls setWirelessAddress(QString).
func (o PhoneMirrorManager) SetWirelessAddress(address string) error {
	return o.c.Call("phoneMirrorManager", "setWirelessAddress", nil, address)
}

// ForgetWireless calls forgetWireless().
func (o PhoneMirrorManager) ForgetWireless() error {
	return o.c.Call("phoneMirrorManager", "forgetWireless", nil)
}

// StartQrPairing calls startQrPairing().
func (o PhoneMirrorManager) StartQrPairing() error {
	return o.c.Call("phoneMirrorManager", "startQrPairing", nil)
}

// CancelQrPairing calls cancelQrPairing().
func (o PhoneMirrorManager) CancelQrPairing() error {
	return o.c.Call("phoneMirrorManager", "cancelQrPairing", nil)
}

// MediaPosition calls mediaPosition() -> int.
func (o PhoneMirrorManager) MediaPosition() (v int, err error) {
	err = o.c.Call("phoneMirrorManager", "mediaPosition", &v)
	return
}

// MediaPlayPause calls mediaPlayPause().
func (o PhoneMirrorManager) MediaPlayPause() error {
	return o.c.Call("phoneMirrorManager", "mediaPlayPause", nil)
}

// MediaPlay calls mediaPlay().
func (o PhoneMirrorManager) MediaPlay() error {
	return o.c.Call("phoneMirrorManager", "mediaPlay", nil)
}

// MediaPause calls mediaPause().
func (o PhoneMirrorManager) MediaPause() error {
	return o.c.Call("phoneMirrorManager", "mediaPause", nil)
}

// MediaNext calls mediaNext().
func (o PhoneMirrorManager) MediaNext() error {
	return o.c.Call("phoneMirrorManager", "mediaNext", nil)
}

// MediaPrevious calls mediaPrevious().
func (o PhoneMirrorManager) MediaPrevious() error {
	return o.c.Call("phoneMirrorManager", "mediaPrevious", nil)
}

// MediaSeek calls mediaSeek(int).
func (o PhoneMirrorManager) MediaSeek(positionMs int) error {
	return o.c.Call("phoneMirrorManager", "mediaSeek", nil, positionMs)
}

// MediaSkipToQueueItem calls mediaSkipToQueueItem(qlonglong).
func (o PhoneMirrorManager) MediaSkipToQueueItem(id int64) error {
	return o.c.Call("phoneMirrorManager", "mediaSkipToQueueItem", nil, id)
}

// ReleaseMedia calls releaseMedia(bool).
func (o PhoneMirrorManager) ReleaseMedia(pausePhone bool) error {
	return o.c.Call("phoneMirrorManager", "releaseMedia", nil, pausePhone)
}

// SettingsManager is the service object "settingsManager" (C++ SettingsManager); NewSettingsManager(c) binds it to a client.
type SettingsManager struct{ c *Client }

// NewSettingsManager binds "settingsManager" to c.
func NewSettingsManager(c *Client) SettingsManager { return SettingsManager{c} }

// Name is the object's name on the wire.
func (SettingsManager) Name() string { return "settingsManager" }

// GetDeviceName reads the property "deviceName" (QString).
func (o SettingsManager) GetDeviceName() (v string, err error) {
	err = o.c.Get("settingsManager", "deviceName", &v)
	return
}

// GetFontSetting reads the property "fontSetting" (QString).
func (o SettingsManager) GetFontSetting() (v string, err error) {
	err = o.c.Get("settingsManager", "fontSetting", &v)
	return
}

// GetThemeSetting reads the property "themeSetting" (QString).
func (o SettingsManager) GetThemeSetting() (v string, err error) {
	err = o.c.Get("settingsManager", "themeSetting", &v)
	return
}

// GetUiScale reads the property "uiScale" (float).
func (o SettingsManager) GetUiScale() (v float64, err error) {
	err = o.c.Get("settingsManager", "uiScale", &v)
	return
}

// GetColorTransitionMs reads the property "colorTransitionMs" (int).
func (o SettingsManager) GetColorTransitionMs() (v int, err error) {
	err = o.c.Get("settingsManager", "colorTransitionMs", &v)
	return
}

// GetSongLengthTransition reads the property "songLengthTransition" (bool).
func (o SettingsManager) GetSongLengthTransition() (v bool, err error) {
	err = o.c.Get("settingsManager", "songLengthTransition", &v)
	return
}

// GetShowClock reads the property "showClock" (bool).
func (o SettingsManager) GetShowClock() (v bool, err error) {
	err = o.c.Get("settingsManager", "showClock", &v)
	return
}

// GetClockFormat24Hour reads the property "clockFormat24Hour" (bool).
func (o SettingsManager) GetClockFormat24Hour() (v bool, err error) {
	err = o.c.Get("settingsManager", "clockFormat24Hour", &v)
	return
}

// GetClockShowSeconds reads the property "clockShowSeconds" (bool).
func (o SettingsManager) GetClockShowSeconds() (v bool, err error) {
	err = o.c.Get("settingsManager", "clockShowSeconds", &v)
	return
}

// GetClockSize reads the property "clockSize" (int).
func (o SettingsManager) GetClockSize() (v int, err error) {
	err = o.c.Get("settingsManager", "clockSize", &v)
	return
}

// GetClockSizeAuto reads the property "clockSizeAuto" (bool).
func (o SettingsManager) GetClockSizeAuto() (v bool, err error) {
	err = o.c.Get("settingsManager", "clockSizeAuto", &v)
	return
}

// GetBackgroundGrid reads the property "backgroundGrid" (QString).
func (o SettingsManager) GetBackgroundGrid() (v string, err error) {
	err = o.c.Get("settingsManager", "backgroundGrid", &v)
	return
}

// GetScreenWidth reads the property "screenWidth" (int).
func (o SettingsManager) GetScreenWidth() (v int, err error) {
	err = o.c.Get("settingsManager", "screenWidth", &v)
	return
}

// GetScreenHeight reads the property "screenHeight" (int).
func (o SettingsManager) GetScreenHeight() (v int, err error) {
	err = o.c.Get("settingsManager", "screenHeight", &v)
	return
}

// GetBackgroundBlurRadius reads the property "backgroundBlurRadius" (int).
func (o SettingsManager) GetBackgroundBlurRadius() (v int, err error) {
	err = o.c.Get("settingsManager", "backgroundBlurRadius", &v)
	return
}

// GetBottomBarOrientation reads the property "bottomBarOrientation" (QString).
func (o SettingsManager) GetBottomBarOrientation() (v string, err error) {
	err = o.c.Get("settingsManager", "bottomBarOrientation", &v)
	return
}

// GetShowBottomBarMediaControls reads the property "showBottomBarMediaControls" (bool).
func (o SettingsManager) GetShowBottomBarMediaControls() (v bool, err error) {
	err = o.c.Get("settingsManager", "showBottomBarMediaControls", &v)
	return
}

// GetSettingsLayoutStyle reads the property "settingsLayoutStyle" (QString).
func (o SettingsManager) GetSettingsLayoutStyle() (v string, err error) {
	err = o.c.Get("settingsManager", "settingsLayoutStyle", &v)
	return
}

// GetAlbumArtColors reads the property "albumArtColors" (QVariantMap).
func (o SettingsManager) GetAlbumArtColors() (v map[string]any, err error) {
	err = o.c.Get("settingsManager", "albumArtColors", &v)
	return
}

// GetSettingsMenuVisibility reads the property "settingsMenuVisibility" (QString).
func (o SettingsManager) GetSettingsMenuVisibility() (v string, err error) {
	err = o.c.Get("settingsManager", "settingsMenuVisibility", &v)
	return
}

// GetStartUpVolume reads the property "startUpVolume" (float).
func (o SettingsManager) GetStartUpVolume() (v float64, err error) {
	err = o.c.Get("settingsManager", "startUpVolume", &v)
	return
}

// GetCurrentVolume reads the property "currentVolume" (int).
func (o SettingsManager) GetCurrentVolume() (v int, err error) {
	err = o.c.Get("settingsManager", "currentVolume", &v)
	return
}

// GetMediaFolder reads the property "mediaFolder" (QString).
func (o SettingsManager) GetMediaFolder() (v string, err error) {
	err = o.c.Get("settingsManager", "mediaFolder", &v)
	return
}

// GetAutoPlayOnStartup reads the property "autoPlayOnStartup" (bool).
func (o SettingsManager) GetAutoPlayOnStartup() (v bool, err error) {
	err = o.c.Get("settingsManager", "autoPlayOnStartup", &v)
	return
}

// GetPersistShuffleState reads the property "persistShuffleState" (bool).
func (o SettingsManager) GetPersistShuffleState() (v bool, err error) {
	err = o.c.Get("settingsManager", "persistShuffleState", &v)
	return
}

// GetLastShuffleState reads the property "lastShuffleState" (bool).
func (o SettingsManager) GetLastShuffleState() (v bool, err error) {
	err = o.c.Get("settingsManager", "lastShuffleState", &v)
	return
}

// GetLastPlayedSong reads the property "lastPlayedSong" (QString).
func (o SettingsManager) GetLastPlayedSong() (v string, err error) {
	err = o.c.Get("settingsManager", "lastPlayedSong", &v)
	return
}

// GetLastPlayedPosition reads the property "lastPlayedPosition" (int).
func (o SettingsManager) GetLastPlayedPosition() (v int, err error) {
	err = o.c.Get("settingsManager", "lastPlayedPosition", &v)
	return
}

// GetLastPlayedPlaylist reads the property "lastPlayedPlaylist" (QString).
func (o SettingsManager) GetLastPlayedPlaylist() (v string, err error) {
	err = o.c.Get("settingsManager", "lastPlayedPlaylist", &v)
	return
}

// GetMusicButtonDefaultPage reads the property "musicButtonDefaultPage" (QString).
func (o SettingsManager) GetMusicButtonDefaultPage() (v string, err error) {
	err = o.c.Get("settingsManager", "musicButtonDefaultPage", &v)
	return
}

// GetReturnToLibraryAfterSelection reads the property "returnToLibraryAfterSelection" (bool).
func (o SettingsManager) GetReturnToLibraryAfterSelection() (v bool, err error) {
	err = o.c.Get("settingsManager", "returnToLibraryAfterSelection", &v)
	return
}

// GetShowWaveformVisualizer reads the property "showWaveformVisualizer" (bool).
func (o SettingsManager) GetShowWaveformVisualizer() (v bool, err error) {
	err = o.c.Get("settingsManager", "showWaveformVisualizer", &v)
	return
}

// GetShow3DButtonTilt reads the property "show3DButtonTilt" (bool).
func (o SettingsManager) GetShow3DButtonTilt() (v bool, err error) {
	err = o.c.Get("settingsManager", "show3DButtonTilt", &v)
	return
}

// GetShow3DAlbumPreview reads the property "show3DAlbumPreview" (bool).
func (o SettingsManager) GetShow3DAlbumPreview() (v bool, err error) {
	err = o.c.Get("settingsManager", "show3DAlbumPreview", &v)
	return
}

// GetRoundedAlbumArt reads the property "roundedAlbumArt" (bool).
func (o SettingsManager) GetRoundedAlbumArt() (v bool, err error) {
	err = o.c.Get("settingsManager", "roundedAlbumArt", &v)
	return
}

// GetAlbumArtCornerRadius reads the property "albumArtCornerRadius" (int).
func (o SettingsManager) GetAlbumArtCornerRadius() (v int, err error) {
	err = o.c.Get("settingsManager", "albumArtCornerRadius", &v)
	return
}

// GetShowAlbumArtShadow reads the property "showAlbumArtShadow" (bool).
func (o SettingsManager) GetShowAlbumArtShadow() (v bool, err error) {
	err = o.c.Get("settingsManager", "showAlbumArtShadow", &v)
	return
}

// GetVinylRecordMode reads the property "vinylRecordMode" (bool).
func (o SettingsManager) GetVinylRecordMode() (v bool, err error) {
	err = o.c.Get("settingsManager", "vinylRecordMode", &v)
	return
}

// GetAlbumArtTransition reads the property "albumArtTransition" (QString).
func (o SettingsManager) GetAlbumArtTransition() (v string, err error) {
	err = o.c.Get("settingsManager", "albumArtTransition", &v)
	return
}

// GetAudioOutputDevice reads the property "audioOutputDevice" (QString).
func (o SettingsManager) GetAudioOutputDevice() (v string, err error) {
	err = o.c.Get("settingsManager", "audioOutputDevice", &v)
	return
}

// GetBackgroundOverlayOpacity reads the property "backgroundOverlayOpacity" (int).
func (o SettingsManager) GetBackgroundOverlayOpacity() (v int, err error) {
	err = o.c.Get("settingsManager", "backgroundOverlayOpacity", &v)
	return
}

// GetSideCardOpacity reads the property "sideCardOpacity" (double).
func (o SettingsManager) GetSideCardOpacity() (v float64, err error) {
	err = o.c.Get("settingsManager", "sideCardOpacity", &v)
	return
}

// GetSideCardAngle reads the property "sideCardAngle" (int).
func (o SettingsManager) GetSideCardAngle() (v int, err error) {
	err = o.c.Get("settingsManager", "sideCardAngle", &v)
	return
}

// GetButtonTiltDuration reads the property "buttonTiltDuration" (int).
func (o SettingsManager) GetButtonTiltDuration() (v int, err error) {
	err = o.c.Get("settingsManager", "buttonTiltDuration", &v)
	return
}

// GetTextScrollSpeed reads the property "textScrollSpeed" (int).
func (o SettingsManager) GetTextScrollSpeed() (v int, err error) {
	err = o.c.Get("settingsManager", "textScrollSpeed", &v)
	return
}

// GetObdBluetoothPort reads the property "obdBluetoothPort" (QString).
func (o SettingsManager) GetObdBluetoothPort() (v string, err error) {
	err = o.c.Get("settingsManager", "obdBluetoothPort", &v)
	return
}

// GetObdSavedAdapters reads the property "obdSavedAdapters" (QVariantList).
func (o SettingsManager) GetObdSavedAdapters() (v []any, err error) {
	err = o.c.Get("settingsManager", "obdSavedAdapters", &v)
	return
}

// GetObdFastMode reads the property "obdFastMode" (bool).
func (o SettingsManager) GetObdFastMode() (v bool, err error) {
	err = o.c.Get("settingsManager", "obdFastMode", &v)
	return
}

// GetObdCardStyleCircular reads the property "obdCardStyleCircular" (bool).
func (o SettingsManager) GetObdCardStyleCircular() (v bool, err error) {
	err = o.c.Get("settingsManager", "obdCardStyleCircular", &v)
	return
}

// GetGaugeTempUnit reads the property "gaugeTempUnit" (QString).
func (o SettingsManager) GetGaugeTempUnit() (v string, err error) {
	err = o.c.Get("settingsManager", "gaugeTempUnit", &v)
	return
}

// GetGaugeSpeedUnit reads the property "gaugeSpeedUnit" (QString).
func (o SettingsManager) GetGaugeSpeedUnit() (v string, err error) {
	err = o.c.Get("settingsManager", "gaugeSpeedUnit", &v)
	return
}

// GetObdAutoReconnectAttempts reads the property "obdAutoReconnectAttempts" (int).
func (o SettingsManager) GetObdAutoReconnectAttempts() (v int, err error) {
	err = o.c.Get("settingsManager", "obdAutoReconnectAttempts", &v)
	return
}

// GetFuelTankCapacity reads the property "fuelTankCapacity" (float).
func (o SettingsManager) GetFuelTankCapacity() (v float64, err error) {
	err = o.c.Get("settingsManager", "fuelTankCapacity", &v)
	return
}

// GetBlackBoxEnabled reads the property "blackBoxEnabled" (bool).
func (o SettingsManager) GetBlackBoxEnabled() (v bool, err error) {
	err = o.c.Get("settingsManager", "blackBoxEnabled", &v)
	return
}

// GetBlackBoxSweepAll reads the property "blackBoxSweepAll" (bool).
func (o SettingsManager) GetBlackBoxSweepAll() (v bool, err error) {
	err = o.c.Get("settingsManager", "blackBoxSweepAll", &v)
	return
}

// GetBlackBoxMaxMB reads the property "blackBoxMaxMB" (int).
func (o SettingsManager) GetBlackBoxMaxMB() (v int, err error) {
	err = o.c.Get("settingsManager", "blackBoxMaxMB", &v)
	return
}

// GetHomeOBDParameters reads the property "homeOBDParameters" (QVariantList).
func (o SettingsManager) GetHomeOBDParameters() (v []any, err error) {
	err = o.c.Get("settingsManager", "homeOBDParameters", &v)
	return
}

// GetMaxHomeObdParameters reads the property "maxHomeObdParameters" (int).
func (o SettingsManager) GetMaxHomeObdParameters() (v int, err error) {
	err = o.c.Get("settingsManager", "maxHomeObdParameters", &v)
	return
}

// GetCustomThemes reads the property "customThemes" (QVariantList).
func (o SettingsManager) GetCustomThemes() (v []any, err error) {
	err = o.c.Get("settingsManager", "customThemes", &v)
	return
}

// GetCustomThemeData reads the property "customThemeData" (QVariantMap).
func (o SettingsManager) GetCustomThemeData() (v map[string]any, err error) {
	err = o.c.Get("settingsManager", "customThemeData", &v)
	return
}

// GetNowPlayingPresets reads the property "nowPlayingPresets" (QVariantList).
func (o SettingsManager) GetNowPlayingPresets() (v []any, err error) {
	err = o.c.Get("settingsManager", "nowPlayingPresets", &v)
	return
}

// GetDirectoryHistory reads the property "directoryHistory" (QVariantList).
func (o SettingsManager) GetDirectoryHistory() (v []any, err error) {
	err = o.c.Get("settingsManager", "directoryHistory", &v)
	return
}

// GetWindowState reads the property "windowState" (QString).
func (o SettingsManager) GetWindowState() (v string, err error) {
	err = o.c.Get("settingsManager", "windowState", &v)
	return
}

// GetLastSettingsSection reads the property "lastSettingsSection" (QString).
func (o SettingsManager) GetLastSettingsSection() (v string, err error) {
	err = o.c.Get("settingsManager", "lastSettingsSection", &v)
	return
}

// GetAndroidAutoEnabled reads the property "androidAutoEnabled" (bool).
func (o SettingsManager) GetAndroidAutoEnabled() (v bool, err error) {
	err = o.c.Get("settingsManager", "androidAutoEnabled", &v)
	return
}

// GetPhoneMirrorEnabled reads the property "phoneMirrorEnabled" (bool).
func (o SettingsManager) GetPhoneMirrorEnabled() (v bool, err error) {
	err = o.c.Get("settingsManager", "phoneMirrorEnabled", &v)
	return
}

// GetScrcpyDisplaySize reads the property "scrcpyDisplaySize" (QString).
func (o SettingsManager) GetScrcpyDisplaySize() (v string, err error) {
	err = o.c.Get("settingsManager", "scrcpyDisplaySize", &v)
	return
}

// GetScrcpyAudioGain reads the property "scrcpyAudioGain" (double).
func (o SettingsManager) GetScrcpyAudioGain() (v float64, err error) {
	err = o.c.Get("settingsManager", "scrcpyAudioGain", &v)
	return
}

// GetScrcpyAudioGainDb reads the property "scrcpyAudioGainDb" (double).
func (o SettingsManager) GetScrcpyAudioGainDb() (v float64, err error) {
	err = o.c.Get("settingsManager", "scrcpyAudioGainDb", &v)
	return
}

// GetScrcpyAudioDuckLevelDb reads the property "scrcpyAudioDuckLevelDb" (double).
func (o SettingsManager) GetScrcpyAudioDuckLevelDb() (v float64, err error) {
	err = o.c.Get("settingsManager", "scrcpyAudioDuckLevelDb", &v)
	return
}

// GetScrcpyAudioDuckEnabled reads the property "scrcpyAudioDuckEnabled" (bool).
func (o SettingsManager) GetScrcpyAudioDuckEnabled() (v bool, err error) {
	err = o.c.Get("settingsManager", "scrcpyAudioDuckEnabled", &v)
	return
}

// GetScrcpyAudioDuckLevel reads the property "scrcpyAudioDuckLevel" (double).
func (o SettingsManager) GetScrcpyAudioDuckLevel() (v float64, err error) {
	err = o.c.Get("settingsManager", "scrcpyAudioDuckLevel", &v)
	return
}

// GetScrcpyPhoneScreenOff reads the property "scrcpyPhoneScreenOff" (bool).
func (o SettingsManager) GetScrcpyPhoneScreenOff() (v bool, err error) {
	err = o.c.Get("settingsManager", "scrcpyPhoneScreenOff", &v)
	return
}

// GetScrcpyDisplayDpi reads the property "scrcpyDisplayDpi" (int).
func (o SettingsManager) GetScrcpyDisplayDpi() (v int, err error) {
	err = o.c.Get("settingsManager", "scrcpyDisplayDpi", &v)
	return
}

// GetScrcpyBitRateUsb reads the property "scrcpyBitRateUsb" (int).
func (o SettingsManager) GetScrcpyBitRateUsb() (v int, err error) {
	err = o.c.Get("settingsManager", "scrcpyBitRateUsb", &v)
	return
}

// GetScrcpyBitRateWifi reads the property "scrcpyBitRateWifi" (int).
func (o SettingsManager) GetScrcpyBitRateWifi() (v int, err error) {
	err = o.c.Get("settingsManager", "scrcpyBitRateWifi", &v)
	return
}

// GetScrcpyAutoWifi reads the property "scrcpyAutoWifi" (bool).
func (o SettingsManager) GetScrcpyAutoWifi() (v bool, err error) {
	err = o.c.Get("settingsManager", "scrcpyAutoWifi", &v)
	return
}

// GetScrcpyBitRateAuto reads the property "scrcpyBitRateAuto" (bool).
func (o SettingsManager) GetScrcpyBitRateAuto() (v bool, err error) {
	err = o.c.Get("settingsManager", "scrcpyBitRateAuto", &v)
	return
}

// GetScrcpyWirelessAddress reads the property "scrcpyWirelessAddress" (QString).
func (o SettingsManager) GetScrcpyWirelessAddress() (v string, err error) {
	err = o.c.Get("settingsManager", "scrcpyWirelessAddress", &v)
	return
}

// GetEsp32VolumeEnabled reads the property "esp32VolumeEnabled" (bool).
func (o SettingsManager) GetEsp32VolumeEnabled() (v bool, err error) {
	err = o.c.Get("settingsManager", "esp32VolumeEnabled", &v)
	return
}

// GetEsp32VolumePort reads the property "esp32VolumePort" (QString).
func (o SettingsManager) GetEsp32VolumePort() (v string, err error) {
	err = o.c.Get("settingsManager", "esp32VolumePort", &v)
	return
}

// GetEsp32VolumeStepSize reads the property "esp32VolumeStepSize" (double).
func (o SettingsManager) GetEsp32VolumeStepSize() (v float64, err error) {
	err = o.c.Get("settingsManager", "esp32VolumeStepSize", &v)
	return
}

// GetEsp32AutoReconnect reads the property "esp32AutoReconnect" (bool).
func (o SettingsManager) GetEsp32AutoReconnect() (v bool, err error) {
	err = o.c.Get("settingsManager", "esp32AutoReconnect", &v)
	return
}

// GetEsp32LedSleepEnabled reads the property "esp32LedSleepEnabled" (bool).
func (o SettingsManager) GetEsp32LedSleepEnabled() (v bool, err error) {
	err = o.c.Get("settingsManager", "esp32LedSleepEnabled", &v)
	return
}

// GetEsp32LedColorMode reads the property "esp32LedColorMode" (QString).
func (o SettingsManager) GetEsp32LedColorMode() (v string, err error) {
	err = o.c.Get("settingsManager", "esp32LedColorMode", &v)
	return
}

// GetEsp32LedStaticColor reads the property "esp32LedStaticColor" (QString).
func (o SettingsManager) GetEsp32LedStaticColor() (v string, err error) {
	err = o.c.Get("settingsManager", "esp32LedStaticColor", &v)
	return
}

// GetImuEnabled reads the property "imuEnabled" (bool).
func (o SettingsManager) GetImuEnabled() (v bool, err error) {
	err = o.c.Get("settingsManager", "imuEnabled", &v)
	return
}

// GetSensorEmitRate reads the property "sensorEmitRate" (int).
func (o SettingsManager) GetSensorEmitRate() (v int, err error) {
	err = o.c.Get("settingsManager", "sensorEmitRate", &v)
	return
}

// GetSensorDecimalPlaces reads the property "sensorDecimalPlaces" (int).
func (o SettingsManager) GetSensorDecimalPlaces() (v int, err error) {
	err = o.c.Get("settingsManager", "sensorDecimalPlaces", &v)
	return
}

// GetSensorTempUnit reads the property "sensorTempUnit" (QString).
func (o SettingsManager) GetSensorTempUnit() (v string, err error) {
	err = o.c.Get("settingsManager", "sensorTempUnit", &v)
	return
}

// GetSensorAltitudeUnit reads the property "sensorAltitudeUnit" (QString).
func (o SettingsManager) GetSensorAltitudeUnit() (v string, err error) {
	err = o.c.Get("settingsManager", "sensorAltitudeUnit", &v)
	return
}

// GetSensorCards reads the property "sensorCards" (QVariantMap).
func (o SettingsManager) GetSensorCards() (v map[string]any, err error) {
	err = o.c.Get("settingsManager", "sensorCards", &v)
	return
}

// GetGestureSensorEnabled reads the property "gestureSensorEnabled" (bool).
func (o SettingsManager) GetGestureSensorEnabled() (v bool, err error) {
	err = o.c.Get("settingsManager", "gestureSensorEnabled", &v)
	return
}

// GetGestureVolumeStep reads the property "gestureVolumeStep" (int).
func (o SettingsManager) GetGestureVolumeStep() (v int, err error) {
	err = o.c.Get("settingsManager", "gestureVolumeStep", &v)
	return
}

// GetGestureCooldown reads the property "gestureCooldown" (int).
func (o SettingsManager) GetGestureCooldown() (v int, err error) {
	err = o.c.Get("settingsManager", "gestureCooldown", &v)
	return
}

// GetPinnedSettings reads the property "pinnedSettings" (QVariantList).
func (o SettingsManager) GetPinnedSettings() (v []any, err error) {
	err = o.c.Get("settingsManager", "pinnedSettings", &v)
	return
}

// SaveDeviceName calls save_device_name(QString).
func (o SettingsManager) SaveDeviceName(name string) error {
	return o.c.Call("settingsManager", "save_device_name", nil, name)
}

// SaveFontSetting calls save_font_setting(QString).
func (o SettingsManager) SaveFontSetting(font string) error {
	return o.c.Call("settingsManager", "save_font_setting", nil, font)
}

// SaveThemeSetting calls save_theme_setting(QString).
func (o SettingsManager) SaveThemeSetting(theme string) error {
	return o.c.Call("settingsManager", "save_theme_setting", nil, theme)
}

// SaveUiScale calls save_ui_scale(float).
func (o SettingsManager) SaveUiScale(scale float64) error {
	return o.c.Call("settingsManager", "save_ui_scale", nil, scale)
}

// SaveColorTransitionMs calls save_color_transition_ms(int).
func (o SettingsManager) SaveColorTransitionMs(ms int) error {
	return o.c.Call("settingsManager", "save_color_transition_ms", nil, ms)
}

// SaveSongLengthTransition calls save_song_length_transition(bool).
func (o SettingsManager) SaveSongLengthTransition(enabled bool) error {
	return o.c.Call("settingsManager", "save_song_length_transition", nil, enabled)
}

// SaveShowClock calls save_show_clock(bool).
func (o SettingsManager) SaveShowClock(show bool) error {
	return o.c.Call("settingsManager", "save_show_clock", nil, show)
}

// SaveClockFormat calls save_clock_format(bool).
func (o SettingsManager) SaveClockFormat(is24hour bool) error {
	return o.c.Call("settingsManager", "save_clock_format", nil, is24hour)
}

// SaveClockShowSeconds calls save_clock_show_seconds(bool).
func (o SettingsManager) SaveClockShowSeconds(show bool) error {
	return o.c.Call("settingsManager", "save_clock_show_seconds", nil, show)
}

// SaveClockSize calls save_clock_size(int).
func (o SettingsManager) SaveClockSize(size int) error {
	return o.c.Call("settingsManager", "save_clock_size", nil, size)
}

// SaveClockSizeAuto calls save_clock_size_auto(bool).
func (o SettingsManager) SaveClockSizeAuto(autoSize bool) error {
	return o.c.Call("settingsManager", "save_clock_size_auto", nil, autoSize)
}

// SaveBackgroundGrid calls save_background_grid(QString).
func (o SettingsManager) SaveBackgroundGrid(grid string) error {
	return o.c.Call("settingsManager", "save_background_grid", nil, grid)
}

// SaveScreenWidth calls save_screen_width(int).
func (o SettingsManager) SaveScreenWidth(width int) error {
	return o.c.Call("settingsManager", "save_screen_width", nil, width)
}

// SaveScreenHeight calls save_screen_height(int).
func (o SettingsManager) SaveScreenHeight(height int) error {
	return o.c.Call("settingsManager", "save_screen_height", nil, height)
}

// SaveBackgroundBlurRadius calls save_background_blur_radius(int).
func (o SettingsManager) SaveBackgroundBlurRadius(radius int) error {
	return o.c.Call("settingsManager", "save_background_blur_radius", nil, radius)
}

// SaveBottomBarOrientation calls save_bottom_bar_orientation(QString).
func (o SettingsManager) SaveBottomBarOrientation(orientation string) error {
	return o.c.Call("settingsManager", "save_bottom_bar_orientation", nil, orientation)
}

// SaveShowBottomBarMediaControls calls save_show_bottom_bar_media_controls(bool).
func (o SettingsManager) SaveShowBottomBarMediaControls(show bool) error {
	return o.c.Call("settingsManager", "save_show_bottom_bar_media_controls", nil, show)
}

// SaveSettingsLayoutStyle calls save_settings_layout_style(QString).
func (o SettingsManager) SaveSettingsLayoutStyle(style string) error {
	return o.c.Call("settingsManager", "save_settings_layout_style", nil, style)
}

// SetAlbumArtColors calls set_album_art_colors(QString).
func (o SettingsManager) SetAlbumArtColors(colorsJson string) error {
	return o.c.Call("settingsManager", "set_album_art_colors", nil, colorsJson)
}

// SaveStartVolume calls save_start_volume(float).
func (o SettingsManager) SaveStartVolume(volume float64) error {
	return o.c.Call("settingsManager", "save_start_volume", nil, volume)
}

// SetCurrentVolume calls setCurrentVolume(int).
func (o SettingsManager) SetCurrentVolume(volume int) error {
	return o.c.Call("settingsManager", "setCurrentVolume", nil, volume)
}

// SaveMediaFolder calls save_media_folder(QString) -> bool.
func (o SettingsManager) SaveMediaFolder(folderPath string) (v bool, err error) {
	err = o.c.Call("settingsManager", "save_media_folder", &v, folderPath)
	return
}

// SaveMediaFolderUrl calls save_media_folder_url(QUrl) -> bool.
func (o SettingsManager) SaveMediaFolderUrl(url string) (v bool, err error) {
	err = o.c.Call("settingsManager", "save_media_folder_url", &v, url)
	return
}

// SaveAutoPlayOnStartup calls save_auto_play_on_startup(bool).
func (o SettingsManager) SaveAutoPlayOnStartup(enabled bool) error {
	return o.c.Call("settingsManager", "save_auto_play_on_startup", nil, enabled)
}

// SavePersistShuffleState calls save_persist_shuffle_state(bool).
func (o SettingsManager) SavePersistShuffleState(enabled bool) error {
	return o.c.Call("settingsManager", "save_persist_shuffle_state", nil, enabled)
}

// SaveLastShuffleState calls save_last_shuffle_state(bool).
func (o SettingsManager) SaveLastShuffleState(enabled bool) error {
	return o.c.Call("settingsManager", "save_last_shuffle_state", nil, enabled)
}

// SaveLastPlayedSong calls save_last_played_song(QString).
func (o SettingsManager) SaveLastPlayedSong(filename string) error {
	return o.c.Call("settingsManager", "save_last_played_song", nil, filename)
}

// SaveLastPlayedPosition calls save_last_played_position(int).
func (o SettingsManager) SaveLastPlayedPosition(positionMs int) error {
	return o.c.Call("settingsManager", "save_last_played_position", nil, positionMs)
}

// SaveLastPlayedPlaylist calls save_last_played_playlist(QString).
func (o SettingsManager) SaveLastPlayedPlaylist(playlistName string) error {
	return o.c.Call("settingsManager", "save_last_played_playlist", nil, playlistName)
}

// SavePlaybackState calls save_playback_state(QString, int, QString).
func (o SettingsManager) SavePlaybackState(song string, positionMs int, playlist string) error {
	return o.c.Call("settingsManager", "save_playback_state", nil, song, positionMs, playlist)
}

// SaveMusicButtonDefaultPage calls save_music_button_default_page(QString).
func (o SettingsManager) SaveMusicButtonDefaultPage(page string) error {
	return o.c.Call("settingsManager", "save_music_button_default_page", nil, page)
}

// SaveReturnToLibraryAfterSelection calls save_return_to_library_after_selection(bool).
func (o SettingsManager) SaveReturnToLibraryAfterSelection(returnToLibrary bool) error {
	return o.c.Call("settingsManager", "save_return_to_library_after_selection", nil, returnToLibrary)
}

// SaveShowWaveformVisualizer calls save_show_waveform_visualizer(bool).
func (o SettingsManager) SaveShowWaveformVisualizer(enabled bool) error {
	return o.c.Call("settingsManager", "save_show_waveform_visualizer", nil, enabled)
}

// SaveShow3dButtonTilt calls save_show_3d_button_tilt(bool).
func (o SettingsManager) SaveShow3dButtonTilt(enabled bool) error {
	return o.c.Call("settingsManager", "save_show_3d_button_tilt", nil, enabled)
}

// SaveShow3dAlbumPreview calls save_show_3d_album_preview(bool).
func (o SettingsManager) SaveShow3dAlbumPreview(enabled bool) error {
	return o.c.Call("settingsManager", "save_show_3d_album_preview", nil, enabled)
}

// SaveRoundedAlbumArt calls save_rounded_album_art(bool).
func (o SettingsManager) SaveRoundedAlbumArt(enabled bool) error {
	return o.c.Call("settingsManager", "save_rounded_album_art", nil, enabled)
}

// SaveAlbumArtCornerRadius calls save_album_art_corner_radius(int).
func (o SettingsManager) SaveAlbumArtCornerRadius(radius int) error {
	return o.c.Call("settingsManager", "save_album_art_corner_radius", nil, radius)
}

// SaveShowAlbumArtShadow calls save_show_album_art_shadow(bool).
func (o SettingsManager) SaveShowAlbumArtShadow(enabled bool) error {
	return o.c.Call("settingsManager", "save_show_album_art_shadow", nil, enabled)
}

// SaveVinylRecordMode calls save_vinyl_record_mode(bool).
func (o SettingsManager) SaveVinylRecordMode(enabled bool) error {
	return o.c.Call("settingsManager", "save_vinyl_record_mode", nil, enabled)
}

// SaveAlbumArtTransition calls save_album_art_transition(QString).
func (o SettingsManager) SaveAlbumArtTransition(transition string) error {
	return o.c.Call("settingsManager", "save_album_art_transition", nil, transition)
}

// SaveAudioOutputDevice calls save_audio_output_device(QString).
func (o SettingsManager) SaveAudioOutputDevice(id string) error {
	return o.c.Call("settingsManager", "save_audio_output_device", nil, id)
}

// SaveBackgroundOverlayOpacity calls save_background_overlay_opacity(int).
func (o SettingsManager) SaveBackgroundOverlayOpacity(opacity int) error {
	return o.c.Call("settingsManager", "save_background_overlay_opacity", nil, opacity)
}

// SaveSideCardOpacity calls save_side_card_opacity(double).
func (o SettingsManager) SaveSideCardOpacity(opacity float64) error {
	return o.c.Call("settingsManager", "save_side_card_opacity", nil, opacity)
}

// SaveSideCardAngle calls save_side_card_angle(int).
func (o SettingsManager) SaveSideCardAngle(angle int) error {
	return o.c.Call("settingsManager", "save_side_card_angle", nil, angle)
}

// SaveButtonTiltDuration calls save_button_tilt_duration(int).
func (o SettingsManager) SaveButtonTiltDuration(duration int) error {
	return o.c.Call("settingsManager", "save_button_tilt_duration", nil, duration)
}

// SaveTextScrollSpeed calls save_text_scroll_speed(int).
func (o SettingsManager) SaveTextScrollSpeed(speed int) error {
	return o.c.Call("settingsManager", "save_text_scroll_speed", nil, speed)
}

// SaveObdBluetoothPort calls save_obd_bluetooth_port(QString).
func (o SettingsManager) SaveObdBluetoothPort(port string) error {
	return o.c.Call("settingsManager", "save_obd_bluetooth_port", nil, port)
}

// AddObdSavedAdapter calls add_obd_saved_adapter(QString, QString).
func (o SettingsManager) AddObdSavedAdapter(name string, mac string) error {
	return o.c.Call("settingsManager", "add_obd_saved_adapter", nil, name, mac)
}

// RemoveObdSavedAdapter calls remove_obd_saved_adapter(QString).
func (o SettingsManager) RemoveObdSavedAdapter(mac string) error {
	return o.c.Call("settingsManager", "remove_obd_saved_adapter", nil, mac)
}

// SaveObdFastMode calls save_obd_fast_mode(bool).
func (o SettingsManager) SaveObdFastMode(enabled bool) error {
	return o.c.Call("settingsManager", "save_obd_fast_mode", nil, enabled)
}

// SaveObdCardStyleCircular calls save_obd_card_style_circular(bool).
func (o SettingsManager) SaveObdCardStyleCircular(circular bool) error {
	return o.c.Call("settingsManager", "save_obd_card_style_circular", nil, circular)
}

// SaveGaugeTempUnit calls save_gauge_temp_unit(QString).
func (o SettingsManager) SaveGaugeTempUnit(unit string) error {
	return o.c.Call("settingsManager", "save_gauge_temp_unit", nil, unit)
}

// SaveGaugeSpeedUnit calls save_gauge_speed_unit(QString).
func (o SettingsManager) SaveGaugeSpeedUnit(unit string) error {
	return o.c.Call("settingsManager", "save_gauge_speed_unit", nil, unit)
}

// SaveObdAutoReconnectAttempts calls save_obd_auto_reconnect_attempts(int).
func (o SettingsManager) SaveObdAutoReconnectAttempts(attempts int) error {
	return o.c.Call("settingsManager", "save_obd_auto_reconnect_attempts", nil, attempts)
}

// SaveFuelTankCapacity calls save_fuel_tank_capacity(float).
func (o SettingsManager) SaveFuelTankCapacity(capacity float64) error {
	return o.c.Call("settingsManager", "save_fuel_tank_capacity", nil, capacity)
}

// SaveBlackBoxEnabled calls save_black_box_enabled(bool).
func (o SettingsManager) SaveBlackBoxEnabled(enabled bool) error {
	return o.c.Call("settingsManager", "save_black_box_enabled", nil, enabled)
}

// SaveBlackBoxSweepAll calls save_black_box_sweep_all(bool).
func (o SettingsManager) SaveBlackBoxSweepAll(enabled bool) error {
	return o.c.Call("settingsManager", "save_black_box_sweep_all", nil, enabled)
}

// SaveBlackBoxMaxMb calls save_black_box_max_mb(int).
func (o SettingsManager) SaveBlackBoxMaxMb(megabytes int) error {
	return o.c.Call("settingsManager", "save_black_box_max_mb", nil, megabytes)
}

// SaveObdParameterEnabled calls save_obd_parameter_enabled(QString, bool).
func (o SettingsManager) SaveObdParameterEnabled(parameter string, enabled bool) error {
	return o.c.Call("settingsManager", "save_obd_parameter_enabled", nil, parameter, enabled)
}

// SaveSupportedObdParameters calls save_supported_obd_parameters(QVariantList).
func (o SettingsManager) SaveSupportedObdParameters(parameters []any) error {
	return o.c.Call("settingsManager", "save_supported_obd_parameters", nil, parameters)
}

// SaveHomeObdParameters calls save_home_obd_parameters(QVariantList).
func (o SettingsManager) SaveHomeObdParameters(parameters []any) error {
	return o.c.Call("settingsManager", "save_home_obd_parameters", nil, parameters)
}

// AddHomeObdParameter2 calls add_home_obd_parameter(QString, int) -> bool.
func (o SettingsManager) AddHomeObdParameter2(id string, index int) (v bool, err error) {
	err = o.c.Call("settingsManager", "add_home_obd_parameter", &v, id, index)
	return
}

// AddHomeObdParameter1 calls add_home_obd_parameter(QString) -> bool.
func (o SettingsManager) AddHomeObdParameter1(id string) (v bool, err error) {
	err = o.c.Call("settingsManager", "add_home_obd_parameter", &v, id)
	return
}

// RemoveHomeObdParameter calls remove_home_obd_parameter(QString) -> bool.
func (o SettingsManager) RemoveHomeObdParameter(id string) (v bool, err error) {
	err = o.c.Call("settingsManager", "remove_home_obd_parameter", &v, id)
	return
}

// MoveHomeObdParameter calls move_home_obd_parameter(int, int) -> bool.
func (o SettingsManager) MoveHomeObdParameter(from int, to int) (v bool, err error) {
	err = o.c.Call("settingsManager", "move_home_obd_parameter", &v, from, to)
	return
}

// SaveToDirectoryHistory calls save_to_directory_history(QString).
func (o SettingsManager) SaveToDirectoryHistory(folderPath string) error {
	return o.c.Call("settingsManager", "save_to_directory_history", nil, folderPath)
}

// RemoveFromDirectoryHistory calls remove_from_directory_history(QString).
func (o SettingsManager) RemoveFromDirectoryHistory(folderPath string) error {
	return o.c.Call("settingsManager", "remove_from_directory_history", nil, folderPath)
}

// SaveCustomTheme3 calls save_custom_theme(QString, QVariantMap, QString).
func (o SettingsManager) SaveCustomTheme3(name string, theme map[string]any, albumArtSource string) error {
	return o.c.Call("settingsManager", "save_custom_theme", nil, name, theme, albumArtSource)
}

// SaveCustomTheme2 calls save_custom_theme(QString, QVariantMap).
func (o SettingsManager) SaveCustomTheme2(name string, theme map[string]any) error {
	return o.c.Call("settingsManager", "save_custom_theme", nil, name, theme)
}

// DeleteCustomTheme calls delete_custom_theme(QString).
func (o SettingsManager) DeleteCustomTheme(name string) error {
	return o.c.Call("settingsManager", "delete_custom_theme", nil, name)
}

// SaveWindowState calls save_window_state(QString).
func (o SettingsManager) SaveWindowState(state string) error {
	return o.c.Call("settingsManager", "save_window_state", nil, state)
}

// SetLastSettingsSection calls set_last_settings_section(QString).
func (o SettingsManager) SetLastSettingsSection(section string) error {
	return o.c.Call("settingsManager", "set_last_settings_section", nil, section)
}

// SaveSettingsSectionVisibility calls save_settings_section_visibility(QString, bool).
func (o SettingsManager) SaveSettingsSectionVisibility(section string, visible bool) error {
	return o.c.Call("settingsManager", "save_settings_section_visibility", nil, section, visible)
}

// SaveAndroidAutoEnabled calls save_android_auto_enabled(bool).
func (o SettingsManager) SaveAndroidAutoEnabled(enabled bool) error {
	return o.c.Call("settingsManager", "save_android_auto_enabled", nil, enabled)
}

// SavePhoneMirrorEnabled calls save_phone_mirror_enabled(bool).
func (o SettingsManager) SavePhoneMirrorEnabled(enabled bool) error {
	return o.c.Call("settingsManager", "save_phone_mirror_enabled", nil, enabled)
}

// SaveScrcpyDisplaySize calls save_scrcpy_display_size(QString).
func (o SettingsManager) SaveScrcpyDisplaySize(size string) error {
	return o.c.Call("settingsManager", "save_scrcpy_display_size", nil, size)
}

// SaveScrcpyAudioGain calls save_scrcpy_audio_gain(double).
func (o SettingsManager) SaveScrcpyAudioGain(gain float64) error {
	return o.c.Call("settingsManager", "save_scrcpy_audio_gain", nil, gain)
}

// SaveScrcpyAudioDuckEnabled calls save_scrcpy_audio_duck_enabled(bool).
func (o SettingsManager) SaveScrcpyAudioDuckEnabled(enabled bool) error {
	return o.c.Call("settingsManager", "save_scrcpy_audio_duck_enabled", nil, enabled)
}

// SaveScrcpyAudioDuckLevel calls save_scrcpy_audio_duck_level(double).
func (o SettingsManager) SaveScrcpyAudioDuckLevel(level float64) error {
	return o.c.Call("settingsManager", "save_scrcpy_audio_duck_level", nil, level)
}

// SaveScrcpyAudioGainDb calls save_scrcpy_audio_gain_db(double).
func (o SettingsManager) SaveScrcpyAudioGainDb(db float64) error {
	return o.c.Call("settingsManager", "save_scrcpy_audio_gain_db", nil, db)
}

// SaveScrcpyAudioDuckLevelDb calls save_scrcpy_audio_duck_level_db(double).
func (o SettingsManager) SaveScrcpyAudioDuckLevelDb(db float64) error {
	return o.c.Call("settingsManager", "save_scrcpy_audio_duck_level_db", nil, db)
}

// SaveScrcpyPhoneScreenOff calls save_scrcpy_phone_screen_off(bool).
func (o SettingsManager) SaveScrcpyPhoneScreenOff(off bool) error {
	return o.c.Call("settingsManager", "save_scrcpy_phone_screen_off", nil, off)
}

// SaveScrcpyDisplayDpi calls save_scrcpy_display_dpi(int).
func (o SettingsManager) SaveScrcpyDisplayDpi(dpi int) error {
	return o.c.Call("settingsManager", "save_scrcpy_display_dpi", nil, dpi)
}

// SaveScrcpyBitRateUsb calls save_scrcpy_bit_rate_usb(int).
func (o SettingsManager) SaveScrcpyBitRateUsb(mbps int) error {
	return o.c.Call("settingsManager", "save_scrcpy_bit_rate_usb", nil, mbps)
}

// SaveScrcpyBitRateWifi calls save_scrcpy_bit_rate_wifi(int).
func (o SettingsManager) SaveScrcpyBitRateWifi(mbps int) error {
	return o.c.Call("settingsManager", "save_scrcpy_bit_rate_wifi", nil, mbps)
}

// SaveScrcpyAutoWifi calls save_scrcpy_auto_wifi(bool).
func (o SettingsManager) SaveScrcpyAutoWifi(enabled bool) error {
	return o.c.Call("settingsManager", "save_scrcpy_auto_wifi", nil, enabled)
}

// SaveScrcpyBitRateAuto calls save_scrcpy_bit_rate_auto(bool).
func (o SettingsManager) SaveScrcpyBitRateAuto(enabled bool) error {
	return o.c.Call("settingsManager", "save_scrcpy_bit_rate_auto", nil, enabled)
}

// SaveScrcpyWirelessAddress calls save_scrcpy_wireless_address(QString).
func (o SettingsManager) SaveScrcpyWirelessAddress(address string) error {
	return o.c.Call("settingsManager", "save_scrcpy_wireless_address", nil, address)
}

// SaveEsp32VolumeEnabled calls save_esp32_volume_enabled(bool).
func (o SettingsManager) SaveEsp32VolumeEnabled(enabled bool) error {
	return o.c.Call("settingsManager", "save_esp32_volume_enabled", nil, enabled)
}

// SaveEsp32VolumePort calls save_esp32_volume_port(QString).
func (o SettingsManager) SaveEsp32VolumePort(port string) error {
	return o.c.Call("settingsManager", "save_esp32_volume_port", nil, port)
}

// SaveEsp32VolumeStepSize calls save_esp32_volume_step_size(double).
func (o SettingsManager) SaveEsp32VolumeStepSize(stepSize float64) error {
	return o.c.Call("settingsManager", "save_esp32_volume_step_size", nil, stepSize)
}

// SaveEsp32AutoReconnect calls save_esp32_auto_reconnect(bool).
func (o SettingsManager) SaveEsp32AutoReconnect(enabled bool) error {
	return o.c.Call("settingsManager", "save_esp32_auto_reconnect", nil, enabled)
}

// SaveEsp32LedSleepEnabled calls save_esp32_led_sleep_enabled(bool).
func (o SettingsManager) SaveEsp32LedSleepEnabled(enabled bool) error {
	return o.c.Call("settingsManager", "save_esp32_led_sleep_enabled", nil, enabled)
}

// SaveEsp32LedColorMode calls save_esp32_led_color_mode(QString).
func (o SettingsManager) SaveEsp32LedColorMode(mode string) error {
	return o.c.Call("settingsManager", "save_esp32_led_color_mode", nil, mode)
}

// SaveEsp32LedStaticColor calls save_esp32_led_static_color(QString).
func (o SettingsManager) SaveEsp32LedStaticColor(color string) error {
	return o.c.Call("settingsManager", "save_esp32_led_static_color", nil, color)
}

// SaveImuEnabled calls save_imu_enabled(bool).
func (o SettingsManager) SaveImuEnabled(enabled bool) error {
	return o.c.Call("settingsManager", "save_imu_enabled", nil, enabled)
}

// SaveSensorEmitRate calls save_sensor_emit_rate(int).
func (o SettingsManager) SaveSensorEmitRate(hz int) error {
	return o.c.Call("settingsManager", "save_sensor_emit_rate", nil, hz)
}

// SaveSensorDecimalPlaces calls save_sensor_decimal_places(int).
func (o SettingsManager) SaveSensorDecimalPlaces(places int) error {
	return o.c.Call("settingsManager", "save_sensor_decimal_places", nil, places)
}

// SaveSensorTempUnit calls save_sensor_temp_unit(QString).
func (o SettingsManager) SaveSensorTempUnit(unit string) error {
	return o.c.Call("settingsManager", "save_sensor_temp_unit", nil, unit)
}

// SaveSensorAltitudeUnit calls save_sensor_altitude_unit(QString).
func (o SettingsManager) SaveSensorAltitudeUnit(unit string) error {
	return o.c.Call("settingsManager", "save_sensor_altitude_unit", nil, unit)
}

// SaveSensorCardVisible calls save_sensor_card_visible(QString, bool).
func (o SettingsManager) SaveSensorCardVisible(card string, visible bool) error {
	return o.c.Call("settingsManager", "save_sensor_card_visible", nil, card, visible)
}

// SaveGestureSensorEnabled calls save_gesture_sensor_enabled(bool).
func (o SettingsManager) SaveGestureSensorEnabled(enabled bool) error {
	return o.c.Call("settingsManager", "save_gesture_sensor_enabled", nil, enabled)
}

// SaveGestureAction calls save_gesture_action(QString, QString).
func (o SettingsManager) SaveGestureAction(gesture string, action string) error {
	return o.c.Call("settingsManager", "save_gesture_action", nil, gesture, action)
}

// SaveGestureMapping calls save_gesture_mapping(QVariantMap).
func (o SettingsManager) SaveGestureMapping(mapping map[string]any) error {
	return o.c.Call("settingsManager", "save_gesture_mapping", nil, mapping)
}

// ResetGestureMapping calls reset_gesture_mapping().
func (o SettingsManager) ResetGestureMapping() error {
	return o.c.Call("settingsManager", "reset_gesture_mapping", nil)
}

// SaveGestureVolumeStep calls save_gesture_volume_step(int).
func (o SettingsManager) SaveGestureVolumeStep(step int) error {
	return o.c.Call("settingsManager", "save_gesture_volume_step", nil, step)
}

// SaveGestureCooldown calls save_gesture_cooldown(int).
func (o SettingsManager) SaveGestureCooldown(ms int) error {
	return o.c.Call("settingsManager", "save_gesture_cooldown", nil, ms)
}

// TogglePinSetting calls toggle_pin_setting(QString).
func (o SettingsManager) TogglePinSetting(settingId string) error {
	return o.c.Call("settingsManager", "toggle_pin_setting", nil, settingId)
}

// ResetToDefaults calls reset_to_defaults().
func (o SettingsManager) ResetToDefaults() error {
	return o.c.Call("settingsManager", "reset_to_defaults", nil)
}

// SaveSetting calls save_setting(QString, QVariant).
func (o SettingsManager) SaveSetting(key string, value any) error {
	return o.c.Call("settingsManager", "save_setting", nil, key, value)
}

// ApplySettings calls apply_settings(QVariantMap) -> QVariantMap.
func (o SettingsManager) ApplySettings(values map[string]any) (v map[string]any, err error) {
	err = o.c.Call("settingsManager", "apply_settings", &v, values)
	return
}

// ApplyNowPlayingPreset calls applyNowPlayingPreset(QString) -> bool.
func (o SettingsManager) ApplyNowPlayingPreset(label string) (v bool, err error) {
	err = o.c.Call("settingsManager", "applyNowPlayingPreset", &v, label)
	return
}

// GetSetting calls get_setting(QString) -> QVariant.
func (o SettingsManager) GetSetting(key string) (v any, err error) {
	err = o.c.Call("settingsManager", "get_setting", &v, key)
	return
}

// GetSettingWithDefault calls get_setting_with_default(QString, QVariant) -> QVariant.
func (o SettingsManager) GetSettingWithDefault(key string, defaultValue any) (v any, err error) {
	err = o.c.Call("settingsManager", "get_setting_with_default", &v, key, defaultValue)
	return
}

// GetObdParameterEnabled2 calls get_obd_parameter_enabled(QString, bool) -> bool.
func (o SettingsManager) GetObdParameterEnabled2(parameter string, defaultVal bool) (v bool, err error) {
	err = o.c.Call("settingsManager", "get_obd_parameter_enabled", &v, parameter, defaultVal)
	return
}

// GetObdParameterEnabled1 calls get_obd_parameter_enabled(QString) -> bool.
func (o SettingsManager) GetObdParameterEnabled1(parameter string) (v bool, err error) {
	err = o.c.Call("settingsManager", "get_obd_parameter_enabled", &v, parameter)
	return
}

// GetSupportedObdParameters calls get_supported_obd_parameters() -> QVariantList.
func (o SettingsManager) GetSupportedObdParameters() (v []any, err error) {
	err = o.c.Call("settingsManager", "get_supported_obd_parameters", &v)
	return
}

// GetDirectoryHistoryCall calls get_directory_history() -> QVariantList.
func (o SettingsManager) GetDirectoryHistoryCall() (v []any, err error) {
	err = o.c.Call("settingsManager", "get_directory_history", &v)
	return
}

// GetHomeObdParameters calls get_home_obd_parameters() -> QVariantList.
func (o SettingsManager) GetHomeObdParameters() (v []any, err error) {
	err = o.c.Call("settingsManager", "get_home_obd_parameters", &v)
	return
}

// GetAutoPlayOnStartupCall calls get_auto_play_on_startup() -> bool.
func (o SettingsManager) GetAutoPlayOnStartupCall() (v bool, err error) {
	err = o.c.Call("settingsManager", "get_auto_play_on_startup", &v)
	return
}

// GetPersistShuffleStateCall calls get_persist_shuffle_state() -> bool.
func (o SettingsManager) GetPersistShuffleStateCall() (v bool, err error) {
	err = o.c.Call("settingsManager", "get_persist_shuffle_state", &v)
	return
}

// GetLastShuffleStateCall calls get_last_shuffle_state() -> bool.
func (o SettingsManager) GetLastShuffleStateCall() (v bool, err error) {
	err = o.c.Call("settingsManager", "get_last_shuffle_state", &v)
	return
}

// GetLastPlayedSongCall calls get_last_played_song() -> QString.
func (o SettingsManager) GetLastPlayedSongCall() (v string, err error) {
	err = o.c.Call("settingsManager", "get_last_played_song", &v)
	return
}

// GetLastPlayedPositionCall calls get_last_played_position() -> int.
func (o SettingsManager) GetLastPlayedPositionCall() (v int, err error) {
	err = o.c.Call("settingsManager", "get_last_played_position", &v)
	return
}

// GetLastPlayedPlaylistCall calls get_last_played_playlist() -> QString.
func (o SettingsManager) GetLastPlayedPlaylistCall() (v string, err error) {
	err = o.c.Call("settingsManager", "get_last_played_playlist", &v)
	return
}

// GetLastSettingsSectionCall calls get_last_settings_section() -> QString.
func (o SettingsManager) GetLastSettingsSectionCall() (v string, err error) {
	err = o.c.Call("settingsManager", "get_last_settings_section", &v)
	return
}

// GetWindowStateCall calls get_window_state() -> QString.
func (o SettingsManager) GetWindowStateCall() (v string, err error) {
	err = o.c.Call("settingsManager", "get_window_state", &v)
	return
}

// GetMusicButtonDefaultPageCall calls get_music_button_default_page() -> QString.
func (o SettingsManager) GetMusicButtonDefaultPageCall() (v string, err error) {
	err = o.c.Call("settingsManager", "get_music_button_default_page", &v)
	return
}

// GetReturnToLibraryAfterSelectionCall calls get_return_to_library_after_selection() -> bool.
func (o SettingsManager) GetReturnToLibraryAfterSelectionCall() (v bool, err error) {
	err = o.c.Call("settingsManager", "get_return_to_library_after_selection", &v)
	return
}

// FlushPendingSave calls flushPendingSave().
func (o SettingsManager) FlushPendingSave() error {
	return o.c.Call("settingsManager", "flushPendingSave", nil)
}

// GetAndroidAutoEnabledCall calls get_android_auto_enabled() -> bool.
func (o SettingsManager) GetAndroidAutoEnabledCall() (v bool, err error) {
	err = o.c.Call("settingsManager", "get_android_auto_enabled", &v)
	return
}

// GetPhoneMirrorEnabledCall calls get_phone_mirror_enabled() -> bool.
func (o SettingsManager) GetPhoneMirrorEnabledCall() (v bool, err error) {
	err = o.c.Call("settingsManager", "get_phone_mirror_enabled", &v)
	return
}

// GetScrcpyDisplaySizeCall calls get_scrcpy_display_size() -> QString.
func (o SettingsManager) GetScrcpyDisplaySizeCall() (v string, err error) {
	err = o.c.Call("settingsManager", "get_scrcpy_display_size", &v)
	return
}

// GetScrcpyAudioGainCall calls get_scrcpy_audio_gain() -> double.
func (o SettingsManager) GetScrcpyAudioGainCall() (v float64, err error) {
	err = o.c.Call("settingsManager", "get_scrcpy_audio_gain", &v)
	return
}

// GetScrcpyAudioDuckEnabledCall calls get_scrcpy_audio_duck_enabled() -> bool.
func (o SettingsManager) GetScrcpyAudioDuckEnabledCall() (v bool, err error) {
	err = o.c.Call("settingsManager", "get_scrcpy_audio_duck_enabled", &v)
	return
}

// GetScrcpyAudioDuckLevelCall calls get_scrcpy_audio_duck_level() -> double.
func (o SettingsManager) GetScrcpyAudioDuckLevelCall() (v float64, err error) {
	err = o.c.Call("settingsManager", "get_scrcpy_audio_duck_level", &v)
	return
}

// GetScrcpyPhoneScreenOffCall calls get_scrcpy_phone_screen_off() -> bool.
func (o SettingsManager) GetScrcpyPhoneScreenOffCall() (v bool, err error) {
	err = o.c.Call("settingsManager", "get_scrcpy_phone_screen_off", &v)
	return
}

// GetScrcpyDisplayDpiCall calls get_scrcpy_display_dpi() -> int.
func (o SettingsManager) GetScrcpyDisplayDpiCall() (v int, err error) {
	err = o.c.Call("settingsManager", "get_scrcpy_display_dpi", &v)
	return
}

// GetScrcpyBitRateUsbCall calls get_scrcpy_bit_rate_usb() -> int.
func (o SettingsManager) GetScrcpyBitRateUsbCall() (v int, err error) {
	err = o.c.Call("settingsManager", "get_scrcpy_bit_rate_usb", &v)
	return
}

// GetScrcpyBitRateWifiCall calls get_scrcpy_bit_rate_wifi() -> int.
func (o SettingsManager) GetScrcpyBitRateWifiCall() (v int, err error) {
	err = o.c.Call("settingsManager", "get_scrcpy_bit_rate_wifi", &v)
	return
}

// GetScrcpyAutoWifiCall calls get_scrcpy_auto_wifi() -> bool.
func (o SettingsManager) GetScrcpyAutoWifiCall() (v bool, err error) {
	err = o.c.Call("settingsManager", "get_scrcpy_auto_wifi", &v)
	return
}

// GetScrcpyBitRateAutoCall calls get_scrcpy_bit_rate_auto() -> bool.
func (o SettingsManager) GetScrcpyBitRateAutoCall() (v bool, err error) {
	err = o.c.Call("settingsManager", "get_scrcpy_bit_rate_auto", &v)
	return
}

// GetScrcpyWirelessAddressCall calls get_scrcpy_wireless_address() -> QString.
func (o SettingsManager) GetScrcpyWirelessAddressCall() (v string, err error) {
	err = o.c.Call("settingsManager", "get_scrcpy_wireless_address", &v)
	return
}

// GetEsp32VolumeEnabledCall calls get_esp32_volume_enabled() -> bool.
func (o SettingsManager) GetEsp32VolumeEnabledCall() (v bool, err error) {
	err = o.c.Call("settingsManager", "get_esp32_volume_enabled", &v)
	return
}

// GetEsp32VolumePortCall calls get_esp32_volume_port() -> QString.
func (o SettingsManager) GetEsp32VolumePortCall() (v string, err error) {
	err = o.c.Call("settingsManager", "get_esp32_volume_port", &v)
	return
}

// GetEsp32VolumeStepSizeCall calls get_esp32_volume_step_size() -> double.
func (o SettingsManager) GetEsp32VolumeStepSizeCall() (v float64, err error) {
	err = o.c.Call("settingsManager", "get_esp32_volume_step_size", &v)
	return
}

// GetEsp32AutoReconnectCall calls get_esp32_auto_reconnect() -> bool.
func (o SettingsManager) GetEsp32AutoReconnectCall() (v bool, err error) {
	err = o.c.Call("settingsManager", "get_esp32_auto_reconnect", &v)
	return
}

// GetEsp32LedSleepEnabledCall calls get_esp32_led_sleep_enabled() -> bool.
func (o SettingsManager) GetEsp32LedSleepEnabledCall() (v bool, err error) {
	err = o.c.Call("settingsManager", "get_esp32_led_sleep_enabled", &v)
	return
}

// GetEsp32LedColorModeCall calls get_esp32_led_color_mode() -> QString.
func (o SettingsManager) GetEsp32LedColorModeCall() (v string, err error) {
	err = o.c.Call("settingsManager", "get_esp32_led_color_mode", &v)
	return
}

// GetEsp32LedStaticColorCall calls get_esp32_led_static_color() -> QString.
func (o SettingsManager) GetEsp32LedStaticColorCall() (v string, err error) {
	err = o.c.Call("settingsManager", "get_esp32_led_static_color", &v)
	return
}

// GetShowWaveformVisualizerCall calls get_show_waveform_visualizer() -> bool.
func (o SettingsManager) GetShowWaveformVisualizerCall() (v bool, err error) {
	err = o.c.Call("settingsManager", "get_show_waveform_visualizer", &v)
	return
}

// GetImuEnabledCall calls get_imu_enabled() -> bool.
func (o SettingsManager) GetImuEnabledCall() (v bool, err error) {
	err = o.c.Call("settingsManager", "get_imu_enabled", &v)
	return
}

// GetGestureSensorEnabledCall calls get_gesture_sensor_enabled() -> bool.
func (o SettingsManager) GetGestureSensorEnabledCall() (v bool, err error) {
	err = o.c.Call("settingsManager", "get_gesture_sensor_enabled", &v)
	return
}

// GetGestureMapping calls get_gesture_mapping() -> QString.
func (o SettingsManager) GetGestureMapping() (v string, err error) {
	err = o.c.Call("settingsManager", "get_gesture_mapping", &v)
	return
}

// GetGestureMappingDict calls get_gesture_mapping_dict() -> QVariantMap.
func (o SettingsManager) GetGestureMappingDict() (v map[string]any, err error) {
	err = o.c.Call("settingsManager", "get_gesture_mapping_dict", &v)
	return
}

// IsSettingPinned calls is_setting_pinned(QString) -> bool.
func (o SettingsManager) IsSettingPinned(settingId string) (v bool, err error) {
	err = o.c.Call("settingsManager", "is_setting_pinned", &v, settingId)
	return
}

// GetSettingsRegistry calls get_settings_registry() -> QString.
func (o SettingsManager) GetSettingsRegistry() (v string, err error) {
	err = o.c.Call("settingsManager", "get_settings_registry", &v)
	return
}

// GetPinnedSettingsMetadata calls get_pinned_settings_metadata() -> QString.
func (o SettingsManager) GetPinnedSettingsMetadata() (v string, err error) {
	err = o.c.Call("settingsManager", "get_pinned_settings_metadata", &v)
	return
}

// GetSettingsMenuVisibilityCall calls get_settings_menu_visibility() -> QString.
func (o SettingsManager) GetSettingsMenuVisibilityCall() (v string, err error) {
	err = o.c.Call("settingsManager", "get_settings_menu_visibility", &v)
	return
}

// IsSettingsSectionVisible calls is_settings_section_visible(QString) -> bool.
func (o SettingsManager) IsSettingsSectionVisible(section string) (v bool, err error) {
	err = o.c.Call("settingsManager", "is_settings_section_visible", &v, section)
	return
}

// GetGitCommitHash calls get_git_commit_hash() -> QString.
func (o SettingsManager) GetGitCommitHash() (v string, err error) {
	err = o.c.Call("settingsManager", "get_git_commit_hash", &v)
	return
}

// GetGitCommitDate calls get_git_commit_date() -> QString.
func (o SettingsManager) GetGitCommitDate() (v string, err error) {
	err = o.c.Call("settingsManager", "get_git_commit_date", &v)
	return
}

// GetGitCommitMessage calls get_git_commit_message() -> QString.
func (o SettingsManager) GetGitCommitMessage() (v string, err error) {
	err = o.c.Call("settingsManager", "get_git_commit_message", &v)
	return
}

// ShiftLightManager is the service object "shiftLightManager" (C++ ShiftLightManager); NewShiftLightManager(c) binds it to a client.
type ShiftLightManager struct{ c *Client }

// NewShiftLightManager binds "shiftLightManager" to c.
func NewShiftLightManager(c *Client) ShiftLightManager { return ShiftLightManager{c} }

// Name is the object's name on the wire.
func (ShiftLightManager) Name() string { return "shiftLightManager" }

// GetEnabled reads the property "enabled" (bool).
func (o ShiftLightManager) GetEnabled() (v bool, err error) {
	err = o.c.Get("shiftLightManager", "enabled", &v)
	return
}

// GetShowOnHomeCard reads the property "showOnHomeCard" (bool).
func (o ShiftLightManager) GetShowOnHomeCard() (v bool, err error) {
	err = o.c.Get("shiftLightManager", "showOnHomeCard", &v)
	return
}

// GetMaxRpm reads the property "maxRpm" (int).
func (o ShiftLightManager) GetMaxRpm() (v int, err error) {
	err = o.c.Get("shiftLightManager", "maxRpm", &v)
	return
}

// GetFullScreenFlashOpacity reads the property "fullScreenFlashOpacity" (double).
func (o ShiftLightManager) GetFullScreenFlashOpacity() (v float64, err error) {
	err = o.c.Get("shiftLightManager", "fullScreenFlashOpacity", &v)
	return
}

// GetLampSize reads the property "lampSize" (double).
func (o ShiftLightManager) GetLampSize() (v float64, err error) {
	err = o.c.Get("shiftLightManager", "lampSize", &v)
	return
}

// GetGlowSize reads the property "glowSize" (double).
func (o ShiftLightManager) GetGlowSize() (v float64, err error) {
	err = o.c.Get("shiftLightManager", "glowSize", &v)
	return
}

// GetGlowIntensity reads the property "glowIntensity" (double).
func (o ShiftLightManager) GetGlowIntensity() (v float64, err error) {
	err = o.c.Get("shiftLightManager", "glowIntensity", &v)
	return
}

// GetColorTransitionSpeed reads the property "colorTransitionSpeed" (int).
func (o ShiftLightManager) GetColorTransitionSpeed() (v int, err error) {
	err = o.c.Get("shiftLightManager", "colorTransitionSpeed", &v)
	return
}

// GetPulseEnabled reads the property "pulseEnabled" (bool).
func (o ShiftLightManager) GetPulseEnabled() (v bool, err error) {
	err = o.c.Get("shiftLightManager", "pulseEnabled", &v)
	return
}

// GetFlags reads the property "flags" (QVariantList).
func (o ShiftLightManager) GetFlags() (v []any, err error) {
	err = o.c.Get("shiftLightManager", "flags", &v)
	return
}

// GetRpm reads the property "rpm" (double).
func (o ShiftLightManager) GetRpm() (v float64, err error) {
	err = o.c.Get("shiftLightManager", "rpm", &v)
	return
}

// GetActive reads the property "active" (bool).
func (o ShiftLightManager) GetActive() (v bool, err error) {
	err = o.c.Get("shiftLightManager", "active", &v)
	return
}

// GetActiveIndex reads the property "activeIndex" (int).
func (o ShiftLightManager) GetActiveIndex() (v int, err error) {
	err = o.c.Get("shiftLightManager", "activeIndex", &v)
	return
}

// GetActiveFlag reads the property "activeFlag" (QVariantMap).
func (o ShiftLightManager) GetActiveFlag() (v map[string]any, err error) {
	err = o.c.Get("shiftLightManager", "activeFlag", &v)
	return
}

// GetLit reads the property "lit" (bool).
func (o ShiftLightManager) GetLit() (v bool, err error) {
	err = o.c.Get("shiftLightManager", "lit", &v)
	return
}

// GetSuppressed reads the property "suppressed" (bool, writable).
func (o ShiftLightManager) GetSuppressed() (v bool, err error) {
	err = o.c.Get("shiftLightManager", "suppressed", &v)
	return
}

// SetSuppressed writes the property "suppressed" and returns the value read back.
func (o ShiftLightManager) SetSuppressed(value bool) (v bool, err error) {
	err = o.c.Set("shiftLightManager", "suppressed", value, &v)
	return
}

// SetRpm calls setRpm(double).
func (o ShiftLightManager) SetRpm(rpm float64) error {
	return o.c.Call("shiftLightManager", "setRpm", nil, rpm)
}

// SetFlags calls setFlags(QVariantList).
func (o ShiftLightManager) SetFlags(flags []any) error {
	return o.c.Call("shiftLightManager", "setFlags", nil, flags)
}

// Sim is the service object "sim" (C++ Simulator); NewSim(c) binds it to a client.
//
// Experimental: not covered by the protocol version; may change or go in any release.
type Sim struct{ c *Client }

// NewSim binds "sim" to c.
func NewSim(c *Client) Sim { return Sim{c} }

// Name is the object's name on the wire.
func (Sim) Name() string { return "sim" }

// GetActive reads the property "active" (bool).
func (o Sim) GetActive() (v bool, err error) {
	err = o.c.Get("sim", "active", &v)
	return
}

// SetThrottle calls setThrottle(double) -> bool.
func (o Sim) SetThrottle(percent float64) (v bool, err error) {
	err = o.c.Call("sim", "setThrottle", &v, percent)
	return
}

// SetBrake calls setBrake(double) -> bool.
func (o Sim) SetBrake(percent float64) (v bool, err error) {
	err = o.c.Call("sim", "setBrake", &v, percent)
	return
}

// Rev calls rev(double) -> bool.
func (o Sim) Rev(intensity float64) (v bool, err error) {
	err = o.c.Call("sim", "rev", &v, intensity)
	return
}

// SetEngineOn calls setEngineOn(bool) -> bool.
func (o Sim) SetEngineOn(on bool) (v bool, err error) {
	err = o.c.Call("sim", "setEngineOn", &v, on)
	return
}

// ToggleEngine calls toggleEngine() -> bool.
func (o Sim) ToggleEngine() (v bool, err error) {
	err = o.c.Call("sim", "toggleEngine", &v)
	return
}

// SetGear calls setGear(int) -> bool.
func (o Sim) SetGear(gear int) (v bool, err error) {
	err = o.c.Call("sim", "setGear", &v, gear)
	return
}

// Upshift calls upshift() -> bool.
func (o Sim) Upshift() (v bool, err error) {
	err = o.c.Call("sim", "upshift", &v)
	return
}

// Downshift calls downshift() -> bool.
func (o Sim) Downshift() (v bool, err error) {
	err = o.c.Call("sim", "downshift", &v)
	return
}

// SetSpeed calls setSpeed(double) -> bool.
func (o Sim) SetSpeed(mph float64) (v bool, err error) {
	err = o.c.Call("sim", "setSpeed", &v, mph)
	return
}

// SetRpm calls setRpm(double) -> bool.
func (o Sim) SetRpm(rpm float64) (v bool, err error) {
	err = o.c.Call("sim", "setRpm", &v, rpm)
	return
}

// SetRealistic calls setRealistic(bool) -> bool.
func (o Sim) SetRealistic(on bool) (v bool, err error) {
	err = o.c.Call("sim", "setRealistic", &v, on)
	return
}

// Scenario calls scenario(QString) -> bool.
func (o Sim) Scenario(name string) (v bool, err error) {
	err = o.c.Call("sim", "scenario", &v, name)
	return
}

// SetDropouts calls setDropouts(bool) -> bool.
func (o Sim) SetDropouts(on bool) (v bool, err error) {
	err = o.c.Call("sim", "setDropouts", &v, on)
	return
}

// DropoutNow calls dropoutNow() -> bool.
func (o Sim) DropoutNow() (v bool, err error) {
	err = o.c.Call("sim", "dropoutNow", &v)
	return
}

// SetFailureRate calls setFailureRate(double) -> bool.
func (o Sim) SetFailureRate(rate float64) (v bool, err error) {
	err = o.c.Call("sim", "setFailureRate", &v, rate)
	return
}

// SetLinkUp calls setLinkUp(bool) -> bool.
func (o Sim) SetLinkUp(up bool) (v bool, err error) {
	err = o.c.Call("sim", "setLinkUp", &v, up)
	return
}

// SetImu calls setImu(bool) -> bool.
func (o Sim) SetImu(on bool) (v bool, err error) {
	err = o.c.Call("sim", "setImu", &v, on)
	return
}

// SetVideo calls setVideo(bool) -> bool.
func (o Sim) SetVideo(on bool) (v bool, err error) {
	err = o.c.Call("sim", "setVideo", &v, on)
	return
}

// PairingCode calls pairingCode() -> QVariantMap.
func (o Sim) PairingCode() (v map[string]any, err error) {
	err = o.c.Call("sim", "pairingCode", &v)
	return
}

// Status calls status() -> QVariantMap.
func (o Sim) Status() (v map[string]any, err error) {
	err = o.c.Call("sim", "status", &v)
	return
}

// ThemeCatalog is the service object "themeCatalog" (C++ ThemeCatalog); NewThemeCatalog(c) binds it to a client.
type ThemeCatalog struct{ c *Client }

// NewThemeCatalog binds "themeCatalog" to c.
func NewThemeCatalog(c *Client) ThemeCatalog { return ThemeCatalog{c} }

// Name is the object's name on the wire.
func (ThemeCatalog) Name() string { return "themeCatalog" }

// GetThemes reads the property "themes" (QVariantMap).
func (o ThemeCatalog) GetThemes() (v map[string]any, err error) {
	err = o.c.Get("themeCatalog", "themes", &v)
	return
}

// GetThemeNames reads the property "themeNames" (QStringList).
func (o ThemeCatalog) GetThemeNames() (v []string, err error) {
	err = o.c.Get("themeCatalog", "themeNames", &v)
	return
}

// GetLightThemeNames reads the property "lightThemeNames" (QStringList).
func (o ThemeCatalog) GetLightThemeNames() (v []string, err error) {
	err = o.c.Get("themeCatalog", "lightThemeNames", &v)
	return
}

// GetDarkThemeNames reads the property "darkThemeNames" (QStringList).
func (o ThemeCatalog) GetDarkThemeNames() (v []string, err error) {
	err = o.c.Get("themeCatalog", "darkThemeNames", &v)
	return
}

// GetActiveThemeName reads the property "activeThemeName" (QString).
func (o ThemeCatalog) GetActiveThemeName() (v string, err error) {
	err = o.c.Get("themeCatalog", "activeThemeName", &v)
	return
}

// GetActiveTheme reads the property "activeTheme" (QVariantMap).
func (o ThemeCatalog) GetActiveTheme() (v map[string]any, err error) {
	err = o.c.Get("themeCatalog", "activeTheme", &v)
	return
}

// TripRecorder is the service object "tripRecorder" (C++ TripRecorder); NewTripRecorder(c) binds it to a client.
type TripRecorder struct{ c *Client }

// NewTripRecorder binds "tripRecorder" to c.
func NewTripRecorder(c *Client) TripRecorder { return TripRecorder{c} }

// Name is the object's name on the wire.
func (TripRecorder) Name() string { return "tripRecorder" }

// GetEnabled reads the property "enabled" (bool).
func (o TripRecorder) GetEnabled() (v bool, err error) {
	err = o.c.Get("tripRecorder", "enabled", &v)
	return
}

// GetStoreReady reads the property "storeReady" (bool).
func (o TripRecorder) GetStoreReady() (v bool, err error) {
	err = o.c.Get("tripRecorder", "storeReady", &v)
	return
}

// GetRecording reads the property "recording" (bool).
func (o TripRecorder) GetRecording() (v bool, err error) {
	err = o.c.Get("tripRecorder", "recording", &v)
	return
}

// GetCurrentTripId reads the property "currentTripId" (qlonglong).
func (o TripRecorder) GetCurrentTripId() (v int64, err error) {
	err = o.c.Get("tripRecorder", "currentTripId", &v)
	return
}

// GetCurrentTripStartMs reads the property "currentTripStartMs" (qlonglong).
func (o TripRecorder) GetCurrentTripStartMs() (v int64, err error) {
	err = o.c.Get("tripRecorder", "currentTripStartMs", &v)
	return
}

// GetCurrentSampleCount reads the property "currentSampleCount" (int).
func (o TripRecorder) GetCurrentSampleCount() (v int, err error) {
	err = o.c.Get("tripRecorder", "currentSampleCount", &v)
	return
}

// GetCurrentTripElapsedS reads the property "currentTripElapsedS" (int).
func (o TripRecorder) GetCurrentTripElapsedS() (v int, err error) {
	err = o.c.Get("tripRecorder", "currentTripElapsedS", &v)
	return
}

// GetTrips reads the property "trips" (QVariantList).
func (o TripRecorder) GetTrips() (v []any, err error) {
	err = o.c.Get("tripRecorder", "trips", &v)
	return
}

// GetHistoryDir reads the property "historyDir" (QString).
func (o TripRecorder) GetHistoryDir() (v string, err error) {
	err = o.c.Get("tripRecorder", "historyDir", &v)
	return
}

// GetTripsDir reads the property "tripsDir" (QString).
func (o TripRecorder) GetTripsDir() (v string, err error) {
	err = o.c.Get("tripRecorder", "tripsDir", &v)
	return
}

// GetStorageBytes reads the property "storageBytes" (qlonglong).
func (o TripRecorder) GetStorageBytes() (v int64, err error) {
	err = o.c.Get("tripRecorder", "storageBytes", &v)
	return
}

// GetCanOpenFolder reads the property "canOpenFolder" (bool).
func (o TripRecorder) GetCanOpenFolder() (v bool, err error) {
	err = o.c.Get("tripRecorder", "canOpenFolder", &v)
	return
}

// GetLastExportPath reads the property "lastExportPath" (QString).
func (o TripRecorder) GetLastExportPath() (v string, err error) {
	err = o.c.Get("tripRecorder", "lastExportPath", &v)
	return
}

// GetLastError reads the property "lastError" (QString).
func (o TripRecorder) GetLastError() (v string, err error) {
	err = o.c.Get("tripRecorder", "lastError", &v)
	return
}

// RecordSample calls recordSample(QString, float).
func (o TripRecorder) RecordSample(paramId string, value float64) error {
	return o.c.Call("tripRecorder", "recordSample", nil, paramId, value)
}

// SetConnected calls setConnected(bool).
func (o TripRecorder) SetConnected(connected bool) error {
	return o.c.Call("tripRecorder", "setConnected", nil, connected)
}

// SetVehicleParameters calls setVehicleParameters(QStringList).
func (o TripRecorder) SetVehicleParameters(ids []string) error {
	return o.c.Call("tripRecorder", "setVehicleParameters", nil, ids)
}

// SetVehicleIdentity calls setVehicleIdentity(QString, QString, int).
func (o TripRecorder) SetVehicleIdentity(vin string, make string, modelYear int) error {
	return o.c.Call("tripRecorder", "setVehicleIdentity", nil, vin, make, modelYear)
}

// NoteEvent calls noteEvent(QString, QString).
func (o TripRecorder) NoteEvent(kind string, detail string) error {
	return o.c.Call("tripRecorder", "noteEvent", nil, kind, detail)
}

// SetEnabled calls setEnabled(bool).
func (o TripRecorder) SetEnabled(enabled bool) error {
	return o.c.Call("tripRecorder", "setEnabled", nil, enabled)
}

// EndTripNow calls endTripNow().
func (o TripRecorder) EndTripNow() error {
	return o.c.Call("tripRecorder", "endTripNow", nil)
}

// Refresh calls refresh().
func (o TripRecorder) Refresh() error {
	return o.c.Call("tripRecorder", "refresh", nil)
}

// RequestDetail calls requestDetail(qlonglong).
func (o TripRecorder) RequestDetail(id int64) error {
	return o.c.Call("tripRecorder", "requestDetail", nil, id)
}

// RequestSeries5 calls requestSeries(qlonglong, QString, int, double, double).
func (o TripRecorder) RequestSeries5(id int64, paramId string, maxPoints int, fromS float64, toS float64) error {
	return o.c.Call("tripRecorder", "requestSeries", nil, id, paramId, maxPoints, fromS, toS)
}

// RequestSeries4 calls requestSeries(qlonglong, QString, int, double).
func (o TripRecorder) RequestSeries4(id int64, paramId string, maxPoints int, fromS float64) error {
	return o.c.Call("tripRecorder", "requestSeries", nil, id, paramId, maxPoints, fromS)
}

// RequestSeries3 calls requestSeries(qlonglong, QString, int).
func (o TripRecorder) RequestSeries3(id int64, paramId string, maxPoints int) error {
	return o.c.Call("tripRecorder", "requestSeries", nil, id, paramId, maxPoints)
}

// RequestSeries2 calls requestSeries(qlonglong, QString).
func (o TripRecorder) RequestSeries2(id int64, paramId string) error {
	return o.c.Call("tripRecorder", "requestSeries", nil, id, paramId)
}

// DeleteTrip calls deleteTrip(qlonglong).
func (o TripRecorder) DeleteTrip(id int64) error {
	return o.c.Call("tripRecorder", "deleteTrip", nil, id)
}

// ClearHistory calls clearHistory().
func (o TripRecorder) ClearHistory() error {
	return o.c.Call("tripRecorder", "clearHistory", nil)
}

// ExportTrip calls exportTrip(qlonglong).
func (o TripRecorder) ExportTrip(id int64) error {
	return o.c.Call("tripRecorder", "exportTrip", nil, id)
}

// OpenHistoryFolder calls openHistoryFolder() -> bool.
func (o TripRecorder) OpenHistoryFolder() (v bool, err error) {
	err = o.c.Call("tripRecorder", "openHistoryFolder", &v)
	return
}

// Units is the service object "units" (C++ Units); NewUnits(c) binds it to a client.
type Units struct{ c *Client }

// NewUnits binds "units" to c.
func NewUnits(c *Client) Units { return Units{c} }

// Name is the object's name on the wire.
func (Units) Name() string { return "units" }

// FormatDuration calls formatDuration(double) -> QString.
func (o Units) FormatDuration(ms float64) (v string, err error) {
	err = o.c.Call("units", "formatDuration", &v, ms)
	return
}

// FormatSeconds calls formatSeconds(double) -> QString.
func (o Units) FormatSeconds(s float64) (v string, err error) {
	err = o.c.Call("units", "formatSeconds", &v, s)
	return
}

// Cardinal calls cardinal(double) -> QString.
func (o Units) Cardinal(headingDeg float64) (v string, err error) {
	err = o.c.Call("units", "cardinal", &v, headingDeg)
	return
}

// CToF calls cToF(double) -> double.
func (o Units) CToF(c float64) (v float64, err error) {
	err = o.c.Call("units", "cToF", &v, c)
	return
}

// MToFt calls mToFt(double) -> double.
func (o Units) MToFt(m float64) (v float64, err error) {
	err = o.c.Call("units", "mToFt", &v, m)
	return
}

// MphToKph calls mphToKph(double) -> double.
func (o Units) MphToKph(mph float64) (v float64, err error) {
	err = o.c.Call("units", "mphToKph", &v, mph)
	return
}

// LinearToDb calls linearToDb(double) -> double.
func (o Units) LinearToDb(x float64) (v float64, err error) {
	err = o.c.Call("units", "linearToDb", &v, x)
	return
}

// DbToLinear calls dbToLinear(double) -> double.
func (o Units) DbToLinear(db float64) (v float64, err error) {
	err = o.c.Call("units", "dbToLinear", &v, db)
	return
}

// VolumeController is the service object "volumeController" (C++ VolumeController); NewVolumeController(c) binds it to a client.
type VolumeController struct{ c *Client }

// NewVolumeController binds "volumeController" to c.
func NewVolumeController(c *Client) VolumeController { return VolumeController{c} }

// Name is the object's name on the wire.
func (VolumeController) Name() string { return "volumeController" }

// GetPercent reads the property "percent" (int).
func (o VolumeController) GetPercent() (v int, err error) {
	err = o.c.Get("volumeController", "percent", &v)
	return
}

// GetMuted reads the property "muted" (bool, writable).
func (o VolumeController) GetMuted() (v bool, err error) {
	err = o.c.Get("volumeController", "muted", &v)
	return
}

// SetMuted writes the property "muted" and returns the value read back.
func (o VolumeController) SetMuted(value bool) (v bool, err error) {
	err = o.c.Set("volumeController", "muted", value, &v)
	return
}

// GetEffectivelyMuted reads the property "effectivelyMuted" (bool).
func (o VolumeController) GetEffectivelyMuted() (v bool, err error) {
	err = o.c.Get("volumeController", "effectivelyMuted", &v)
	return
}

// ApplyVolume calls applyVolume(int).
func (o VolumeController) ApplyVolume(percent int) error {
	return o.c.Call("volumeController", "applyVolume", nil, percent)
}

// SetMutedCall calls setMuted(bool).
func (o VolumeController) SetMutedCall(muted bool) error {
	return o.c.Call("volumeController", "setMuted", nil, muted)
}

// ToggleMute calls toggleMute().
func (o VolumeController) ToggleMute() error {
	return o.c.Call("volumeController", "toggleMute", nil)
}

// ToLinear calls toLinear(float) -> float.
func (o VolumeController) ToLinear(percent float64) (v float64, err error) {
	err = o.c.Call("volumeController", "toLinear", &v, percent)
	return
}

// ToPercent calls toPercent(float) -> int.
func (o VolumeController) ToPercent(linear float64) (v int, err error) {
	err = o.c.Call("volumeController", "toPercent", &v, linear)
	return
}
