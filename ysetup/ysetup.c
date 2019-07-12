/**
 * @file ysetup/ysetup.c
 *
 * Yori shell bootstrap installer
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

#include <yoripch.h>
#include <yorilib.h>
#include <yoripkg.h>
#include "resource.h"

/**
 HWND_BROADCAST casts an integer to a pointer, which in 64 bit means
 increasing the size.
 */
#pragma warning(disable: 4306)

/**
 Help text to display to the user.
 */
const
CHAR strHelpText[] =
        "\n"
        "Installs a basic Yori system.\n"
        "\n"
        "YSETUP [-license] [directory]\n";

/**
 Display usage text to the user.
 */
BOOL
YsetupHelp()
{
    YoriLibOutput(YORI_LIB_OUTPUT_STDOUT, _T("Ysetup %i.%02i\n"), YSETUP_VER_MAJOR, YSETUP_VER_MINOR);
#if YORI_BUILD_ID
    YoriLibOutput(YORI_LIB_OUTPUT_STDOUT, _T("  Build %i\n"), YORI_BUILD_ID);
#endif
    YoriLibOutput(YORI_LIB_OUTPUT_STDOUT, _T("%hs"), strHelpText);
    return TRUE;
}

/**
 Install the default set of packages to a specified directory.

 @param InstallDirectory Specifies the directory to install to.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOL
SetupInstallToDirectory(
    __inout PYORI_STRING InstallDirectory
    )
{
    YORI_STRING PkgNames[4];

    if (!YoriLibCreateDirectoryAndParents(InstallDirectory)) {
        DWORD Err = GetLastError();
        LPTSTR ErrText = YoriLibGetWinErrorText(Err);
        YoriLibOutput(YORI_LIB_OUTPUT_STDERR, _T("ysetup: Could not create installation directory %y: %s\n"), InstallDirectory, ErrText);
        YoriLibFreeWinErrorText(ErrText);
        return FALSE;
    }

    YoriLibConstantString(&PkgNames[0], _T("yori-ypm"));
    YoriLibConstantString(&PkgNames[1], _T("yori-core"));
    YoriLibConstantString(&PkgNames[2], _T("yori-typical"));
    YoriLibConstantString(&PkgNames[3], _T("yori-completion"));

    if (YoriPkgInstallRemotePackages(PkgNames, sizeof(PkgNames)/sizeof(PkgNames[0]), InstallDirectory, NULL, NULL)) {
        return TRUE;
    }
    return FALSE;
}

/**
 Append a new path component to an existing registry path.

 @param hRootKey The root of the registry hive to update.

 @param SubKey The sub key to update.

 @param ValueName The name of the value to update.

 @param PathToAdd The path that should be added to the specified value.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOL
SetupAppendPath(
    __in HKEY hRootKey,
    __in LPTSTR SubKey,
    __in LPTSTR ValueName,
    __in PYORI_STRING PathToAdd
    )
{
    HKEY hKey;
    DWORD Err;
    DWORD Disposition;
    DWORD LengthRequired;
    YORI_STRING ExistingValue;

    Err = RegCreateKeyEx(hRootKey, SubKey, 0, NULL, 0, KEY_SET_VALUE | KEY_QUERY_VALUE, NULL, &hKey, &Disposition);
    if (Err != ERROR_SUCCESS) {
        return FALSE;
    }

    LengthRequired = 0;
    YoriLibInitEmptyString(&ExistingValue);
    Err = RegQueryValueEx(hKey, ValueName, NULL, NULL, NULL, &LengthRequired);
    if (Err == ERROR_MORE_DATA || LengthRequired > 0) {
        if (!YoriLibAllocateString(&ExistingValue, (LengthRequired / sizeof(TCHAR)) + PathToAdd->LengthInChars + 1)) {
            RegCloseKey(hKey);
            return FALSE;
        }

        Err = RegQueryValueEx(hKey, ValueName, NULL, NULL, (LPBYTE)ExistingValue.StartOfString, &LengthRequired);
        if (Err != ERROR_SUCCESS) {
            YoriLibFreeStringContents(&ExistingValue);
            RegCloseKey(hKey);
            return FALSE;
        }

        ExistingValue.LengthInChars = LengthRequired / sizeof(TCHAR) - 1;

        if (!YoriLibAddEnvironmentComponentToString(&ExistingValue, PathToAdd, TRUE)) {
            YoriLibFreeStringContents(&ExistingValue);
            RegCloseKey(hKey);
            return FALSE;
        }

        Err = RegSetValueEx(hKey, ValueName, 0, REG_EXPAND_SZ, (LPBYTE)ExistingValue.StartOfString, (ExistingValue.LengthInChars + 1) * sizeof(TCHAR));
        RegCloseKey(hKey);
        YoriLibFreeStringContents(&ExistingValue);
    } else {
        Err = RegSetValueEx(hKey, ValueName, 0, REG_EXPAND_SZ, (LPBYTE)PathToAdd->StartOfString, (PathToAdd->LengthInChars + 1) * sizeof(TCHAR));
        RegCloseKey(hKey);
    }

    if (Err == ERROR_SUCCESS) {
        return TRUE;
    }
    return FALSE;
}


/**
 Install the user specified set of packages and options from the dialog.

 @param hDlg Specifies the hWnd of the dialog box.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOL
SetupInstallSelectedFromDialog(
    __in HWND hDlg
    )
{
    DWORD LengthNeeded;
    YORI_STRING InstallDir;
    YORI_STRING StatusText;
    PYORI_STRING PkgNames;
    PYORI_STRING PackageUrls;
    DWORD PkgCount;
    DWORD PkgIndex;
    DWORD PkgUrlCount;
    BOOL Result = FALSE;
    enum {
        InstallTypeCore = 1,
        InstallTypeTypical = 2,
        InstallTypeComplete = 3
    } InstallType;

    //
    //  Query the install directory and attempt to create it
    //

    LengthNeeded = (DWORD)SendDlgItemMessage(hDlg, IDC_INSTALLDIR, WM_GETTEXTLENGTH, 0, 0);
    if (!YoriLibAllocateString(&InstallDir, LengthNeeded + 1)) {
        MessageBox(hDlg, _T("Installation failed."), _T("Installation failed."), MB_ICONSTOP);
        return FALSE;
    }
    InstallDir.LengthInChars = GetDlgItemText(hDlg, IDC_INSTALLDIR, InstallDir.StartOfString, InstallDir.LengthAllocated);


    //
    //  Truncate trailing seperators
    //

    while (InstallDir.LengthInChars > 0 &&
           YoriLibIsSep(InstallDir.StartOfString[InstallDir.LengthInChars - 1])) {

        InstallDir.StartOfString[InstallDir.LengthInChars - 1] = '\0';
        InstallDir.LengthInChars--;
    }

    if (InstallDir.LengthInChars == 0) {
        MessageBox(hDlg, _T("Installation failed."), _T("Installation failed."), MB_ICONSTOP);
        YoriLibFreeStringContents(&InstallDir);
        return FALSE;
    }

    if (!YoriLibCreateDirectoryAndParents(&InstallDir)) {
        MessageBox(hDlg, _T("Failed to create installation directory.  If installing into a system location, you may want to run the installer as Administrator."), _T("Installation failed."), MB_ICONSTOP);
        YoriLibFreeStringContents(&InstallDir);
        return FALSE;
    }

    //
    //  Count the number of packages we want to install
    //

    if (IsDlgButtonChecked(hDlg, IDC_COMPLETE)) {
        PkgCount = 4;
        InstallType = InstallTypeComplete;
    } else if (IsDlgButtonChecked(hDlg, IDC_COREONLY)) {
        PkgCount = 2;
        InstallType = InstallTypeCore;
    } else {
        PkgCount = 3;
        InstallType = InstallTypeTypical;
    }

    if (IsDlgButtonChecked(hDlg, IDC_SYMBOLS)) {
        PkgCount *= 2;
    }

    PkgCount++;

    if (IsDlgButtonChecked(hDlg, IDC_SOURCE)) {
        PkgCount++;
    }

    //
    //  Allocate a package array
    //

    PkgNames = YoriLibMalloc(PkgCount * sizeof(YORI_STRING));
    if (PkgNames == NULL) {
        MessageBox(hDlg, _T("Installation failed."), _T("Installation failed."), MB_ICONSTOP);
        YoriLibFreeStringContents(&InstallDir);
        return FALSE;
    }

    //
    //  Populate the array with the names of what we want to install
    //

    if (IsDlgButtonChecked(hDlg, IDC_SYMBOLS)) {
        YoriLibConstantString(&PkgNames[0], _T("yori-ypm"));
        YoriLibConstantString(&PkgNames[1], _T("yori-ypm-pdb"));
        YoriLibConstantString(&PkgNames[2], _T("yori-core"));
        YoriLibConstantString(&PkgNames[3], _T("yori-core-pdb"));
        PkgIndex = 4;
        if (InstallType >= InstallTypeTypical) {
            YoriLibConstantString(&PkgNames[4], _T("yori-typical"));
            YoriLibConstantString(&PkgNames[5], _T("yori-typical-pdb"));
            PkgIndex = 6;
        }
        if (InstallType >= InstallTypeComplete) {
            YoriLibConstantString(&PkgNames[6], _T("yori-extra"));
            YoriLibConstantString(&PkgNames[7], _T("yori-extra-pdb"));
            PkgIndex = 8;
        }
    } else {
        YoriLibConstantString(&PkgNames[0], _T("yori-ypm"));
        YoriLibConstantString(&PkgNames[1], _T("yori-core"));
        PkgIndex = 2;
        if (InstallType >= InstallTypeTypical) {
            YoriLibConstantString(&PkgNames[2], _T("yori-typical"));
            PkgIndex = 3;
        }
        if (InstallType >= InstallTypeComplete) {
            YoriLibConstantString(&PkgNames[3], _T("yori-extra"));
            PkgIndex = 4;
        }
    }

    YoriLibConstantString(&PkgNames[PkgIndex], _T("yori-completion"));
    PkgIndex++;

    if (IsDlgButtonChecked(hDlg, IDC_SOURCE)) {
        YoriLibConstantString(&PkgNames[PkgIndex], _T("yori-source"));
        PkgIndex++;
    }

    //
    //  Obtain URLs for the specified packages.
    //

    YoriLibInitEmptyString(&StatusText);
    SetDlgItemText(hDlg, IDC_STATUS, _T("Obtaining package URLs..."));
    PkgUrlCount = YoriPkgGetRemotePackageUrls(PkgNames, PkgIndex, &InstallDir, &PackageUrls);

    if (PkgUrlCount != PkgCount) {
        MessageBox(hDlg, _T("Could not locate selected package files."), _T("Installation failed."), MB_ICONSTOP);
        goto Exit;
    }

    //
    //  Install the packages
    //

    for (PkgCount = 0; PkgCount < PkgUrlCount; PkgCount++) {
        YoriLibYPrintf(&StatusText, _T("Installing %i of %i: %y"), PkgCount + 1, PkgUrlCount, &PackageUrls[PkgCount]);
        if (StatusText.StartOfString != NULL) {
            SetDlgItemText(hDlg, IDC_STATUS, StatusText.StartOfString);
        }
        if (!YoriPkgInstallSinglePackage(&PackageUrls[PkgCount], &InstallDir)) {
            YoriLibYPrintf(&StatusText, _T("Failed to install %y from %y"), &PkgNames[PkgCount], &PackageUrls[PkgCount]);
            MessageBox(hDlg, StatusText.StartOfString, _T("Installation failed."), MB_ICONSTOP);
            goto Exit;
        }
    }

    SetDlgItemText(hDlg, IDC_STATUS, _T("Applying installation options..."));

    //
    //  Create shortcuts if requested
    //

    if (IsDlgButtonChecked(hDlg, IDC_DESKTOP_SHORTCUT) ||
        IsDlgButtonChecked(hDlg, IDC_START_SHORTCUT)) {

        YORI_STRING YoriExeFullPath;
        YORI_STRING ShortcutNameFullPath;
        YORI_STRING RelativeShortcutName;
        YORI_STRING Description;

        YoriLibInitEmptyString(&YoriExeFullPath);
        YoriLibInitEmptyString(&ShortcutNameFullPath);
        YoriLibConstantString(&Description, _T("Yori"));

        YoriLibYPrintf(&YoriExeFullPath, _T("%y\\yori.exe"), &InstallDir);
        if (YoriExeFullPath.StartOfString == NULL) {
            MessageBox(hDlg, _T("Installation failed."), _T("Installation failed."), MB_ICONSTOP);
            goto Exit;
        }

        if (IsDlgButtonChecked(hDlg, IDC_DESKTOP_SHORTCUT)) {
            YoriLibConstantString(&RelativeShortcutName, _T("~Desktop\\Yori.lnk"));
            if (!YoriLibUserStringToSingleFilePath(&RelativeShortcutName, TRUE, &ShortcutNameFullPath)) {
                YoriLibFreeStringContents(&YoriExeFullPath);
                MessageBox(hDlg, _T("Installation failed."), _T("Installation failed."), MB_ICONSTOP);
                goto Exit;
            }
            if (!YoriLibCreateShortcut(&ShortcutNameFullPath, &YoriExeFullPath, NULL, &Description, NULL, NULL, 0, 1, (WORD)-1, TRUE, TRUE)) {
                YoriLibFreeStringContents(&YoriExeFullPath);
                YoriLibFreeStringContents(&ShortcutNameFullPath);
                MessageBox(hDlg, _T("Failed to create desktop shortcut."), _T("Installation failed."), MB_ICONSTOP);
                goto Exit;
            }

            YoriLibFreeStringContents(&ShortcutNameFullPath);
        }

        if (IsDlgButtonChecked(hDlg, IDC_START_SHORTCUT)) {
            YoriLibConstantString(&RelativeShortcutName, _T("~Programs\\Yori.lnk"));
            if (!YoriLibUserStringToSingleFilePath(&RelativeShortcutName, TRUE, &ShortcutNameFullPath)) {
                YoriLibFreeStringContents(&YoriExeFullPath);
                MessageBox(hDlg, _T("Installation failed."), _T("Installation failed."), MB_ICONSTOP);
                goto Exit;
            }
            if (!YoriLibCreateShortcut(&ShortcutNameFullPath, &YoriExeFullPath, NULL, &Description, NULL, NULL, 0, 1, (WORD)-1, TRUE, TRUE)) {
                YoriLibFreeStringContents(&YoriExeFullPath);
                YoriLibFreeStringContents(&ShortcutNameFullPath);
                MessageBox(hDlg, _T("Failed to create start menu shortcut."), _T("Installation failed."), MB_ICONSTOP);
                goto Exit;
            }

            YoriLibFreeStringContents(&ShortcutNameFullPath);
        }

        YoriLibFreeStringContents(&YoriExeFullPath);
    }

    //
    //  Update paths if requested
    //

    if (IsDlgButtonChecked(hDlg, IDC_USER_PATH)) {
        SetupAppendPath(HKEY_CURRENT_USER, _T("Environment"), _T("Path"), &InstallDir);
    }

    if (IsDlgButtonChecked(hDlg, IDC_SYSTEM_PATH)) {
        SetupAppendPath(HKEY_LOCAL_MACHINE, _T("SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment"), _T("Path"), &InstallDir);
    }

    if (IsDlgButtonChecked(hDlg, IDC_USER_PATH) ||
        IsDlgButtonChecked(hDlg, IDC_SYSTEM_PATH)) {

        DWORD_PTR NotifyResult;
        SendMessageTimeout(HWND_BROADCAST, WM_WININICHANGE, 0, (LPARAM)_T("Environment"), SMTO_NORMAL, 200, &NotifyResult);
    }

    SetDlgItemText(hDlg, IDC_STATUS, _T("Installation complete."));
    MessageBox(hDlg, _T("Installation complete."), _T("Installation complete."), MB_ICONINFORMATION);
    Result = TRUE;
Exit:
    for (PkgCount = 0; PkgCount < PkgUrlCount; PkgCount++) {
        YoriLibFreeStringContents(&PackageUrls[PkgCount]);
    }
    YoriLibDereference(PackageUrls);
    YoriLibFreeStringContents(&InstallDir);
    YoriLibFreeStringContents(&StatusText);
    YoriLibFree(PkgNames);
    return Result;
}

/**
 The default application install directory, under Program Files.
 */
