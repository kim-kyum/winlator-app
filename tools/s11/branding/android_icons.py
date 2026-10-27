#!/usr/bin/env python3
"""The icons of S11 Workspace's Android screens: Winlator's bitmap icons replaced by Material
Symbols Rounded (outlined, weight 400; Apache License 2.0), under the same resource names, so
every screen, menu and dialog that uses them gets one consistent, plain line style.

The vectors are tinted with a theme colour unless the view tints them itself; the toolbar's
icons take the text colour, the others the grey of inactive controls.

Usage: python -I android_icons.py <material-design-icons checkout> <app/src/main/res>
  (a sparse checkout is enough: symbols/android/<name>/materialsymbolsrounded/<name>_24px.xml)
"""
import os
import re
import sys

# resource name -> Material Symbols name
ICONS = {
    "icon_about": "info",
    "icon_active_windows": "select_window",
    "icon_add": "add",
    "icon_audio_settings": "volume_up",
    "icon_confirm": "check",
    "icon_container": "desktop_windows",
    "icon_cpu": "memory",
    "icon_debug": "bug_report",
    "icon_display_settings": "display_settings",
    "icon_duplicate": "content_copy",
    "icon_edit": "edit",
    "icon_env_var": "data_object",
    "icon_exit": "logout",
    "icon_export": "upload",
    "icon_folder_search": "folder_open",
    "icon_fullscreen": "fullscreen",
    "icon_gamepad": "sports_esports",
    "icon_help": "help",
    "icon_hide": "visibility_off",
    "icon_info": "info",
    "icon_input_controls": "gamepad",
    "icon_install": "download",
    "icon_keyboard": "keyboard",
    "icon_list_item_menu": "more_vert",
    "icon_magnifier": "zoom_in",
    "icon_memory": "memory_alt",
    "icon_menu": "menu",
    "icon_move": "open_with",
    "icon_open": "open_in_new",
    "icon_paste": "content_paste",
    "icon_pause": "pause",
    "icon_piano": "piano",
    "icon_pip_mode": "picture_in_picture_alt",
    "icon_play": "play_arrow",
    "icon_power": "battery_full",
    "icon_remove": "delete",
    "icon_remove2": "close",
    "icon_reset": "restart_alt",
    "icon_run": "play_arrow",
    "icon_screen_effect": "contrast",
    "icon_settings": "settings",
    "icon_shortcut": "apps",
    "icon_task_manager": "monitoring",
    "icon_temperature": "device_thermostat",
    "icon_wine": "wine_bar",
    "icon_zoom_minus": "zoom_out",
    "icon_zoom_plus": "zoom_in",
    "icon_action_bar_add": "add",
    "icon_action_bar_back": "arrow_back",
    "icon_action_bar_grid": "grid_view",
    "icon_action_bar_home": "home",
    "icon_action_bar_list": "view_list",
    "icon_action_bar_menu": "menu",
    "icon_action_bar_new_folder": "create_new_folder",
    "icon_popup_menu_add_favorite": "star",
    "icon_popup_menu_bring_to_front": "flip_to_front",
    "icon_popup_menu_copy": "content_copy",
    "icon_popup_menu_cpu": "memory",
    "icon_popup_menu_cut": "content_cut",
    "icon_popup_menu_download": "download",
    "icon_popup_menu_duplicate": "file_copy",
    "icon_popup_menu_edit": "edit",
    "icon_popup_menu_file_manager": "folder",
    "icon_popup_menu_folder_list": "folder_copy",
    "icon_popup_menu_folder_open": "folder_open",
    "icon_popup_menu_info": "info",
    "icon_popup_menu_remove": "delete",
    "icon_popup_menu_settings": "settings",
    # the S11 container settings pages
    "s11_ic_general": "tune",
    "s11_ic_graphics": "display_settings",
    "s11_ic_desktop": "desktop_windows",
    "s11_ic_components": "extension",
    "s11_ic_drives": "hard_drive",
    "s11_ic_env": "data_object",
    "s11_ic_advanced": "build",
    "s11_ic_audio": "volume_up",
    "s11_ic_chevron_down": "expand_more",
    # (the power menu keeps its own coloured icons: s11_ic_power, _restart, _force, _menu)
}

TEXT_COLOURED = ("icon_action_bar_",)


def main(symbols_root, res):
    drawable = os.path.join(res, "drawable")
    for res_name, symbol in sorted(ICONS.items()):
        source = os.path.join(symbols_root, "symbols", "android", symbol, "materialsymbolsrounded", symbol + "_24px.xml")
        xml = open(source, encoding="utf-8").read()
        paths = re.findall(r'android:pathData="([^"]+)"', xml)
        if not paths:
            sys.exit("no path in " + source)
        tint = "?attr/colorPrimaryText" if res_name.startswith(TEXT_COLOURED) else "?attr/colorControlNormal"
        out = ['<?xml version="1.0" encoding="utf-8"?>',
               f'<!-- Material Symbols Rounded "{symbol}" (Apache License 2.0); made by android_icons.py -->',
               '<vector xmlns:android="http://schemas.android.com/apk/res/android"',
               '    android:width="24dp"',
               '    android:height="24dp"',
               '    android:viewportWidth="960"',
               '    android:viewportHeight="960"',
               f'    android:tint="{tint}">']
        for path in paths:
            out += ['    <path',
                    '        android:fillColor="@android:color/white"',
                    f'        android:pathData="{path}" />']
        out.append('</vector>')
        with open(os.path.join(drawable, res_name + ".xml"), "w", encoding="utf-8") as f:
            f.write("\n".join(out) + "\n")
        for folder in os.listdir(res):
            if folder.startswith("drawable-"):
                for ext in (".png", ".9.png", ".webp"):
                    old = os.path.join(res, folder, res_name + ext)
                    if os.path.exists(old):
                        os.remove(old)
    print(f"{len(ICONS)} icons")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
