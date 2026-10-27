package com.winlator.s11;

import android.content.Context;

import com.winlator.R;
import com.winlator.box64.Box64Preset;
import com.winlator.box64.Box64PresetManager;
import com.winlator.container.AudioDrivers;
import com.winlator.container.Container;
import com.winlator.container.DXWrappers;
import com.winlator.container.Drive;
import com.winlator.container.GraphicsDrivers;
import com.winlator.contentdialog.AudioDriverConfigDialog;
import com.winlator.contentdialog.VKD3DConfigDialog;
import com.winlator.contentdialog.VirGLConfigDialog;
import com.winlator.contentdialog.VortekConfigDialog;
import com.winlator.core.DefaultVersion;
import com.winlator.core.EnvVars;
import com.winlator.core.FileUtils;
import com.winlator.core.GPUHelper;
import com.winlator.core.GeneralComponents;
import com.winlator.core.KeyValueSet;
import com.winlator.core.StringUtils;
import com.winlator.xenvironment.RootFS;
import com.winlator.xenvironment.components.VortekRendererComponent;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Locale;

/**
 * Every container setting, one by one, for the S11 Settings app (tools/s11/shell/s11settings.c).
 *
 * The app cannot read the container data, so at every start and after every change this
 * writes the settings with their current values, choices and labels (in the app's language)
 * to ~/.config/s11-container.txt (Z:\home\xuser\.config in Wine). The Settings app shows one
 * row per setting and sends a change back as "key TAB value" over winhandler's UDP port
 * (code 109, WinHandler); apply() checks it and stores it in the container. Like the container
 * settings in the app, changes take effect the next time the container starts.
 *
 * The file is UTF-8 text, one record per line, fields separated by tabs:
 *   S11CONTAINER  1  revision
 *   ROW  page  type  key  title  detail  value      (type: section choice text cpu checklist
 *                                                    drive adddrive envvar addenv)
 *   OPT  value  label                               (choices of the row above)
 */
public final class S11ContainerSettings {
    public static final String FILE_NAME = "s11-container.txt";
    private static int revision = (int)(System.currentTimeMillis() / 1000 % 1000000);

    private final Context context;
    private final Container container;
    private final StringBuilder out = new StringBuilder();

    private S11ContainerSettings(Context context, Container container) {
        this.context = context;
        this.container = container;
    }

    public static File getFile(Context context) {
        return new File(RootFS.find(context).getRootDir(), RootFS.USER_CONFIG_PATH + "/" + FILE_NAME);
    }

    /** Writes the settings file; call before Wine starts and after every change. */
    public static synchronized void write(Context context, Container container) {
        S11ContainerSettings settings = new S11ContainerSettings(context, container);
        settings.out.append("S11CONTAINER\t1\t").append(++revision).append('\n');
        try {
            settings.writeAll();
        }
        catch (RuntimeException e) {
            // a broken setting must not stop the container from starting
            e.printStackTrace();
        }

        File file = getFile(context);
        File parent = file.getParentFile();
        if (parent != null && !parent.isDirectory()) parent.mkdirs();
        File temp = new File(parent, FILE_NAME + ".tmp");
        try (FileOutputStream stream = new FileOutputStream(temp)) {
            stream.write(settings.out.toString().getBytes(StandardCharsets.UTF_8));
        }
        catch (IOException e) {
            return;
        }
        // whole or not at all: the Settings app may read it at any moment
        if (!temp.renameTo(file)) FileUtils.delete(temp);
    }

    // ------------------------------------------------------------------------------------
    // Writing
    // ------------------------------------------------------------------------------------

    private void writeAll() {
        writeGraphics();
        writeAudio();
        writeCompatibility();
        writeComponents();
        writeDrives();
        writeEnvVars();
        row("about", "text", "name", getString(R.string.name), getString(R.string.s11_cs_name_desc), container.getName());
    }

