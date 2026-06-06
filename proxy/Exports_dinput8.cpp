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

// Forwarder exports for dinput8.dll proxy.
//
// Forwarders use the absolute System32 path, so no rename is required —
// the user just drops dinput8.dll (our proxy) into the game folder.
//
// Parser note: the LAST "." in the /EXPORT: RHS separates module from
// function name. Module = "C:\Windows\System32\dinput8" (no extension,
// LoadLibrary appends .dll), Function = "DirectInput8Create".
//
// The .def file syntax `Name=OtherDll.Other` is NOT honored by modern
// MSVC link.exe — it treats it as a rename and fails LNK2001. The
// /EXPORT: switch IS documented to accept forwarders.

#pragma comment(linker, "/EXPORT:DirectInput8Create=C:\\Windows\\System32\\dinput8.DirectInput8Create")
#pragma comment(linker, "/EXPORT:DllCanUnloadNow=C:\\Windows\\System32\\dinput8.DllCanUnloadNow,PRIVATE")
#pragma comment(linker, "/EXPORT:DllGetClassObject=C:\\Windows\\System32\\dinput8.DllGetClassObject,PRIVATE")
#pragma comment(linker, "/EXPORT:DllRegisterServer=C:\\Windows\\System32\\dinput8.DllRegisterServer,PRIVATE")
#pragma comment(linker, "/EXPORT:DllUnregisterServer=C:\\Windows\\System32\\dinput8.DllUnregisterServer,PRIVATE")
#pragma comment(linker, "/EXPORT:GetdfDIJoystick=C:\\Windows\\System32\\dinput8.GetdfDIJoystick")