#define SETUP_APP_DIR "\\Yori"

/**
 The default application install directory, under Program Files, as Unicode.
 */
#define TSETUP_APP_DIR _T("\\Yori")

/**
 Return the default installation directory.  This is normally
 "C:\Program Files\Yori" but the path can be reconfigured.

 @param InstallDir On successful completion, populated with the default
        install path.

 @return TRUE to indicate successful completion, FALSE on error.
 */
BOOL
SetupGetDefaultInstallDir(
    __out PYORI_STRING InstallDir
    )
{
    HKEY hKey;
    DWORD SizeNeeded;
    DWORD RegType;

    if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, _T("SOFTWARE\\Microsoft\\Windows\\CurrentVersion"), 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
        YoriLibConstantString(InstallDir, _T("C:\\Program Files"));
        return TRUE;
    }

    if (RegQueryValueEx(hKey, _T("ProgramW6432Dir"), NULL, NULL, NULL, &SizeNeeded) == ERROR_SUCCESS) {
        if (!YoriLibAllocateString(InstallDir, SizeNeeded/sizeof(TCHAR) + sizeof(SETUP_APP_DIR))) {
            goto ReturnDefault;
        }

        SizeNeeded = InstallDir->LengthAllocated * sizeof(TCHAR);
        if (RegQueryValueEx(hKey, _T("ProgramW6432Dir"), NULL, &RegType, (LPBYTE)InstallDir->StartOfString, &SizeNeeded) == ERROR_SUCCESS) {
            if (RegType != REG_SZ && RegType != REG_EXPAND_SZ) {
                goto ReturnDefault;
            }
            InstallDir->LengthInChars = SizeNeeded / sizeof(TCHAR) - 1;
            if (InstallDir->LengthInChars + sizeof(SETUP_APP_DIR) <= InstallDir->LengthAllocated) {
                memcpy(&InstallDir->StartOfString[InstallDir->LengthInChars], TSETUP_APP_DIR, sizeof(SETUP_APP_DIR) * sizeof(TCHAR));
                InstallDir->LengthInChars += sizeof(SETUP_APP_DIR) - 1;
            } else {
                InstallDir->StartOfString[InstallDir->LengthInChars] = '\0';
            }
        }
        RegCloseKey(hKey);
        return TRUE;
    }

    if (RegQueryValueEx(hKey, _T("ProgramFilesDir"), NULL, NULL, NULL, &SizeNeeded) == ERROR_SUCCESS) {
        if (!YoriLibAllocateString(InstallDir, SizeNeeded/sizeof(TCHAR) + sizeof("\\Yori"))) {
            goto ReturnDefault;
        }

        SizeNeeded = InstallDir->LengthAllocated * sizeof(TCHAR);
        if (RegQueryValueEx(hKey, _T("ProgramFilesDir"), NULL, &RegType, (LPBYTE)InstallDir->StartOfString, &SizeNeeded) == ERROR_SUCCESS) {
            if (RegType != REG_SZ && RegType != REG_EXPAND_SZ) {
                goto ReturnDefault;
            }
            InstallDir->LengthInChars = SizeNeeded / sizeof(TCHAR) - 1;
            if (InstallDir->LengthInChars + sizeof(SETUP_APP_DIR) <= InstallDir->LengthAllocated) {
                memcpy(&InstallDir->StartOfString[InstallDir->LengthInChars], TSETUP_APP_DIR, sizeof(SETUP_APP_DIR) * sizeof(TCHAR));
                InstallDir->LengthInChars += sizeof(SETUP_APP_DIR) - 1;
            } else {
                InstallDir->StartOfString[InstallDir->LengthInChars] = '\0';
            }
        }
        RegCloseKey(hKey);
        return TRUE;
    }