    private void writeGraphics() {
        String[] drivers = GraphicsDrivers.parseIdentifiers(container.getGraphicsDriver());
        KeyValueSet[] configs = GraphicsDrivers.parseConfigs(container.getGraphicsDriver(), container.getGraphicsDriverConfig());

        row("graphics", "choice", "vulkanDriver", "Vulkan " + getString(R.string.graphics_driver),
            getString(R.string.s11_cs_graphics_driver_desc), drivers[0]);
        option(GraphicsDrivers.VORTEK, GraphicsDrivers.getName(GraphicsDrivers.VORTEK));
        option(GraphicsDrivers.TURNIP, GraphicsDrivers.getName(GraphicsDrivers.TURNIP) + " (Adreno)");

        if (drivers[0].equals(GraphicsDrivers.VORTEK)) writeVortek(configs[0]);
        else if (drivers[0].equals(GraphicsDrivers.TURNIP)) writeTurnip(configs[0]);

        row("graphics", "choice", "openglDriver", "OpenGL " + getString(R.string.graphics_driver), "", drivers[1]);
        option(GraphicsDrivers.GLADIO, GraphicsDrivers.getName(GraphicsDrivers.GLADIO));
        option(GraphicsDrivers.VIRGL, GraphicsDrivers.getName(GraphicsDrivers.VIRGL));
        option(GraphicsDrivers.ZINK, GraphicsDrivers.getName(GraphicsDrivers.ZINK));

        if (drivers[1].equals(GraphicsDrivers.VIRGL)) {
            section("graphics", "VirGL");
            row("graphics", "choice", "virgl.glVersion", getString(R.string.opengl_version), "",
                configs[1].get("glVersion", VirGLConfigDialog.DEFAULT_GL_VERSION));
            for (String entry : getArray(R.array.opengl_version_entries)) option(StringUtils.parseNumber(entry), entry);
            onOff("graphics", "virgl.disableVertexArrayBGRA", getString(R.string.disable_vertex_array_bgra), "",
                  configs[1].getBoolean("disableVertexArrayBGRA", true));
        }

        String dxwrapper = DXWrappers.parseIdentifier(container.getDXWrapper());
        KeyValueSet[] dxConfigs = DXWrappers.parseConfigs(container.getDXWrapper(), container.getDXWrapperConfig());
        row("graphics", "choice", "dxwrapper", "Direct3D (DirectX 9~11)", getString(R.string.s11_cs_dxwrapper_desc), dxwrapper);
        option(DXWrappers.DXVK, DXWrappers.getName(DXWrappers.DXVK) + " (Vulkan)");
        option(DXWrappers.WINED3D, DXWrappers.getName(DXWrappers.WINED3D) + " (OpenGL)");

        if (dxwrapper.equals(DXWrappers.DXVK)) writeDXVK(dxConfigs[0], drivers[0]);
        else writeWineD3D(dxConfigs[0]);

        section("graphics", "DirectX 12 (VKD3D)");
        versionRow("graphics", "vkd3d.version", GeneralComponents.Type.VKD3D, dxConfigs[1].get("version"), DefaultVersion.VKD3D);
        row("graphics", "choice", "vkd3d.featureLevel", getString(R.string.d3d_feature_level), "",
            dxConfigs[1].get("featureLevel", VKD3DConfigDialog.DEFAULT_FEATURE_LEVEL));
        for (String entry : getArray(R.array.d3d_feature_level_entries)) option(entry, entry);

        section("graphics", getString(R.string.hud_mode));
        row("graphics", "choice", "hudMode", getString(R.string.hud_mode), getString(R.string.s11_cs_hud_desc),
            String.valueOf(container.getHUDMode()));
        indexedOptions(getArray(R.array.hud_mode_entries));
    }

    private void writeVortek(KeyValueSet config) {
        section("graphics", "Vortek");
        String defaultVkVersion = VortekConfigDialog.DEFAULT_VK_MAX_VERSION;
        row("graphics", "choice", "vortek.vkMaxVersion", getString(R.string.vulkan_version), "",
            StringUtils.parseNumber(config.get("vkMaxVersion", defaultVkVersion), defaultVkVersion));
        for (String entry : getArray(R.array.vk_version_entries)) option(StringUtils.parseNumber(entry), entry);

        memoryRow("vortek.maxDeviceMemory", getString(R.string.max_device_memory), config.get("maxDeviceMemory", "0"),
                  getArray(R.array.max_device_memory_entries));

        row("graphics", "choice", "vortek.imageCacheSize", getString(R.string.image_cache_size), "",
            config.get("imageCacheSize", String.valueOf(VortekRendererComponent.IMAGE_CACHE_SIZE)));
        for (String entry : getArray(R.array.image_cache_size_entries)) option(StringUtils.parseNumber(entry), entry);

        row("graphics", "choice", "vortek.resourceMemoryType", getString(R.string.resource_memory_type), "",
            String.valueOf(config.getInt("resourceMemoryType")));
        indexedOptions(getArray(R.array.resource_memory_type_entries));

        versionRow("graphics", "vortek.adrenotoolsDriver", GeneralComponents.Type.ADRENOTOOLS_DRIVER,
                   config.get("adrenotoolsDriver"), "System");

        String[] extensions = deviceExtensions();
        if (extensions.length > 0) {
            String exposed = config.get("exposedDeviceExtensions", "all");
            if (exposed.contains("|") || exposed.isEmpty()) exposed = "all";
            row("graphics", "checklist", "vortek.exposedDeviceExtensions", getString(R.string.exposed_extensions), "", exposed);
            for (String extension : extensions) option(extension, extension);
        }
    }

