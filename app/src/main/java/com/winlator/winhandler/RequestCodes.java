package com.winlator.winhandler;

abstract class RequestCodes {
    public static final byte EXIT = 0;
    public static final byte INIT = 1;
    public static final byte EXEC = 2;
    public static final byte KILL_PROCESS = 3;
    public static final byte LIST_PROCESSES = 4;
    public static final byte GET_PROCESS = 5;
    public static final byte SET_PROCESS_AFFINITY = 6;
    public static final byte MOUSE_EVENT = 7;
    public static final byte GET_GAMEPAD = 8;
    public static final byte GET_GAMEPAD_STATE = 9;
    public static final byte RELEASE_GAMEPAD = 10;
    public static final byte KEYBOARD_EVENT = 11;
    public static final byte BRING_TO_FRONT = 12;
    public static final byte CURSOR_POS_FEEDBACK = 13;
    public static final byte SET_CLIPBOARD_DATA = 14;
    public static final byte OPEN_URL = 15;
    public static final byte MIDI_OPEN = 16;
    public static final byte MIDI_CLOSE = 17;
    public static final byte SHOW_DESKTOP = 18;
    public static final byte SET_GAMEPAD_STATE = 19;
    public static final byte SHOW_WINDOW = 20;
    public static final byte GET_EXECUTABLE_PATH = 21;
    public static final byte PRE_EXEC = 22;
    // Winlator S11 start menu (tools/s11/shell)
    public static final byte S11_SHUTDOWN = 100;
    public static final byte S11_RESTART = 101;
    // Winlator S11 Settings: container settings that Winlator keeps (an ASCII value follows the code)
    public static final byte S11_SET_SCREEN = 102;
    public static final byte S11_SET_THEME = 103;
    public static final byte S11_SET_WALLPAPER = 104;
    public static final byte S11_WIFI_PANEL = 105;
    public static final byte S11_BLUETOOTH_PANEL = 106;
}
