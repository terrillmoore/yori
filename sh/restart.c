/**
 * @file sh/restart.c
 *
 * Yori shell application recovery on restart
 *
 * Copyright (c) 2018 Malcolm J. Smith
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "yori.h"

/**
 Set to TRUE once the process has been registered for restart processing.
 This is only done once for the lifetime of the process.
 */
BOOL YoriShProcessRegisteredForRestart = FALSE;

/**
 Return a path to the temp directory, but allocate extra space for a file name
 to append to it.

 @param RestartFileName On successful completion, returns a newly allocated
        string populated with the temp directory.

 @param ExtraChars Specifies the number of extra characters to allocate in the
        string in addition to the temp directory size.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOL
YoriShGetTempPath(
    __out PYORI_STRING RestartFileName,
    __in DWORD ExtraChars
    )
{
    RestartFileName->LengthAllocated = GetTempPath(0, NULL);
    if (RestartFileName->LengthAllocated == 0) {
        return FALSE;
    }

    RestartFileName->LengthAllocated += ExtraChars;

    if (!YoriLibAllocateString(RestartFileName, RestartFileName->LengthAllocated)) {
        return FALSE;
    }

    RestartFileName->LengthInChars = GetTempPath(RestartFileName->LengthAllocated,
                                                 RestartFileName->StartOfString);


    return TRUE;
}

/**
 Try to save the current state of the process so that it can be recovered
 from this state after a subsequent unexpected termination.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOL
YoriShSaveRestartState()
{
    YORI_CONSOLE_SCREEN_BUFFER_INFOEX ScreenBufferInfo;
    YORI_CONSOLE_FONT_INFOEX FontInfo;

    YORI_STRING WriteBuffer;
    YORI_STRING RestartFileName;
    YORI_STRING RestartBufferFileName;
    YORI_STRING Env;
    DWORD Count;

    //
    //  The restart APIs are available in Vista+.  By happy coincidence, so
    //  is GetConsoleScreenBufferInfoEx, so if either don't exist, just give
    //  up; this allows us to keep a single piece of logic for recording
    //  console state.
    //

    if (DllKernel32.pRegisterApplicationRestart == NULL ||
        DllKernel32.pGetConsoleScreenBufferInfoEx == NULL ||
        DllKernel32.pGetCurrentConsoleFontEx == NULL) {

        return FALSE;
    }

    //
    //  If the user hasn't opted in by setting YORIAUTORESTART, do nothing.
    //

    Count = YoriShGetEnvironmentVariableWithoutSubstitution(_T("YORIAUTORESTART"), NULL, 0, NULL);
    if (Count == 0) {
        return FALSE;
    }

    if (!YoriLibAllocateString(&RestartFileName, Count)) {
        YoriLibFreeStringContents(&RestartFileName);
        return FALSE;
    }

    RestartFileName.LengthInChars = YoriShGetEnvironmentVariableWithoutSubstitution(_T("YORIAUTORESTART"), RestartFileName.StartOfString, RestartFileName.LengthAllocated, NULL);
    if (RestartFileName.LengthInChars == 0) {
        YoriLibFreeStringContents(&RestartFileName);
        return FALSE;
    }

    if (YoriLibCompareStringWithLiteral(&RestartFileName, _T("1")) != 0) {
        YoriLibFreeStringContents(&RestartFileName);
        return FALSE;
    }
    YoriLibFreeStringContents(&RestartFileName);

    //
    //  Query window dimensions and state, and save it.
    //

    ZeroMemory(&ScreenBufferInfo, sizeof(ScreenBufferInfo));
    ScreenBufferInfo.cbSize = sizeof(ScreenBufferInfo);

    if (!DllKernel32.pGetConsoleScreenBufferInfoEx(GetStdHandle(STD_OUTPUT_HANDLE), &ScreenBufferInfo)) {
        return FALSE;
    }

    if (!YoriShGetTempPath(&RestartFileName, sizeof("\\yori-restart-.ini") + 2 * sizeof(DWORD))) {
        return FALSE;
    }

    YoriLibSPrintf(RestartFileName.StartOfString + RestartFileName.LengthInChars,
                   _T("\\yori-restart-%x.ini"),
                   GetCurrentProcessId());

    if (!YoriLibAllocateString(&WriteBuffer, 64 * 1024)) {
        YoriLibFreeStringContents(&RestartFileName);
        return FALSE;
    }

    YoriLibSPrintf(WriteBuffer.StartOfString, _T("%i"), ScreenBufferInfo.dwSize.X);
    WritePrivateProfileString(_T("Window"), _T("BufferWidth"), WriteBuffer.StartOfString, RestartFileName.StartOfString);
    YoriLibSPrintf(WriteBuffer.StartOfString, _T("%i"), ScreenBufferInfo.dwSize.Y);
    WritePrivateProfileString(_T("Window"), _T("BufferHeight"), WriteBuffer.StartOfString, RestartFileName.StartOfString);
    YoriLibSPrintf(WriteBuffer.StartOfString, _T("%i"), ScreenBufferInfo.srWindow.Right - ScreenBufferInfo.srWindow.Left + 1);
    WritePrivateProfileString(_T("Window"), _T("WindowWidth"), WriteBuffer.StartOfString, RestartFileName.StartOfString);
    YoriLibSPrintf(WriteBuffer.StartOfString, _T("%i"), ScreenBufferInfo.srWindow.Bottom - ScreenBufferInfo.srWindow.Top + 1);
    WritePrivateProfileString(_T("Window"), _T("WindowHeight"), WriteBuffer.StartOfString, RestartFileName.StartOfString);

    YoriLibSPrintf(WriteBuffer.StartOfString, _T("%i"), YoriLibVtGetDefaultColor());
    WritePrivateProfileString(_T("Window"), _T("DefaultColor"), WriteBuffer.StartOfString, RestartFileName.StartOfString);
    YoriLibSPrintf(WriteBuffer.StartOfString, _T("%i"), ScreenBufferInfo.wPopupAttributes);
    WritePrivateProfileString(_T("Window"), _T("PopupColor"), WriteBuffer.StartOfString, RestartFileName.StartOfString);

    for (Count = 0; Count < sizeof(ScreenBufferInfo.ColorTable)/sizeof(ScreenBufferInfo.ColorTable[0]); Count++) {
        TCHAR ColorName[32];
        YoriLibSPrintf(ColorName, _T("Color%i"), Count);
        YoriLibSPrintf(WriteBuffer.StartOfString, _T("%i"), ScreenBufferInfo.ColorTable[Count]);
        WritePrivateProfileString(_T("Window"), ColorName, WriteBuffer.StartOfString, RestartFileName.StartOfString);
    }

    //
    //  Query the window title and save it.
    //

    WriteBuffer.LengthInChars = GetConsoleTitle(WriteBuffer.StartOfString, 4095);
    if (WriteBuffer.LengthInChars > 0) {
        WritePrivateProfileString(_T("Window"), _T("Title"), WriteBuffer.StartOfString, RestartFileName.StartOfString);
    } else {
        YoriLibOutput(YORI_LIB_OUTPUT_STDOUT, _T("Error getting window title: %i\n"), GetLastError());
    }

    //
    //  Query window font information and save it.
    //

    ZeroMemory(&FontInfo, sizeof(FontInfo));
    FontInfo.cbSize = sizeof(FontInfo);
    if (DllKernel32.pGetCurrentConsoleFontEx(GetStdHandle(STD_OUTPUT_HANDLE), FALSE, &FontInfo)) {
        YoriLibSPrintf(WriteBuffer.StartOfString, _T("%i"), FontInfo.nFont);
        WritePrivateProfileString(_T("Window"), _T("FontIndex"), WriteBuffer.StartOfString, RestartFileName.StartOfString);
        YoriLibSPrintf(WriteBuffer.StartOfString, _T("%i"), FontInfo.dwFontSize.X);
        WritePrivateProfileString(_T("Window"), _T("FontWidth"), WriteBuffer.StartOfString, RestartFileName.StartOfString);
        YoriLibSPrintf(WriteBuffer.StartOfString, _T("%i"), FontInfo.dwFontSize.Y);
        WritePrivateProfileString(_T("Window"), _T("FontHeight"), WriteBuffer.StartOfString, RestartFileName.StartOfString);
        YoriLibSPrintf(WriteBuffer.StartOfString, _T("%i"), FontInfo.FontFamily);
        WritePrivateProfileString(_T("Window"), _T("FontFamily"), WriteBuffer.StartOfString, RestartFileName.StartOfString);
        YoriLibSPrintf(WriteBuffer.StartOfString, _T("%i"), FontInfo.FontWeight);
        WritePrivateProfileString(_T("Window"), _T("FontWeight"), WriteBuffer.StartOfString, RestartFileName.StartOfString);
        WritePrivateProfileString(_T("Window"), _T("FontName"), FontInfo.FaceName, RestartFileName.StartOfString);
    }

    //
    //  Query the current directory and save it.
    //

    WriteBuffer.LengthInChars = GetCurrentDirectory(WriteBuffer.LengthAllocated, WriteBuffer.StartOfString);
    if (WriteBuffer.LengthInChars > 0 && WriteBuffer.LengthInChars < WriteBuffer.LengthAllocated) {
        WritePrivateProfileString(_T("Window"), _T("CurrentDirectory"), WriteBuffer.StartOfString, RestartFileName.StartOfString);
    }

    //
    //  Write the current environment
    //

    if (YoriLibGetEnvironmentStrings(&Env)) {
        LPTSTR ThisPair;
        LPTSTR ThisVar;
        LPTSTR ThisValue;

        ThisPair = Env.StartOfString;
        while (*ThisPair != '\0') {
            ThisVar = ThisPair;
            ThisPair += _tcslen(ThisPair) + 1;

            if (ThisVar[0] != '=') {
                ThisValue = _tcschr(ThisVar, '=');
                if (ThisValue) {
                    ThisValue[0] = '\0';
                    ThisValue++;

                    WritePrivateProfileString(_T("Environment"), ThisVar, ThisValue, RestartFileName.StartOfString);

                    ThisValue--;
                    ThisValue[0] = '=';
                }
            }
        }

        //
        //  With the "regular" environment done, go through and write a new
        //  section for current directories on alternate drives.  These are
        //  part of the environment but inexpressible in the INI format as
        //  regular entries, so they get their own section.
        //

        ThisPair = Env.StartOfString;
        while (*ThisPair != '\0') {
            ThisVar = ThisPair;
            ThisPair += _tcslen(ThisPair) + 1;

            if (ThisVar[0] == '=' &&
                ((ThisVar[1] >= 'A' && ThisVar[1] <= 'Z') ||
                 (ThisVar[1] >= 'a' && ThisVar[1] <= 'z')) &&
                ThisVar[2] == ':' &&
                ThisVar[3] == '=') {

                ThisValue = &ThisVar[3];
                ThisVar++;
                ThisValue[0] = '\0';
                ThisValue++;

                WritePrivateProfileString(_T("CurrentDirectories"), ThisVar, ThisValue, RestartFileName.StartOfString);

                ThisValue--;
                ThisValue[0] = '=';
            }
        }

        YoriLibFreeStringContents(&Env);
    }

    //
    //  Write the current aliases
    //

    if (YoriShGetAliasStrings(YORI_SH_GET_ALIAS_STRINGS_INCLUDE_USER, &Env)) {
        LPTSTR ThisPair;
        LPTSTR ThisVar;
        LPTSTR ThisValue;

        ThisPair = Env.StartOfString;
        while (*ThisPair != '\0') {
            ThisVar = ThisPair;
            ThisPair += _tcslen(ThisPair) + 1;
            if (ThisVar[0] != '=') {
                ThisValue = _tcschr(ThisVar, '=');
                if (ThisValue) {
                    ThisValue[0] = '\0';
                    ThisValue++;

                    WritePrivateProfileString(_T("Aliases"), ThisVar, ThisValue, RestartFileName.StartOfString);
                }
            }
        }

        YoriLibFreeStringContents(&Env);
    }

    //
    //  Write history.  We only care about values here but want to maintain
    //  sort order, so zero prefix count to use as a key.
    //

    if (YoriShGetHistoryStrings(100, &Env)) {
        LPTSTR ThisValue;

        ThisValue = Env.StartOfString;
        Count = 1;
        while (*ThisValue != '\0') {
            YoriLibSPrintf(WriteBuffer.StartOfString, _T("%03i"), Count);
            WritePrivateProfileString(_T("History"), WriteBuffer.StartOfString, ThisValue, RestartFileName.StartOfString);
            ThisValue += _tcslen(ThisValue) + 1;
            Count++;
        }

        YoriLibFreeStringContents(&Env);
    }

    //
    //  Write the window contents
    //

    if (YoriLibAllocateString(&RestartBufferFileName, RestartFileName.LengthAllocated)) {

        HANDLE hBufferFile;

        memcpy(RestartBufferFileName.StartOfString, RestartFileName.StartOfString, RestartFileName.LengthInChars * sizeof(TCHAR));
        RestartBufferFileName.LengthInChars = RestartFileName.LengthInChars;
        YoriLibSPrintf(RestartBufferFileName.StartOfString + RestartBufferFileName.LengthInChars,
                       _T("\\yori-restart-%x.txt"),
                       GetCurrentProcessId());

        hBufferFile = CreateFile(RestartBufferFileName.StartOfString,
                                 GENERIC_WRITE,
                                 FILE_SHARE_READ | FILE_SHARE_DELETE,
                                 NULL,
                                 CREATE_ALWAYS,
                                 FILE_ATTRIBUTE_NORMAL,
                                 NULL);

        if (hBufferFile != INVALID_HANDLE_VALUE) {
            YoriLibRewriteConsoleContents(hBufferFile, 0, 0);
            WritePrivateProfileString(_T("Window"), _T("Contents"), RestartBufferFileName.StartOfString, RestartFileName.StartOfString);
            CloseHandle(hBufferFile);
        }

        YoriLibFreeStringContents(&RestartBufferFileName);
    }

    //
    //  Register the process to be restarted on failure
    //
    
    if (!YoriShProcessRegisteredForRestart) {
        YoriLibSPrintf(WriteBuffer.StartOfString, _T("-restart %x"), GetCurrentProcessId());

        DllKernel32.pRegisterApplicationRestart(WriteBuffer.StartOfString, 0);
        YoriShProcessRegisteredForRestart = TRUE;
    }

    YoriLibFreeStringContents(&RestartFileName);
    YoriLibFreeStringContents(&WriteBuffer);

    return TRUE;
}


/**
 Try to recover a previous process ID that terminated unexpectedly.

 @param ProcessId Pointer to the process ID to try to recover.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOL
YoriShLoadSavedRestartState(
    __in PYORI_STRING ProcessId
    )
{
    YORI_STRING RestartFileName;
    YORI_STRING ReadBuffer;
    YORI_CONSOLE_SCREEN_BUFFER_INFOEX ScreenBufferInfo;
    DWORD Count;
    YORI_CONSOLE_FONT_INFOEX FontInfo;

    if (DllKernel32.pSetConsoleScreenBufferInfoEx == NULL ||
        DllKernel32.pSetCurrentConsoleFontEx == NULL) {

        return FALSE;
    }


    if (!YoriShGetTempPath(&RestartFileName, sizeof("\\yori-restart-.ini") + 2 * sizeof(DWORD))) {
        return FALSE;
    }

    ZeroMemory(&ScreenBufferInfo, sizeof(ScreenBufferInfo));
    ScreenBufferInfo.cbSize = sizeof(ScreenBufferInfo);

    YoriLibSPrintf(RestartFileName.StartOfString + RestartFileName.LengthInChars,
                   _T("\\yori-restart-%y.ini"),
                   ProcessId);

    //
    //  Read and populate window settings
    //

    ScreenBufferInfo.dwSize.X = (USHORT)GetPrivateProfileInt(_T("Window"), _T("BufferWidth"), 0, RestartFileName.StartOfString);
    ScreenBufferInfo.dwSize.Y = (USHORT)GetPrivateProfileInt(_T("Window"), _T("BufferHeight"), 0, RestartFileName.StartOfString);

    if (ScreenBufferInfo.dwSize.X == 0 || ScreenBufferInfo.dwSize.Y == 0) {
        YoriLibFreeStringContents(&RestartFileName);
        return FALSE;
    }

    ScreenBufferInfo.dwMaximumWindowSize.X = (USHORT)GetPrivateProfileInt(_T("Window"), _T("WindowWidth"), 0, RestartFileName.StartOfString);
    ScreenBufferInfo.dwMaximumWindowSize.Y = (USHORT)GetPrivateProfileInt(_T("Window"), _T("WindowHeight"), 0, RestartFileName.StartOfString);

    if (ScreenBufferInfo.dwMaximumWindowSize.X == 0 || ScreenBufferInfo.dwMaximumWindowSize.Y == 0) {
        YoriLibFreeStringContents(&RestartFileName);
        return FALSE;
    }

    ScreenBufferInfo.srWindow.Bottom = (USHORT)(ScreenBufferInfo.dwMaximumWindowSize.Y - 1);
    ScreenBufferInfo.srWindow.Right = (USHORT)(ScreenBufferInfo.dwMaximumWindowSize.X);

    ScreenBufferInfo.wAttributes = (USHORT)GetPrivateProfileInt(_T("Window"), _T("DefaultColor"), 0, RestartFileName.StartOfString);
    ScreenBufferInfo.wPopupAttributes = (USHORT)GetPrivateProfileInt(_T("Window"), _T("PopupColor"), 0, RestartFileName.StartOfString);

    for (Count = 0; Count < sizeof(ScreenBufferInfo.ColorTable)/sizeof(ScreenBufferInfo.ColorTable[0]); Count++) {
        TCHAR ColorName[32];
        YoriLibSPrintf(ColorName, _T("Color%i"), Count);
        ScreenBufferInfo.ColorTable[Count] = GetPrivateProfileInt(_T("Window"), ColorName, 0, RestartFileName.StartOfString);
    }

    YoriLibVtSetDefaultColor(ScreenBufferInfo.wAttributes);


    //
    //  Apparently GetConsoleTitle can't tell us how much memory it needs, but
    //  it needs less than 64Kb
    //

    if (!YoriLibAllocateString(&ReadBuffer, 64 * 1024)) {
        YoriLibFreeStringContents(&RestartFileName);
        return FALSE;
    }

    //
    //  Read and populate window fonts
    //

    ZeroMemory(&FontInfo, sizeof(FontInfo));
    FontInfo.cbSize = sizeof(FontInfo);
    FontInfo.nFont = GetPrivateProfileInt(_T("Window"), _T("FontIndex"), 0, RestartFileName.StartOfString);
    FontInfo.dwFontSize.X = (USHORT)GetPrivateProfileInt(_T("Window"), _T("FontWidth"), 0, RestartFileName.StartOfString);
    FontInfo.dwFontSize.Y = (USHORT)GetPrivateProfileInt(_T("Window"), _T("FontHeight"), 0, RestartFileName.StartOfString);
    FontInfo.FontFamily = GetPrivateProfileInt(_T("Window"), _T("FontFamily"), 0, RestartFileName.StartOfString);
    FontInfo.FontWeight = GetPrivateProfileInt(_T("Window"), _T("FontWeight"), 0, RestartFileName.StartOfString);
    GetPrivateProfileString(_T("Window"), _T("FontName"), _T(""), FontInfo.FaceName, sizeof(FontInfo.FaceName)/sizeof(FontInfo.FaceName[0]), RestartFileName.StartOfString);

    if (FontInfo.dwFontSize.X > 0 && FontInfo.dwFontSize.Y > 0 && FontInfo.FontWeight > 0) {
        DllKernel32.pSetCurrentConsoleFontEx(GetStdHandle(STD_OUTPUT_HANDLE), FALSE, &FontInfo);
    }

    DllKernel32.pSetConsoleScreenBufferInfoEx(GetStdHandle(STD_OUTPUT_HANDLE), &ScreenBufferInfo);

    //
    //  Read and populate the window title
    //

    GetPrivateProfileString(_T("Window"), _T("Title"), _T("Yori"), ReadBuffer.StartOfString, ReadBuffer.LengthAllocated, RestartFileName.StartOfString);
    SetConsoleTitle(ReadBuffer.StartOfString);

    //
    //  Read and populate the current directory
    //

    ReadBuffer.LengthInChars = GetPrivateProfileString(_T("Window"), _T("CurrentDirectory"), _T(""), ReadBuffer.StartOfString, ReadBuffer.LengthAllocated, RestartFileName.StartOfString);
    if (ReadBuffer.LengthInChars > 0) {
        SetCurrentDirectory(ReadBuffer.StartOfString);
    }

    //
    //  Populate the environment.
    //

    ReadBuffer.LengthInChars = GetPrivateProfileSection(_T("Environment"), ReadBuffer.StartOfString, ReadBuffer.LengthAllocated, RestartFileName.StartOfString);

    if (ReadBuffer.LengthInChars > 0) {
        LPTSTR ThisPair;
        LPTSTR ThisVar;
        LPTSTR ThisValue;

        ThisPair = ReadBuffer.StartOfString;
        while (*ThisPair != '\0') {
            ThisVar = ThisPair;
            ThisPair += _tcslen(ThisPair) + 1;
            if (ThisVar[0] != '=') {
                ThisValue = _tcschr(ThisVar, '=');
                if (ThisValue) {
                    ThisValue[0] = '\0';
                    ThisValue++;

                    SetEnvironmentVariable(ThisVar, ThisValue);
                }
            }
        }
    }

    //
    //  Populate current directories.
    //

    ReadBuffer.LengthInChars = GetPrivateProfileSection(_T("CurrentDirectories"), ReadBuffer.StartOfString, ReadBuffer.LengthAllocated, RestartFileName.StartOfString);

    if (ReadBuffer.LengthInChars > 0) {
        LPTSTR ThisPair;
        LPTSTR ThisVar;
        LPTSTR ThisValue;
        TCHAR DriveLetterBuffer[sizeof("=C:")];

        ThisPair = ReadBuffer.StartOfString;
        while (*ThisPair != '\0') {
            ThisVar = ThisPair;
            ThisPair += _tcslen(ThisPair) + 1;
            if (ThisVar[0] != '=') {
                ThisValue = _tcschr(ThisVar, '=');
                if (ThisValue) {
                    ThisValue[0] = '\0';
                    ThisValue++;

                    DriveLetterBuffer[0] = '=';
                    DriveLetterBuffer[1] = ThisVar[0];
                    DriveLetterBuffer[2] = ':';
                    DriveLetterBuffer[3] = '\0';

                    SetEnvironmentVariable(DriveLetterBuffer, ThisValue);
                }
            }
        }
    }

    //
    //  Populate aliases
    //

    ReadBuffer.LengthInChars = GetPrivateProfileSection(_T("Aliases"), ReadBuffer.StartOfString, ReadBuffer.LengthAllocated, RestartFileName.StartOfString);

    if (ReadBuffer.LengthInChars > 0) {
        LPTSTR ThisPair;
        LPTSTR ThisVar;
        LPTSTR ThisValue;

        ThisPair = ReadBuffer.StartOfString;
        while (*ThisPair != '\0') {
            ThisVar = ThisPair;
            ThisPair += _tcslen(ThisPair) + 1;
            if (ThisVar[0] != '=') {
                ThisValue = _tcschr(ThisVar, '=');
                if (ThisValue) {
                    ThisValue[0] = '\0';
                    ThisValue++;

                    YoriShAddAliasLiteral(ThisVar, ThisValue, FALSE);
                }
            }
        }
    }

    //
    //  Populate history
    //

    ReadBuffer.LengthInChars = GetPrivateProfileSection(_T("History"), ReadBuffer.StartOfString, ReadBuffer.LengthAllocated, RestartFileName.StartOfString);

    if (ReadBuffer.LengthInChars > 0) {
        LPTSTR ThisPair;
        LPTSTR ThisVar;
        LPTSTR ThisValue;
        YORI_STRING ThisEntry;
        DWORD ValueLength;

        YoriShInitHistory();

        ThisPair = ReadBuffer.StartOfString;
        while (*ThisPair != '\0') {
            ThisVar = ThisPair;
            ThisPair += _tcslen(ThisPair) + 1;
            if (ThisVar[0] != '=') {
                ThisValue = _tcschr(ThisVar, '=');
                if (ThisValue) {
                    ThisValue[0] = '\0';
                    ThisValue++;
                    ValueLength = _tcslen(ThisValue);
                    if (YoriLibAllocateString(&ThisEntry, ValueLength + 1)) {
                        memcpy(ThisEntry.StartOfString, ThisValue, (ValueLength + 1) * sizeof(TCHAR));
                        ThisEntry.LengthInChars = ValueLength;

                        YoriShAddToHistory(&ThisEntry);
                    }
                }
            }
        }
    }

    //
    //  Populate window contents
    //

    ReadBuffer.LengthInChars = GetPrivateProfileString(_T("Window"), _T("Contents"), _T(""), ReadBuffer.StartOfString, ReadBuffer.LengthAllocated, RestartFileName.StartOfString);

    if (ReadBuffer.LengthInChars > 0) {
        HANDLE hBufferFile;

        hBufferFile = CreateFile(ReadBuffer.StartOfString,
                                 GENERIC_READ,
                                 FILE_SHARE_READ | FILE_SHARE_DELETE,
                                 NULL,
                                 OPEN_EXISTING,
                                 FILE_ATTRIBUTE_NORMAL,
                                 NULL);

        if (hBufferFile != INVALID_HANDLE_VALUE) {
            YORI_STRING LineString;
            PVOID LineContext = NULL;
            YoriLibInitEmptyString(&LineString);
            while (TRUE) {
                if (!YoriLibReadLineToString(&LineString, &LineContext, hBufferFile)) {
                    break;
                }

                YoriLibOutput(YORI_LIB_OUTPUT_STDOUT, _T("%y"), &LineString);
            }
            YoriLibLineReadClose(LineContext);
            YoriLibFreeStringContents(&LineString);
            CloseHandle(hBufferFile);
        }
    }

    YoriLibFreeStringContents(&ReadBuffer);
    YoriLibFreeStringContents(&RestartFileName);

    return TRUE;
}

/**
 Delete any restart information from disk.

 @param ProcessId Optionally points to a process ID corresponding to the
        information to remove.  This is used after recovering that process ID.
        If this value is NULL, the current process ID is used.
 */