    private void writeTurnip(KeyValueSet config) {
        section("graphics", "Turnip");
        versionRow("graphics", "turnip.version", GeneralComponents.Type.TURNIP, config.get("version"), DefaultVersion.TURNIP);
        memoryRow("turnip.maxDeviceMemory", getString(R.string.max_device_memory), config.get("maxDeviceMemory", "0"),
                  getArray(R.array.max_device_memory_entries));
        onOff("graphics", "turnip.directRendering", getString(R.string.enable_direct_rendering), "",
              config.getBoolean("directRendering", true));
        row("graphics", "choice", "turnip.presentMode", getString(R.string.present_mode), "",
            String.valueOf(config.getInt("presentMode", GPUHelper.VkPresentMode.MAILBOX.ordinal())));
        indexedOptions(getArray(R.array.present_mode_entries));
    }

    private void writeDXVK(KeyValueSet config, String vulkanDriver) {
        section("graphics", "DXVK");
        versionRow("graphics", "dxvk.version", GeneralComponents.Type.DXVK, config.get("version"), DefaultVersion.DXVK(vulkanDriver));
        row("graphics", "choice", "dxvk.framerate", getString(R.string.frame_rate), "", config.get("framerate", "0"));
        for (String entry : getArray(R.array.dxvk_framerate_entries)) option(StringUtils.parseNumber(entry, "0"), entry);
        memoryRow("dxvk.maxDeviceMemory", getString(R.string.max_device_memory), config.get("maxDeviceMemory", "0"),
                  getArray(R.array.max_device_memory_entries));

        String customDevice = config.get("customDevice");
        String deviceId = customDevice.contains(":") ? customDevice.split(":")[0] : "";
        row("graphics", "choice", "dxvk.customDevice", getString(R.string.custom_device), "", deviceId);
        option("", getString(R.string.none));
        for (GPUCard card : gpuCards()) option(String.format("%04x", card.deviceId), card.name);

        ddrawRow("dxvk.ddrawWrapper", config);
    }

    private void writeWineD3D(KeyValueSet config) {
        section("graphics", "WineD3D");
        versionRow("graphics", "wined3d.version", GeneralComponents.Type.WINED3D, config.get("version"), DefaultVersion.WINED3D);
        row("graphics", "choice", "wined3d.renderer", getString(R.string.renderer), "", config.get("renderer", "gl"));
        for (String entry : getArray(R.array.wined3d_renderer_entries)) option(StringUtils.parseIdentifier(entry), entry);
        onOff("graphics", "wined3d.csmt", getString(R.string.enable_csmt), "", config.getInt("csmt", 3) != 0);

        row("graphics", "choice", "wined3d.gpu", getString(R.string.gpu_name), "", String.valueOf(config.getInt("VideoPciDeviceID", 1728)));
        for (GPUCard card : gpuCards()) option(String.valueOf(card.deviceId), card.name);

        row("graphics", "choice", "wined3d.offscreen", getString(R.string.offscreen_rendering_mode), "",
            config.get("OffscreenRenderingMode", "fbo").toLowerCase(Locale.ENGLISH));
        option("backbuffer", "Backbuffer");
        option("fbo", "FBO");
        onOff("graphics", "wined3d.strictShaderMath", getString(R.string.enable_strict_shader_math), "",
              config.getInt("strict_shader_math", 1) != 0);
        row("graphics", "choice", "wined3d.videoMemorySize", getString(R.string.video_memory_size), "",
            config.get("VideoMemorySize", "2048"));
        for (String entry : getArray(R.array.video_memory_size_entries)) option(StringUtils.parseMemorySize(entry), entry);
        ddrawRow("wined3d.ddrawWrapper", config);
    }

    private void ddrawRow(String key, KeyValueSet config) {
        row("graphics", "choice", key, getString(R.string.ddraw_wrapper), "", config.get("ddrawWrapper", DXWrappers.WINED3D));
        for (String entry : getArray(R.array.ddraw_wrapper_entries)) option(StringUtils.parseIdentifier(entry), entry);
    }

    private void writeAudio() {
        KeyValueSet config = new KeyValueSet(container.getAudioDriverConfig());
        row("audio", "choice", "audioDriver", getString(R.string.audio_driver), getString(R.string.s11_cs_audio_desc),
            container.getAudioDriver());
        option(AudioDrivers.PULSEAUDIO, "PulseAudio");
        option(AudioDrivers.ALSA, "ALSA");

        row("audio", "choice", "audio.performanceMode", getString(R.string.performance_mode), "",
            String.valueOf(config.getInt("performanceMode", AudioDriverConfigDialog.DEFAULT_PERFORMANCE_MODE)));
        indexedOptions(getArray(R.array.audio_performance_mode_entries));

        int volume = Math.round(config.getFloat("volume", AudioDriverConfigDialog.DEFAULT_VOLUME) * 100);
        volume = Math.max(0, Math.min(100, volume));
        row("audio", "choice", "audio.volume", getString(R.string.volume), "", String.valueOf(volume));
        LinkedHashSet<Integer> volumes = new LinkedHashSet<>();
        for (int v = 100; v >= 0; v -= 10) volumes.add(v);
        volumes.add(volume);
        for (int v : sorted(volumes, true)) option(String.valueOf(v), v + "%");

        int latency = config.getInt("latencyMillis", AudioDriverConfigDialog.DEFAULT_LATENCY_MILLIS);
        row("audio", "choice", "audio.latencyMillis", getString(R.string.average_latency), "", String.valueOf(latency));
        LinkedHashSet<Integer> latencies = new LinkedHashSet<>(Arrays.asList(12, 16, 20, 24, 32, 40, 48, 64, 80, 100, 120));
        latencies.add(latency);
        for (int v : sorted(latencies, false)) option(String.valueOf(v), v + " ms");
    }

