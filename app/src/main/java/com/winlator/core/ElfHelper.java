package com.winlator.core;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;

public abstract class ElfHelper {
    private static final byte ELF_CLASS_32 = 1;
    private static final byte ELF_CLASS_64 = 2;

    private static int getEIClass(File binFile) {
        try (InputStream inStream = new FileInputStream(binFile)) {
            byte[] header = new byte[52];
            inStream.read(header);
            if (header[0] == 0x7F && header[1] == 'E' && header[2] == 'L' && header[3] == 'F') {
                return header[4];
            }
        }
        catch (IOException e) {}
        return 0;
    }

    public static boolean is32Bit(File binFile) {
        return getEIClass(binFile) == ELF_CLASS_32;
    }

    public static boolean is64Bit(File binFile) {
        return getEIClass(binFile) == ELF_CLASS_64;
    }

    /** Winlator S11: an ARM64 program (e_machine EM_AARCH64), such as an ARM64 Wine's */
    public static boolean isAarch64(File binFile) {
        try (InputStream inStream = new FileInputStream(binFile)) {
            byte[] header = new byte[20];
            if (inStream.read(header) < 20) return false;
            if (header[0] != 0x7F || header[1] != 'E' || header[2] != 'L' || header[3] != 'F') return false;
            int machine = (header[18] & 0xff) | ((header[19] & 0xff) << 8);
            return machine == 183;
        }
        catch (IOException e) {
            return false;
        }
    }
}