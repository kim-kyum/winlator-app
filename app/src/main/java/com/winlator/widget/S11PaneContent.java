package com.winlator.widget;

import android.content.Context;
import android.util.AttributeSet;
import android.widget.LinearLayout;

/**
 * Winlator S11: the content beside S11DrawerLayout's pinned drawer. DrawerLayout hides the
 * content from accessibility services (and UI tests) while a drawer is open; beside a pinned
 * one it stays.
 */
public class S11PaneContent extends LinearLayout {
    private boolean pinned = false;

    public S11PaneContent(Context context) {
        super(context);
    }

    public S11PaneContent(Context context, AttributeSet attrs) {
        super(context, attrs);
    }

    public void setPinned(boolean pinned) {
        this.pinned = pinned;
        if (pinned) super.setImportantForAccessibility(IMPORTANT_FOR_ACCESSIBILITY_AUTO);
    }

    @Override
    public void setImportantForAccessibility(int mode) {
        if (pinned && mode == IMPORTANT_FOR_ACCESSIBILITY_NO_HIDE_DESCENDANTS) mode = IMPORTANT_FOR_ACCESSIBILITY_AUTO;
        super.setImportantForAccessibility(mode);
    }
}