    private void writeCompatibility() {
        row("compat", "choice", "box64Preset", getString(R.string.box64_preset), getString(R.string.s11_cs_box64_desc),
            container.getBox64Preset());
        for (Box64Preset preset : Box64PresetManager.getPresets(context)) option(preset.id, preset.name);

        row("compat", "choice", "startupSelection", getString(R.string.startup_selection), getString(R.string.s11_cs_startup_desc),
            String.valueOf(container.getStartupSelection()));
        indexedOptions(getArray(R.array.startup_selection_entries));

        String[] cpuLabels = cpuLabels();
        row("compat", "cpu", "cpuList", getString(R.string.processor_affinity), getString(R.string.s11_cs_cpu_desc),
            container.getCPUList(true));
        for (int i = 0; i < cpuLabels.length; i++) option(String.valueOf(i), cpuLabels[i]);
        row("compat", "cpu", "cpuListWoW64", getString(R.string.processor_affinity_32_bit_apps), "",
            container.getCPUListWoW64(true));
        for (int i = 0; i < cpuLabels.length; i++) option(String.valueOf(i), cpuLabels[i]);
    }

    private void writeComponents() {
        String[] labels = getArray(R.array.wincomponent_entries);
        for (String[] wincomponent : new KeyValueSet(container.getWinComponents())) {
            String name = wincomponent[0];
            String title = StringUtils.getString(context, name);
            row("components", "choice", "wincomponent." + name, title != null ? title : name, "", wincomponent[1]);
            indexedOptions(labels);
        }
    }

    private void writeDrives() {
        // adding first, above the list, so it is in view however many there are
        LinkedHashSet<String> used = new LinkedHashSet<>();
        for (Drive drive : container.drivesIterator()) used.add(drive.letter.toUpperCase(Locale.ENGLISH));
        row("drives", "adddrive", "drives.add", getString(R.string.s11_cs_add_drive), getString(R.string.s11_cs_drives_desc), "");
        for (char letter = 'D'; letter <= 'Y'; letter++) {
            if (!used.contains(String.valueOf(letter))) option(String.valueOf(letter), letter + ":");
        }
        for (Drive drive : container.drivesIterator()) row("drives", "drive", drive.letter, drive.letter + ":", drive.path, drive.path);
    }

    private void writeEnvVars() {
        row("env", "addenv", "env.set", getString(R.string.s11_cs_add_env_var), getString(R.string.s11_cs_env_vars_desc), "");
        EnvVars envVars = new EnvVars(container.getEnvVars());
        for (String item : envVars.toStringArray()) {
            int index = item.indexOf('=');
            if (index <= 0) continue;
            String name = item.substring(0, index);
            row("env", "envvar", name, name, item.substring(index + 1), item.substring(index + 1));
        }
    }

    // ------------------------------------------------------------------------------------
    // Applying a change from the Settings app
    // ------------------------------------------------------------------------------------

    /** Stores one change in the container. Returns true when the container data changed. */
    public static boolean apply(Context context, Container container, String key, String value) {
        if (key == null || value == null) return false;
        key = key.trim();
        value = value.trim();
        try {
            return new S11ContainerSettings(context, container).applyChange(key, value);
        }
        catch (RuntimeException e) {
            e.printStackTrace();
            return false;
        }
    }

