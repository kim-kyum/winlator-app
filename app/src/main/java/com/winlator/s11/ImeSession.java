package com.winlator.s11;

import android.content.Context;
import android.text.Editable;
import android.text.InputType;
import android.view.KeyCharacterMap;
import android.view.KeyEvent;
import android.view.View;
import android.view.inputmethod.BaseInputConnection;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputConnection;
import android.view.inputmethod.InputMethodManager;

import androidx.core.view.ViewCompat;
import androidx.core.view.WindowInsetsCompat;
import androidx.preference.PreferenceManager;

import com.winlator.xserver.XKeycode;
import com.winlator.xserver.XServer;

/**
 * Bridges the Android keyboard (Samsung Keyboard, Gboard, ...) to the X server so
 * composed text such as Hangul reaches Windows programs.
 *
 * The view that hosts the keyboard connection declares itself a text editor, so the
 * keyboard composes syllables and reports them through {@link InputConnection}. Text is
 * typed into Wine as ASCII through the matching physical keys, everything else as X11
 * Unicode keysyms, which Wine turns into WM_CHAR.
 *
 * The syllable being composed goes into the Windows program right away, as with the
 * Windows Korean IME: when the keyboard changes it (ㅎ, 하, 한), Backspace takes back what
 * changed and the new text is typed. With that turned off it waits in a
 * {@link ComposingOverlay} at the top of the screen until the keyboard commits it.
 */
public class ImeSession {
    public static final String PREF_KEY = "s11_ime_input";
    public static final String PREF_INLINE_KEY = "s11_ime_inline";

    private final XServer xServer;
    private final ComposingOverlay overlay;
    private final boolean inline;
    private final KeyCharacterMap charMap = KeyCharacterMap.load(KeyCharacterMap.VIRTUAL_KEYBOARD);
    private Connection connection;

    public ImeSession(XServer xServer, ComposingOverlay overlay, boolean inline) {
        this.xServer = xServer;
        this.overlay = overlay;
        this.inline = inline;
    }

    public static boolean isEnabled(Context context) {
        return PreferenceManager.getDefaultSharedPreferences(context).getBoolean(PREF_KEY, true);
    }

    public static boolean isInline(Context context) {
        return PreferenceManager.getDefaultSharedPreferences(context).getBoolean(PREF_INLINE_KEY, true);
    }

    /**
     * A key the keyboard let through (arrows, Enter, a shortcut) ends the syllable being
     * composed first, so it lands after the syllable and not inside it.
     */
    public void onKeyPassedThrough(View view, KeyEvent event) {
        if (event.getAction() == KeyEvent.ACTION_DOWN && !KeyEvent.isModifierKey(event.getKeyCode())) {
            commitPendingComposition(view);
        }
    }

    public InputConnection createInputConnection(View view, EditorInfo outAttrs) {
        // A plain text field without suggestions: keyboards still compose Hangul,
        // but do not underline words or auto-correct what reaches Windows.
        outAttrs.inputType = InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS;
        outAttrs.imeOptions = EditorInfo.IME_FLAG_NO_EXTRACT_UI | EditorInfo.IME_FLAG_NO_FULLSCREEN
                | EditorInfo.IME_ACTION_NONE | EditorInfo.IME_FLAG_NO_ENTER_ACTION;
        outAttrs.initialSelStart = 0;
        outAttrs.initialSelEnd = 0;
        connection = new Connection(view);
        return connection;
    }

    /** Sends any half-composed text before a tap moves the Windows caret elsewhere. */
    public void commitPendingComposition(View view) {
        if (connection != null && connection.hasComposingText()) {
            connection.finishComposingText();
            InputMethodManager imm = (InputMethodManager)view.getContext().getSystemService(Context.INPUT_METHOD_SERVICE);
            if (imm != null) imm.restartInput(view);
        }
    }

    public static void toggleKeyboard(View view) {
        InputMethodManager imm = (InputMethodManager)view.getContext().getSystemService(Context.INPUT_METHOD_SERVICE);
        if (imm == null) return;
        WindowInsetsCompat insets = ViewCompat.getRootWindowInsets(view);
        boolean visible = insets != null && insets.isVisible(WindowInsetsCompat.Type.ime());
        if (visible) {
            imm.hideSoftInputFromWindow(view.getWindowToken(), 0);
        }
        else {
            // Called from the side drawer: wait until it has closed and handed focus back.
            view.postDelayed(() -> {
                view.requestFocus();
                imm.showSoftInput(view, InputMethodManager.SHOW_IMPLICIT);
            }, 400);
        }
    }

