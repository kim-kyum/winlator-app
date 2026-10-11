package com.winlator.s11;

import java.io.File;
import java.io.IOException;
import java.io.RandomAccessFile;
import java.util.Arrays;

/**
 * Title bar buttons for the bundled Wine 10.10 (win32u.so).
 *
 * - Flat buttons: Wine draws minimize, maximize and close as raised Windows 95 push buttons
 *   with a Marlett glyph on top (draw_frame_caption). Windows 10 and 11 show just the glyph;
 *   the call that draws the push button becomes a NOP. The thin glyphs come from the S11
 *   Marlett font in the rootfs overlay.
 * - Where a click lands: Wine's hit test (handle_nc_hit_test) takes the close button to be
 *   as wide as the title bar is tall (SM_CYCAPTION), while it draws every button
 *   SM_CXSIZE wide. With Windows 10's wide buttons the clickable areas slid off the
 *   pictures: the left part of × maximized, the left part of □ minimized. Both close
 *   checks (left-to-right and right-to-left layouts) now use SM_CXSIZE.
 *
 * Each patch checks the bytes around it first, so another Wine build is left alone.
 */
public final class CaptionButtons {
    private static final String WIN32U = "opt/wine/lib/wine/x86_64-unix/win32u.so";
    private static final long FILE_SIZE = 1508480;

    private static final class Patch {
        final long offset;
        final byte[] prefix, original, patched, suffix;

        Patch(long offset, String prefix, String original, String patched, String suffix) {
            this.offset = offset;
            this.prefix = bytes(prefix);
            this.original = bytes(original);
            this.patched = bytes(patched);
            this.suffix = bytes(suffix);
        }
    }

    private static final Patch[] PATCHES = {
        // draw_frame_caption: mov %r13d,%eax; sar %r12d; mov $0x180f,%ecx (BF_RECT|BF_MIDDLE|BF_SOFT);
        // ...; mov %rbx,%rdi | call draw_rect_edge -> nopl 0x0(%rax,%rax,1) | mov %r13d,%eax; and $0xf,%eax
        new Patch(0x3a1fa,
            "44 89 e8 41 d1 fc b9 0f 18 00 00 25 00 46 00 00 83 f8 01 19 d2 83 e2 fb 83 c2 0a c7 07 00 00 00 00 48 89 df",
            "e8 7d f9 ff ff", "0f 1f 44 00 00", "44 89 e8 83 e0 0f"),
        // handle_nc_hit_test, close button, left to right: mov $SM_CYCAPTION,%edi -> mov $SM_CXSIZE,%edi
        new Patch(0x3c5f7,
            "8d 44 02 ff 89 44 24 10 39 d8 0f 8f ba 00 00 00",
            "bf 04 00 00 00", "bf 1e 00 00 00", "e8 8f 41 0c 00 89 c2 8b 44 24 18 29 d0 89 44 24"),
        // the same, right to left
        new Patch(0x3c557,
            "24 18 29 d0 89 44 24 18 39 d8 0f 8c 5a 01 00 00",
            "bf 04 00 00 00", "bf 1e 00 00 00", "e8 2f 42 0c 00 03 44 24 10 89 44 24 10 39 d8 0f"),
    };

    private CaptionButtons() {}

    /** Call before Wine starts. Cheap when the file is already patched or unknown. */
    public static synchronized void applyIfNeeded(File rootDir) {
        File file = new File(rootDir, WIN32U);
        if (!file.isFile() || file.length() != FILE_SIZE) return;

        try (RandomAccessFile raf = new RandomAccessFile(file, "rw")) {
            for (Patch patch : PATCHES) apply(raf, patch);
        }
        catch (IOException e) {}
    }

    private static void apply(RandomAccessFile raf, Patch patch) throws IOException {
        byte[] data = new byte[patch.prefix.length + patch.original.length + patch.suffix.length];
        raf.seek(patch.offset);
        raf.readFully(data);

        byte[] prefix = Arrays.copyOfRange(data, 0, patch.prefix.length);
        byte[] code = Arrays.copyOfRange(data, patch.prefix.length, patch.prefix.length + patch.original.length);
        byte[] suffix = Arrays.copyOfRange(data, patch.prefix.length + patch.original.length, data.length);
        if (!Arrays.equals(prefix, patch.prefix) || !Arrays.equals(suffix, patch.suffix)) return;
        if (!Arrays.equals(code, patch.original)) return; // patched already, or not the expected build

        raf.seek(patch.offset + patch.prefix.length);
        raf.write(patch.patched);
    }

    private static byte[] bytes(String hex) {
        String[] items = hex.trim().split("\\s+");
        byte[] result = new byte[items.length];
        for (int i = 0; i < items.length; i++) result[i] = (byte)Integer.parseInt(items[i], 16);
        return result;
    }
}