    private boolean applyChange(String key, String value) {
        if (value.contains("\t") || value.contains("\n") || value.contains("|")) return false;
        switch (key) {
            case "name":
                if (value.isEmpty() || value.length() > 64 || value.equals(container.getName())) return false;
                container.setName(value);
                return true;
            case "vulkanDriver":
                if (!value.equals(GraphicsDrivers.VORTEK) && !value.equals(GraphicsDrivers.TURNIP)) return false;
                return setGraphicsDriver(0, value);
            case "openglDriver":
                if (!value.equals(GraphicsDrivers.GLADIO) && !value.equals(GraphicsDrivers.VIRGL) && !value.equals(GraphicsDrivers.ZINK)) return false;
                return setGraphicsDriver(1, value);
            case "dxwrapper": {
                if (!value.equals(DXWrappers.DXVK) && !value.equals(DXWrappers.WINED3D)) return false;
                String old = DXWrappers.parseIdentifier(container.getDXWrapper());
                if (old.equals(value)) return false;
                KeyValueSet[] configs = DXWrappers.parseConfigs(container.getDXWrapper(), container.getDXWrapperConfig());
                container.setDXWrapper(value);
                // the settings of one wrapper mean nothing to the other (as in the container settings)
                container.setDXWrapperConfig("|" + configs[1]);
                return true;
            }
            case "hudMode":
                return setByte(value, 2, container.getHUDMode(), (b) -> container.setHUDMode(b));
            case "startupSelection":
                return setByte(value, 2, container.getStartupSelection(), (b) -> container.setStartupSelection(b));
            case "audioDriver":
                if (!value.equals(AudioDrivers.ALSA) && !value.equals(AudioDrivers.PULSEAUDIO)) return false;
                if (value.equals(container.getAudioDriver())) return false;
                container.setAudioDriver(value);
                return true;
            case "box64Preset":
                if (Box64PresetManager.getPreset(context, value) == null || value.equals(container.getBox64Preset())) return false;
                container.setBox64Preset(value);
                return true;
            case "cpuList":
            case "cpuListWoW64": {
                String list = parseCPUList(value);
                if (list == null) return false;
                if (key.equals("cpuList")) container.setCPUList(list);
                else container.setCPUListWoW64(list);
                return true;
            }
            case "drives.add":
                return addDrive(value);
            case "drives.remove":
                return removeDrive(value);
            case "env.set":
                return setEnvVar(value);
            case "env.remove": {
                EnvVars envVars = new EnvVars(container.getEnvVars());
                if (!envVars.has(value)) return false;
                envVars.remove(value);
                container.setEnvVars(envVars.toString());
                return true;
            }
        }

        if (key.startsWith("wincomponent.")) {
            String name = key.substring("wincomponent.".length());
            if (!value.equals("0") && !value.equals("1")) return false;
            KeyValueSet wincomponents = new KeyValueSet(container.getWinComponents());
            String old = wincomponents.get(name, null);
            if (old == null || old.equals(value)) return false;
            wincomponents.put(name, value);
            container.setWinComponents(wincomponents.toString());
            return true;
        }
        if (key.startsWith("vortek.") || key.startsWith("turnip.")) return setGraphicsDriverConfig(0, key, value);
        if (key.startsWith("virgl.")) return setGraphicsDriverConfig(1, key, value);
        if (key.startsWith("dxvk.") || key.startsWith("wined3d.")) return setDXWrapperConfig(0, key, value);
        if (key.startsWith("vkd3d.")) return setDXWrapperConfig(1, key, value);
        if (key.startsWith("audio.")) return setAudioConfig(key, value);
        return false;
    }

    private interface ByteSetter {
        void set(byte value);
    }

    private static boolean setByte(String value, int max, byte current, ByteSetter setter) {
        int number = parseInt(value, -1);
        if (number < 0 || number > max || number == current) return false;
        setter.set((byte)number);
        return true;
    }

    private boolean setGraphicsDriver(int index, String value) {
        String[] drivers = GraphicsDrivers.parseIdentifiers(container.getGraphicsDriver());
        if (drivers[index].equals(value)) return false;
        KeyValueSet[] configs = GraphicsDrivers.parseConfigs(container.getGraphicsDriver(), container.getGraphicsDriverConfig());
        drivers[index] = value;
        configs[index] = new KeyValueSet();
        container.setGraphicsDriver(drivers[0] + "," + drivers[1]);
        container.setGraphicsDriverConfig(configs[0] + "|" + configs[1]);
        return true;
    }

    private boolean setGraphicsDriverConfig(int index, String key, String value) {
        String[] drivers = GraphicsDrivers.parseIdentifiers(container.getGraphicsDriver());
        KeyValueSet[] configs = GraphicsDrivers.parseConfigs(container.getGraphicsDriver(), container.getGraphicsDriverConfig());
        String name = key.substring(key.indexOf('.') + 1);
        String driver = key.substring(0, key.indexOf('.'));
        if (!driver.equals(drivers[index])) return false;
        String stored = configValue(driver + "." + name, value);
        if (stored == null) return false;
        if (stored.equals(configs[index].get(name, null))) return false;
        configs[index].put(name, stored);
        container.setGraphicsDriverConfig(configs[0] + "|" + configs[1]);
        return true;
    }

    private boolean setDXWrapperConfig(int index, String key, String value) {
        String dxwrapper = DXWrappers.parseIdentifier(container.getDXWrapper());
        KeyValueSet[] configs = DXWrappers.parseConfigs(container.getDXWrapper(), container.getDXWrapperConfig());
        String name = key.substring(key.indexOf('.') + 1);
        String wrapper = key.substring(0, key.indexOf('.'));
        if (index == 0 && !wrapper.equals(dxwrapper)) return false;

        if (key.equals("dxvk.customDevice")) {
            if (value.isEmpty()) {
                if (configs[0].get("customDevice").isEmpty()) return false;
                configs[0] = without(configs[0], "customDevice");
            }
            else {
                GPUCard card = findGPUCard(parseInt(value, 16, -1));
                if (card == null) return false;
                configs[0].put("customDevice", String.format("%04x", card.deviceId) + ":" + String.format("%04x", card.vendorId) + ":" + card.name);
            }
        }
        else if (key.equals("wined3d.gpu")) {
            GPUCard card = findGPUCard(parseInt(value, -1));
            if (card == null) return false;
            configs[0].put("VideoPciDeviceID", String.valueOf(card.deviceId));
            configs[0].put("VideoPciVendorID", String.valueOf(card.vendorId));
        }
        else {
            String stored = configValue(key, value);
            if (stored == null) return false;
            String storedName = configName(key, name);
            if (stored.equals(configs[index].get(storedName, null))) return false;
            configs[index].put(storedName, stored);
        }
        container.setDXWrapperConfig(configs[0] + "|" + configs[1]);
        return true;
    }

