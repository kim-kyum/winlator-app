package com.winlator.s11;

import android.content.Context;

import com.winlator.core.FileUtils;
import com.winlator.core.TarCompressorUtils;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;

/**
 * Winlator S11 additions to the system files (Korean locale data, Korean fonts).
 * They ship as assets/s11/rootfs-overlay.tzst, built by tools/s11/build_overlay.py,
 * and are extracted again whenever their version changes or the rootfs is reinstalled
 * (a reinstall wipes the marker file along with everything else outside home/ and opt/).
 */
public final class RootfsOverlay {
    private static final String ASSET = "s11/rootfs-overlay.tzst";
    private static final String VERSION_ASSET = "s11/rootfs-overlay.version";
    private static final String MARKER = ".s11-overlay-version";

    private RootfsOverlay() {}

    /** Call off the UI thread before Wine starts. Cheap when nothing changed. */
    public static synchronized void installIfNeeded(Context context, File rootDir) {
        String wanted = readAsset(context, VERSION_ASSET);
        if (wanted == null || wanted.isEmpty()) return;

        File marker = new File(rootDir, MARKER);
        if (marker.isFile() && wanted.equals(FileUtils.readString(marker).trim())) return;

        if (TarCompressorUtils.extract(TarCompressorUtils.Type.ZSTD, context, ASSET, rootDir)) {
            FileUtils.writeString(marker, wanted);
        }
    }

    private static String readAsset(Context context, String name) {
        try (InputStream in = context.getAssets().open(name);
             ByteArrayOutputStream out = new ByteArrayOutputStream()) {
            byte[] buffer = new byte[4096];
            int n;
            while ((n = in.read(buffer)) > 0) out.write(buffer, 0, n);
            return new String(out.toByteArray(), StandardCharsets.UTF_8).trim();
        }
        catch (IOException e) {
            return null;
        }
    }
}