ReturnDefault:
    YoriLibConstantString(InstallDir, _T("C:\\Program Files") TSETUP_APP_DIR);
    RegCloseKey(hKey);
    return TRUE;
}

/**
 The DialogProc for the setup dialog box.

 @param hDlg Specifies the hWnd of the dialog box.

 @param uMsg Specifies the window message received by the dialog box.

 @param wParam Specifies the first parameter to the window message.

 @param lParam Specifeis the second parameter to the window message.

 @return TRUE to indicate the message was successfully processed.
 */
BOOL CALLBACK
SetupUiDialogProc(
    __in HWND hDlg,
    __in UINT uMsg,
    __in WPARAM wParam,
    __in LPARAM lParam
    )
{
    RECT rcDesktop, rcDlg, rcNew;
    WORD CtrlId;
    HICON hIcon;
    YORI_STRING InstallDir;

    UNREFERENCED_PARAMETER(lParam);

    switch(uMsg) {
        case WM_COMMAND:
            switch(LOWORD(wParam)) {
                case IDC_COREONLY:
                case IDC_TYPICAL:
                case IDC_COMPLETE:
                    for (CtrlId = IDC_COREONLY; CtrlId <= IDC_COMPLETE; CtrlId++) {
                        CheckDlgButton(hDlg, CtrlId, FALSE);
                    }
                    CheckDlgButton(hDlg, LOWORD(wParam), TRUE);
                    break;
                case IDC_DESKTOP_SHORTCUT:
                case IDC_START_SHORTCUT:
                case IDC_SYSTEM_PATH:
                case IDC_USER_PATH:
                case IDC_SOURCE:
                case IDC_SYMBOLS:
                    CtrlId = LOWORD(wParam);
                    if (IsDlgButtonChecked(hDlg, CtrlId)) {
                        CheckDlgButton(hDlg, CtrlId, FALSE);
                    } else {
                        CheckDlgButton(hDlg, CtrlId, TRUE);
                    }
                    break;
                case IDC_OK:
                    if (!SetupInstallSelectedFromDialog(hDlg)) {
                        EndDialog(hDlg, FALSE);
                    } else {
                        EndDialog(hDlg, TRUE);
                    }
                    return TRUE;
                case IDC_CANCEL:
                    EndDialog(hDlg, FALSE);
                    return TRUE;
                case IDC_BROWSE:
                    YoriLibLoadShell32Functions();
                    if (DllShell32.pSHBrowseForFolderW != NULL &&
                        DllShell32.pSHGetPathFromIDListW != NULL) {
                        YORI_BROWSEINFO BrowseInfo;
                        PVOID ShellIdentifierForPath;
                        ZeroMemory(&BrowseInfo, sizeof(BrowseInfo));
                        BrowseInfo.hWndOwner = hDlg;
                        BrowseInfo.Title = _T("Please select a folder to install Yori:");
                        BrowseInfo.Flags = 0x51;
                        ShellIdentifierForPath = DllShell32.pSHBrowseForFolderW(&BrowseInfo);
                        if (ShellIdentifierForPath != NULL) {
                            YoriLibAllocateString(&InstallDir, MAX_PATH);
                            DllShell32.pSHGetPathFromIDListW(ShellIdentifierForPath, InstallDir.StartOfString);
                            SetDlgItemText(hDlg, IDC_INSTALLDIR, InstallDir.StartOfString);
                            YoriLibFreeStringContents(&InstallDir);
                            if (DllOle32.pCoTaskMemFree != NULL) {
                                DllOle32.pCoTaskMemFree(ShellIdentifierForPath);
                            }
                        }
                    }
            }
            break;
        case WM_CLOSE:
            EndDialog(hDlg, 0);
            return TRUE;
        case WM_INITDIALOG:
            hIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(1));
            SendMessage(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
            SendMessage(hDlg, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
            GetWindowRect(GetDesktopWindow(), &rcDesktop);
            GetWindowRect(hDlg, &rcDlg);

            rcNew.left = ((rcDesktop.right - rcDesktop.left) - (rcDlg.right - rcDlg.left)) / 2;
            rcNew.top = ((rcDesktop.bottom - rcDesktop.top) - (rcDlg.bottom - rcDlg.top)) / 2;

            SetWindowPos(hDlg, HWND_TOP, rcNew.left, rcNew.top, 0, 0, SWP_NOSIZE);

            {
                TCHAR Version[32];

#if YORI_BUILD_ID
                YoriLibSPrintf(Version, _T("%i.%02i.%i"), YSETUP_VER_MAJOR, YSETUP_VER_MINOR, YORI_BUILD_ID);
#else
                YoriLibSPrintf(Version, _T("%i.%02i"), YSETUP_VER_MAJOR, YSETUP_VER_MINOR);
#endif
                SetDlgItemText(hDlg, IDC_VERSION, Version);
            }

            YoriLibInitEmptyString(&InstallDir);
            SetupGetDefaultInstallDir(&InstallDir);
            SetDlgItemText(hDlg, IDC_INSTALLDIR, InstallDir.StartOfString);
            YoriLibFreeStringContents(&InstallDir);
            CheckDlgButton(hDlg, IDC_TYPICAL, TRUE);
            return TRUE;

    }
    return FALSE;
}

/**
 Display a dialog allowing the user to select the installation options and
 perform the requested operation.

 @return TRUE to indicate successful completion, FALSE to indicate failure.
 */
BOOL
SetupDisplayUi()
{
    CONSOLE_SCREEN_BUFFER_INFO ScreenInfo;

    //
    //  Initialize COM for the benefit of the shell's browse for folder
    //  dialog
    //

    YoriLibLoadOle32Functions();
    if (DllOle32.pCoInitialize != NULL) {
        DllOle32.pCoInitialize(NULL);
    }

    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &ScreenInfo)) {
        if (ScreenInfo.dwCursorPosition.X == 0 && ScreenInfo.dwCursorPosition.Y == 0) {
            FreeConsole();
        }
    }

    DialogBox(NULL, MAKEINTRESOURCE(SETUPDIALOG), NULL, (DLGPROC)SetupUiDialogProc);
    return TRUE;
}


