#pragma once

// Plain #defines: this header is also read by the resource compiler.
// Control IDs 1100-1399 belong to a page (11xx Play, 12xx Setup, 13xx Settings); the rest are always shown.
// Captions carry their style in the tens digit: x4x heading, x5x label, x7x hint, x8x warning.
#define IDD_MAIN 100
#define IDI_APP 101
#define IDR_WALLPAPER 102

#define IDC_TAB_PLAY 1001
#define IDC_TAB_SETUP 1002
#define IDC_TAB_SETTINGS 1003
#define IDC_MINIMIZE 1004
#define IDC_MESSAGE 1005

#define IDC_PLAY_HEADING 1140
#define IDC_PLAY_MOD_LABEL 1150
#define IDC_PLAY_RUNTIME_LABEL 1151
#define IDC_PLAY_GAME_LABEL 1152
#define IDC_MOD 1110
#define IDC_RUNTIME_STATUS 1111
#define IDC_RUNNING 1112
#define IDC_START 1113
#define IDC_OPEN_LOG 1114
#define IDC_MODE 1115

#define IDC_SETUP_GAME_HEADING 1240
#define IDC_SETUP_MOD_HEADING 1241
#define IDC_SETUP_VR_HEADING 1242
#define IDC_SETUP_FOLDER_LABEL 1250
#define IDC_SETUP_BUILD_LABEL 1251
#define IDC_SETUP_PROFILE_LABEL 1252
#define IDC_SETUP_INI_LABEL 1253
#define IDC_SETUP_HINT 1270
#define IDC_SETUP_MODE_HINT 1271
#define IDC_SETUP_WARNING 1280
#define IDC_FOLDER 1210
#define IDC_CHANGE_FOLDER 1211
#define IDC_OPEN_FOLDER 1212
#define IDC_BUILD 1213
#define IDC_MOD_SETUP 1214
#define IDC_INSTALL 1215
#define IDC_REMOVE 1216
#define IDC_PROFILE_STATUS 1217
#define IDC_INI_STATUS 1218
#define IDC_SETUP 1219
#define IDC_CHECK_UPDATE 1221

// The Settings page is one child window (launcher/settings_view), created at start-up.
#define IDC_SETTINGS_VIEW 1390
