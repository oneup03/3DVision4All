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

// Shared DllMain for all four 3DVision4All proxy DLLs (dinput8, dsound,
// winmm, version). Each proxy's .vcxproj compiles this same file alongside
// its own .def (which forwards exports to <name>_real.dll).
//
// The proxy DLL has no exports of its own beyond the .def forwarders. Its
// only job is to invoke Injector_EnsureInit() on attach, which spawns
// the init thread that installs all the D3D9 / NvAPI hooks.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

extern "C" void Injector_EnsureInit(HMODULE hSelf);


BOOL APIENTRY DllMain(HMODULE hSelf, DWORD reason, LPVOID /*reserved*/)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hSelf);
        Injector_EnsureInit(hSelf);
    }
    return TRUE;
}
