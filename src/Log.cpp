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

#include <stdarg.h>
#include <share.h>

static FILE* s_logFile = nullptr;
static CRITICAL_SECTION s_logCS;
static bool s_logCSInit = false;


void Log_Init(const wchar_t* logPath)
{
    if (!s_logCSInit) {
        InitializeCriticalSection(&s_logCS);
        s_logCSInit = true;
    }

    if (s_logFile) {
        fclose(s_logFile);
        s_logFile = nullptr;
    }

    if (!logPath || !*logPath)
        return;

    // Truncate on each run — last run is what matters when debugging.
    s_logFile = _wfsopen(logPath, L"w", _SH_DENYNO);
    if (s_logFile) {
        setvbuf(s_logFile, nullptr, _IONBF, 0);
        fwprintf(s_logFile, L"\n==== 3DVision4All log opened ====\n");
        fflush(s_logFile);
    }
}


void Log_Close()
{
    if (s_logFile) {
        fwprintf(s_logFile, L"==== 3DVision4All log closed ====\n\n");
        fclose(s_logFile);
        s_logFile = nullptr;
    }
}


void Log_Write(const wchar_t* fmt, ...)
{
    if (!s_logFile) return;

    EnterCriticalSection(&s_logCS);

    va_list args;
    va_start(args, fmt);
    vfwprintf(s_logFile, fmt, args);
    va_end(args);
    fflush(s_logFile);

    LeaveCriticalSection(&s_logCS);
}


void Log_Fatal(const wchar_t* msg, HRESULT code)
{
    wchar_t info[512];
    swprintf_s(info, _countof(info), L"3DVision4All fatal: %s (0x%x)\n", msg, code);
    Log_Write(L"%s", info);

    MessageBoxW(nullptr, info, L"3DVision4All: Fatal Error", MB_OK | MB_ICONERROR);
    exit(1);
}
