/*
 * This file is part of 3DVision4All.
 *
 * 3DVision4All is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * 3DVision4All is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with 3DVision4All. If not, see <http://www.gnu.org/licenses/>.
 */

#include "Core.h"

#include <shlwapi.h>
#pragma comment(lib, "Shlwapi.lib")


// Resolve 3dvision4all.ini path next to the running EXE.
static void GetIniPath(wchar_t out[MAX_PATH])
{
    wchar_t exePath[MAX_PATH] = L"";
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    wchar_t* slash = wcsrchr(exePath, L'\\');
    if (slash) *(slash + 1) = L'\0';
    swprintf_s(out, MAX_PATH, L"%s3dvision4all.ini", exePath);
}


// Resolve a path string from the INI: if relative, anchor it to the EXE
// directory so logs land next to the game.
static void ResolveRelativePath(const wchar_t* in, wchar_t out[MAX_PATH])
{
    if (PathIsRelativeW(in)) {
        wchar_t exePath[MAX_PATH] = L"";
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        wchar_t* slash = wcsrchr(exePath, L'\\');
        if (slash) *(slash + 1) = L'\0';
        swprintf_s(out, MAX_PATH, L"%s%s", exePath, in);
    } else {
        wcscpy_s(out, MAX_PATH, in);
    }
}


static StereoMode ParseMode(const wchar_t* s)
{
    if (_wcsicmp(s, L"sbs")               == 0) return StereoMode::Sbs;
    if (_wcsicmp(s, L"tab")               == 0) return StereoMode::Tab;
    if (_wcsicmp(s, L"row_interlaced")    == 0) return StereoMode::RowInterlaced;
    if (_wcsicmp(s, L"column_interlaced") == 0) return StereoMode::ColumnInterlaced;
    if (_wcsicmp(s, L"checkerboard")      == 0) return StereoMode::Checkerboard;
    if (_wcsicmp(s, L"leiasr")            == 0) return StereoMode::LeiaSR;
    return StereoMode::Sbs;
}


void Config_Load(Config& cfg)
{
    wchar_t iniPath[MAX_PATH] = L"";
    GetIniPath(iniPath);

    wchar_t buf[MAX_PATH];

    GetPrivateProfileStringW(L"stereo", L"mode", L"sbs", buf, _countof(buf), iniPath);
    cfg.mode = ParseMode(buf);

    cfg.swap_eyes = GetPrivateProfileIntW(L"stereo", L"swap_eyes", 0, iniPath) != 0;

    cfg.defeat_directflip = GetPrivateProfileIntW(L"render", L"defeat_directflip", 1, iniPath);

    cfg.force_windowed = GetPrivateProfileIntW(L"render", L"force_windowed", 1, iniPath);

    cfg.disable_vsync = GetPrivateProfileIntW(L"render", L"disable_vsync", 0, iniPath);

    cfg.confine_cursor = GetPrivateProfileIntW(L"render", L"confine_cursor", 0, iniPath);
    cfg.hide_cursor    = GetPrivateProfileIntW(L"render", L"hide_cursor",    0, iniPath);

    cfg.alternate_capture_mode = GetPrivateProfileIntW(L"render", L"alternate_capture_mode", 1, iniPath);

    cfg.render_width  = (UINT)GetPrivateProfileIntW(L"render", L"render_width",  0, iniPath);
    cfg.render_height = (UINT)GetPrivateProfileIntW(L"render", L"render_height", 0, iniPath);

    cfg.copy_width  = (UINT)GetPrivateProfileIntW(L"render", L"copy_width",  0, iniPath);
    cfg.copy_height = (UINT)GetPrivateProfileIntW(L"render", L"copy_height", 0, iniPath);

    cfg.install_device_hooks            = GetPrivateProfileIntW(L"debug", L"install_device_hooks",            1, iniPath);
    cfg.install_d3d9_vtable_hooks       = GetPrivateProfileIntW(L"debug", L"install_d3d9_vtable_hooks",       1, iniPath);
    cfg.install_d3d9_display_mode_hooks = GetPrivateProfileIntW(L"debug", L"install_d3d9_display_mode_hooks", 1, iniPath);

    wchar_t logRel[MAX_PATH] = L"";
    GetPrivateProfileStringW(L"debug", L"log_file", L"3dvision4all.log",
                             logRel, _countof(logRel), iniPath);
    ResolveRelativePath(logRel, cfg.log_path);

    cfg.log_level = GetPrivateProfileIntW(L"debug", L"log_level", 1, iniPath);
}