    private boolean setAudioConfig(String key, String value) {
        KeyValueSet config = new KeyValueSet(container.getAudioDriverConfig());
        String name = key.substring("audio.".length());
        String stored;
        switch (name) {
            case "performanceMode": {
                int mode = parseInt(value, -1);
                if (mode < 0 || mode > 2) return false;
                stored = String.valueOf(mode);
                break;
            }
            case "volume": {
                int volume = parseInt(value, -1);
                if (volume < 0 || volume > 100) return false;
                stored = String.valueOf(volume / 100.0f);
                break;
            }
            case "latencyMillis": {
                int latency = parseInt(value, -1);
                if (latency < 8 || latency > 250) return false;
                stored = String.valueOf(latency);
                break;
            }
            default:
                return false;
        }
        if (stored.equals(config.get(name, null))) return false;
        config.put(name, stored);
        container.setAudioDriverConfig(config.toString());
        return true;
    }

    /** The KeyValueSet name of a setting where it differs from the key (WineD3D's). */
    private static String configName(String key, String name) {
        switch (key) {
            case "wined3d.offscreen": return "OffscreenRenderingMode";
            case "wined3d.strictShaderMath": return "strict_shader_math";
            case "wined3d.videoMemorySize": return "VideoMemorySize";
            default: return name;
        }
    }

    /** The stored form of a value, or null when it is not one of the choices. */
    private String configValue(String key, String value) {
        switch (key) {
            case "vortek.vkMaxVersion":
                return oneOf(value, numbers(getArray(R.array.vk_version_entries)));
            case "vortek.maxDeviceMemory":
            case "turnip.maxDeviceMemory":
            case "dxvk.maxDeviceMemory":
                return oneOf(value, memorySizes(getArray(R.array.max_device_memory_entries)));
            case "vortek.imageCacheSize":
                return oneOf(value, numbers(getArray(R.array.image_cache_size_entries)));
            case "vortek.resourceMemoryType":
                return index(value, getArray(R.array.resource_memory_type_entries).length);
            case "vortek.adrenotoolsDriver":
                return oneOf(value, componentVersions(GeneralComponents.Type.ADRENOTOOLS_DRIVER));
            case "vortek.exposedDeviceExtensions": {
                if (value.equals("all")) return value;
                List<String> known = Arrays.asList(deviceExtensions());
                ArrayList<String> chosen = new ArrayList<>();
                for (String extension : value.split(":")) if (known.contains(extension)) chosen.add(extension);
                if (chosen.isEmpty()) return null;
                return chosen.size() == known.size() ? "all" : String.join(":", chosen);
            }
            case "turnip.version":
                return oneOf(value, componentVersions(GeneralComponents.Type.TURNIP));
            case "turnip.directRendering":
            case "virgl.disableVertexArrayBGRA":
            case "wined3d.strictShaderMath":
                return value.equals("1") || value.equals("0") ? value : null;
            case "wined3d.csmt":
                return value.equals("1") ? "3" : value.equals("0") ? "0" : null;
            case "turnip.presentMode":
                return index(value, getArray(R.array.present_mode_entries).length);
            case "virgl.glVersion":
                return oneOf(value, numbers(getArray(R.array.opengl_version_entries)));
            case "dxvk.version":
                return oneOf(value, componentVersions(GeneralComponents.Type.DXVK));
            case "dxvk.framerate": {
                ArrayList<String> framerates = new ArrayList<>();
                for (String entry : getArray(R.array.dxvk_framerate_entries)) framerates.add(StringUtils.parseNumber(entry, "0"));
                return oneOf(value, framerates);
            }
            case "dxvk.ddrawWrapper":
            case "wined3d.ddrawWrapper":
                return oneOf(value, identifiers(getArray(R.array.ddraw_wrapper_entries)));
            case "wined3d.version":
                return oneOf(value, componentVersions(GeneralComponents.Type.WINED3D));
            case "wined3d.renderer":
                return oneOf(value, identifiers(getArray(R.array.wined3d_renderer_entries)));
            case "wined3d.offscreen":
                return value.equals("fbo") || value.equals("backbuffer") ? value : null;
            case "wined3d.videoMemorySize":
                return oneOf(value, memorySizes(getArray(R.array.video_memory_size_entries)));
            case "vkd3d.version":
                return oneOf(value, componentVersions(GeneralComponents.Type.VKD3D));
            case "vkd3d.featureLevel":
                return oneOf(value, Arrays.asList(getArray(R.array.d3d_feature_level_entries)));
            default:
                return null;
        }
    }

