package com.winlator.core;

import android.content.Context;

import com.winlator.container.Container;
import com.winlator.win32.MSLink;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.File;

public abstract class WineStartMenuCreator {
    // Winlator S11: Korean names for the entries of wine_startmenu.json. "Programs" stays: it is
    // the Windows folder of that name.
    private static final String[][] KOREAN_NAMES = {
        {"System Tools", "시스템 도구"}, {"Games", "게임"}, {"Installers", "설치 도구"},
        {"Notepad", "메모장"}, {"Wordpad", "워드패드"}, {"WineMine", "지뢰 찾기"},
        {"GPUInfo", "GPU 정보"}, {"Test Direct3D", "Direct3D 테스트"}, {"Computer", "파일 탐색기"},
        {"Task Manager", "작업 관리자"}, {"Registry Editor", "레지스트리 편집기"},
        {"Command Prompt", "명령 프롬프트"}, {"Wine Configuration", "Wine 설정"},
        {"Wine Mono (.NET Framework)", "Wine Mono 설치 (.NET Framework)"},
        {"Wine Gecko (For HTML Pages)", "Wine Gecko 설치 (HTML 표시)"},
        {"Steam (Legacy)", "Steam 설치 (구버전)"}
    };

    private static void localize(JSONArray items) throws JSONException {
        for (int i = 0; i < items.length(); i++) {
            JSONObject item = items.getJSONObject(i);
            String name = item.getString("name");
            for (String[] pair : KOREAN_NAMES) {
                if (pair[0].equals(name)) {
                    item.put("name", pair[1]);
                    break;
                }
            }
            if (item.has("children")) localize(item.getJSONArray("children"));
        }
    }

    private static int parseShowCommand(String value) {
        if (value.equals("SW_SHOWMAXIMIZED")) {
            return MSLink.SW_SHOWMAXIMIZED;
        }
        else if (value.equals("SW_SHOWMINNOACTIVE")) {
            return MSLink.SW_SHOWMINNOACTIVE;
        }
        else return MSLink.SW_SHOWNORMAL;
    }

    private static void createMenuEntry(JSONObject item, File currentDir) throws JSONException {
        if (item.has("children")) {
            currentDir = new File(currentDir, item.getString("name"));
            currentDir.mkdirs();

            JSONArray children = item.getJSONArray("children");
            for (int i = 0; i < children.length(); i++) createMenuEntry(children.getJSONObject(i), currentDir);
        }
        else {
            File outputFile = new File(currentDir, item.getString("name")+".lnk");
            MSLink.LinkInfo linkInfo = new MSLink.LinkInfo();
            linkInfo.targetPath = item.getString("path");
            linkInfo.arguments = item.optString("cmdArgs");
            linkInfo.iconLocation = item.optString("iconLocation", linkInfo.targetPath);
            linkInfo.iconIndex = item.optInt("iconIndex", 0);
            if (item.has("showCommand")) linkInfo.showCommand = parseShowCommand(item.getString("showCommand"));
            MSLink.createFile(linkInfo, outputFile);
        }
    }

    private static void removeMenuEntry(JSONObject item, File currentDir) throws JSONException {
        if (item.has("children")) {
            currentDir = new File(currentDir, item.getString("name"));

            JSONArray children = item.getJSONArray("children");
            for (int i = 0; i < children.length(); i++) removeMenuEntry(children.getJSONObject(i), currentDir);

            if (FileUtils.isEmpty(currentDir)) currentDir.delete();
        }
        else (new File(currentDir, item.getString("name")+".lnk")).delete();
    }

    private static void removeOldMenu(File containerStartMenuFile, File startMenuDir) {
        if (!containerStartMenuFile.isFile()) return;
        try {
            JSONArray data = new JSONArray(FileUtils.readString(containerStartMenuFile));
            for (int i = 0; i < data.length(); i++) removeMenuEntry(data.getJSONObject(i), startMenuDir);
        }
        catch (JSONException e) {
            containerStartMenuFile.delete();
        }
    }

    public static void create(Context context, Container container) {
        File startMenuDir = container.getStartMenuDir();
        File containerStartMenuFile = new File(container.getRootDir(), ".startmenu");
        removeOldMenu(containerStartMenuFile, startMenuDir);

        try {
            JSONArray data = new JSONArray(FileUtils.readString(context, "wine_startmenu.json"));
            if (java.util.Locale.getDefault().getLanguage().equals("ko")) localize(data);
            // what was created, so the next start can remove exactly that (names may be localized)
            FileUtils.writeString(containerStartMenuFile, data.toString());
            for (int i = 0; i < data.length(); i++) createMenuEntry(data.getJSONObject(i), startMenuDir);
        }
        catch (JSONException e) {}
    }
}
