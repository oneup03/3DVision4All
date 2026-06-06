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