    private boolean addDrive(String value) {
        // "X:/absolute/path"; the Settings app sends the Unix path of a folder it picked
        if (value.length() < 4 || value.charAt(1) != ':') return false;
        String letter = value.substring(0, 1).toUpperCase(Locale.ENGLISH);
        char c = letter.charAt(0);
        if (c < 'D' || c > 'Y') return false;
        String path = value.substring(2);
        if (!path.startsWith("/")) return false;
        // Wine names the folder through dosdevices ("…/dosdevices/e:/Music"); the drive gets the real one
        try {
            path = new File(path).getCanonicalPath();
        }
        catch (IOException e) {
            return false;
        }
        // the container keeps drives as letter:path one after another, so a path holds no colon
        if (path.contains(":") || !new File(path).isDirectory()) return false;
        for (Drive drive : container.drivesIterator()) if (drive.letter.equalsIgnoreCase(letter)) return false;
        container.setDrives(container.getDrives() + letter + ":" + path);
        return true;
    }

    private boolean removeDrive(String letter) {
        StringBuilder drives = new StringBuilder();
        boolean removed = false;
        for (Drive drive : container.drivesIterator()) {
            if (drive.letter.equalsIgnoreCase(letter)) {
                removed = true;
                continue;
            }
            drives.append(drive.letter).append(':').append(drive.path);
        }
        if (!removed) return false;
        container.setDrives(drives.toString());
        return true;
    }

    private boolean setEnvVar(String value) {
        int index = value.indexOf('=');
        if (index <= 0) return false;
        String name = value.substring(0, index).trim();
        String data = value.substring(index + 1).trim();
        // the container keeps them space separated, so neither part may hold a space
        if (!name.matches("[A-Za-z_][A-Za-z0-9_]*") || data.contains(" ")) return false;
        EnvVars envVars = new EnvVars(container.getEnvVars());
        if (envVars.has(name) && envVars.get(name).equals(data)) return false;
        envVars.put(name, data);
        container.setEnvVars(envVars.toString());
        return true;
    }

    private String parseCPUList(String value) {
        int count = Runtime.getRuntime().availableProcessors();
        LinkedHashSet<Integer> cpus = new LinkedHashSet<>();
        for (String part : value.split(",")) {
            int cpu = parseInt(part.trim(), -1);
            if (cpu < 0 || cpu >= count) return null;
            cpus.add(cpu);
        }
        if (cpus.isEmpty()) return null;
        StringBuilder list = new StringBuilder();
        for (int cpu : sorted(cpus, false)) {
            if (list.length() > 0) list.append(',');
            list.append(cpu);
        }
        return list.toString();
    }

    // ------------------------------------------------------------------------------------
    // Helpers
    // ------------------------------------------------------------------------------------

    private void row(String page, String type, String key, String title, String detail, String value) {
        out.append("ROW\t").append(page).append('\t').append(type).append('\t').append(clean(key)).append('\t')
           .append(clean(title)).append('\t').append(clean(detail)).append('\t').append(clean(value)).append('\n');
    }

    private void option(String value, String label) {
        out.append("OPT\t").append(clean(value)).append('\t').append(clean(label)).append('\n');
    }

    private void section(String page, String title) {
        row(page, "section", "", title, "", "");
    }

    private void indexedOptions(String[] labels) {
        for (int i = 0; i < labels.length; i++) option(String.valueOf(i), labels[i]);
    }

    private void onOff(String page, String key, String title, String detail, boolean on) {
        row(page, "choice", key, title, detail, on ? "1" : "0");
        option("1", context.getString(R.string.s11_on));
        option("0", context.getString(R.string.s11_off));
    }

    private void memoryRow(String key, String title, String value, String[] entries) {
        row("graphics", "choice", key, title, "", value);
        for (String entry : entries) option(StringUtils.parseMemorySize(entry), entry);
    }

    private void versionRow(String page, String key, GeneralComponents.Type type, String value, String defaultValue) {
        List<String> versions = componentVersions(type);
        if (value == null || value.isEmpty() || !versions.contains(value)) value = defaultValue;
        row(page, "choice", key, getString(R.string.version) + " (" + typeTitle(type) + ")", "", value);
        for (String version : versions) option(version, version);
    }

    private static String typeTitle(GeneralComponents.Type type) {
        switch (type) {
            case DXVK: return "DXVK";
            case VKD3D: return "VKD3D";
            case WINED3D: return "WineD3D";
            case TURNIP: return "Turnip";
            case ADRENOTOOLS_DRIVER: return "Adrenotools";
            default: return type.name();
        }
    }

