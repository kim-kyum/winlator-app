package com.winlator.core;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Rect;
import android.graphics.RectF;

import com.winlator.s11.ModernLook;
import com.winlator.win32.MSBitmap;
import com.winlator.xenvironment.RootFS;
import com.winlator.xserver.ScreenInfo;

import org.json.JSONException;
import org.json.JSONObject;

import java.io.File;

public abstract class WineThemeManager {
    public enum Theme {LIGHT, DARK}
    public enum BackgroundType {IMAGE, COLOR}
    public static final String DEFAULT_DESKTOP_THEME = Theme.LIGHT+","+BackgroundType.IMAGE+",#0277bd";
    public static final String DEFAULT_WALLPAPER_ID = "wallpaper-s11";

    /*
     * Winlator S11 palettes: Windows 10/11 colours. Title bars match the dialog surface and
     * the 3D colours are set up so DrawEdge draws flat 1 px borders: raised edges (buttons,
     * frames) take their outer line from ButtonLight/ButtonDkShadow, sunken ones (edit
     * boxes) their inner line from ButtonDkShadow/ButtonLight, while ButtonHilight and
     * ButtonShadow stay close to the face.
     */
    private static final String[][] LIGHT_COLORS = {
        {"ActiveBorder", "243 243 243"}, {"ActiveTitle", "243 243 243"}, {"AppWorkspace", "171 171 171"},
        {"ButtonAlternateFace", "243 243 243"}, {"ButtonDkShadow", "160 160 160"}, {"ButtonFace", "243 243 243"},
        {"ButtonHilight", "255 255 255"}, {"ButtonLight", "191 191 191"}, {"ButtonShadow", "213 213 213"},
        {"ButtonText", "0 0 0"}, {"GradientActiveTitle", "243 243 243"}, {"GradientInactiveTitle", "243 243 243"},
        {"GrayText", "109 109 109"}, {"Hilight", "0 120 215"}, {"HilightText", "255 255 255"},
        {"HotTrackingColor", "0 102 204"}, {"InactiveBorder", "243 243 243"}, {"InactiveTitle", "243 243 243"},
        {"InactiveTitleText", "140 140 140"}, {"InfoText", "0 0 0"}, {"InfoWindow", "255 255 255"},
        {"Menu", "249 249 249"}, {"MenuBar", "243 243 243"}, {"MenuHilight", "0 120 215"}, {"MenuText", "0 0 0"},
        {"Scrollbar", "240 240 240"}, {"TitleText", "0 0 0"}, {"Window", "255 255 255"},
        {"WindowFrame", "160 160 160"}, {"WindowText", "0 0 0"}
    };

    private static final String[][] DARK_COLORS = {
        {"ActiveBorder", "32 32 32"}, {"ActiveTitle", "32 32 32"}, {"AppWorkspace", "40 40 40"},
        {"ButtonAlternateFace", "43 43 43"}, {"ButtonDkShadow", "96 96 96"}, {"ButtonFace", "43 43 43"},
        {"ButtonHilight", "58 58 58"}, {"ButtonLight", "80 80 80"}, {"ButtonShadow", "50 50 50"},
        {"ButtonText", "255 255 255"}, {"GradientActiveTitle", "32 32 32"}, {"GradientInactiveTitle", "32 32 32"},
        {"GrayText", "140 140 140"}, {"Hilight", "0 120 215"}, {"HilightText", "255 255 255"},
        {"HotTrackingColor", "96 205 255"}, {"InactiveBorder", "32 32 32"}, {"InactiveTitle", "32 32 32"},
        {"InactiveTitleText", "150 150 150"}, {"InfoText", "255 255 255"}, {"InfoWindow", "43 43 43"},
        {"Menu", "43 43 43"}, {"MenuBar", "32 32 32"}, {"MenuHilight", "0 120 215"}, {"MenuText", "255 255 255"},
        {"Scrollbar", "43 43 43"}, {"TitleText", "255 255 255"}, {"Window", "25 25 25"},
        {"WindowFrame", "96 96 96"}, {"WindowText", "255 255 255"}
    };

    public static class ThemeInfo {
        public final Theme theme;
        public final BackgroundType backgroundType;
        public final int backgroundColor;
        public final String wallpaperId;

        public ThemeInfo(String value) {
            String[] values = value.split(",");
            theme = Theme.valueOf(values[0]);

            if (values.length < 3) {
                backgroundColor = Color.parseColor(values[1]);
                backgroundType = BackgroundType.IMAGE;
            }
            else {
                backgroundType = BackgroundType.valueOf(values[1]);
                backgroundColor = Color.parseColor(values[2]);
            }

            String lastValue = values[values.length-1];
            wallpaperId = lastValue.startsWith("wallpaper-") ? lastValue : (lastValue.equals("0") ? DEFAULT_WALLPAPER_ID : "user-wallpaper");
        }
    }

