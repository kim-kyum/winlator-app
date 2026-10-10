package com.winlator.s11;

import android.content.Context;
import android.graphics.Color;
import android.graphics.drawable.GradientDrawable;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.widget.FrameLayout;
import android.widget.TextView;

/**
 * Shows the syllable the keyboard is still composing. Wine has no idea about Android
 * composition, so the text only appears in the Windows program once it is committed;
 * this pill at the top of the screen fills that gap.
 */
public class ComposingOverlay extends TextView {
    public ComposingOverlay(Context context) {
        super(context);
        float density = context.getResources().getDisplayMetrics().density;

        GradientDrawable background = new GradientDrawable();
        background.setColor(0xE6202124);
        background.setCornerRadius(14 * density);
        background.setStroke((int)(1 * density), 0x33FFFFFF);
        setBackground(background);

        setTextColor(Color.WHITE);
        setTextSize(TypedValue.COMPLEX_UNIT_SP, 24);
        int padH = (int)(18 * density), padV = (int)(8 * density);
        setPadding(padH, padV, padH, padV);
        setElevation(6 * density);
        setClickable(false);
        setFocusable(false);
        setVisibility(View.GONE);

        FrameLayout.LayoutParams params = new FrameLayout.LayoutParams(
            FrameLayout.LayoutParams.WRAP_CONTENT, FrameLayout.LayoutParams.WRAP_CONTENT,
            Gravity.TOP | Gravity.CENTER_HORIZONTAL);
        params.topMargin = (int)(20 * density);
        setLayoutParams(params);
    }

    public void show(CharSequence text) {
        if (text == null || text.length() == 0) {
            hide();
            return;
        }
        setText(text);
        if (getVisibility() != View.VISIBLE) setVisibility(View.VISIBLE);
    }

    public void hide() {
        if (getVisibility() != View.GONE) setVisibility(View.GONE);
        setText("");
    }
}
