package com.winlator.widget;

import android.content.Context;
import android.graphics.Color;
import android.graphics.drawable.ColorDrawable;
import android.graphics.drawable.StateListDrawable;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputConnection;
import android.widget.FrameLayout;

import com.winlator.core.AppUtils;
import com.winlator.math.Mathf;
import com.winlator.math.XForm;
import com.winlator.renderer.GLRenderer;
import com.winlator.renderer.ViewTransformation;
import com.winlator.s11.ImeSession;
import com.winlator.winhandler.MouseEventFlags;
import com.winlator.winhandler.WinHandler;
import com.winlator.xserver.Pointer;
import com.winlator.xserver.XServer;

import java.util.ArrayList;

public class TouchpadView extends View implements View.OnCapturedPointerListener {
    private static final byte MAX_FINGERS = 4;
    private static final short MAX_TWO_FINGERS_SCROLL_DISTANCE = 350;
    public static final byte MAX_TAP_TRAVEL_DISTANCE = 10;
    public static final short MAX_TAP_MILLISECONDS = 200;
    public static final float CURSOR_ACCELERATION = 1.5f;
    public static final byte CURSOR_ACCELERATION_THRESHOLD = 6;
    private final Finger[] fingers = new Finger[MAX_FINGERS];
    private byte numFingers = 0;
    private float sensitivity = 1.0f;
    private Finger mouseMoveFinger = null;
    private boolean pointerButtonLeftEnabled = true;
    private boolean pointerButtonRightEnabled = true;
    private boolean moveCursorToTouchpoint = false;
    private Finger fingerPointerButtonLeft;
    private Finger fingerPointerButtonRight;
    private float scrollAccumY = 0;
    private boolean scrolling = false;
    private final XServer xServer;
    private Runnable fourFingersTapCallback;
    private final float[] xform = XForm.getInstance();
    private ImeSession imeSession;
    // Winlator S11: Android controls drawn over Wine (the power tab...). A captured mouse only
    // reaches this view, so its clicks over those controls are handed to them here.
    private final ArrayList<View> overlayTargets = new ArrayList<>();
    private final ViewTransformation cursorTransformation = new ViewTransformation();
    private final float[] cursorPoint = new float[2];
    private final int[] tmpLocation = new int[2];
    private View hoveredOverlayTarget;
    private View pressedOverlayTarget;
    private int overlayButtons;
    private OnKeyListener preImeKeyListener;

