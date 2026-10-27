package com.winlator.core;

import android.content.Context;
import android.os.Parcel;
import android.os.Parcelable;

import androidx.annotation.NonNull;

import com.winlator.xenvironment.RootFS;

import java.io.File;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

public class WineInfo implements Parcelable {
    public static final String MAIN_WINE_VERSION = "10.10";
    public static final WineInfo MAIN_WINE_INFO = new WineInfo(MAIN_WINE_VERSION);
    private static final Pattern pattern = Pattern.compile("^wine\\-([0-9\\.]+)\\-?([0-9\\.]+)?\\-?(x86|x86_64|arm64)?$");
    /** Winlator S11: a Wine for x64 programs (run by Box64), or an ARM64 Wine (ARM64EC: it runs
     *  ARM64 programs as they are and x64 and x86 ones with its own FEX and Box64) */
    public static final String ARCH_X86_64 = "x86_64";
    public static final String ARCH_ARM64 = "arm64";
    public final String version;
    public final String subversion;
    public final String path;
    public final String arch;

    public WineInfo(String version) {
        this.version = version;
        this.subversion = null;
        this.path = null;
        this.arch = ARCH_X86_64;
    }

    public WineInfo(String version, String subversion, String path) {
        this(version, subversion, path, ARCH_X86_64);
    }

    public WineInfo(String version, String subversion, String path, String arch) {
        this.version = version;
        this.subversion = subversion != null && !subversion.isEmpty() ? subversion : null;
        this.path = path;
        this.arch = ARCH_ARM64.equals(arch) ? ARCH_ARM64 : ARCH_X86_64;
    }

    private WineInfo(Parcel in) {
        version = in.readString();
        subversion = in.readString();
        path = in.readString();
        String arch = in.readString();
        this.arch = ARCH_ARM64.equals(arch) ? ARCH_ARM64 : ARCH_X86_64;
    }

    public boolean isArm64() {
        return ARCH_ARM64.equals(arch);
    }

    public String identifier() {
        return "wine-"+fullVersion()+(this == MAIN_WINE_INFO ? "-custom" : "")+(isArm64() ? "-"+ARCH_ARM64 : "");
    }

    /** The prefix a new container of this Wine starts from, in the installed Wine folder */
    public String containerPatternName() {
        return "container-pattern-"+fullVersion()+(isArm64() ? "-"+ARCH_ARM64 : "")+".tzst";
    }

    public String fullVersion() {
        return version+(subversion != null ? "-"+subversion : "");
    }

    @NonNull
    @Override
    public String toString() {
        return "Wine "+fullVersion()+(this == MAIN_WINE_INFO ? " (Custom)" : "")+(isArm64() ? " ARM64" : "");
    }

    @Override
    public int describeContents() {
        return 0;
    }

    public static final Parcelable.Creator<WineInfo> CREATOR = new Parcelable.Creator<WineInfo>() {
        public WineInfo createFromParcel(Parcel in) {
            return new WineInfo(in);
        }

        public WineInfo[] newArray(int size) {
            return new WineInfo[size];
        }
    };

    @Override
    public void writeToParcel(Parcel dest, int flags) {
        dest.writeString(version);
        dest.writeString(subversion);
        dest.writeString(path);
        dest.writeString(arch);
    }

    @NonNull
    public static WineInfo fromIdentifier(Context context, String identifier) {
        if (identifier.equals(MAIN_WINE_INFO.identifier())) return MAIN_WINE_INFO;
        Matcher matcher = pattern.matcher(identifier);
        if (matcher.find()) {
            File installedWineDir = RootFS.find(context).getInstalledWineDir();
            String path = (new File(installedWineDir, identifier)).getPath();
            return new WineInfo(matcher.group(1), matcher.group(2), path, matcher.group(3));
        }
        else return MAIN_WINE_INFO;
    }

    public static boolean isMainWineVersion(String wineVersion) {
        return wineVersion == null ||wineVersion.equals(MAIN_WINE_INFO.identifier());
    }
}
