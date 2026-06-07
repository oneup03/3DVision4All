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

// Forwarder exports for dsound.dll proxy. See Exports_dinput8.cpp for the
// absolute-path forwarder rationale. No rename required.
//
// Direct3DCreate9 / Direct3DCreate9Ex are exported as REAL FUNCTIONS (not
// forwarders) — defined in DllMain.cpp as Proxy_Direct3DCreate9 /
// Proxy_Direct3DCreate9Ex. Some games (Alice: Madness Returns is the
// known case) get their EXE import table patched by HelixMod / 3D-fix
// installers to pull the D3D9 entry points through DSOUND. Real exports
// give us a defined hook point: we install our IDirect3D9 vtable hooks on
// the returned object before handing it back to the EXE, so HelixMod's
// per-game fix AND our SbS-overlay composition both run.
//
// Harmless when no patched EXE is involved — the exports just sit unused.
// WINAPI is __stdcall, which on x86 decorates the OBJ symbol as
// _Name@bytes (4 bytes per ptr/UINT param). The /EXPORT directive operates
// on OBJ-level symbols so we must match the decorated name on x86; on x64
// there's no stdcall decoration.
#ifdef _M_IX86
#pragma comment(linker, "/EXPORT:Direct3DCreate9=_Proxy_Direct3DCreate9@4")
#pragma comment(linker, "/EXPORT:Direct3DCreate9Ex=_Proxy_Direct3DCreate9Ex@8")
#else
#pragma comment(linker, "/EXPORT:Direct3DCreate9=Proxy_Direct3DCreate9")
#pragma comment(linker, "/EXPORT:Direct3DCreate9Ex=Proxy_Direct3DCreate9Ex")
#endif

#pragma comment(linker, "/EXPORT:DirectSoundCaptureCreate=C:\\Windows\\System32\\dsound.DirectSoundCaptureCreate")
#pragma comment(linker, "/EXPORT:DirectSoundCaptureCreate8=C:\\Windows\\System32\\dsound.DirectSoundCaptureCreate8")
#pragma comment(linker, "/EXPORT:DirectSoundCaptureEnumerateA=C:\\Windows\\System32\\dsound.DirectSoundCaptureEnumerateA")
#pragma comment(linker, "/EXPORT:DirectSoundCaptureEnumerateW=C:\\Windows\\System32\\dsound.DirectSoundCaptureEnumerateW")
#pragma comment(linker, "/EXPORT:DirectSoundCreate=C:\\Windows\\System32\\dsound.DirectSoundCreate")
#pragma comment(linker, "/EXPORT:DirectSoundCreate8=C:\\Windows\\System32\\dsound.DirectSoundCreate8")
#pragma comment(linker, "/EXPORT:DirectSoundEnumerateA=C:\\Windows\\System32\\dsound.DirectSoundEnumerateA")
#pragma comment(linker, "/EXPORT:DirectSoundEnumerateW=C:\\Windows\\System32\\dsound.DirectSoundEnumerateW")
#pragma comment(linker, "/EXPORT:DirectSoundFullDuplexCreate=C:\\Windows\\System32\\dsound.DirectSoundFullDuplexCreate")
#pragma comment(linker, "/EXPORT:DllCanUnloadNow=C:\\Windows\\System32\\dsound.DllCanUnloadNow,PRIVATE")
#pragma comment(linker, "/EXPORT:DllGetClassObject=C:\\Windows\\System32\\dsound.DllGetClassObject,PRIVATE")
#pragma comment(linker, "/EXPORT:GetDeviceID=C:\\Windows\\System32\\dsound.GetDeviceID")