    public TouchpadView(Context context, XServer xServer, boolean capturePointerOnExternalMouse) {
        super(context);
        this.xServer = xServer;
        setLayoutParams(new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        setBackground(createTransparentBackground());
        setClickable(true);
        setFocusable(true);
        setFocusableInTouchMode(capturePointerOnExternalMouse);
        updateXform(AppUtils.getScreenWidth(), AppUtils.getScreenHeight(), xServer.screenInfo.width, xServer.screenInfo.height);

        if (capturePointerOnExternalMouse) {
            setOnCapturedPointerListener(this);
            setOnClickListener(view -> {
                if (requestFocus()) requestPointerCapture();
            });
        }
    }

    /** Winlator S11: lets this view host the Android keyboard so composed text (Hangul) works. */
    public void setImeSession(ImeSession imeSession) {
        this.imeSession = imeSession;
        if (imeSession != null) setFocusableInTouchMode(true);
    }

    @Override
    public boolean onCheckIsTextEditor() {
        return imeSession != null;
    }

    @Override
    public InputConnection onCreateInputConnection(EditorInfo outAttrs) {
        if (imeSession != null) return imeSession.createInputConnection(this, outAttrs);
        return super.onCreateInputConnection(outAttrs);
    }

    /** Winlator S11: lets a captured (or plain) mouse click this Android control drawn over Wine. */
    public void addOverlayTarget(View target) {
        if (!overlayTargets.contains(target)) overlayTargets.add(target);
    }

    /** Winlator S11: sees keys before the Android keyboard does (shortcuts it would swallow). */
    public void setPreImeKeyListener(OnKeyListener preImeKeyListener) {
        this.preImeKeyListener = preImeKeyListener;
    }

    @Override
    public boolean onKeyPreIme(int keyCode, KeyEvent event) {
        if (preImeKeyListener != null && preImeKeyListener.onKey(this, keyCode, event)) return true;
        return super.onKeyPreIme(keyCode, event);
    }

    /** Where the Wine cursor is drawn, in this view's coordinates (renderer letterbox, stretch and zoom included). */
    private boolean getCursorPositionInView(float[] out) {
        GLRenderer renderer = xServer.getRenderer();
        int width = getWidth(), height = getHeight();
        if (renderer == null || width == 0 || height == 0) return false;

        int screenWidth = xServer.screenInfo.width, screenHeight = xServer.screenInfo.height;
        float x = xServer.pointer.getClampedX(), y = xServer.pointer.getClampedY();

        // Same pan as GLRenderer.drawFrame (magnifier, or screen raised above the soft keyboard)
        boolean raised = renderer.isScreenOffsetYRelativeToCursor();
        float zoom = raised ? 1.0f : renderer.getMagnifierZoom();
        float panX = 0, panY = 0;
        if (zoom != 1.0f) panX = Mathf.clamp(x * zoom - screenWidth * 0.5f, 0, screenWidth * Math.abs(1.0f - zoom));
        if (raised || zoom != 1.0f) {
            float scaleY = zoom != 1.0f ? Math.abs(1.0f - zoom) : 0.5f;
            panY = Mathf.clamp(y * zoom - screenHeight * (raised ? 0.25f : 0.5f), 0, screenHeight * scaleY);
        }
        x = x * zoom - panX;
        y = y * zoom - panY;

        if (renderer.isFullscreen()) {
            out[0] = x * width / screenWidth;
            out[1] = y * height / screenHeight;
        }
        else {
            cursorTransformation.update(width, height, screenWidth, screenHeight);
            out[0] = x * cursorTransformation.aspect + cursorTransformation.viewOffsetX;
            out[1] = y * cursorTransformation.aspect + cursorTransformation.viewOffsetY;
        }
        return true;
    }

    /** The overlay control under the Wine cursor, if any. */
    private View findOverlayTarget() {
        if (overlayTargets.isEmpty() || !getCursorPositionInView(cursorPoint)) return null;
        getLocationInWindow(tmpLocation);
        float x = cursorPoint[0] + tmpLocation[0];
        float y = cursorPoint[1] + tmpLocation[1];

        for (int i = overlayTargets.size() - 1; i >= 0; i--) {
            View target = overlayTargets.get(i);
            if (!target.isShown() || !target.isEnabled()) continue;
            target.getLocationInWindow(tmpLocation);
            if (x >= tmpLocation[0] && x < tmpLocation[0] + target.getWidth() &&
                y >= tmpLocation[1] && y < tmpLocation[1] + target.getHeight()) return target;
        }
        return null;
    }

    private void updateHoveredOverlayTarget() {
        if (overlayTargets.isEmpty()) return;
        View target = findOverlayTarget();
        if (target == hoveredOverlayTarget) return;
        if (hoveredOverlayTarget != null) hoveredOverlayTarget.setHovered(false);
        hoveredOverlayTarget = target;
        if (target != null) target.setHovered(true);
    }

    /**
     * Mouse buttons pressed over an overlay control go to that control, not to Wine (and so do
     * their releases). captured: Android won't deliver the click itself, so it is done here.
     */
    private boolean routeButtonToOverlay(MotionEvent event, boolean captured) {
        int action = event.getActionMasked();
        int button = event.getActionButton();
        if (action == MotionEvent.ACTION_BUTTON_PRESS) {
            View target = findOverlayTarget();
            if (target == null) return false;
            overlayButtons |= button;
            if (captured && button == MotionEvent.BUTTON_PRIMARY) {
                target.setPressed(true);
                pressedOverlayTarget = target;
            }
            return true;
        }
        else if (action == MotionEvent.ACTION_BUTTON_RELEASE && (overlayButtons & button) != 0) {
            overlayButtons &= ~button;
            if (button == MotionEvent.BUTTON_PRIMARY && pressedOverlayTarget != null) {
                View target = pressedOverlayTarget;
                pressedOverlayTarget = null;
                target.setPressed(false);
                if (findOverlayTarget() == target) target.performClick();
            }
            return true;
        }
        return false;
    }

    private static StateListDrawable createTransparentBackground() {
        StateListDrawable stateListDrawable = new StateListDrawable();
        ColorDrawable focusedDrawable = new ColorDrawable(Color.TRANSPARENT);
        ColorDrawable defaultDrawable = new ColorDrawable(Color.TRANSPARENT);

        stateListDrawable.addState(new int[]{android.R.attr.state_focused}, focusedDrawable);
        stateListDrawable.addState(new int[0], defaultDrawable);
        return stateListDrawable;
    }

    @Override
    protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        super.onSizeChanged(w, h, oldw, oldh);
        updateXform(w, h, xServer.screenInfo.width, xServer.screenInfo.height);
    }

