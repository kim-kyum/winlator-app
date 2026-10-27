package com.winlator.s11;

import android.Manifest;
import android.app.Activity;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothClass;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothProfile;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.PackageManager;
import android.location.LocationManager;
import android.net.Uri;
import android.net.wifi.ScanResult;
import android.net.wifi.SupplicantState;
import android.net.wifi.WifiConfiguration;
import android.net.wifi.WifiInfo;
import android.net.wifi.WifiManager;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.provider.Settings;

import com.winlator.R;
import com.winlator.core.FileUtils;
import com.winlator.xenvironment.RootFS;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.lang.reflect.Method;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Set;

/**
 * Wi-Fi and Bluetooth for the Connectivity page of the S11 Settings app (s11settings.c).
 *
 * The app targets API 28, so it may still do what newer targets lost: switch Wi-Fi and
 * Bluetooth on and off, and join a network with WifiManager's own calls. Nearby networks need
 * the location permission and location services, as for any app; the page asks for them.
 * Pairing new Bluetooth devices and work networks (802.1X) stay in Android's settings.
 *
 * Like S11ContainerSettings it writes the page's rows to ~/.config/s11-connectivity.txt
 * whenever something changes (Android's broadcasts), and takes commands as "key TAB value"
 * (winhandler code 109; keys starting with "net."):
 *   S11CONNECTIVITY  1  revision
 *   ROW  connectivity  type  key  title  detail  value
 *        type: section; toggle (value 1/0); action (value: the button's label);
 *        network (key "net.connect SSID", value open/secured/saved/enterprise, or current
 *        for the connected one); info (no control)
 */
public final class S11Connectivity {
    public static final String FILE_NAME = "s11-connectivity.txt";
    public static final int PERMISSION_REQUEST_CODE = 4711;
    private static final int MAX_NETWORKS = 20;
    private static final long CONNECT_TIMEOUT_MS = 25000;

    private final Activity activity;
    private final File file;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private final WifiManager wifi;
    private final BluetoothAdapter bluetooth;
    private final Set<String> aclConnected = new HashSet<>();
    private final HashMap<Integer, BluetoothProfile> profiles = new HashMap<>();
    private int revision = (int)(System.currentTimeMillis() / 1000 % 1000000);
    private String written = "";
    private boolean started, scanning, permissionAsked;
    private long lastScan;
    // the network being joined and what became of it
    private String joining;
    private String status = "";