    /** The length of the start two strings share, not splitting a surrogate pair */
    static int sharedPrefix(String a, String b) {
        int common = 0, max = Math.min(a.length(), b.length());
        while (common < max && a.charAt(common) == b.charAt(common)) common++;
        if (common > 0 && Character.isHighSurrogate(a.charAt(common - 1))) common--;
        return common;
    }

    private void typeText(CharSequence text) {
        for (int i = 0; i < text.length(); ) {
            int codePoint = Character.codePointAt(text, i);
            i += Character.charCount(codePoint);
            typeCodePoint(codePoint);
        }
    }

    private void typeCodePoint(int codePoint) {
        if (codePoint == '\n' || codePoint == '\r') {
            pressKey(XKeycode.KEY_ENTER);
            return;
        }
        if (codePoint == '\t') {
            pressKey(XKeycode.KEY_TAB);
            return;
        }
        if (codePoint < 0x80) {
            // Real keys for ASCII, so programs that read key codes (shortcuts, games,
            // terminals) see the same thing a hardware keyboard would send.
            KeyEvent[] events = charMap.getEvents(new char[]{(char)codePoint});
            if (events != null && events.length > 0) {
                boolean typed = true;
                for (KeyEvent event : events) {
                    boolean handled = xServer.keyboard.onKeyEvent(event);
                    if (!handled && event.getAction() == KeyEvent.ACTION_DOWN && !KeyEvent.isModifierKey(event.getKeyCode())) typed = false;
                }
                if (typed) return;
            }
        }
        xServer.keyboard.typeUnicode(codePoint);
    }

    private void pressKey(XKeycode keycode) {
        xServer.injectKeyPress(keycode);
        xServer.injectKeyRelease(keycode);
    }

    /**
     * Keeps its own buffer to the syllable being composed: committed text leaves it for
     * Wine right away. Inline, Wine already shows the whole buffer (see {@link #mirror}).
     */
    private class Connection extends BaseInputConnection {
        /** Inline: the text Wine shows for this buffer, typed after the Windows caret */
        private String shown = "";

        Connection(View view) {
            super(view, true);
        }

        boolean hasComposingText() {
            Editable content = getEditable();
            return content != null && getComposingSpanStart(content) != -1
                    && getComposingSpanEnd(content) > getComposingSpanStart(content);
        }

        /** Inline: Backspace over what changed since the last time, then type the rest. */
        private void mirror() {
            Editable content = getEditable();
            String text = content != null ? content.toString() : "";
            int common = sharedPrefix(shown, text);
            int remove = shown.codePointCount(common, shown.length());
            for (int i = 0; i < remove; i++) pressKey(XKeycode.KEY_BKSP);
            typeText(text.substring(common));
            shown = text;
        }

        private void showComposing() {
            if (inline) mirror();
            else {
                Editable content = getEditable();
                if (content != null) overlay.show(content.toString());
            }
        }

        private void flush() {
            Editable content = getEditable();
            if (content == null) return;
            if (inline) mirror();
            else if (content.length() > 0) typeText(content.toString());
            content.clear();
            shown = "";
            overlay.hide();
        }

        @Override
        public boolean setComposingText(CharSequence text, int newCursorPosition) {
            super.setComposingText(text, newCursorPosition);
            showComposing();
            return true;
        }

        @Override
        public boolean setComposingRegion(int start, int end) {
            boolean result = super.setComposingRegion(start, end);
            showComposing();
            return result;
        }

        @Override
        public boolean commitText(CharSequence text, int newCursorPosition) {
            super.commitText(text, newCursorPosition);
            flush();
            return true;
        }

        @Override
        public boolean finishComposingText() {
            super.finishComposingText();
            flush();
            return true;
        }

        @Override
        public boolean deleteSurroundingText(int beforeLength, int afterLength) {
            // Nothing before the cursor lives in this buffer, so deletions are meant for
            // the Windows program. Samsung Keyboard may ask for several at once. A syllable
            // still being composed stays as it is.
            if (hasComposingText()) flush();
            for (int i = 0; i < beforeLength; i++) pressKey(XKeycode.KEY_BKSP);
            for (int i = 0; i < afterLength; i++) pressKey(XKeycode.KEY_DEL);
            return true;
        }

        @Override
        public boolean performEditorAction(int actionCode) {
            flush();
            pressKey(XKeycode.KEY_ENTER);
            return true;
        }
    }
}