    private void updateXform(int outerWidth, int outerHeight, int innerWidth, int innerHeight) {
        ViewTransformation viewTransformation = new ViewTransformation();
        viewTransformation.update(outerWidth, outerHeight, innerWidth, innerHeight);

        float invAspect = 1.0f / viewTransformation.aspect;
        if (!xServer.getRenderer().isFullscreen()) {
            XForm.makeTranslation(xform, -viewTransformation.viewOffsetX, -viewTransformation.viewOffsetY);
            XForm.scale(xform, invAspect, invAspect);
        }
        else XForm.makeScale(xform, invAspect, invAspect);
    }

    private class Finger {
        private int x;
        private int y;
        private final int startX;
        private final int startY;
        private int lastX;
        private int lastY;
        private final long touchTime;

        public Finger(float x, float y) {
            float[] transformedPoint = XForm.transformPoint(xform, x, y);
            this.x = this.startX = this.lastX = (int)transformedPoint[0];
            this.y = this.startY = this.lastY = (int)transformedPoint[1];
            touchTime = System.currentTimeMillis();
        }

        public void update(float x, float y) {
            lastX = this.x;
            lastY = this.y;
            float[] transformedPoint = XForm.transformPoint(xform, x, y);
            this.x = (int)transformedPoint[0];
            this.y = (int)transformedPoint[1];
        }

        private int deltaX() {
            float dx = (x - lastX) * sensitivity;
            if (Math.abs(dx) > CURSOR_ACCELERATION_THRESHOLD) {
                dx *= CURSOR_ACCELERATION;
            }
            return Mathf.roundPoint(dx);
        }

        private int deltaY() {
            float dy = (y - lastY) * sensitivity;
            if (Math.abs(dy) > CURSOR_ACCELERATION_THRESHOLD) {
                dy *= CURSOR_ACCELERATION;
            }
            return Mathf.roundPoint(dy);
        }

        private boolean isTap() {
            return (System.currentTimeMillis() - touchTime) < MAX_TAP_MILLISECONDS && travelDistance() < MAX_TAP_TRAVEL_DISTANCE;
        }

        private float travelDistance() {
            return (float)Math.hypot(x - startX, y - startY);
        }
    }

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        int actionIndex = event.getActionIndex();
        int pointerId = event.getPointerId(actionIndex);
        int actionMasked = event.getActionMasked();
        if (pointerId >= MAX_FINGERS) return true;
        if (actionMasked == MotionEvent.ACTION_DOWN && imeSession != null) {
            imeSession.commitPendingComposition(this);
            // Take focus back (e.g. after the side drawer) so a hardware keyboard keeps composing.
            if (!hasFocus()) requestFocus();
        }