    private List<String> componentVersions(GeneralComponents.Type type) {
        ArrayList<String> versions = GeneralComponents.getBuiltinComponentNames(type);
        for (String name : GeneralComponents.getInstalledComponentNames(type, context)) {
            if (!versions.contains(name)) versions.add(name);
        }
        if (type != GeneralComponents.Type.ADRENOTOOLS_DRIVER) {
            versions.sort((a, b) -> Integer.compare(GPUHelper.vkMakeVersion(a), GPUHelper.vkMakeVersion(b)));
        }
        return versions;
    }

    private static String clean(String text) {
        return text == null ? "" : text.replace('\t', ' ').replace('\n', ' ').replace('\r', ' ');
    }

    private String getString(int id) {
        return context.getString(id);
    }

    private String[] getArray(int id) {
        return context.getResources().getStringArray(id);
    }

    private static List<String> numbers(String[] entries) {
        ArrayList<String> result = new ArrayList<>();
        for (String entry : entries) result.add(StringUtils.parseNumber(entry));
        return result;
    }

    private static List<String> memorySizes(String[] entries) {
        ArrayList<String> result = new ArrayList<>();
        for (String entry : entries) result.add(StringUtils.parseMemorySize(entry));
        return result;
    }

    private static List<String> identifiers(String[] entries) {
        ArrayList<String> result = new ArrayList<>();
        for (String entry : entries) result.add(StringUtils.parseIdentifier(entry));
        return result;
    }

    private static String oneOf(String value, List<String> choices) {
        return choices.contains(value) ? value : null;
    }

    private static String index(String value, int count) {
        int number = parseInt(value, -1);
        return number >= 0 && number < count ? String.valueOf(number) : null;
    }

    private static int parseInt(String value, int fallback) {
        return parseInt(value, 10, fallback);
    }

    private static int parseInt(String value, int radix, int fallback) {
        try {
            return Integer.parseInt(value, radix);
        }
        catch (NumberFormatException e) {
            return fallback;
        }
    }

    private static List<Integer> sorted(LinkedHashSet<Integer> values, boolean descending) {
        ArrayList<Integer> list = new ArrayList<>(values);
        list.sort((a, b) -> descending ? Integer.compare(b, a) : Integer.compare(a, b));
        return list;
    }

    private static KeyValueSet without(KeyValueSet config, String name) {
        KeyValueSet result = new KeyValueSet();
        for (String[] item : config) if (!item[0].equals(name)) result.put(item[0], item[1]);
        return result;
    }

    private static String[] deviceExtensionsCache;

    private static synchronized String[] deviceExtensions() {
        if (deviceExtensionsCache == null) {
            try {
                String[] extensions = GPUHelper.vkGetDeviceExtensions();
                deviceExtensionsCache = extensions != null ? extensions : new String[0];
            }
            catch (Throwable e) {
                deviceExtensionsCache = new String[0];
            }
        }
        return deviceExtensionsCache;
    }

    private static class GPUCard {
        final String name;
        final int deviceId, vendorId;

        GPUCard(String name, int deviceId, int vendorId) {
            this.name = name;
            this.deviceId = deviceId;
            this.vendorId = vendorId;
        }
    }

    private static List<GPUCard> gpuCardsCache;

    private List<GPUCard> gpuCards() {
        if (gpuCardsCache == null) {
            ArrayList<GPUCard> cards = new ArrayList<>();
            try {
                JSONArray items = new JSONArray(FileUtils.readString(context, "gpu_cards.json"));
                for (int i = 0; i < items.length(); i++) {
                    JSONObject item = items.getJSONObject(i);
                    // the name goes into a comma separated config (customDevice)
                    cards.add(new GPUCard(item.getString("name").replace(",", " ").replace("|", " "),
                                          item.getInt("deviceID"), item.getInt("vendorID")));
                }
            }
            catch (JSONException e) {}
            gpuCardsCache = cards;
        }
        return gpuCardsCache;
    }

    private GPUCard findGPUCard(int deviceId) {
        for (GPUCard card : gpuCards()) if (card.deviceId == deviceId) return card;
        return null;
    }

    /** "Core 1 · 3.63 GHz" from the highest clock of each core, so the big ones are easy to tell. */
    private String[] cpuLabels() {
        int count = Runtime.getRuntime().availableProcessors();
        String[] labels = new String[count];
        for (int i = 0; i < count; i++) {
            String label = context.getString(R.string.s11_cpu_core, i + 1);
            File file = new File("/sys/devices/system/cpu/cpu" + i + "/cpufreq/cpuinfo_max_freq");
            try {
                String text = FileUtils.readString(file).trim();
                long khz = Long.parseLong(text);
                if (khz > 0) label += String.format(Locale.ENGLISH, " · %.2f GHz", khz / 1000000.0);
            }
            catch (RuntimeException e) {}
            labels[i] = label;
        }
        return labels;
    }
}
