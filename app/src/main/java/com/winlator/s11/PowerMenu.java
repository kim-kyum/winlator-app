package com.winlator.s11;

import android.content.Context;
import android.graphics.Color;
import android.graphics.drawable.GradientDrawable;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.LayoutInflater;
import android.view.View;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.TextView;

import com.winlator.R;

/**
 * Windows-style power menu for the running container (Ctrl+Alt+Del, or Alt+F4 on the
 * desktop): shut down back to the container list, restart, force quit, or open Winlator's
 * side menu. Works with keyboard only (arrows/Tab, Enter, Esc) and with a mouse.
 *
 * The activity decides what each action does; this class only draws the menu and the
 * "closing programs" status shown while Wine ends its session.
 */
public class PowerMenu {
    public interface Listener {
        /** Ask programs to close (they may offer to save), then leave the container. */
        void onShutdown();
        /** Same as shutdown, then start this container again. */
        void onRestart();
        /** Stop everything at once. */
        void onForceQuit();
        void onOpenAppMenu();
        /** restoreInput: hand keyboard and mouse back to Wine. */
        void onHidden(boolean restoreInput);
    }

    private static final long STATUS_TIMEOUT_MS = 15000;

    private final Context context;
    private final Listener listener;
    private final View root;
    private final View firstItem;
    private final TextView status;
    private final Runnable hideStatus;

    public PowerMenu(Context context, FrameLayout parent, String containerName, Listener listener) {
        this.context = context;
        this.listener = listener;

        root = LayoutInflater.from(context).inflate(R.layout.s11_power_menu, parent, false);
        parent.addView(root);
        root.setOnClickListener(v -> hide(true));

        TextView title = root.findViewById(R.id.S11PowerTitle);
        if (containerName != null && !containerName.isEmpty()) title.setText(containerName);

        firstItem = setupItem(R.id.S11PowerShutdown, R.drawable.s11_ic_power, R.string.s11_power_shutdown, R.string.s11_power_shutdown_desc, () -> {
            hide(true);
            listener.onShutdown();
        });
        setupItem(R.id.S11PowerRestart, R.drawable.s11_ic_restart, R.string.s11_power_restart, R.string.s11_power_restart_desc, () -> {
            hide(true);
            listener.onRestart();
        });
        setupItem(R.id.S11PowerForce, R.drawable.s11_ic_force, R.string.s11_power_force, R.string.s11_power_force_desc, listener::onForceQuit);
        setupItem(R.id.S11PowerAppMenu, R.drawable.s11_ic_menu, R.string.s11_power_app_menu, R.string.s11_power_app_menu_desc, () -> {
            hide(false);
            listener.onOpenAppMenu();
        });
        root.findViewById(R.id.S11PowerCancel).setOnClickListener(v -> hide(true));

        status = createStatusView(context);
        parent.addView(status);
        hideStatus = () -> status.setVisibility(View.GONE);
    }

    private View setupItem(int id, int icon, int title, int description, Runnable action) {
        View item = root.findViewById(id);
        ((ImageView)item.findViewById(R.id.S11PowerItemIcon)).setImageResource(icon);
        ((TextView)item.findViewById(R.id.S11PowerItemTitle)).setText(title);
        ((TextView)item.findViewById(R.id.S11PowerItemDesc)).setText(description);
        item.setOnClickListener(v -> action.run());
        return item;
    }

    private static TextView createStatusView(Context context) {
        float density = context.getResources().getDisplayMetrics().density;
        TextView view = new TextView(context);

        GradientDrawable background = new GradientDrawable();
        background.setColor(0xE6202124);
        background.setCornerRadius(14 * density);
        view.setBackground(background);

        view.setTextColor(Color.WHITE);
        view.setTextSize(TypedValue.COMPLEX_UNIT_SP, 14);
        view.setGravity(Gravity.CENTER);
        int padH = (int)(20 * density), padV = (int)(10 * density);
        view.setPadding(padH, padV, padH, padV);
        view.setElevation(8 * density);
        view.setClickable(false);
        view.setFocusable(false);
        view.setVisibility(View.GONE);

        FrameLayout.LayoutParams params = new FrameLayout.LayoutParams(
            FrameLayout.LayoutParams.WRAP_CONTENT, FrameLayout.LayoutParams.WRAP_CONTENT,
            Gravity.TOP | Gravity.CENTER_HORIZONTAL);
        params.topMargin = (int)(24 * density);
        view.setLayoutParams(params);
        return view;
    }

    public boolean isShowing() {
        return root.getVisibility() == View.VISIBLE;
    }

    public void show() {
        if (isShowing()) return;
        root.setAlpha(0f);
        root.setVisibility(View.VISIBLE);
        root.animate().alpha(1f).setDuration(120).start();
        // Leaves touch mode so the focus ring shows and arrow keys work right away
        firstItem.requestFocusFromTouch();
    }

    public void hide(boolean restoreInput) {
        if (!isShowing()) return;
        root.animate().cancel();
        root.setVisibility(View.GONE);
        listener.onHidden(restoreInput);
    }

    /** Keys while the menu is open: Esc and Back cancel; the rest move focus and activate. */
    public boolean onKeyEvent(KeyEvent event) {
        int keyCode = event.getKeyCode();
        if (keyCode == KeyEvent.KEYCODE_ESCAPE || keyCode == KeyEvent.KEYCODE_BACK) {
            if (event.getAction() == KeyEvent.ACTION_UP) hide(true);
            return true;
        }
        return false;
    }

    /** Small non-blocking note at the top while Wine asks programs to close. */
    public void showClosingStatus(boolean restart) {
        status.setText(context.getString(restart ? R.string.s11_power_restarting : R.string.s11_power_closing)
            + "\n" + context.getString(R.string.s11_power_closing_hint));
        status.setVisibility(View.VISIBLE);
        status.removeCallbacks(hideStatus);
        status.postDelayed(hideStatus, STATUS_TIMEOUT_MS);
    }
}
