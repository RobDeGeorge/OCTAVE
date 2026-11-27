import QtQuick
import "." as App

// The Media Room's album art (square or, in vinyl mode, a record), with the
// one track-change transition: the old art leaves a short way in the
// direction the track list moved, shrinking a little and fading, while the
// new one arrives from the other side and settles. The same for the square
// art and the record; the 3D players (MusicDeviceScene) have their own.
//
// There are two cards. One shows the art; the other, hidden, loads the next
// art and comes in only once its picture is ready, and then the two change
// roles. A card on screen never changes its picture, so the art cannot be
// seen switching in the middle of a transition, whenever the new art turns
// up: with the track, before it, or a second later (a phone, Spotify).
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

    // The card showing the art, and the one that brings the next art in
    property CoverflowCard _front: cardA
    readonly property CoverflowCard _back: _front === cardA ? cardB : cardA
    // The back card has its new art and waits for the picture to load
    property bool _loading: false
    // The two cards are moving
    property bool _sliding: false
    // A track change asked for a transition even if the art stays the same
    property bool _slideAsked: false
    property bool _ready: false

    Component.onCompleted: {
        cardA.artSource = artSource
        _ready = true
    }

    // The track changed. artMayFollow: the source sends its art apart from
    // the track (a phone, Spotify), so the transition belongs to the art and
    // runs when that changes; a track with the same cover moves nothing.
    // Otherwise the art is known now (local music): the transition runs at
    // once, to the art as it is at the end of this turn of the event loop
    // (callers change the track just before or just after this call).
    function triggerSlide(artMayFollow) {
        animBusy = true
        if (artMayFollow === true) {
            if (!_loading && !_sliding) settleTimer.restart()
            return
        }
        if (previewEnabled) _sideShown = 0
        _slideAsked = true
        evalTimer.restart()
    }

    onArtSourceChanged: {
        if (vinylMode) _vinylAngleInternal = 0
        evalTimer.restart()
    }

    // One decision per turn of the event loop, whatever the order the track
    // change and the art change came in
    Timer {
        id: evalTimer
        interval: 0
        onTriggered: carousel._eval()
    }

    // The art that is, or is about to be, on screen
    function _pendingArt() { return (_loading || _sliding) ? _back.artSource : _front.artSource }

    function _eval() {
        if (!_ready) return
        if (!_slideAsked && String(artSource) === String(_pendingArt())) return
        _slideAsked = false
        _begin(artSource)
    }

    function _begin(target) {
        // A transition under way ends where it was going; one still loading
        // just gets the newer art
        if (_sliding) {
            swapAnim.stop()
            _commit()
        }
        settleTimer.stop()
        animBusy = true
        _loading = true
        loadGuard.restart()
        _back.artSource = target
        _cardStatus(_back)
    }

    // A card's picture is ready, or failed
    function _cardStatus(card) {
        var st = card.image.status
        if (st === Image.Error && (card === _front || _loading)) {
            // The parent answers with a fallback art, which is a new target
            artLoadFailed()
            return
        }
        if (card === _back && _loading && (st === Image.Ready || st === Image.Null))
            _start()
    }

    // A picture that neither loads nor fails must not hold the page's
    // "busy" state for good: go with what there is
    Timer {
        id: loadGuard
        interval: 2000
        onTriggered: if (carousel._loading) carousel._start()
    }

    function _start() {
        if (!_loading) return
        _loading = false
        loadGuard.stop()
        swapAnim._travel = (direction === -1 ? -1 : 1) * Math.round(artSize * kSwapTravel)
        // What moves is each card's holder (see the cards). Animators do
        // not write the property until they end: put both where the
        // animation starts them
        var outSlot = _front.parent, inSlot = _back.parent
        outSlot.opacity = 1.0; outSlot.scale = 1.0; outSlot.x = 0
        inSlot.opacity = 0; inSlot.scale = kSwapScale; inSlot.x = swapAnim._travel
        swapAnim._out = outSlot
        swapAnim._in = inSlot
        _sliding = true
        swapAnim.start()
    }

    // The card that came in is the art now
    function _commit() {
        // Once per transition: stopping the animation may or may not report
        // it finished, and a second swap would put the old art back
        if (!_sliding) return
        var inSlot = _back.parent, outSlot = _front.parent
        _front = _back
        _sliding = false
        inSlot.opacity = 1.0; inSlot.scale = 1.0; inSlot.x = 0
        outSlot.opacity = 1.0; outSlot.scale = 1.0; outSlot.x = 0
    }

    // Animators only, so the whole of it runs on the render thread and a
    // busy GUI thread (the track change itself) cannot make it stutter.
    // Their targets must be plain items: an item with a layer effect (the
    // cards have two, the corner mask and the shadow) is drawn by the
    // effect, which an Animator does not reach, so it would stay where it
    // was for the whole animation and jump at the end.
    ParallelAnimation {
        id: swapAnim
        // Signed: positive when the new art comes from the right
        property real _travel: 0
        property Item _out: null
        property Item _in: null

        onFinished: {
            carousel._commit()
            carousel.animationFinished()
        }

        // The old art: away, and gone a little past half-way
        XAnimator { target: swapAnim._out
            from: 0; to: -swapAnim._travel; duration: carousel.kSwapDuration; easing.type: Easing.OutCubic }
        ScaleAnimator { target: swapAnim._out
            from: 1.0; to: carousel.kSwapScale; duration: carousel.kSwapDuration; easing.type: Easing.OutCubic }
        OpacityAnimator { target: swapAnim._out
            from: 1.0; to: 0; duration: Math.round(carousel.kSwapDuration * 0.6); easing.type: Easing.OutQuad }

        // The new art: in from the other side, settling without a bounce
        XAnimator { target: swapAnim._in
            from: swapAnim._travel; to: 0; duration: carousel.kSwapDuration; easing.type: Easing.OutQuint }
        ScaleAnimator { target: swapAnim._in
            from: carousel.kSwapScale; to: 1.0; duration: carousel.kSwapDuration; easing.type: Easing.OutQuint }
        OpacityAnimator { target: swapAnim._in
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
    }

    // The two art cards, each in a plain holder: the holder is what the
    // transition moves and fades (an Animator cannot move the card itself,
    // see swapAnim). The cards' layers stay live: the picture inside does
    // not change while a card moves, so they cost nothing then, and a frozen
    // one would go on showing the art the card had before.
    Item {
        id: centerSlot
        anchors.verticalCenter: parent.verticalCenter
        anchors.horizontalCenter: parent.horizontalCenter
        width: carousel.artSize; height: width
        z: 1

        Item {
            id: slotA
            width: parent.width; height: parent.height
            visible: carousel._front === cardA || carousel._sliding
            z: carousel._front === cardA ? 0 : 1

            CoverflowCard {
                id: cardA
                anchors.fill: parent

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

                image.onStatusChanged: carousel._cardStatus(cardA)
            }
        }

        Item {
            id: slotB
            width: parent.width; height: parent.height
            visible: carousel._front === cardB || carousel._sliding
            z: carousel._front === cardB ? 0 : 1

            CoverflowCard {
                id: cardB
                anchors.fill: parent

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

                image.onStatusChanged: carousel._cardStatus(cardB)
            }
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

    // ═══════════════════════════════════════════════════════════════════
    //  Settle timer
    // ═══════════════════════════════════════════════════════════════════

    Timer {
        id: settleTimer
        interval: 50
        onTriggered: {
            if (carousel._loading || carousel._sliding || carousel._slideAsked) return
            carousel.animBusy = false
            carousel.settled()
        }
    }

    onAnimationFinished: settleTimer.restart()
}