    private final Runnable writer = this::write;
    private final Runnable connectTimeout = this::connectTimedOut;
    // a scan that never reports back must not say "searching" forever
    private final Runnable scanTimeout = () -> {
        scanning = false;
        scheduleWrite();
    };
    private final BroadcastReceiver receiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            onBroadcast(intent);
        }
    };

    public S11Connectivity(Activity activity) {
        this.activity = activity;
        this.file = new File(RootFS.find(activity).getRootDir(), RootFS.USER_CONFIG_PATH + "/" + FILE_NAME);
        this.wifi = (WifiManager)activity.getApplicationContext().getSystemService(Context.WIFI_SERVICE);
        BluetoothAdapter adapter = null;
        try {
            adapter = BluetoothAdapter.getDefaultAdapter();
        }
        catch (Exception e) {}
        this.bluetooth = adapter;
    }

    private void connectTimedOut() {
        if (joining != null && !joining.equals(currentSsid())) {
            status = activity.getString(R.string.s11_net_connect_failed, joining);
            joining = null;
            scheduleWrite();
        }
    }

    private void onBroadcast(Intent intent) {
        String action = intent.getAction();
        if (action == null) return;
        switch (action) {
            case WifiManager.SCAN_RESULTS_AVAILABLE_ACTION:
                scanning = false;
                handler.removeCallbacks(scanTimeout);
                break;
            case WifiManager.SUPPLICANT_STATE_CHANGED_ACTION:
                if (joining != null && intent.getIntExtra(WifiManager.EXTRA_SUPPLICANT_ERROR, 0) == WifiManager.ERROR_AUTHENTICATING) {
                    status = activity.getString(R.string.s11_net_wrong_password, joining);
                    joining = null;
                }
                break;
            case WifiManager.NETWORK_STATE_CHANGED_ACTION:
                if (joining != null && joining.equals(currentSsid())) {
                    joining = null;
                    status = "";
                }
                break;
            case BluetoothDevice.ACTION_ACL_CONNECTED:
            case BluetoothDevice.ACTION_ACL_DISCONNECTED: {
                BluetoothDevice device = intent.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE);
                if (device != null) {
                    if (action.equals(BluetoothDevice.ACTION_ACL_CONNECTED)) aclConnected.add(device.getAddress());
                    else aclConnected.remove(device.getAddress());
                }
                break;
            }
        }
        scheduleWrite();
    }

    /** Starts following Wi-Fi and Bluetooth and writes the page once. */
    public void start() {
        handler.post(() -> {
            if (started) return;
            started = true;
            IntentFilter filter = new IntentFilter();
            filter.addAction(WifiManager.WIFI_STATE_CHANGED_ACTION);
            filter.addAction(WifiManager.NETWORK_STATE_CHANGED_ACTION);
            filter.addAction(WifiManager.SCAN_RESULTS_AVAILABLE_ACTION);
            filter.addAction(WifiManager.SUPPLICANT_STATE_CHANGED_ACTION);
            filter.addAction(LocationManager.PROVIDERS_CHANGED_ACTION);
            filter.addAction(BluetoothAdapter.ACTION_STATE_CHANGED);
            filter.addAction(BluetoothDevice.ACTION_ACL_CONNECTED);
            filter.addAction(BluetoothDevice.ACTION_ACL_DISCONNECTED);
            filter.addAction(BluetoothDevice.ACTION_BOND_STATE_CHANGED);
            filter.addAction(BluetoothDevice.ACTION_NAME_CHANGED);
            try {
                activity.registerReceiver(receiver, filter);
            }
            catch (Exception e) {}
            connectProfile(BluetoothProfile.A2DP);
            connectProfile(BluetoothProfile.HEADSET);
            write();
        });
    }

    public void stop() {
        handler.post(() -> {
            if (!started) return;
            started = false;
            try {
                activity.unregisterReceiver(receiver);
            }
            catch (Exception e) {}
            if (bluetooth != null) {
                for (Integer profile : profiles.keySet()) {
                    try {
                        bluetooth.closeProfileProxy(profile, profiles.get(profile));
                    }
                    catch (Exception e) {}
                }
            }
            profiles.clear();
            handler.removeCallbacksAndMessages(null);
        });
    }

    private void connectProfile(int profile) {
        if (bluetooth == null) return;
        try {
            bluetooth.getProfileProxy(activity, new BluetoothProfile.ServiceListener() {
                @Override
                public void onServiceConnected(int which, BluetoothProfile proxy) {
                    profiles.put(which, proxy);
                    scheduleWrite();
                }

                @Override
                public void onServiceDisconnected(int which) {
                    profiles.remove(which);
                }
            }, profile);
        }
        catch (Exception e) {}
    }

    // ------------------------------------------------------------------------------------
    // Commands from the Settings app
    // ------------------------------------------------------------------------------------

    public static boolean isCommand(String key) {
        return key.startsWith("net.");
    }

    public void apply(String key, String value) {
        handler.post(() -> {
            try {
                applyNow(key, value);
            }
            catch (RuntimeException e) {
                e.printStackTrace();
            }
            write();
        });
    }

    @SuppressWarnings("deprecation")
    private void applyNow(String key, String value) {
        switch (key) {
            case "net.refresh":
                if (wifi != null && wifi.isWifiEnabled() && canScan() && SystemClock.elapsedRealtime() - lastScan > 20000) startScan();
                return;
            case "net.wifi":
                status = "";
                if (wifi != null) wifi.setWifiEnabled(value.equals("1"));
                return;
            case "net.scan":
                if (canScan()) startScan();
                return;
            case "net.permission":
                requestLocationPermission();
                return;
            case "net.appsettings":
                open(new Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS, Uri.parse("package:" + activity.getPackageName())));
                return;
            case "net.location":
                open(new Intent(Settings.ACTION_LOCATION_SOURCE_SETTINGS));
                return;
            case "net.disconnect":
                if (wifi != null) wifi.disconnect();
                return;
            case "net.bluetooth":
                if (bluetooth != null) {
                    if (value.equals("1")) bluetooth.enable();
                    else bluetooth.disable();
                }
                return;
            case "net.open.wifi":
                open(new Intent(Settings.ACTION_WIFI_SETTINGS));
                return;
            case "net.open.bluetooth":
                open(new Intent(Settings.ACTION_BLUETOOTH_SETTINGS));
                return;
        }
        if (key.startsWith("net.connect ")) connect(key.substring("net.connect ".length()), value);
    }

    private void open(Intent intent) {
        try {
            activity.startActivity(intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
        }
        catch (Exception e) {
            try {
                activity.startActivity(new Intent(Settings.ACTION_SETTINGS).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
            }
            catch (Exception ignored) {}
        }
    }

    @SuppressWarnings("deprecation")
    private void startScan() {
        if (wifi == null) return;
        lastScan = SystemClock.elapsedRealtime();
        scanning = wifi.startScan();   // false when Android throttles scans (4 in 2 minutes)
        handler.removeCallbacks(scanTimeout);
        if (scanning) handler.postDelayed(scanTimeout, 15000);
    }

    private boolean hasLocationPermission() {
        return activity.checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION) == PackageManager.PERMISSION_GRANTED ||
               activity.checkSelfPermission(Manifest.permission.ACCESS_COARSE_LOCATION) == PackageManager.PERMISSION_GRANTED;
    }

    private boolean isLocationEnabled() {
        LocationManager manager = (LocationManager)activity.getSystemService(Context.LOCATION_SERVICE);
        if (manager == null) return true;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) return manager.isLocationEnabled();
        return manager.isProviderEnabled(LocationManager.NETWORK_PROVIDER) || manager.isProviderEnabled(LocationManager.GPS_PROVIDER);
    }

    private boolean canScan() {
        return hasLocationPermission() && isLocationEnabled();
    }

    private void requestLocationPermission() {
        if (hasLocationPermission()) return;
        permissionAsked = true;
        activity.requestPermissions(new String[]{Manifest.permission.ACCESS_FINE_LOCATION, Manifest.permission.ACCESS_COARSE_LOCATION},
                                    PERMISSION_REQUEST_CODE);
    }

    /** From the activity's onRequestPermissionsResult */
    public void onPermissionResult() {
        handler.post(() -> {
            if (canScan()) startScan();
            write();
        });
    }

    @SuppressWarnings("deprecation")
    private void connect(String ssid, String password) {
        if (wifi == null || ssid.isEmpty()) return;
        String capabilities = "";
        if (hasLocationPermission()) {
            for (ScanResult result : wifi.getScanResults()) {
                if (ssid.equals(result.SSID)) {
                    capabilities = result.capabilities != null ? result.capabilities : "";
                    break;
                }
            }
        }
        if (capabilities.contains("EAP")) {
            status = activity.getString(R.string.s11_net_enterprise_hint);
            open(new Intent(Settings.ACTION_WIFI_SETTINGS));
            return;
        }

        WifiConfiguration saved = findSaved(ssid);
        int networkId;
        if (saved != null && password.isEmpty()) networkId = saved.networkId;
        else {
            WifiConfiguration config = new WifiConfiguration();
            config.SSID = quote(ssid);
            if (capabilities.contains("WEP")) {
                config.allowedKeyManagement.set(WifiConfiguration.KeyMgmt.NONE);
                config.allowedAuthAlgorithms.set(WifiConfiguration.AuthAlgorithm.OPEN);
                config.allowedAuthAlgorithms.set(WifiConfiguration.AuthAlgorithm.SHARED);
                config.wepKeys[0] = quote(password);
                config.wepTxKeyIndex = 0;
            }
            else if (capabilities.contains("SAE") && !capabilities.contains("PSK") && Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                config.allowedKeyManagement.set(WifiConfiguration.KeyMgmt.SAE);
                config.preSharedKey = quote(password);
            }
            else if (!password.isEmpty()) config.preSharedKey = quote(password);
            else config.allowedKeyManagement.set(WifiConfiguration.KeyMgmt.NONE);

            if (saved != null) {
                config.networkId = saved.networkId;
                networkId = wifi.updateNetwork(config);
            }
            else networkId = wifi.addNetwork(config);
        }
        if (networkId == -1) {
            status = activity.getString(R.string.s11_net_connect_failed, ssid);
            return;
        }
        wifi.disconnect();
        wifi.enableNetwork(networkId, true);
        wifi.reconnect();
        joining = ssid;
        status = activity.getString(R.string.s11_net_connecting, ssid);
        handler.removeCallbacks(connectTimeout);
        handler.postDelayed(connectTimeout, CONNECT_TIMEOUT_MS);
    }

    @SuppressWarnings("deprecation")
    private WifiConfiguration findSaved(String ssid) {
        if (wifi == null || !hasLocationPermission()) return null;
        try {
            List<WifiConfiguration> configs = wifi.getConfiguredNetworks();
            if (configs == null) return null;
            for (WifiConfiguration config : configs) if (quote(ssid).equals(config.SSID)) return config;
        }
        catch (SecurityException e) {}
        return null;
    }

    private static String quote(String text) {
        return "\"" + text + "\"";
    }

    @SuppressWarnings("deprecation")
    private String currentSsid() {
        if (wifi == null) return null;
        WifiInfo info = wifi.getConnectionInfo();
        if (info == null || info.getNetworkId() == -1 || info.getSupplicantState() != SupplicantState.COMPLETED) return null;
        String ssid = info.getSSID();
        if (ssid == null || ssid.equals("<unknown ssid>")) return "";
        if (ssid.length() >= 2 && ssid.startsWith("\"") && ssid.endsWith("\"")) ssid = ssid.substring(1, ssid.length() - 1);
        return ssid;
    }

    // ------------------------------------------------------------------------------------
    // The page
    // ------------------------------------------------------------------------------------

    private void scheduleWrite() {
        handler.removeCallbacks(writer);
        handler.postDelayed(writer, 250);
    }

    private static String clean(String text) {
        return text == null ? "" : text.replace('\t', ' ').replace('\n', ' ').replace('\r', ' ');
    }

    private static void row(StringBuilder out, String type, String key, String title, String detail, String value) {
        out.append("ROW\tconnectivity\t").append(type).append('\t').append(clean(key)).append('\t').append(clean(title))
           .append('\t').append(clean(detail)).append('\t').append(clean(value)).append('\n');
    }

    private String signal(int rssi) {
        @SuppressWarnings("deprecation")
        int level = WifiManager.calculateSignalLevel(rssi, 4);
        int[] names = {R.string.s11_net_signal_0, R.string.s11_net_signal_1, R.string.s11_net_signal_2, R.string.s11_net_signal_3};
        return activity.getString(names[Math.max(0, Math.min(3, level))]);
    }

    private static String ipAddress(int ip) {
        return String.format(Locale.ENGLISH, "%d.%d.%d.%d", ip & 0xff, (ip >> 8) & 0xff, (ip >> 16) & 0xff, (ip >> 24) & 0xff);
    }

    @SuppressWarnings("deprecation")
    private void writeWifi(StringBuilder out) {
        row(out, "section", "net.wifisection", activity.getString(R.string.s11_net_wifi), "", "");
        if (wifi == null) {
            row(out, "info", "net.nowifi", activity.getString(R.string.s11_net_wifi), activity.getString(R.string.s11_net_unavailable), "");
            return;
        }
        int state = wifi.getWifiState();
        boolean on = state == WifiManager.WIFI_STATE_ENABLED || state == WifiManager.WIFI_STATE_ENABLING;
        String ssid = state == WifiManager.WIFI_STATE_ENABLED ? currentSsid() : null;
        String detail;
        if (state == WifiManager.WIFI_STATE_ENABLING) detail = activity.getString(R.string.s11_net_turning_on);
        else if (state == WifiManager.WIFI_STATE_DISABLING) detail = activity.getString(R.string.s11_net_turning_off);
        else if (!on) detail = activity.getString(R.string.s11_net_state_off);
        else if (ssid != null && !ssid.isEmpty()) detail = activity.getString(R.string.s11_net_connected_to, ssid);
        else detail = activity.getString(R.string.s11_net_state_on);
        row(out, "toggle", "net.wifi", activity.getString(R.string.s11_net_wifi), detail, on ? "1" : "0");
        if (state != WifiManager.WIFI_STATE_ENABLED) return;

        if (ssid != null) {
            WifiInfo info = wifi.getConnectionInfo();
            StringBuilder text = new StringBuilder(activity.getString(R.string.s11_net_connected));
            text.append(" · ").append(signal(info.getRssi()));
            if (info.getIpAddress() != 0) text.append(" · ").append(activity.getString(R.string.s11_net_ip, ipAddress(info.getIpAddress())));
            row(out, "network", "net.disconnect", ssid.isEmpty() ? activity.getString(R.string.s11_net_current_unknown) : ssid,
                text.toString(), "current");
        }
        if (!status.isEmpty()) row(out, "info", "net.status", status, "", "");

        if (!hasLocationPermission()) {
            boolean blocked = permissionAsked && !activity.shouldShowRequestPermissionRationale(Manifest.permission.ACCESS_FINE_LOCATION);
            if (blocked) row(out, "action", "net.appsettings", activity.getString(R.string.s11_net_nearby),
                             activity.getString(R.string.s11_net_permission_denied_desc), activity.getString(R.string.s11_net_app_settings));
            else row(out, "action", "net.permission", activity.getString(R.string.s11_net_nearby),
                     activity.getString(R.string.s11_net_permission_desc), activity.getString(R.string.s11_net_permission_button));
            return;
        }
        if (!isLocationEnabled()) {
            row(out, "action", "net.location", activity.getString(R.string.s11_net_nearby),
                activity.getString(R.string.s11_net_location_desc), activity.getString(R.string.s11_net_location_button));
            return;
        }

        // the strongest of each name, strongest first
        HashMap<String, ScanResult> best = new HashMap<>();
        for (ScanResult result : wifi.getScanResults()) {
            if (result.SSID == null || result.SSID.isEmpty() || result.SSID.equals(ssid)) continue;
            ScanResult other = best.get(result.SSID);
            if (other == null || result.level > other.level) best.put(result.SSID, result);
        }
        ArrayList<ScanResult> results = new ArrayList<>(best.values());
        results.sort((a, b) -> Integer.compare(b.level, a.level));
        Set<String> saved = new HashSet<>();
        try {
            List<WifiConfiguration> configs = wifi.getConfiguredNetworks();
            if (configs != null) {
                for (WifiConfiguration config : configs) {
                    if (config.SSID != null && config.SSID.length() >= 2) saved.add(config.SSID.substring(1, config.SSID.length() - 1));
                }
            }
        }
        catch (SecurityException e) {}

        String nearby = scanning ? activity.getString(R.string.s11_net_scanning) :
                        activity.getString(R.string.s11_net_nearby_count, results.size());
        row(out, "action", "net.scan", activity.getString(R.string.s11_net_nearby), nearby, activity.getString(R.string.s11_net_refresh));
        int n = 0;
        for (ScanResult result : results) {
            if (n++ >= MAX_NETWORKS) break;
            String caps = result.capabilities != null ? result.capabilities : "";
            String kind;
            String security;
            if (caps.contains("EAP")) {
                kind = "enterprise";
                security = activity.getString(R.string.s11_net_enterprise);
            }
            else if (saved.contains(result.SSID)) {
                kind = "saved";
                security = activity.getString(R.string.s11_net_saved);
            }
            else if (caps.contains("WPA") || caps.contains("WEP") || caps.contains("SAE") || caps.contains("PSK")) {
                kind = "secured";
                security = activity.getString(R.string.s11_net_secured);
            }
            else {
                kind = "open";
                security = activity.getString(R.string.s11_net_open);
            }
            if (result.SSID.contains("\t") || result.SSID.contains("\n")) continue;
            row(out, "network", "net.connect " + result.SSID, result.SSID, security + " · " + signal(result.level), kind);
        }
    }

    private String deviceKind(BluetoothDevice device) {
        BluetoothClass type;
        try {
            type = device.getBluetoothClass();
        }
        catch (Exception e) {
            type = null;
        }
        if (type == null) return activity.getString(R.string.s11_net_bt_device);
        int major = type.getMajorDeviceClass(), minor = type.getDeviceClass();
        switch (major) {
            case BluetoothClass.Device.Major.PERIPHERAL: {
                boolean keyboard = (minor & 0x40) != 0, pointing = (minor & 0x80) != 0;
                if (keyboard && pointing) return activity.getString(R.string.s11_net_bt_keyboard_mouse);
                if (keyboard) return activity.getString(R.string.s11_net_bt_keyboard);
                if (pointing) return activity.getString(R.string.s11_net_bt_mouse);
                if ((minor & 0x3c) == 0x08 || (minor & 0x3c) == 0x04) return activity.getString(R.string.s11_net_bt_gamepad);
                return activity.getString(R.string.s11_net_bt_device);
            }
            case BluetoothClass.Device.Major.AUDIO_VIDEO: return activity.getString(R.string.s11_net_bt_audio);
            case BluetoothClass.Device.Major.PHONE: return activity.getString(R.string.s11_net_bt_phone);
            case BluetoothClass.Device.Major.COMPUTER: return activity.getString(R.string.s11_net_bt_computer);
            case BluetoothClass.Device.Major.WEARABLE: return activity.getString(R.string.s11_net_bt_wearable);
            default: return activity.getString(R.string.s11_net_bt_device);
        }
    }

    private boolean isConnected(BluetoothDevice device) {
        if (aclConnected.contains(device.getAddress())) return true;
        for (BluetoothProfile profile : profiles.values()) {
            try {
                if (profile.getConnectionState(device) == BluetoothProfile.STATE_CONNECTED) return true;
            }
            catch (Exception e) {}
        }
        try {
            // keyboards and mice: no public profile to ask, but the device knows
            Method method = BluetoothDevice.class.getMethod("isConnected");
            Object connected = method.invoke(device);
            return connected instanceof Boolean && (Boolean)connected;
        }
        catch (Exception e) {
            return false;
        }
    }

    private void writeBluetooth(StringBuilder out) {
        row(out, "section", "net.btsection", activity.getString(R.string.s11_net_bluetooth), "", "");
        if (bluetooth == null) {
            row(out, "info", "net.nobluetooth", activity.getString(R.string.s11_net_bluetooth), activity.getString(R.string.s11_net_unavailable), "");
            return;
        }
        int state = bluetooth.getState();
        boolean on = state == BluetoothAdapter.STATE_ON || state == BluetoothAdapter.STATE_TURNING_ON;
        String detail = state == BluetoothAdapter.STATE_TURNING_ON ? activity.getString(R.string.s11_net_turning_on) :
                        state == BluetoothAdapter.STATE_TURNING_OFF ? activity.getString(R.string.s11_net_turning_off) :
                        on ? activity.getString(R.string.s11_net_state_on) : activity.getString(R.string.s11_net_state_off);
        row(out, "toggle", "net.bluetooth", activity.getString(R.string.s11_net_bluetooth), detail, on ? "1" : "0");
        if (state == BluetoothAdapter.STATE_ON) {
            Set<BluetoothDevice> bonded;
            try {
                bonded = bluetooth.getBondedDevices();
            }
            catch (SecurityException e) {
                bonded = null;
            }
            ArrayList<String[]> devices = new ArrayList<>();
            if (bonded != null) {
                for (BluetoothDevice device : bonded) {
                    String name = null;
                    try {
                        name = device.getName();
                    }
                    catch (SecurityException e) {}
                    if (name == null || name.isEmpty()) name = device.getAddress();
                    boolean connected = isConnected(device);
                    devices.add(new String[]{connected ? "0" : "1", name,
                        (connected ? activity.getString(R.string.s11_net_bt_connected) : activity.getString(R.string.s11_net_bt_not_connected)) +
                        " · " + deviceKind(device), device.getAddress()});
                }
            }
            // connected ones first, then by name
            devices.sort((a, b) -> a[0].equals(b[0]) ? a[1].compareToIgnoreCase(b[1]) : a[0].compareTo(b[0]));
            if (devices.isEmpty()) row(out, "info", "net.btnone", activity.getString(R.string.s11_net_bt_none), "", "");
            for (String[] device : devices) row(out, "info", "net.device " + device[3], device[1], device[2], "");
        }
        row(out, "action", "net.open.bluetooth", activity.getString(R.string.s11_net_bt_add),
            activity.getString(R.string.s11_net_bt_add_desc), activity.getString(R.string.s11_net_open_settings));
    }

    private void write() {
        handler.removeCallbacks(writer);
        StringBuilder out = new StringBuilder();
        try {
            writeWifi(out);
            writeBluetooth(out);
            row(out, "section", "net.moresection", activity.getString(R.string.s11_net_more), "", "");
            row(out, "action", "net.open.wifi", activity.getString(R.string.s11_net_android_wifi),
                activity.getString(R.string.s11_net_android_wifi_desc), activity.getString(R.string.s11_net_open_settings));
        }
        catch (RuntimeException e) {
            // a device quirk must not stop the page from showing what it can
            e.printStackTrace();
        }
        String rows = out.toString();
        if (rows.equals(written) && file.isFile()) return;
        written = rows;

        File parent = file.getParentFile();
        if (parent != null && !parent.isDirectory()) parent.mkdirs();
        File temp = new File(parent, FILE_NAME + ".tmp");
        try (FileOutputStream stream = new FileOutputStream(temp)) {
            stream.write(("S11CONNECTIVITY\t1\t" + (++revision) + "\n" + rows).getBytes(StandardCharsets.UTF_8));
        }
        catch (IOException e) {
            return;
        }
        if (!temp.renameTo(file)) FileUtils.delete(temp);
    }
}
