package org.octave.phoneserver.control;

public final class DeviceMessage {

    public static final int TYPE_CLIPBOARD = 0;
    public static final int TYPE_ACK_CLIPBOARD = 1;
    public static final int TYPE_UHID_OUTPUT = 2;
    // OCTAVE extension: phone asleep / in use / panel dark (MirrorKeeper)
    public static final int TYPE_OCTAVE_PHONE_STATE = 100;
    // OCTAVE extension: the phone's active media session (PhoneMedia)
    public static final int TYPE_OCTAVE_MEDIA_STATE = 101;
    public static final int TYPE_OCTAVE_MEDIA_ART = 102;

    private int type;
    private String text;
    private long sequence;
    private int id;
    private byte[] data;
    private boolean asleep;
    private boolean inUse;
    private boolean panelDark;
    private PhoneMedia.State mediaState;

    private DeviceMessage() {
    }

    public static DeviceMessage createClipboard(String text) {
        DeviceMessage event = new DeviceMessage();
        event.type = TYPE_CLIPBOARD;
        event.text = text;
        return event;
    }

    public static DeviceMessage createAckClipboard(long sequence) {
        DeviceMessage event = new DeviceMessage();
        event.type = TYPE_ACK_CLIPBOARD;
        event.sequence = sequence;
        return event;
    }

    public static DeviceMessage createUhidOutput(int id, byte[] data) {
        DeviceMessage event = new DeviceMessage();
        event.type = TYPE_UHID_OUTPUT;
        event.id = id;
        event.data = data;
        return event;
    }

    public static DeviceMessage createOctavePhoneState(boolean asleep, boolean inUse, boolean panelDark) {
        DeviceMessage event = new DeviceMessage();
        event.type = TYPE_OCTAVE_PHONE_STATE;
        event.asleep = asleep;
        event.inUse = inUse;
        event.panelDark = panelDark;
        return event;
    }

    public static DeviceMessage createOctaveMediaState(PhoneMedia.State state) {
        DeviceMessage event = new DeviceMessage();
        event.type = TYPE_OCTAVE_MEDIA_STATE;
        event.mediaState = state;
        return event;
    }

    /** JPEG bytes, empty for "no art". */
    public static DeviceMessage createOctaveMediaArt(byte[] jpeg) {
        DeviceMessage event = new DeviceMessage();
        event.type = TYPE_OCTAVE_MEDIA_ART;
        event.data = jpeg;
        return event;
    }

    public PhoneMedia.State getMediaState() {
        return mediaState;
    }

    public boolean isAsleep() {
        return asleep;
    }

    public boolean isInUse() {
        return inUse;
    }

    public boolean isPanelDark() {
        return panelDark;
    }

    public int getType() {
        return type;
    }

    public String getText() {
        return text;
    }

    public long getSequence() {
        return sequence;
    }

    public int getId() {
        return id;
    }

    public byte[] getData() {
        return data;
    }
}
