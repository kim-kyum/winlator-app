package com.winlator.widget;

import android.content.Context;
import android.util.AttributeSet;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;

import androidx.drawerlayout.widget.DrawerLayout;

import java.util.ArrayList;

/**
 * Winlator S11: a DrawerLayout whose drawer can be pinned beside the content, as One UI's side
 * pane on a wide screen. Pinned, the drawer is locked open, the content (beside it by its left
 * padding) gets its touches, keys, focus and accessibility as when no drawer is open;
 * DrawerLayout itself would take every touch on the content to close the drawer.
 */
public class S11DrawerLayout extends DrawerLayout {
    private boolean pinned = false;

    public S11DrawerLayout(Context context) {
        super(context);
    }

    public S11DrawerLayout(Context context, AttributeSet attrs) {
        super(context, attrs);
    }

    public S11DrawerLayout(Context context, AttributeSet attrs, int defStyle) {
        super(context, attrs, defStyle);
    }

    public void setPinned(boolean pinned) {
        this.pinned = pinned;
    }

    public boolean isPinned() {
        return pinned;
    }

    @Override
    public boolean onInterceptTouchEvent(MotionEvent ev) {
        return !pinned && super.onInterceptTouchEvent(ev);
    }

    @Override
    public boolean onTouchEvent(MotionEvent ev) {
        return !pinned && super.onTouchEvent(ev);
    }

    @Override
    public boolean onKeyDown(int keyCode, KeyEvent event) {
        if (pinned && keyCode == KeyEvent.KEYCODE_BACK) return false;
        return super.onKeyDown(keyCode, event);
    }

    @Override
    public boolean onKeyUp(int keyCode, KeyEvent event) {
        if (pinned && keyCode == KeyEvent.KEYCODE_BACK) return false;
        return super.onKeyUp(keyCode, event);
    }

    /** DrawerLayout offers only an open drawer's views to focus; pinned, every child's */
    @Override
    public void addFocusables(ArrayList<View> views, int direction, int focusableMode) {
        if (!pinned) {
            super.addFocusables(views, direction, focusableMode);
            return;
        }
        for (int i = 0; i < getChildCount(); i++) {
            View child = getChildAt(i);
            if (child.getVisibility() == VISIBLE) child.addFocusables(views, direction, focusableMode);
        }
    }
}
