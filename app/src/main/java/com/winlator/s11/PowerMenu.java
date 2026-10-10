package com.winlator.s11;

import android.content.Context;
import android.view.KeyEvent;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.TextView;

import com.winlator.R;

/**
 * Windows-style power menu for the running container, opened from the power tab at the top
 * of the screen (or Ctrl+Alt+Del / Ctrl+Alt+Backspace): shut down back to the container list,
 * restart, force quit, or open Winlator's side menu. Works with touch, mouse and keyboard.
 *
 * The activity decides what each action does; this class draws the tab, the menu and the
 * "closing programs" card shown while Wine ends its session.
 */
public class PowerMenu {
    public interface Listener {
        /** The power tab at the top of the screen was clicked. */
        void onTabClicked();
        /** Ask programs to close (they may offer to save), then leave the container. */
        void onShutdown();
        /** Same as shutdown, then start this container again. */
        void onRestart();
        /** Stop everything at once; restart: start this container again afterwards. */
        void onForceQuit(boolean restart);
        /** The user gave up waiting for programs to close (the session goes on). */
        void onClosingCancelled();
        void onOpenAppMenu();
        /** restoreInput: hand keyboard and mouse back to Wine. */
        void onHidden(boolean restoreInput);
    }

    /** Programs normally close within a couple of seconds; after this, offer "anyway" and "cancel". */
    private static final long STATUS_ACTIONS_DELAY_MS = 6000;
    private static final long STATUS_TIMEOUT_MS = 120000;

    private final Listener listener;
    private final View tab;
    private final View root;
    private final View firstItem;
    private final View status;
    private final TextView statusTitle;
    private final View statusActions;
    private final View statusCancel;
    private final TextView statusForce;
    private final Runnable showStatusActions;
    private final Runnable statusTimeout;
    private boolean statusRestart;
    private int topInset = -1;

    public PowerMenu(Context context, FrameLayout parent, String containerName, Listener listener) {
        this.listener = listener;
        LayoutInflater inflater = LayoutInflater.from(context);

        // Order matters: the menu's dim backdrop covers the tab, the status card sits on top
        tab = inflater.inflate(R.layout.s11_power_tab, parent, false);
        parent.addView(tab);
        tab.setOnClickListener(v -> listener.onTabClicked());
        tab.setTooltipText(context.getString(R.string.s11_power_tab_tip));

        root = inflater.inflate(R.layout.s11_power_menu, parent, false);
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
        setupItem(R.id.S11PowerForce, R.drawable.s11_ic_force, R.string.s11_power_force, R.string.s11_power_force_desc, () -> listener.onForceQuit(false));
        setupItem(R.id.S11PowerAppMenu, R.drawable.s11_ic_menu, R.string.s11_power_app_menu, R.string.s11_power_app_menu_desc, () -> {
            hide(false);
            listener.onOpenAppMenu();
        });
        root.findViewById(R.id.S11PowerCancel).setOnClickListener(v -> hide(true));

        status = inflater.inflate(R.layout.s11_power_status, parent, false);
        parent.addView(status);
        statusTitle = status.findViewById(R.id.S11PowerStatusTitle);
        statusActions = status.findViewById(R.id.S11PowerStatusActions);
        statusCancel = status.findViewById(R.id.S11PowerStatusCancel);
        statusForce = status.findViewById(R.id.S11PowerStatusForce);
        statusCancel.setOnClickListener(v -> {
            hideClosingStatus();
            listener.onClosingCancelled();
        });
        statusForce.setOnClickListener(v -> {
            hideClosingStatus();
            listener.onForceQuit(statusRestart);
        });
        showStatusActions = () -> statusActions.setVisibility(View.VISIBLE);
        statusTimeout = () -> {
            if (status.getVisibility() != View.VISIBLE) return;
            hideClosingStatus();
            listener.onClosingCancelled();
        };
    }

    private View setupItem(int id, int icon, int title, int description, Runnable action) {
        View item = root.findViewById(id);
        ((ImageView)item.findViewById(R.id.S11PowerItemIcon)).setImageResource(icon);
        ((TextView)item.findViewById(R.id.S11PowerItemTitle)).setText(title);
        ((TextView)item.findViewById(R.id.S11PowerItemDesc)).setText(description);
        item.setOnClickListener(v -> action.run());
        return item;
    }

    /**
     * Controls a captured mouse must be able to click. A captured mouse only talks to the
     * touchpad view, which routes clicks over these to them instead of to Wine.
     */
    public View[] getPointerTargets() {
        return new View[]{tab, statusCancel, statusForce};
    }

    /** Keeps the tab and the status card on the picture when it doesn't reach the top edge. */
    public void setTopInset(int inset) {
        if (inset == topInset) return;
        int delta = inset - Math.max(topInset, 0);
        topInset = inset;
        ((ViewGroup.MarginLayoutParams)tab.getLayoutParams()).topMargin = inset;
        ((ViewGroup.MarginLayoutParams)status.getLayoutParams()).topMargin += delta;
        tab.requestLayout();
        status.requestLayout();
    }

    /** Hidden in picture-in-picture. */
    public void setTabVisible(boolean visible) {
        tab.setVisibility(visible ? View.VISIBLE : View.GONE);
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

    /** Card at the top while Wine asks programs to close; offers "anyway"/"cancel" if it takes long. */
    public void showClosingStatus(boolean restart) {
        statusRestart = restart;
        statusTitle.setText(restart ? R.string.s11_power_restarting : R.string.s11_power_closing);
        statusForce.setText(restart ? R.string.s11_power_restart_anyway : R.string.s11_power_shutdown_anyway);
        statusActions.setVisibility(View.GONE);
        status.setVisibility(View.VISIBLE);

        status.removeCallbacks(showStatusActions);
        status.removeCallbacks(statusTimeout);
        status.postDelayed(showStatusActions, STATUS_ACTIONS_DELAY_MS);
        status.postDelayed(statusTimeout, STATUS_TIMEOUT_MS);
    }

    public void hideClosingStatus() {
        status.removeCallbacks(showStatusActions);
        status.removeCallbacks(statusTimeout);
        status.setVisibility(View.GONE);
    }
}