/**
 The main entrypoint for the setup cmdlet.

 @param ArgC The number of arguments.

 @param ArgV An array of arguments.

 @return Exit code of the child process on success, or failure if the child
         could not be launched.
 */
DWORD
ymain(
    __in DWORD ArgC,
    __in YORI_STRING ArgV[]
    )
{
    BOOL ArgumentUnderstood;
    DWORD i;
    DWORD StartArg = 0;
    YORI_STRING Arg;
    YORI_STRING NewDirectory;

    YoriLibInitEmptyString(&NewDirectory);

    for (i = 1; i < ArgC; i++) {

        ArgumentUnderstood = FALSE;
        ASSERT(YoriLibIsStringNullTerminated(&ArgV[i]));

        if (YoriLibIsCommandLineOption(&ArgV[i], &Arg)) {

            if (YoriLibCompareStringWithLiteralInsensitive(&Arg, _T("?")) == 0) {
                YsetupHelp();
                return EXIT_SUCCESS;
            } else if (YoriLibCompareStringWithLiteralInsensitive(&Arg, _T("license")) == 0) {
                YoriLibDisplayMitLicense(_T("2018"));
                return EXIT_SUCCESS;
            } else if (YoriLibCompareStringWithLiteralInsensitive(&Arg, _T("-")) == 0) {
                ArgumentUnderstood = TRUE;
                StartArg = i + 1;
                break;
            }
        } else {
            ArgumentUnderstood = TRUE;
            StartArg = i;
            break;
        }

        if (!ArgumentUnderstood) {
            YoriLibOutput(YORI_LIB_OUTPUT_STDERR, _T("Argument not understood, ignored: %y\n"), &ArgV[i]);
        }
    }

    if (StartArg > 0) {
        YoriLibUserStringToSingleFilePath(&ArgV[StartArg], TRUE, &NewDirectory);
        if (!SetupInstallToDirectory(&NewDirectory)) {
            YoriLibOutput(YORI_LIB_OUTPUT_STDERR, _T("ysetup: install failed\n"));
            YoriLibFreeStringContents(&NewDirectory);
            return EXIT_FAILURE;
        }
        YoriLibFreeStringContents(&NewDirectory);
    } else {
        SetupDisplayUi();
    }


    return EXIT_SUCCESS;
}

// vim:sw=4:ts=4:et:
