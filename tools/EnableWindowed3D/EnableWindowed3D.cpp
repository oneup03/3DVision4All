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

// EnableWindowed3D.exe -- companion tool for 3DVision4All.
//
// Some games ship in NVIDIA's predefined DRS profile database with stereo
// compatibility set to off (e.g. Batman Arkham Origins Blackgate HD). For
// those titles, the in-process NvAPI_Stereo_Activate call succeeds but
// NvAPI_Stereo_IsActivated stays 0 -- meaning ReverseStereoBlit silently
// no-ops and both halves of the staging surface carry the same mono
// content, so downstream consumers (anaglyph compose / LeiaSR weaver)
// produce no parallax.
//
// Fix: set two DRS settings on the per-title profile, in this order:
//
//   1. StereoProfile (id 0x701EB457) = 1. Officially STEREO_STEREOPROFILE.
//      The MERE EXISTENCE of this setting on a DX9 profile is what enables
//      stereo in windowed mode, regardless of value -- a lot of stereo
//      code in the driver is gated behind it, and without it certain
//      other stereo settings (StereoTextureEnable etc.) are ignored.
//      So this has to land BEFORE the hidden flag below or the hidden
//      flag has nothing to gate.
//
//   2. StereoHiddenProfile (id 0x70E46F20, unnamed in nvapi.h) = 1.
//      Flips the per-title compatibility override that the 3D Vision
//      driver consults to decide whether to actually activate stereo.
//
// Writing DRS settings requires elevation (NvAPI_DRS_SaveSettings returns
// NVAPI_ACCESS_DENIED otherwise). Doing it from the injected runtime DLL
// failed silently for non-elevated game launches, so this is a separate
// one-shot tool the user runs with admin rights once per game folder.
//
// Usage: drop EnableWindowed3D.exe in the game's folder (next to the
// game's main .exe), right-click -> Run as administrator. The tool
// scans its own directory for *.exe files (excluding itself) and writes
// StereoHiddenProfile=1 into each one's NV driver profile, creating a
// user profile + application entry when NV's DB has none for that EXE.

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

#include "nvapi.h"

#pragma comment(lib, "nvapi.lib")


// DRS setting IDs. StereoProfile is documented in NVIDIA Profile
// Inspector's schema; StereoHiddenProfile is hidden / unnamed in
// nvapi.h and comes from NV Inspector's CustomSetting XML.
static const NvU32 kStereoProfile_SettingID        = 0x701EB457;
static const NvU32 kStereoHiddenProfile_SettingID  = 0x70E46F20;


// Copy a wchar_t string into an NvAPI_UnicodeString (NvU16 array).
// wchar_t is 16-bit on Windows, so a memcpy preserves the encoding.
static void CopyToNvApiString(NvAPI_UnicodeString dst, const wchar_t* src)
{
    memset(dst, 0, sizeof(NvAPI_UnicodeString));
    size_t n = wcslen(src);
    if (n >= NVAPI_UNICODE_STRING_MAX) n = NVAPI_UNICODE_STRING_MAX - 1;
    memcpy(dst, src, n * sizeof(NvU16));
}


// Write StereoHiddenProfile=1 to the NV driver profile for exeBasename.
// Returns true on success. The session must already be open and have had
// LoadSettings called on it.
static bool SetStereoHiddenProfileForExe(NvDRSSessionHandle hSession,
                                         const wchar_t* exeBasename)
{
    NvAPI_UnicodeString appNameU;
    CopyToNvApiString(appNameU, exeBasename);

    NvDRSProfileHandle hProfile = nullptr;
    NVDRS_APPLICATION app = {};
    app.version = NVDRS_APPLICATION_VER;
    NvAPI_Status st = NvAPI_DRS_FindApplicationByName(hSession, appNameU, &hProfile, &app);
    if (st != NVAPI_OK) {
        // No existing profile contains this EXE. Create a user profile
        // named after the EXE and add the EXE to it. The "3DVision4All - "
        // prefix avoids clashing with any predefined profile name in
        // NV's DB.
        wprintf(L"  %s: not in any existing profile (status %d); creating user profile\n",
                exeBasename, st);

        wchar_t profileName[NVAPI_UNICODE_STRING_MAX];
        swprintf_s(profileName, NVAPI_UNICODE_STRING_MAX,
                   L"3DVision4All - %s", exeBasename);

        NVDRS_PROFILE profileInfo = {};
        profileInfo.version            = NVDRS_PROFILE_VER;
        profileInfo.isPredefined       = 0;
        profileInfo.gpuSupport.geforce = 1;
        profileInfo.gpuSupport.quadro  = 1;
        CopyToNvApiString(profileInfo.profileName, profileName);

        st = NvAPI_DRS_CreateProfile(hSession, &profileInfo, &hProfile);
        if (st != NVAPI_OK) {
            // Profile name may already exist from a previous run -- look it up
            // by name and reuse it.
            NvAPI_UnicodeString profileNameU;
            CopyToNvApiString(profileNameU, profileName);
            NvAPI_Status findSt = NvAPI_DRS_FindProfileByName(hSession, profileNameU, &hProfile);
            if (findSt != NVAPI_OK) {
                wprintf(L"  %s: CreateProfile failed %d, FindProfileByName fallback failed %d\n",
                        exeBasename, st, findSt);
                return false;
            }
        }

        NVDRS_APPLICATION newApp = {};
        newApp.version = NVDRS_APPLICATION_VER;
        newApp.isPredefined = 0;
        CopyToNvApiString(newApp.appName, exeBasename);
        CopyToNvApiString(newApp.userFriendlyName, exeBasename);
        st = NvAPI_DRS_CreateApplication(hSession, hProfile, &newApp);
        if (st != NVAPI_OK && st != NVAPI_EXECUTABLE_ALREADY_IN_USE) {
            wprintf(L"  %s: CreateApplication failed %d\n", exeBasename, st);
            return false;
        }
    } else {
        NVDRS_PROFILE profileInfo = {};
        profileInfo.version = NVDRS_PROFILE_VER;
        if (NvAPI_DRS_GetProfileInfo(hSession, hProfile, &profileInfo) == NVAPI_OK) {
            wprintf(L"  %s: found in profile \"%s\"\n",
                    exeBasename, (const wchar_t*)profileInfo.profileName);
        } else {
            wprintf(L"  %s: found in existing profile\n", exeBasename);
        }
    }

    // StereoProfile MUST land first: its mere presence on the profile
    // gates a lot of stereo code in the driver (windowed-mode activation,
    // StereoTextureEnable interpretation, profile save-via-Ctrl+F7).
    // The hidden flag below has nothing to gate without it.
    NVDRS_SETTING stereoProfile = {};
    stereoProfile.version         = NVDRS_SETTING_VER;
    stereoProfile.settingId       = kStereoProfile_SettingID;
    stereoProfile.settingType     = NVDRS_DWORD_TYPE;
    stereoProfile.u32CurrentValue = 0x00000001;
    st = NvAPI_DRS_SetSetting(hSession, hProfile, &stereoProfile);
    if (st != NVAPI_OK) {
        wprintf(L"  %s: SetSetting(StereoProfile=1) failed %d%s\n",
                exeBasename, st,
                st == NVAPI_ACCESS_DENIED ? L" (run as administrator)" : L"");
        return false;
    }

    NVDRS_SETTING hidden = {};
    hidden.version         = NVDRS_SETTING_VER;
    hidden.settingId       = kStereoHiddenProfile_SettingID;
    hidden.settingType     = NVDRS_DWORD_TYPE;
    hidden.u32CurrentValue = 0x00000001;
    st = NvAPI_DRS_SetSetting(hSession, hProfile, &hidden);
    if (st != NVAPI_OK) {
        wprintf(L"  %s: SetSetting(StereoHiddenProfile=1) failed %d%s\n",
                exeBasename, st,
                st == NVAPI_ACCESS_DENIED ? L" (run as administrator)" : L"");
        return false;
    }

    wprintf(L"  %s: wrote StereoProfile=1 + StereoHiddenProfile=1\n", exeBasename);
    return true;
}