VOID
YoriShDiscardSavedRestartState(
    __in_opt PYORI_STRING ProcessId
    )
{
    YORI_STRING RestartFileName;

    if (!YoriShGetTempPath(&RestartFileName, sizeof("\\yori-restart-.ini") + 2 * sizeof(DWORD))) {
        return;
    }

    if (ProcessId != NULL) {
        YoriLibSPrintf(RestartFileName.StartOfString + RestartFileName.LengthInChars,
                       _T("\\yori-restart-%y.ini"),
                       ProcessId);
    } else {
        YoriLibSPrintf(RestartFileName.StartOfString + RestartFileName.LengthInChars,
                       _T("\\yori-restart-%x.ini"),
                       GetCurrentProcessId());
    }

    DeleteFile(RestartFileName.StartOfString);

    if (ProcessId != NULL) {
        YoriLibSPrintf(RestartFileName.StartOfString + RestartFileName.LengthInChars,
                       _T("\\yori-restart-%y.txt"),
                       ProcessId);
    } else {
        YoriLibSPrintf(RestartFileName.StartOfString + RestartFileName.LengthInChars,
                       _T("\\yori-restart-%x.txt"),
                       GetCurrentProcessId());
    }

    DeleteFile(RestartFileName.StartOfString);
    YoriLibFreeStringContents(&RestartFileName);
}



// vim:sw=4:ts=4:et:
