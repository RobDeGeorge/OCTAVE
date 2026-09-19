import QtQuick
import Qt5Compat.GraphicalEffects
import "." as App

// Album art display with selectable transition animations.
//
// One unified transition list (transitionStyle picks any of the 10 names).
// Each animation reads `previewEnabled` to know whether 3D side cards are
// visible and adapts: 3D-flavored animations animate the side cards when
// previewEnabled is true, and gracefully no-op the side-card portion when
// it's false (cards are hidden anyway). 2D-flavored animations leave side
// cards untouched in either mode.
Item {
    // Local dp/dpMin wrappers — work around Qt Android singleton-function bug.
    function dp(size) { return Math.round(size * (App.Spacing.effectiveScale || 1.0)) }
    function dpMin(size, floor) { return Math.max(floor, Math.round(size * (App.Spacing.effectiveScale || 1.0))) }

    id: carousel
    clip: false

    // ═══════════════════════════════════════════════════════════════════
    //  Constants — single-card animations
    // ═══════════════════════════════════════════════════════════════════

    readonly property real kCrossfadeScale: 0.97
    readonly property int  kCrossfadeDuration: 250

    readonly property real kSlideDistance: 80
    readonly property int  kSlideDuration: 280

    readonly property real kLiftScale: 0.85
    readonly property int  kLiftDuration: 180
    readonly property int  kLiftPause: 60

    readonly property int  kDissolveDuration: 900
    readonly property int  kDissolveGridSize: 20
    readonly property real kDissolveSmoothness: 0.1

    readonly property int  kFlipDuration: 350

    // ═══════════════════════════════════════════════════════════════════
    //  Constants — 3D preview animations
    // ═══════════════════════════════════════════════════════════════════

    readonly property real k3dSideOffsetFactor: 0.42
    readonly property real k3dSideScale: 0.72
    readonly property real k3dFarScale: 0.5
    readonly property int  k3dDuration: 350

    // ═══════════════════════════════════════════════════════════════════
    //  Public API
    // ═══════════════════════════════════════════════════════════════════

    property int direction: 1
    property url artSource: ""
    property url prevArtSource: ""
    property url nextArtSource: ""
    property bool animBusy: false

    property string transitionStyle: "Crossfade"

    // Settings
    property bool previewEnabled: false
    property bool roundedArt: false
    property real artRadius: 0
    property bool vinylMode: false
    property real sideCardAngle: 30
    property real sideCardOpacity: 0.4
    property bool showShadow: false

    property real playbackPosition: 0
    property real playbackDuration: 0

    // Dissolve state exposed for background sync
    readonly property bool dissolveActive: _dissolveActive
    readonly property real dissolveProgress: _dissolveProgress
    readonly property url dissolveNewArt: _dissolveNewArt

    signal animationFinished()
    signal settled()
    signal artClicked()
    signal artLoadFailed()

    // Restore side-card opacity after parent refreshes sources
    function showSideCards() {
        if (!previewEnabled) return
        prevCard.cfOpacity = Qt.binding(function() { return carousel.sideCardOpacity })
        nextCard.cfOpacity = Qt.binding(function() { return carousel.sideCardOpacity })
    }

    // ═══════════════════════════════════════════════════════════════════
    //  Derived
    // ═══════════════════════════════════════════════════════════════════

    property real artSize: {
        var factor = previewEnabled ? 0.75 : 0.85
        return Math.min(width * factor, height * factor)
    }

    property real _sideOffset: artSize * k3dSideOffsetFactor

    // ═══════════════════════════════════════════════════════════════════
    //  Shared masks
    // ═══════════════════════════════════════════════════════════════════

    Rectangle {
        id: artRoundedMask
        width: carousel.artSize; height: width
        radius: carousel.roundedArt ? carousel.artRadius : 0
        visible: false; layer.enabled: true
    }

    Rectangle {
        id: vinylMaskRect
        width: carousel.artSize; height: width
        radius: width / 2
        visible: false; color: "white"; layer.enabled: true
        Rectangle {
            anchors.centerIn: parent
            width: parent.width * 0.12; height: width
            radius: width / 2; color: "black"
        }
    }

    // ═══════════════════════════════════════════════════════════════════
    //  triggerSlide — dispatch to single-card or 3D animation
    // ═══════════════════════════════════════════════════════════════════

    function triggerSlide() {
        _stopAll()

        switch (transitionStyle) {
        // 2D-flavored — center-card only; side cards (if visible) stay put.
        case "Slide":       _startSlide(); break
        case "Vinyl Lift":  _startVinylLift(); break
        case "Dissolve":    _startDissolve(); break
        case "Flip":        _startFlip(); break
        case "Crossfade":   _startCrossfade(); break

        // 3D-flavored — animates side cards when previewEnabled, otherwise
        // the side-card portion fades against hidden cards (cfOpacity=0) and
        // is invisible. The center/exit-snapshot motion still plays.
        case "Coverflow":   _start3DCommon(); _startCoverflow(direction); break
        case "Conveyor":    _start3DCommon(); _startConveyor(direction); break
        case "Stack":       _start3DCommon(); _startStack(direction); break
        case "Depth":       _start3DCommon(); _startDepth(direction); break
        case "Swing":       _start3DCommon(); _startSwing(direction); break

        default:            _startCrossfade(); break
        }
    }

    function _start3DCommon() {
        // Common setup for 3D-flavored animations: freeze exit snapshot,
        // hide center, set z-order so the entering card is on top.
        _showExit()
        centerCard.opacity = 0

        var dir = direction
        var enterCard = (dir === 1) ? nextCard : prevCard
        var farCard   = (dir === 1) ? prevCard : nextCard
        enterCard.z = 2
        farCard.z   = 0
        exitSnapshot.z = 1
    }

    function _stopAll() {
        // Stop all single-card animations
        if (crossfadeAnim.running)  crossfadeAnim.stop()
        if (slideAnim.running)      slideAnim.stop()
        if (liftAnim.running)       liftAnim.stop()
        if (dissolveAnim.running)   dissolveAnim.stop()
        if (flipAnim.running)       flipAnim.stop()
        // Stop all 3D animations
        if (coverflowAnim.running)  coverflowAnim.stop()
        if (conveyorAnim.running)   conveyorAnim.stop()
        if (stackAnim.running)      stackAnim.stop()
        if (depthAnim.running)      depthAnim.stop()
        if (swingAnim.running)      swingAnim.stop()
        // Reset visual state
        _resetAll()
    }

    // Dissolve state
    property real _dissolveProgress: 0.0   // 0 = old art fully visible, 1 = fully dissolved
    property bool _dissolveActive: false

    function _resetAll() {
        exitSnapshot.opacity = 0
        exitSnapshot.visible = false
        exitSnapshot.scale = 1.0
        exitSnapshot._slideX = 0
        exitSnapshot._flipAngle = 0
        exitSnapshot.z = 0
        centerCard.opacity = 1.0
        centerCard.scale = 1.0
        centerCard.cfOffset = 0
        centerCard.cfAngle = 0
        _dissolveProgress = 0.0
        _dissolveActive = false
        if (previewEnabled) _resetSideCards()
    }

    function _resetSideCards() {
        prevCard.cfOffset = Qt.binding(function() { return -carousel._sideOffset })
        prevCard.cfAngle  = Qt.binding(function() { return carousel.sideCardAngle })
        prevCard.cfScale  = k3dSideScale
        prevCard.cfOpacity = 0  // Hidden until showSideCards()
        prevCard.z = 0

        nextCard.cfOffset = Qt.binding(function() { return carousel._sideOffset })
        nextCard.cfAngle  = Qt.binding(function() { return -carousel.sideCardAngle })
        nextCard.cfScale  = k3dSideScale
        nextCard.cfOpacity = 0
        nextCard.z = 0
    }

    property bool _anyAnimRunning: crossfadeAnim.running || slideAnim.running
                                   || liftAnim.running || dissolveAnim.running
                                   || flipAnim.running || coverflowAnim.running
                                   || conveyorAnim.running || stackAnim.running
                                   || depthAnim.running || swingAnim.running

    // ═══════════════════════════════════════════════════════════════════
    //  Shared helpers
    // ═══════════════════════════════════════════════════════════════════

    function _showExit() {
        exitSnapshot.visible = true
        exitSnapshot.opacity = 1.0
        exitSnapshot.scale = 1.0
        exitSnapshot._slideX = 0
        exitSnapshot._flipAngle = 0
    }

    function _onAnimDone() {
        _resetAll()
        carousel.animationFinished()
    }

    // Common 3D done handler — resets side cards then fires finished
    function _on3DDone() {
        exitSnapshot.opacity = 0
        exitSnapshot.visible = false
        exitSnapshot.scale = 1.0
        exitSnapshot._slideX = 0
        exitSnapshot._flipAngle = 0
        centerCard.opacity = 1.0
        centerCard.scale = 1.0
        centerCard.cfOffset = 0
        centerCard.cfAngle = 0
        _resetSideCards()
        carousel.animationFinished()
    }

    // ═══════════════════════════════════════════════════════════════════
    //  SINGLE-CARD ANIMATIONS (1–5)
    // ═══════════════════════════════════════════════════════════════════

    // ── 1. Crossfade ────────────────────────────────────────────────

    function _startCrossfade() {
        _showExit()
        centerCard.opacity = 0
        centerCard.scale = kCrossfadeScale
        crossfadeAnim.start()
    }

    // All Animator types → entire group runs on the render thread
    ParallelAnimation {
        id: crossfadeAnim
        onStarted: carousel.animBusy = true
        onFinished: carousel._onAnimDone()

        OpacityAnimator { target: exitSnapshot
            from: 1.0; to: 0; duration: carousel.kCrossfadeDuration; easing.type: Easing.InQuad }
        ScaleAnimator { target: exitSnapshot
            from: 1.0; to: carousel.kCrossfadeScale; duration: carousel.kCrossfadeDuration; easing.type: Easing.InQuad }
        OpacityAnimator { target: centerCard
            from: 0; to: 1.0; duration: carousel.kCrossfadeDuration; easing.type: Easing.OutQuad }
        ScaleAnimator { target: centerCard
            from: carousel.kCrossfadeScale; to: 1.0; duration: carousel.kCrossfadeDuration; easing.type: Easing.OutQuad }
    }

    // ── 2. Slide ────────────────────────────────────────────────────

    function _startSlide() {
        _showExit()
        var travel = dp(kSlideDistance)
        exitSnapshot._slideX = 0
        centerCard.opacity = 0
        centerCard.cfOffset = direction === 1 ? travel : -travel
        slideAnim._exitTo = direction === 1 ? -travel : travel
        slideAnim._enterFrom = centerCard.cfOffset
        slideAnim.start()
    }

    ParallelAnimation {
        id: slideAnim
        property real _exitTo: 0
        property real _enterFrom: 0
        onStarted: carousel.animBusy = true
        onFinished: { centerCard.cfOffset = 0; carousel._onAnimDone() }

        NumberAnimation { target: exitSnapshot; property: "_slideX"
            from: 0; to: slideAnim._exitTo; duration: carousel.kSlideDuration; easing.type: Easing.OutCubic }
        NumberAnimation { target: exitSnapshot; property: "opacity"
            from: 1.0; to: 0; duration: carousel.kSlideDuration; easing.type: Easing.InQuad }
        NumberAnimation { target: centerCard; property: "cfOffset"
            from: slideAnim._enterFrom; to: 0; duration: carousel.kSlideDuration; easing.type: Easing.OutCubic }
        NumberAnimation { target: centerCard; property: "opacity"
            from: 0; to: 1.0; duration: carousel.kSlideDuration; easing.type: Easing.OutQuad }
    }

    // ── 3. Vinyl Lift ───────────────────────────────────────────────

    function _startVinylLift() {
        _showExit()
        centerCard.opacity = 0
        centerCard.scale = kLiftScale
        liftAnim.start()
    }

    SequentialAnimation {
        id: liftAnim
        onStarted: carousel.animBusy = true
        onFinished: carousel._onAnimDone()

        // Each inner ParallelAnimation is all-Animators → render thread
        ParallelAnimation {
            ScaleAnimator { target: exitSnapshot
                from: 1.0; to: carousel.kLiftScale; duration: carousel.kLiftDuration; easing.type: Easing.InQuad }
            OpacityAnimator { target: exitSnapshot
                from: 1.0; to: 0; duration: carousel.kLiftDuration; easing.type: Easing.InQuad }
        }
        PauseAnimation { duration: carousel.kLiftPause }
        ParallelAnimation {
            ScaleAnimator { target: centerCard
                from: carousel.kLiftScale; to: 1.0; duration: carousel.kLiftDuration; easing.type: Easing.OutBack; easing.overshoot: 1.2 }
            OpacityAnimator { target: centerCard
                from: 0; to: 1.0; duration: carousel.kLiftDuration; easing.type: Easing.OutQuad }
        }
    }

    // ── 4. Dissolve ─────────────────────────────────────────────────
    // Grid dissolve — old art is broken into a grid of squares that
    // each fade out at random times, revealing the new art beneath.

    property url _dissolveNewArt: ""

    function _startDissolve() {
        // Grab the incoming art based on skip direction
        _dissolveNewArt = (direction === 1) ? nextArtSource : prevArtSource
        _dissolveProgress = 0.0
        _dissolveActive = true

        // Current art stays visible via centerCard, new art dissolves in on top via shader
        centerCard.opacity = 1.0
        dissolveAnim.start()
    }

    NumberAnimation {
        id: dissolveAnim
        target: carousel; property: "_dissolveProgress"
        from: 0.0; to: 1.0
        duration: carousel.kDissolveDuration
        easing.type: Easing.InOutQuad

        onStarted: carousel.animBusy = true
        onFinished: carousel._onAnimDone()
    }

    // ── 5. Flip ─────────────────────────────────────────────────────

    function _startFlip() {
        _showExit()
        centerCard.opacity = 0
        centerCard.cfAngle = -90
        flipAnim.start()
    }

    SequentialAnimation {
        id: flipAnim
        onStarted: carousel.animBusy = true
        onFinished: { centerCard.cfAngle = 0; carousel._onAnimDone() }

        NumberAnimation { target: exitSnapshot; property: "_flipAngle"
            from: 0; to: 90; duration: carousel.kFlipDuration / 2; easing.type: Easing.InQuad }
        ScriptAction { script: { exitSnapshot.opacity = 0; centerCard.opacity = 1.0 } }
        NumberAnimation { target: centerCard; property: "cfAngle"
            from: -90; to: 0; duration: carousel.kFlipDuration / 2; easing.type: Easing.OutQuad }
    }

    // ═══════════════════════════════════════════════════════════════════
    //  3D PREVIEW ANIMATIONS (6–10)
    // ═══════════════════════════════════════════════════════════════════

    // ── 6. Coverflow ────────────────────────────────────────────────
    // Classic iTunes-style: side cards tilted on Y-axis, entering card
    // straightens to center with slight overshoot, exiting tilts away.

    function _startCoverflow(dir) {
        coverflowAnim._dir = dir
        coverflowAnim._offset = _sideOffset
        coverflowAnim._angle = sideCardAngle
        coverflowAnim._opacity = sideCardOpacity
        coverflowAnim.start()
    }

    ParallelAnimation {
        id: coverflowAnim
        property int  _dir: 1
        property real _offset: 0
        property real _angle: 0
        property real _opacity: 0.4

        onStarted: carousel.animBusy = true
        onFinished: carousel._on3DDone()

        // Exit: center → side
        NumberAnimation { target: exitSnapshot; property: "_slideX"
            from: 0; to: coverflowAnim._dir === 1 ? -coverflowAnim._offset : coverflowAnim._offset
            duration: carousel.k3dDuration; easing.type: Easing.OutCubic }
        NumberAnimation { target: exitSnapshot; property: "_flipAngle"
            from: 0; to: coverflowAnim._dir === 1 ? coverflowAnim._angle : -coverflowAnim._angle
            duration: carousel.k3dDuration; easing.type: Easing.OutCubic }
        NumberAnimation { target: exitSnapshot; property: "scale"
            from: 1.0; to: carousel.k3dSideScale
            duration: carousel.k3dDuration; easing.type: Easing.OutCubic }
        NumberAnimation { target: exitSnapshot; property: "opacity"
            from: 1.0; to: coverflowAnim._opacity
            duration: carousel.k3dDuration; easing.type: Easing.OutCubic }

        // Prev card
        NumberAnimation { target: prevCard; property: "cfOffset"
            from: -coverflowAnim._offset
            to: coverflowAnim._dir === 1 ? -coverflowAnim._offset * 2 : 0
            duration: carousel.k3dDuration
            easing.type: coverflowAnim._dir === 1 ? Easing.InCubic : Easing.OutBack; easing.overshoot: 1.4 }
        NumberAnimation { target: prevCard; property: "cfAngle"
            from: coverflowAnim._angle
            to: coverflowAnim._dir === 1 ? coverflowAnim._angle : 0
            duration: carousel.k3dDuration
            easing.type: coverflowAnim._dir === 1 ? Easing.InCubic : Easing.OutBack; easing.overshoot: 1.4 }
        NumberAnimation { target: prevCard; property: "cfScale"
            from: carousel.k3dSideScale
            to: coverflowAnim._dir === 1 ? carousel.k3dFarScale : 1.0
            duration: carousel.k3dDuration
            easing.type: coverflowAnim._dir === 1 ? Easing.InCubic : Easing.OutBack; easing.overshoot: 1.4 }
        NumberAnimation { target: prevCard; property: "cfOpacity"
            from: coverflowAnim._opacity
            to: coverflowAnim._dir === 1 ? 0 : 1.0
            duration: coverflowAnim._dir === 1 ? 220 : carousel.k3dDuration
            easing.type: coverflowAnim._dir === 1 ? Easing.InCubic : Easing.OutQuart }

        // Next card
        NumberAnimation { target: nextCard; property: "cfOffset"
            from: coverflowAnim._offset
            to: coverflowAnim._dir === 1 ? 0 : coverflowAnim._offset * 2
            duration: carousel.k3dDuration
            easing.type: coverflowAnim._dir === 1 ? Easing.OutBack : Easing.InCubic; easing.overshoot: 1.4 }
        NumberAnimation { target: nextCard; property: "cfAngle"
            from: -coverflowAnim._angle
            to: coverflowAnim._dir === 1 ? 0 : -coverflowAnim._angle
            duration: carousel.k3dDuration
            easing.type: coverflowAnim._dir === 1 ? Easing.OutBack : Easing.InCubic; easing.overshoot: 1.4 }
        NumberAnimation { target: nextCard; property: "cfScale"
            from: carousel.k3dSideScale
            to: coverflowAnim._dir === 1 ? 1.0 : carousel.k3dFarScale
            duration: carousel.k3dDuration
            easing.type: coverflowAnim._dir === 1 ? Easing.OutBack : Easing.InCubic; easing.overshoot: 1.4 }
        NumberAnimation { target: nextCard; property: "cfOpacity"
            from: coverflowAnim._opacity
            to: coverflowAnim._dir === 1 ? 1.0 : 0
            duration: coverflowAnim._dir === 1 ? carousel.k3dDuration : 220
            easing.type: coverflowAnim._dir === 1 ? Easing.OutQuart : Easing.InCubic }
    }

    // ── 7. Conveyor ─────────────────────────────────────────────────
    // All three cards translate laterally in sync — flat, no rotation.
    // Like items on a moving belt. Clean, mechanical feel.

    function _startConveyor(dir) {
        var step = _sideOffset * 2  // distance between card centers
        conveyorAnim._dir = dir
        conveyorAnim._step = step
        conveyorAnim.start()
    }

    ParallelAnimation {
        id: conveyorAnim
        property int  _dir: 1
        property real _step: 0

        onStarted: carousel.animBusy = true
        onFinished: carousel._on3DDone()

        // Exit card slides one step in travel direction
        NumberAnimation { target: exitSnapshot; property: "_slideX"
            from: 0; to: conveyorAnim._dir === 1 ? -conveyorAnim._step : conveyorAnim._step
            duration: carousel.k3dDuration; easing.type: Easing.InOutCubic }
        NumberAnimation { target: exitSnapshot; property: "opacity"
            from: 1.0; to: 0
            duration: carousel.k3dDuration; easing.type: Easing.InQuad }

        // Prev card
        NumberAnimation { target: prevCard; property: "cfOffset"
            from: -carousel._sideOffset
            to: conveyorAnim._dir === 1 ? -carousel._sideOffset - conveyorAnim._step : 0
            duration: carousel.k3dDuration; easing.type: Easing.InOutCubic }
        NumberAnimation { target: prevCard; property: "cfAngle"
            to: 0; duration: carousel.k3dDuration; easing.type: Easing.OutCubic }
        NumberAnimation { target: prevCard; property: "cfScale"
            from: carousel.k3dSideScale
            to: conveyorAnim._dir === 1 ? carousel.k3dFarScale : 1.0
            duration: carousel.k3dDuration; easing.type: Easing.InOutCubic }
        NumberAnimation { target: prevCard; property: "cfOpacity"
            from: carousel.sideCardOpacity
            to: conveyorAnim._dir === 1 ? 0 : 1.0
            duration: carousel.k3dDuration; easing.type: Easing.InOutQuad }

        // Next card
        NumberAnimation { target: nextCard; property: "cfOffset"
            from: carousel._sideOffset
            to: conveyorAnim._dir === 1 ? 0 : carousel._sideOffset + conveyorAnim._step
            duration: carousel.k3dDuration; easing.type: Easing.InOutCubic }
        NumberAnimation { target: nextCard; property: "cfAngle"
            to: 0; duration: carousel.k3dDuration; easing.type: Easing.OutCubic }
        NumberAnimation { target: nextCard; property: "cfScale"
            from: carousel.k3dSideScale
            to: conveyorAnim._dir === 1 ? 1.0 : carousel.k3dFarScale
            duration: carousel.k3dDuration; easing.type: Easing.InOutCubic }
        NumberAnimation { target: nextCard; property: "cfOpacity"
            from: carousel.sideCardOpacity
            to: conveyorAnim._dir === 1 ? 1.0 : 0
            duration: carousel.k3dDuration; easing.type: Easing.InOutQuad }
    }

    // ── 8. Stack ────────────────────────────────────────────────────
    // Cards layered with depth. Front card shrinks + fades back,
    // next card scales up from behind. No lateral movement.

    function _startStack(dir) {
        stackAnim._dir = dir
        stackAnim.start()
    }

    ParallelAnimation {
        id: stackAnim
        property int _dir: 1

        onStarted: carousel.animBusy = true
        onFinished: carousel._on3DDone()

        // Exit: shrink and fade away
        NumberAnimation { target: exitSnapshot; property: "scale"
            from: 1.0; to: carousel.k3dSideScale
            duration: carousel.k3dDuration; easing.type: Easing.InCubic }
        NumberAnimation { target: exitSnapshot; property: "opacity"
            from: 1.0; to: 0
            duration: carousel.k3dDuration; easing.type: Easing.InQuad }

        // Entering card: grows from behind
        NumberAnimation { target: prevCard; property: "cfOffset"
            from: -carousel._sideOffset; to: stackAnim._dir === -1 ? 0 : -carousel._sideOffset
            duration: carousel.k3dDuration
            easing.type: stackAnim._dir === -1 ? Easing.OutCubic : Easing.InCubic }
        NumberAnimation { target: prevCard; property: "cfAngle"
            from: carousel.sideCardAngle; to: stackAnim._dir === -1 ? 0 : carousel.sideCardAngle
            duration: carousel.k3dDuration; easing.type: Easing.OutCubic }
        NumberAnimation { target: prevCard; property: "cfScale"
            from: carousel.k3dSideScale; to: stackAnim._dir === -1 ? 1.0 : carousel.k3dFarScale
            duration: carousel.k3dDuration
            easing.type: stackAnim._dir === -1 ? Easing.OutBack : Easing.InCubic; easing.overshoot: 1.3 }
        NumberAnimation { target: prevCard; property: "cfOpacity"
            from: carousel.sideCardOpacity; to: stackAnim._dir === -1 ? 1.0 : 0
            duration: carousel.k3dDuration; easing.type: Easing.InOutQuad }

        NumberAnimation { target: nextCard; property: "cfOffset"
            from: carousel._sideOffset; to: stackAnim._dir === 1 ? 0 : carousel._sideOffset
            duration: carousel.k3dDuration
            easing.type: stackAnim._dir === 1 ? Easing.OutCubic : Easing.InCubic }
        NumberAnimation { target: nextCard; property: "cfAngle"
            from: -carousel.sideCardAngle; to: stackAnim._dir === 1 ? 0 : -carousel.sideCardAngle
            duration: carousel.k3dDuration; easing.type: Easing.OutCubic }
        NumberAnimation { target: nextCard; property: "cfScale"
            from: carousel.k3dSideScale; to: stackAnim._dir === 1 ? 1.0 : carousel.k3dFarScale
            duration: carousel.k3dDuration
            easing.type: stackAnim._dir === 1 ? Easing.OutBack : Easing.InCubic; easing.overshoot: 1.3 }
        NumberAnimation { target: nextCard; property: "cfOpacity"
            from: carousel.sideCardOpacity; to: stackAnim._dir === 1 ? 1.0 : 0
            duration: carousel.k3dDuration; easing.type: Easing.InOutQuad }
    }

    // ── 9. Depth ────────────────────────────────────────────────────
    // Parallax depth effect. Entering card rises from far behind
    // (small + transparent → full + opaque). Exiting card recedes
    // into the distance. Side cards stay in place but scale shift
    // creates a strong sense of z-depth.

    function _startDepth(dir) {
        depthAnim._dir = dir
        depthAnim.start()
    }

    ParallelAnimation {
        id: depthAnim
        property int _dir: 1

        onStarted: carousel.animBusy = true
        onFinished: carousel._on3DDone()

        // Exit: recede into distance
        NumberAnimation { target: exitSnapshot; property: "scale"
            from: 1.0; to: 0.5
            duration: carousel.k3dDuration; easing.type: Easing.InCubic }
        NumberAnimation { target: exitSnapshot; property: "opacity"
            from: 1.0; to: 0
            duration: carousel.k3dDuration; easing.type: Easing.InCubic }

        // Prev card
        NumberAnimation { target: prevCard; property: "cfOffset"
            from: -carousel._sideOffset
            to: depthAnim._dir === -1 ? 0 : -carousel._sideOffset * 1.5
            duration: carousel.k3dDuration; easing.type: Easing.OutCubic }
        NumberAnimation { target: prevCard; property: "cfAngle"
            to: depthAnim._dir === -1 ? 0 : carousel.sideCardAngle
            duration: carousel.k3dDuration; easing.type: Easing.OutCubic }
        NumberAnimation { target: prevCard; property: "cfScale"
            from: carousel.k3dSideScale
            to: depthAnim._dir === -1 ? 1.0 : 0.4
            duration: carousel.k3dDuration
            easing.type: depthAnim._dir === -1 ? Easing.OutBack : Easing.InCubic; easing.overshoot: 1.2 }
        NumberAnimation { target: prevCard; property: "cfOpacity"
            from: carousel.sideCardOpacity
            to: depthAnim._dir === -1 ? 1.0 : 0
            duration: carousel.k3dDuration; easing.type: Easing.InOutQuad }

        // Next card
        NumberAnimation { target: nextCard; property: "cfOffset"
            from: carousel._sideOffset
            to: depthAnim._dir === 1 ? 0 : carousel._sideOffset * 1.5
            duration: carousel.k3dDuration; easing.type: Easing.OutCubic }
        NumberAnimation { target: nextCard; property: "cfAngle"
            to: depthAnim._dir === 1 ? 0 : -carousel.sideCardAngle
            duration: carousel.k3dDuration; easing.type: Easing.OutCubic }
        NumberAnimation { target: nextCard; property: "cfScale"
            from: carousel.k3dSideScale
            to: depthAnim._dir === 1 ? 1.0 : 0.4
            duration: carousel.k3dDuration
            easing.type: depthAnim._dir === 1 ? Easing.OutBack : Easing.InCubic; easing.overshoot: 1.2 }
        NumberAnimation { target: nextCard; property: "cfOpacity"
            from: carousel.sideCardOpacity
            to: depthAnim._dir === 1 ? 1.0 : 0
            duration: carousel.k3dDuration; easing.type: Easing.InOutQuad }
    }

    // ── 10. Swing ───────────────────────────────────────────────────
    // Cards swing on a hinge at their outer edge, like pages in a
    // book. The exiting card rotates away on its near edge, the
    // entering card swings in from its far edge. Dramatic, physical.

    function _startSwing(dir) {
        swingAnim._dir = dir
        swingAnim.start()
    }

    ParallelAnimation {
        id: swingAnim
        property int _dir: 1

        onStarted: carousel.animBusy = true
        onFinished: carousel._on3DDone()

        // Exit: swing away from center edge (large Y-axis rotation)
        NumberAnimation { target: exitSnapshot; property: "_flipAngle"
            from: 0; to: swingAnim._dir === 1 ? -70 : 70
            duration: carousel.k3dDuration; easing.type: Easing.InCubic }
        NumberAnimation { target: exitSnapshot; property: "_slideX"
            from: 0; to: swingAnim._dir === 1 ? -carousel._sideOffset * 0.6 : carousel._sideOffset * 0.6
            duration: carousel.k3dDuration; easing.type: Easing.InCubic }
        NumberAnimation { target: exitSnapshot; property: "opacity"
            from: 1.0; to: 0
            duration: carousel.k3dDuration; easing.type: Easing.InQuad }

        // Prev card
        NumberAnimation { target: prevCard; property: "cfOffset"
            from: -carousel._sideOffset
            to: swingAnim._dir === -1 ? 0 : -carousel._sideOffset * 1.3
            duration: carousel.k3dDuration
            easing.type: swingAnim._dir === -1 ? Easing.OutBack : Easing.InCubic; easing.overshoot: 1.2 }
        NumberAnimation { target: prevCard; property: "cfAngle"
            from: carousel.sideCardAngle
            to: swingAnim._dir === -1 ? 0 : 60
            duration: carousel.k3dDuration
            easing.type: swingAnim._dir === -1 ? Easing.OutBack : Easing.InCubic; easing.overshoot: 1.4 }
        NumberAnimation { target: prevCard; property: "cfScale"
            from: carousel.k3dSideScale
            to: swingAnim._dir === -1 ? 1.0 : carousel.k3dFarScale
            duration: carousel.k3dDuration
            easing.type: swingAnim._dir === -1 ? Easing.OutCubic : Easing.InCubic }
        NumberAnimation { target: prevCard; property: "cfOpacity"
            from: carousel.sideCardOpacity
            to: swingAnim._dir === -1 ? 1.0 : 0
            duration: carousel.k3dDuration; easing.type: Easing.InOutQuad }

        // Next card
        NumberAnimation { target: nextCard; property: "cfOffset"
            from: carousel._sideOffset
            to: swingAnim._dir === 1 ? 0 : carousel._sideOffset * 1.3
            duration: carousel.k3dDuration
            easing.type: swingAnim._dir === 1 ? Easing.OutBack : Easing.InCubic; easing.overshoot: 1.2 }
        NumberAnimation { target: nextCard; property: "cfAngle"
            from: -carousel.sideCardAngle
            to: swingAnim._dir === 1 ? 0 : -60
            duration: carousel.k3dDuration
            easing.type: swingAnim._dir === 1 ? Easing.OutBack : Easing.InCubic; easing.overshoot: 1.4 }
        NumberAnimation { target: nextCard; property: "cfScale"
            from: carousel.k3dSideScale
            to: swingAnim._dir === 1 ? 1.0 : carousel.k3dFarScale
            duration: carousel.k3dDuration
            easing.type: swingAnim._dir === 1 ? Easing.OutCubic : Easing.InCubic }
        NumberAnimation { target: nextCard; property: "cfOpacity"
            from: carousel.sideCardOpacity
            to: swingAnim._dir === 1 ? 1.0 : 0
            duration: carousel.k3dDuration; easing.type: Easing.InOutQuad }
    }

    // ═══════════════════════════════════════════════════════════════════
    //  Cards
    // ═══════════════════════════════════════════════════════════════════

    // Previous card (tilted left) — only visible with 3D preview
    CoverflowCard {
        id: prevCard
        anchors.verticalCenter: parent.verticalCenter
        anchors.horizontalCenter: parent.horizontalCenter
        width: carousel.artSize; height: width
        visible: artSource != "" && carousel.previewEnabled
        z: 0

        cfOffset: -carousel._sideOffset
        cfAngle: carousel.sideCardAngle
        cfScale: carousel.k3dSideScale
        cfOpacity: carousel.sideCardOpacity

        artSource: carousel.prevArtSource
        artSize: carousel.artSize * 0.9
        roundedArt: carousel.roundedArt
        vinylMode: carousel.vinylMode
        artRadius: carousel.artRadius
        roundedMask: artRoundedMask
        vinylMask: vinylMaskRect
        animating: carousel.animBusy

        image.layer.enabled: carousel.roundedArt || carousel.vinylMode
        image.layer.live: !carousel.animBusy
    }

    // Next card (tilted right) — only visible with 3D preview
    CoverflowCard {
        id: nextCard
        anchors.verticalCenter: parent.verticalCenter
        anchors.horizontalCenter: parent.horizontalCenter
        width: carousel.artSize; height: width
        visible: artSource != "" && carousel.previewEnabled
        z: 0

        cfOffset: carousel._sideOffset
        cfAngle: -carousel.sideCardAngle
        cfScale: carousel.k3dSideScale
        cfOpacity: carousel.sideCardOpacity

        artSource: carousel.nextArtSource
        artSize: carousel.artSize * 0.9
        roundedArt: carousel.roundedArt
        vinylMode: carousel.vinylMode
        artRadius: carousel.artRadius
        roundedMask: artRoundedMask
        vinylMask: vinylMaskRect
        animating: carousel.animBusy

        image.layer.enabled: carousel.roundedArt || carousel.vinylMode
        image.layer.live: !carousel.animBusy
    }

    // Exit snapshot — frozen texture of old art for transitions
    Item {
        id: exitSnapshot
        anchors.verticalCenter: parent.verticalCenter
        anchors.horizontalCenter: parent.horizontalCenter
        width: carousel.artSize; height: width
        z: 0
        visible: false

        property real _slideX: 0
        property real _flipAngle: 0

        transform: [
            Translate { x: exitSnapshot._slideX },
            Rotation {
                axis { x: 0; y: 1; z: 0 }
                angle: exitSnapshot._flipAngle
                origin.x: exitSnapshot.width / 2
                origin.y: exitSnapshot.height / 2
            }
        ]

        ShaderEffectSource {
            id: exitTexture
            anchors.fill: parent
            sourceItem: centerCard.image
            live: !carousel._anyAnimRunning
            hideSource: false

            layer.enabled: carousel.roundedArt || carousel.vinylMode
            layer.effect: OpacityMask {
                maskSource: carousel.vinylMode ? vinylMaskRect : artRoundedMask
            }
        }
    }

    // Dissolve — GPU shader replaces the old 400-item grid.
    // A single ShaderEffect with one draw call dissolves the new art in
    // over the old art (visible via centerCard underneath).
    Image {
        id: dissolveNewArtImage
        visible: false
        width: carousel.artSize; height: width
        source: carousel._dissolveNewArt
        sourceSize: Qt.size(carousel.artSize, carousel.artSize)
        fillMode: Image.PreserveAspectFit
        asynchronous: true
        layer.enabled: carousel._dissolveActive
        layer.smooth: true
    }

    ShaderEffect {
        id: dissolveShader
        anchors.verticalCenter: parent.verticalCenter
        anchors.horizontalCenter: parent.horizontalCenter
        width: carousel.artSize; height: width
        z: 2
        visible: carousel._dissolveActive

        property var source: dissolveNewArtImage
        property real progress: carousel._dissolveProgress
        property real gridSize: carousel.kDissolveGridSize
        property real smoothness: carousel.kDissolveSmoothness

        // On Android, QML is bundled under assets:/ which Qt RHI's ShaderEffect
        // rejects (only qrc:/ or local files). Embed the .qsb via qrc for Android.
        fragmentShader: Qt.platform.os === "android"
            ? "qrc:/shaders/dissolve_squares.frag.qsb"
            : "shaders/dissolve_squares.frag.qsb"

        // Clip to rounded or vinyl shape
        layer.enabled: carousel.roundedArt || carousel.vinylMode
        layer.effect: OpacityMask {
            maskSource: carousel.vinylMode ? vinylMaskRect : artRoundedMask
        }
    }

    // Center card — current track
    CoverflowCard {
        id: centerCard
        anchors.verticalCenter: parent.verticalCenter
        anchors.horizontalCenter: parent.horizontalCenter
        width: carousel.artSize; height: width
        z: 1

        artSource: carousel.artSource
        artSize: carousel.artSize
        roundedArt: carousel.roundedArt
        vinylMode: carousel.vinylMode
        artRadius: carousel.artRadius
        roundedMask: artRoundedMask
        vinylMask: vinylMaskRect
        asynchronous: true
        animating: carousel.animBusy

        vinylAngle: carousel.vinylMode ? carousel._vinylAngleInternal : 0

        showShadow: carousel.showShadow
        shadowLive: !carousel._anyAnimRunning

        image.onStatusChanged: {
            if (centerCard.image.status === Image.Error)
                carousel.artLoadFailed()
        }

        MouseArea {
            anchors.fill: parent
            z: 2
            cursorShape: Qt.PointingHandCursor
            onClicked: carousel.artClicked()
        }
    }

    // ═══════════════════════════════════════════════════════════════════
    //  Vinyl rotation
    // ═══════════════════════════════════════════════════════════════════

    property real _vinylAngleInternal: 0

    Binding {
        target: carousel
        property: "_vinylAngleInternal"
        value: carousel.playbackDuration > 0
            ? (carousel.playbackPosition / carousel.playbackDuration) * 360 : 0
        when: carousel.vinylMode
        restoreMode: Binding.RestoreNone
    }

    Behavior on _vinylAngleInternal {
        enabled: carousel.vinylMode && !carousel.animBusy
        NumberAnimation { duration: 200; easing.type: Easing.Linear }
    }

    onArtSourceChanged: {
        if (vinylMode) _vinylAngleInternal = 0
    }

    // ═══════════════════════════════════════════════════════════════════
    //  Settle timer
    // ═══════════════════════════════════════════════════════════════════

    Timer {
        id: settleTimer
        interval: 50
        onTriggered: {
            carousel.animBusy = false
            carousel.settled()
        }
    }

    onAnimationFinished: settleTimer.restart()
}