int wmain(int argc, wchar_t* argv[])
{
    (void)argc; (void)argv;

    // Resolve our own directory; we scan THAT, not the CWD (which may be
    // wherever the user launched us from -- e.g. via a shortcut).
    wchar_t selfPath[MAX_PATH] = L"";
    GetModuleFileNameW(nullptr, selfPath, MAX_PATH);

    wchar_t selfBasename[MAX_PATH] = L"";
    const wchar_t* lastSlash = wcsrchr(selfPath, L'\\');
    if (lastSlash) wcscpy_s(selfBasename, MAX_PATH, lastSlash + 1);

    wchar_t scanDir[MAX_PATH] = L"";
    wcscpy_s(scanDir, MAX_PATH, selfPath);
    wchar_t* slash = wcsrchr(scanDir, L'\\');
    if (slash) *(slash + 1) = L'\0';

    wprintf(L"3DVision4All EnableWindowed3D\n");
    wprintf(L"Scanning %s for game executables...\n\n", scanDir);

    NvAPI_Status st = NvAPI_Initialize();
    if (st != NVAPI_OK) {
        wprintf(L"NvAPI_Initialize failed %d -- is an NVIDIA driver installed?\n", st);
        wprintf(L"\nPress Enter to exit...\n");
        (void)getwchar();
        return 1;
    }

    NvDRSSessionHandle hSession = nullptr;
    st = NvAPI_DRS_CreateSession(&hSession);
    if (st != NVAPI_OK) {
        wprintf(L"NvAPI_DRS_CreateSession failed %d\n", st);
        wprintf(L"\nPress Enter to exit...\n");
        (void)getwchar();
        return 1;
    }

    st = NvAPI_DRS_LoadSettings(hSession);
    if (st != NVAPI_OK) {
        wprintf(L"NvAPI_DRS_LoadSettings failed %d\n", st);
        NvAPI_DRS_DestroySession(hSession);
        wprintf(L"\nPress Enter to exit...\n");
        (void)getwchar();
        return 1;
    }

    // Scan our own directory for *.exe (non-recursive). The "next-to-the-
    // -game" install assumption keeps this simple -- one folder, all the
    // game's launcher / main / DLL-stub EXEs land in one DRS pass.
    wchar_t searchPattern[MAX_PATH] = L"";
    swprintf_s(searchPattern, MAX_PATH, L"%s*.exe", scanDir);

    WIN32_FIND_DATAW findData;
    HANDLE hFind = FindFirstFileW(searchPattern, &findData);
    if (hFind == INVALID_HANDLE_VALUE) {
        wprintf(L"No .exe files found in %s\n", scanDir);
        NvAPI_DRS_DestroySession(hSession);
        wprintf(L"\nPress Enter to exit...\n");
        (void)getwchar();
        return 1;
    }

    int touched = 0;
    int failed  = 0;
    do {
        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (_wcsicmp(findData.cFileName, selfBasename) == 0) continue;

        if (SetStereoHiddenProfileForExe(hSession, findData.cFileName))
            touched++;
        else
            failed++;
    } while (FindNextFileW(hFind, &findData));
    FindClose(hFind);

    if (touched > 0) {
        st = NvAPI_DRS_SaveSettings(hSession);
        if (st != NVAPI_OK) {
            wprintf(L"\nNvAPI_DRS_SaveSettings failed %d%s\n", st,
                    st == NVAPI_ACCESS_DENIED
                        ? L" -- run this tool as administrator (right-click -> Run as administrator)"
                        : L"");
            failed = touched;
            touched = 0;
        } else {
            wprintf(L"\nSaved %d profile change%s.\n",
                    touched, touched == 1 ? L"" : L"s");
        }
    }

    if (failed > 0)
        wprintf(L"%d EXE%s could not be updated.\n", failed, failed == 1 ? L"" : L"s");
    if (touched == 0 && failed == 0)
        wprintf(L"No EXEs to update.\n");

    NvAPI_DRS_DestroySession(hSession);

    wprintf(L"\nPress Enter to exit...\n");
    (void)getwchar();
    return (failed > 0 && touched == 0) ? 1 : 0;
}
