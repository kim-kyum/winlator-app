package com.winlator.s11;

import java.io.File;
import java.io.IOException;
import java.io.RandomAccessFile;
import java.util.Arrays;

/**
 * Flat title bar buttons for the bundled Wine.
 *
 * Wine draws the minimize, maximize and close buttons as raised Windows 95 push buttons
 * with a Marlett glyph on top (win32u, draw_frame_caption). Windows 10 and 11 show just
 * the glyph on the title bar. This removes the push button frame from the bundled
 * Wine 10.10's win32u.so by turning that one call into a NOP; the thin glyphs come
 * from the S11 Marlett font in the rootfs overlay.
 *
 * The bytes around the call are checked first, so another Wine build is left alone.
 */
public final class CaptionButtons {
    private static final String WIN32U = "opt/wine/lib/wine/x86_64-unix/win32u.so";
    private static final long FILE_SIZE = 1508480;
    private static final long OFFSET = 0x3a1fa;
    // mov %r13d,%eax; sar %r12d; mov $0x180f,%ecx (BF_RECT|BF_MIDDLE|BF_SOFT); ...; mov %rbx,%rdi
    private static final byte[] PREFIX = bytes(
        "44 89 e8 41 d1 fc b9 0f 18 00 00 25 00 46 00 00 83 f8 01 19 d2 83 e2 fb 83 c2 0a c7 07 00 00 00 00 48 89 df");
    // call draw_rect_edge
    private static final byte[] ORIGINAL = bytes("e8 7d f9 ff ff");
    // nopl 0x0(%rax,%rax,1)
    private static final byte[] PATCHED = bytes("0f 1f 44 00 00");
    // mov %r13d,%eax; and $0xf,%eax (the glyph switch that follows)
    private static final byte[] SUFFIX = bytes("44 89 e8 83 e0 0f");

    private CaptionButtons() {}

    /** Call before Wine starts. Cheap when the file is already patched or unknown. */
    public static synchronized void applyIfNeeded(File rootDir) {
        File file = new File(rootDir, WIN32U);
        if (!file.isFile() || file.length() != FILE_SIZE) return;

        try (RandomAccessFile raf = new RandomAccessFile(file, "rw")) {
            byte[] data = new byte[PREFIX.length + ORIGINAL.length + SUFFIX.length];
            raf.seek(OFFSET);
            raf.readFully(data);

            byte[] prefix = Arrays.copyOfRange(data, 0, PREFIX.length);
            byte[] call = Arrays.copyOfRange(data, PREFIX.length, PREFIX.length + ORIGINAL.length);
            byte[] suffix = Arrays.copyOfRange(data, PREFIX.length + ORIGINAL.length, data.length);
            if (!Arrays.equals(prefix, PREFIX) || !Arrays.equals(suffix, SUFFIX)) return;
            if (!Arrays.equals(call, ORIGINAL)) return; // patched already, or not the expected build

            raf.seek(OFFSET + PREFIX.length);
            raf.write(PATCHED);
        }
        catch (IOException e) {}
    }

    private static byte[] bytes(String hex) {
        String[] items = hex.trim().split("\\s+");
        byte[] result = new byte[items.length];
        for (int i = 0; i < items.length; i++) result[i] = (byte)Integer.parseInt(items[i], 16);
        return result;
    }
}
