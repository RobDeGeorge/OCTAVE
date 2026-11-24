import QtQuick
import Qt5Compat.GraphicalEffects
import "." as App

// The Media Room's album art (square or, in vinyl mode, a record), with the
// one track-change transition: the old art leaves a short way in the
// direction the track list moved, shrinking a little and fading, while the
// new one arrives from the other side and settles. The same for the square
// art and the record; the 3D players (MusicDeviceScene) have their own.
//
// With the 3D preview on, the neighbours' art stands tilted at the sides:
// those cards fade out as a transition starts and back in once the parent
// has given them the new neighbours (showSideCards()).
Item {
    // Local dp/dpMin wrappers — work around Qt Android singleton-function bug.
    function dp(size) { return Math.round(size * (App.Spacing.effectiveScale || 1.0)) }
    function dpMin(size, floor) { return Math.max(floor, Math.round(size * (App.Spacing.effectiveScale || 1.0))) }

    id: carousel
    clip: false

    // ═══════════════════════════════════════════════════════════════════
    //  Constants
    // ═══════════════════════════════════════════════════════════════════

    // The transition: how long, how far the art travels (a share of its
    // size) and how small it is at the far end
    readonly property int  kSwapDuration: 340
    readonly property real kSwapTravel: 0.10
    readonly property real kSwapScale: 0.94

    // The 3D preview's side cards
    readonly property real k3dSideOffsetFactor: 0.42
    readonly property real k3dSideScale: 0.72
    readonly property int  kSideFadeDuration: 160

    // ═══════════════════════════════════════════════════════════════════
    //  Public API
    // ═══════════════════════════════════════════════════════════════════

    // 1 = to the next track (the new art comes from the right), -1 = to the previous
    property int direction: 1
    property url artSource: ""
    property url prevArtSource: ""
    property url nextArtSource: ""
    property bool animBusy: false

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

    signal animationFinished()
    signal settled()
    signal artClicked()
    signal artLoadFailed()

    // The parent has refreshed prevArtSource / nextArtSource: show them again
    function showSideCards() { _sideShown = 1 }

    // 0 while a transition hides the side cards, 1 otherwise
    property real _sideShown: 1
    Behavior on _sideShown { NumberAnimation { duration: carousel.kSideFadeDuration; easing.type: Easing.OutQuad } }

    // ═══════════════════════════════════════════════════════════════════
    //  Derived
    // ═══════════════════════════════════════════════════════════════════

    // Share of the shorter side the art takes when there are no side cards
    property real artFill: 0.85
    property real artSize: {
        var factor = previewEnabled ? 0.75 : artFill
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
    //  The transition
    // ═══════════════════════════════════════════════════════════════════

    // Run it: call before (or as) artSource changes. The old art is kept as
    // a frozen picture (exitTexture) while the card takes the new one.
    function triggerSlide() {
        _stopAll()
        if (previewEnabled) _sideShown = 0
        swapAnim._travel = (direction === -1 ? -1 : 1) * Math.round(artSize * kSwapTravel)
        // Animators do not write the property until they end: put both
        // pictures where the animation starts them
        exitSnapshot.visible = true
        exitTexture.opacity = 1.0
        centerCard.opacity = 0
        centerCard.scale = kSwapScale
        centerCard.x = swapAnim._travel
        swapAnim.start()
    }

    function _stopAll() {
        // The previous run's pending "settled" would clear animBusy in the
        // middle of the one about to start (which settles itself)
        settleTimer.stop()
        if (swapAnim.running) swapAnim.stop()
        _resetAll()
    }

    function _resetAll() {
        exitSnapshot.visible = false
        exitTexture.opacity = 0
        exitTexture.scale = 1.0
        exitTexture.x = 0
        centerCard.opacity = 1.0
        centerCard.scale = 1.0
        centerCard.x = 0
    }

    // Animators only, so the whole of it runs on the render thread and a
    // busy GUI thread (the track change itself) cannot make it stutter
    ParallelAnimation {
        id: swapAnim
        // Signed: positive when the new art comes from the right
        property real _travel: 0

        onStarted: carousel.animBusy = true
        onFinished: {
            carousel._resetAll()
            carousel.animationFinished()
        }

        // The old art: away, and gone a little past half-way
        XAnimator { target: exitTexture
            from: 0; to: -swapAnim._travel; duration: carousel.kSwapDuration; easing.type: Easing.OutCubic }
        ScaleAnimator { target: exitTexture
            from: 1.0; to: carousel.kSwapScale; duration: carousel.kSwapDuration; easing.type: Easing.OutCubic }
        OpacityAnimator { target: exitTexture
            from: 1.0; to: 0; duration: Math.round(carousel.kSwapDuration * 0.6); easing.type: Easing.OutQuad }

        // The new art: in from the other side, settling without a bounce
        XAnimator { target: centerCard
            from: swapAnim._travel; to: 0; duration: carousel.kSwapDuration; easing.type: Easing.OutQuint }
        ScaleAnimator { target: centerCard
            from: carousel.kSwapScale; to: 1.0; duration: carousel.kSwapDuration; easing.type: Easing.OutQuint }
        OpacityAnimator { target: centerCard
            from: 0; to: 1.0; duration: Math.round(carousel.kSwapDuration * 0.75); easing.type: Easing.OutQuad }
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
        cfOpacity: carousel.sideCardOpacity * carousel._sideShown

        artSource: carousel.prevArtSource
        artSize: carousel.artSize * 0.9
        roundedArt: carousel.roundedArt
        vinylMode: carousel.vinylMode
        artRadius: carousel.artRadius
        roundedMask: artRoundedMask
        vinylMask: vinylMaskRect
        animating: carousel.animBusy

        image.layer.enabled: carousel.roundedArt || carousel.vinylMode
        // layer.live is set through a Binding, not declared as `layer.live:`.
        // It is a revisioned property (Qt 6.5), and on Qt 6.7 whether the QML
        // compiler accepts the declaration depends on which file first compiled
        // the layer type: sometimes it fails with '".live" is not available due
        // to component versioning', which made MediaRoom unloadable (seen when
        // pages are pre-built at startup). A Binding sets it by name at runtime.
        Binding { target: prevCard.image.layer; property: "live"; value: !carousel.animBusy }
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
        cfOpacity: carousel.sideCardOpacity * carousel._sideShown

        artSource: carousel.nextArtSource
        artSize: carousel.artSize * 0.9
        roundedArt: carousel.roundedArt
        vinylMode: carousel.vinylMode
        artRadius: carousel.artRadius
        roundedMask: artRoundedMask
        vinylMask: vinylMaskRect
        animating: carousel.animBusy

        image.layer.enabled: carousel.roundedArt || carousel.vinylMode
        Binding { target: nextCard.image.layer; property: "live"; value: !carousel.animBusy }
    }

    // Exit snapshot — frozen texture of the old art for the transition.
    // The holder keeps the place; the texture inside it is what moves.
    Item {
        id: exitSnapshot
        anchors.verticalCenter: parent.verticalCenter
        anchors.horizontalCenter: parent.horizontalCenter
        width: carousel.artSize; height: width
        z: 0
        visible: false

        ShaderEffectSource {
            id: exitTexture
            width: parent.width; height: parent.height
            opacity: 0
            sourceItem: centerCard.image
            live: !swapAnim.running
            hideSource: false

            layer.enabled: carousel.roundedArt || carousel.vinylMode
            layer.effect: OpacityMask {
                maskSource: carousel.vinylMode ? vinylMaskRect : artRoundedMask
            }
        }
    }

    // Center card — current track. The holder keeps the place, so the card
    // inside it is free to be moved by the transition.
    Item {
        id: centerSlot
        anchors.verticalCenter: parent.verticalCenter
        anchors.horizontalCenter: parent.horizontalCenter
        width: carousel.artSize; height: width
        z: 1

        CoverflowCard {
            id: centerCard
            width: parent.width; height: parent.height

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
            shadowLive: !swapAnim.running

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