    public static void apply(Context context, ThemeInfo themeInfo, ScreenInfo screenInfo) {
        File rootDir = RootFS.find(context).getRootDir();
        File userRegFile = new File(rootDir, RootFS.WINEPREFIX+"/user.reg");
        String background = Color.red(themeInfo.backgroundColor)+" "+Color.green(themeInfo.backgroundColor)+" "+Color.blue(themeInfo.backgroundColor);

        if (themeInfo.backgroundType == BackgroundType.IMAGE) createWallpaperBMPFile(context, themeInfo.wallpaperId, screenInfo);

        try (WineRegistryEditor registryEditor = new WineRegistryEditor(userRegFile)) {
            if (themeInfo.backgroundType == BackgroundType.IMAGE) {
                registryEditor.setStringValue("Control Panel\\Desktop", "Wallpaper", RootFS.getDosUserCachePath()+"\\wallpaper.bmp");
            }
            else registryEditor.removeValue("Control Panel\\Desktop", "Wallpaper");

            registryEditor.setStringValues("Control Panel\\Colors", themeInfo.theme == Theme.LIGHT ? LIGHT_COLORS : DARK_COLORS);
            registryEditor.setStringValue("Control Panel\\Colors", "Background", background);

            // Winlator S11: visual style (light theme) and Windows 10 window metrics
            ModernLook.applyTheme(rootDir, registryEditor, themeInfo.theme == Theme.LIGHT);
        }
    }

    private static void createWallpaperBMPFile(Context context, String wallpaperId, ScreenInfo screenInfo) {
        // Winlator S11: render at the screen's own size (Wine stretches the bitmap to the
        // screen, so the old 480 px tall bitmap came out blurry)
        int outputWidth = screenInfo.width;
        int outputHeight = screenInfo.height;

        Bitmap outputBitmap = Bitmap.createBitmap(outputWidth, outputHeight, Bitmap.Config.ARGB_8888);
        Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);
        Canvas canvas = new Canvas(outputBitmap);

        File userWallpaperFile = getUserWallpaperFile(context);
        if (wallpaperId.equals("user-wallpaper") && userWallpaperFile.isFile()) {
            Bitmap image = BitmapFactory.decodeFile(userWallpaperFile.getPath());
            if (image != null) drawCover(canvas, image, outputWidth, outputHeight, paint);
        }
        else if (wallpaperId.startsWith("wallpaper-")) {
            String wallpaperDir = "wallpapers/"+wallpaperId;
            JSONObject config;
            try {
                config = new JSONObject(FileUtils.readString(context, wallpaperDir+"/config.json"));
            }
            catch (JSONException | NullPointerException e) {
                return;
            }

            String fullImage = config.optString("full", "");
            if (!fullImage.isEmpty()) {
                // A picture that fills the screen
                Bitmap image = ImageUtils.getBitmapFromAsset(context, wallpaperDir+"/"+fullImage);
                if (image != null) drawCover(canvas, image, outputWidth, outputHeight, paint);
            }
            else {
                int primaryColor = Color.parseColor(config.optString("primaryColor", "#ffffff"));
                int secondaryColor = Color.parseColor(config.optString("secondaryColor", "#ffffff"));
                String orientation = config.optString("orientation", "none");
                float scale = (float)config.optDouble("scale", 1.0);
                Bitmap wallpaperBitmap = ImageUtils.getBitmapFromAsset(context, wallpaperDir+"/image.png");
                paint.setStyle(Paint.Style.FILL);

                if (orientation.equals("horizontal")) {
                    paint.setColor(primaryColor);
                    canvas.drawRect(0, 0, outputWidth * 0.5f, outputHeight, paint);
                    paint.setColor(secondaryColor);
                    canvas.drawRect(outputWidth * 0.5f, 0, outputWidth, outputHeight, paint);
                }
                else if (orientation.equals("vertical")) {
                    paint.setColor(primaryColor);
                    canvas.drawRect(0, 0, outputWidth, outputHeight * 0.5f, paint);
                    paint.setColor(secondaryColor);
                    canvas.drawRect(0, outputHeight * 0.5f, outputWidth, outputHeight, paint);
                }

                float targetSize = outputHeight * scale;
                float centerX = (outputWidth - targetSize) * 0.5f;
                float centerY = (outputHeight - targetSize) * 0.5f;
                Rect srcRect = new Rect(0, 0, wallpaperBitmap.getWidth(), wallpaperBitmap.getHeight());
                RectF dstRect = new RectF(centerX, centerY, centerX + targetSize, centerY + targetSize);
                canvas.drawBitmap(wallpaperBitmap, srcRect, dstRect, paint);
            }
        }

        RootFS rootFS = RootFS.find(context);
        MSBitmap.create(outputBitmap, new File(rootFS.getRootDir(), RootFS.USER_CACHE_PATH +"/wallpaper.bmp"));
        outputBitmap.recycle();
    }

    /** Scales the image to cover the whole output, cropping the overflow evenly. */
    private static void drawCover(Canvas canvas, Bitmap image, int outputWidth, int outputHeight, Paint paint) {
        float scale = Math.max((float)outputWidth / image.getWidth(), (float)outputHeight / image.getHeight());
        float width = image.getWidth() * scale;
        float height = image.getHeight() * scale;
        float left = (outputWidth - width) * 0.5f;
        float top = (outputHeight - height) * 0.5f;
        Rect srcRect = new Rect(0, 0, image.getWidth(), image.getHeight());
        canvas.drawBitmap(image, srcRect, new RectF(left, top, left + width, top + height), paint);
    }

    public static File getUserWallpaperFile(Context context) {
        return new File(RootFS.find(context).getRootDir(), RootFS.USER_CONFIG_PATH +"/user-wallpaper.png");
    }
}