        switch (actionMasked) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN:
                if (event.isFromSource(InputDevice.SOURCE_MOUSE)) return true;
                scrollAccumY = 0;
                scrolling = false;
                fingers[pointerId] = new Finger(event.getX(actionIndex), event.getY(actionIndex));
                numFingers++;
                break;
            case MotionEvent.ACTION_MOVE:
                if (event.isFromSource(InputDevice.SOURCE_MOUSE)) {
                    float[] transformedPoint = XForm.transformPoint(xform, event.getX(), event.getY());
                    if (isEnabled()) xServer.injectPointerMove((int)transformedPoint[0], (int)transformedPoint[1]);
                }
                else {
                    for (byte i = 0; i < MAX_FINGERS; i++) {
                        if (fingers[i] != null) {
                            int pointerIndex = event.findPointerIndex(i);
                            if (pointerIndex >= 0) {
                                fingers[i].update(event.getX(pointerIndex), event.getY(pointerIndex));
                                handleFingerMove(fingers[i]);
                            }
                            else {
                                handleFingerUp(fingers[i]);
                                fingers[i] = null;
                                numFingers--;
                            }
                        }
                    }
                }
                break;
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_POINTER_UP:
                if (fingers[pointerId] != null) {
                    fingers[pointerId].update(event.getX(actionIndex), event.getY(actionIndex));
                    handleFingerUp(fingers[pointerId]);
                    fingers[pointerId] = null;
                    numFingers--;
                }
                break;
            case MotionEvent.ACTION_CANCEL:
                for (byte i = 0; i < MAX_FINGERS; i++) fingers[i] = null;
                numFingers = 0;
                break;
        }

        return true;
    }

    private void handleFingerUp(Finger finger1) {
        switch (numFingers) {
            case 1:
                if (finger1.isTap()) {
                    if (moveCursorToTouchpoint) xServer.injectPointerMove(finger1.x, finger1.y);
                    pressPointerButtonLeft(finger1);
                }
                break;
            case 2:
                Finger finger2 = findSecondFinger(finger1);
                if (finger2 != null && finger1.isTap()) pressPointerButtonRight(finger1);
                break;
            case 4:
                if (fourFingersTapCallback != null) {
                    for (byte i = 0; i < 4; i++) {
                        if (fingers[i] != null && !fingers[i].isTap()) return;
                    }
                    fourFingersTapCallback.run();
                }
                break;
        }

        releasePointerButtonLeft(finger1);
        releasePointerButtonRight(finger1);
    }

    private void handleFingerMove(Finger finger1) {
        if (!isEnabled()) return;
        boolean skipPointerMove = false;

        Finger finger2 = numFingers == 2 ? findSecondFinger(finger1) : null;
        if (finger2 != null) {
            final float resolutionScale = 1000.0f / Math.min(xServer.screenInfo.width, xServer.screenInfo.height);
            float currDistance = (float)Math.hypot(finger1.x - finger2.x, finger1.y - finger2.y) * resolutionScale;

            if (currDistance < MAX_TWO_FINGERS_SCROLL_DISTANCE) {
                scrollAccumY += ((finger1.y + finger2.y) * 0.5f) - (finger1.lastY + finger2.lastY) * 0.5f;

                if (scrollAccumY < -100) {
                    xServer.injectPointerButtonPress(Pointer.Button.BUTTON_SCROLL_DOWN);
                    xServer.injectPointerButtonRelease(Pointer.Button.BUTTON_SCROLL_DOWN);
                    scrollAccumY = 0;
                }
                else if (scrollAccumY > 100) {
                    xServer.injectPointerButtonPress(Pointer.Button.BUTTON_SCROLL_UP);
                    xServer.injectPointerButtonRelease(Pointer.Button.BUTTON_SCROLL_UP);
                    scrollAccumY = 0;
                }
                scrolling = true;
            }
            else if (currDistance >= MAX_TWO_FINGERS_SCROLL_DISTANCE && !xServer.pointer.isButtonPressed(Pointer.Button.BUTTON_LEFT) &&
                     finger2.travelDistance() < MAX_TAP_TRAVEL_DISTANCE) {
                pressPointerButtonLeft(finger1);
                skipPointerMove = true;
            }
        }

        if (!scrolling && numFingers <= 2 && !skipPointerMove) {
            if (moveCursorToTouchpoint && numFingers == 1) {
                xServer.injectPointerMove(finger1.x, finger1.y);
            }
            else {
                int dx = finger1.deltaX();
                int dy = finger1.deltaY();

                WinHandler winHandler = xServer.getWinHandler();
                if (xServer.isRelativeMouseMovement()) {
                    winHandler.mouseEvent(MouseEventFlags.MOVE, dx, dy, 0);
                }
                else xServer.injectPointerMoveDelta(dx, dy);
            }
        }
    }

    public void mouseMove(float x, float y, int action) {
        switch (action) {
            case MotionEvent.ACTION_DOWN:
                mouseMoveFinger = new Finger(x, y);
                break;
            case MotionEvent.ACTION_MOVE:
                if (mouseMoveFinger != null) {
                    mouseMoveFinger.update(x, y);
                    handleFingerMove(mouseMoveFinger);
                }
                break;
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_CANCEL:
                mouseMoveFinger = null;
                break;
        }
    }

    private Finger findSecondFinger(Finger finger) {
        for (byte i = 0; i < MAX_FINGERS; i++) {
            if (fingers[i] != null && fingers[i] != finger) return fingers[i];
        }
        return null;
    }

    private void pressPointerButtonLeft(Finger finger) {
        if (isEnabled() && pointerButtonLeftEnabled && !xServer.pointer.isButtonPressed(Pointer.Button.BUTTON_LEFT)) {
            xServer.injectPointerButtonPress(Pointer.Button.BUTTON_LEFT);
            fingerPointerButtonLeft = finger;
        }
    }

    private void pressPointerButtonRight(Finger finger) {
        if (isEnabled() && pointerButtonRightEnabled && !xServer.pointer.isButtonPressed(Pointer.Button.BUTTON_RIGHT)) {
            xServer.injectPointerButtonPress(Pointer.Button.BUTTON_RIGHT);
            fingerPointerButtonRight = finger;
        }
    }

    private void releasePointerButtonLeft(final Finger finger) {
        if (isEnabled() && pointerButtonLeftEnabled && finger == fingerPointerButtonLeft && xServer.pointer.isButtonPressed(Pointer.Button.BUTTON_LEFT)) {
            postDelayed(() -> {
                xServer.injectPointerButtonRelease(Pointer.Button.BUTTON_LEFT);
                fingerPointerButtonLeft = null;
            }, 30);
        }
    }

    private void releasePointerButtonRight(final Finger finger) {
        if (isEnabled() && pointerButtonRightEnabled && finger == fingerPointerButtonRight && xServer.pointer.isButtonPressed(Pointer.Button.BUTTON_RIGHT)) {
            postDelayed(() -> {
                xServer.injectPointerButtonRelease(Pointer.Button.BUTTON_RIGHT);
                fingerPointerButtonRight = null;
            }, 30);
        }
    }

    public void setSensitivity(float sensitivity) {
        this.sensitivity = sensitivity;
    }

    public boolean isPointerButtonLeftEnabled() {
        return pointerButtonLeftEnabled;
    }

    public void setPointerButtonLeftEnabled(boolean pointerButtonLeftEnabled) {
        this.pointerButtonLeftEnabled = pointerButtonLeftEnabled;
    }

    public boolean isPointerButtonRightEnabled() {
        return pointerButtonRightEnabled;
    }

    public void setPointerButtonRightEnabled(boolean pointerButtonRightEnabled) {
        this.pointerButtonRightEnabled = pointerButtonRightEnabled;
    }

    public void setFourFingersTapCallback(Runnable fourFingersTapCallback) {
        this.fourFingersTapCallback = fourFingersTapCallback;
    }

    public boolean isMoveCursorToTouchpoint() {
        return moveCursorToTouchpoint;
    }

    public void setMoveCursorToTouchpoint(boolean moveCursorToTouchpoint) {
        this.moveCursorToTouchpoint = moveCursorToTouchpoint;
    }

    public boolean onExternalMouseEvent(MotionEvent event) {
        // Winlator S11: a click on an overlay control (power tab...) is Android's to deliver
        if (isEnabled() && event.isFromSource(InputDevice.SOURCE_MOUSE) && routeButtonToOverlay(event, false)) return false;
        return handleMouseEvent(event);
    }

    private boolean handleMouseEvent(MotionEvent event) {
        boolean handled = false;
        if (isEnabled() && event.isFromSource(InputDevice.SOURCE_MOUSE)) {
            int actionButton = event.getActionButton();
            switch (event.getAction()) {
                case MotionEvent.ACTION_BUTTON_PRESS:
                    if (actionButton == MotionEvent.BUTTON_PRIMARY) {
                        xServer.injectPointerButtonPress(Pointer.Button.BUTTON_LEFT);
                    }
                    else if (actionButton == MotionEvent.BUTTON_SECONDARY) {
                        xServer.injectPointerButtonPress(Pointer.Button.BUTTON_RIGHT);
                    }
                    handled = true;
                    break;
                case MotionEvent.ACTION_BUTTON_RELEASE:
                    if (actionButton == MotionEvent.BUTTON_PRIMARY) {
                        xServer.injectPointerButtonRelease(Pointer.Button.BUTTON_LEFT);
                    }
                    else if (actionButton == MotionEvent.BUTTON_SECONDARY) {
                        xServer.injectPointerButtonRelease(Pointer.Button.BUTTON_RIGHT);
                    }
                    handled = true;
                    break;
                case MotionEvent.ACTION_HOVER_MOVE:
                    float[] transformedPoint = XForm.transformPoint(xform, event.getX(), event.getY());
                    xServer.injectPointerMove((int)transformedPoint[0], (int)transformedPoint[1]);
                    updateHoveredOverlayTarget();
                    handled = true;
                    break;
                case MotionEvent.ACTION_SCROLL:
                    float scrollY = event.getAxisValue(MotionEvent.AXIS_VSCROLL);
                    if (scrollY <= -1.0f) {
                        xServer.injectPointerButtonPress(Pointer.Button.BUTTON_SCROLL_DOWN);
                        xServer.injectPointerButtonRelease(Pointer.Button.BUTTON_SCROLL_DOWN);
                    }
                    else if (scrollY >= 1.0f) {
                        xServer.injectPointerButtonPress(Pointer.Button.BUTTON_SCROLL_UP);
                        xServer.injectPointerButtonRelease(Pointer.Button.BUTTON_SCROLL_UP);
                    }
                    handled = true;
                    break;
            }
        }
        return handled;
    }

    public float[] computeDeltaPoint(float lastX, float lastY, float x, float y) {
        final float[] result = {0, 0};

        XForm.transformPoint(xform, lastX, lastY, result);
        lastX = result[0];
        lastY = result[1];

        XForm.transformPoint(xform, x, y, result);
        x = result[0];
        y = result[1];

        result[0] = x - lastX;
        result[1] = y - lastY;
        return result;
    }

    @Override
    public boolean onCapturedPointer(View view, MotionEvent event) {
        if (event.getAction() == MotionEvent.ACTION_MOVE) {
            float dx = event.getX() * sensitivity;
            if (Math.abs(dx) > CURSOR_ACCELERATION_THRESHOLD) dx *= CURSOR_ACCELERATION;

            float dy = event.getY() * sensitivity;
            if (Math.abs(dy) > CURSOR_ACCELERATION_THRESHOLD) dy *= CURSOR_ACCELERATION;

            xServer.injectPointerMoveDelta(Mathf.roundPoint(dx), Mathf.roundPoint(dy));
            updateHoveredOverlayTarget();
            return true;
        }
        else {
            if (routeButtonToOverlay(event, true)) return true;
            if (event.getActionMasked() == MotionEvent.ACTION_BUTTON_PRESS && imeSession != null) imeSession.commitPendingComposition(this);
            event.setSource(event.getSource() | InputDevice.SOURCE_MOUSE);
            return handleMouseEvent(event);
        }
    }
}
