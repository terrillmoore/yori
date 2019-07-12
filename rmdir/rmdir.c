/**
 * @file rmdir/rmdir.c
 *
 * Yori shell rmdir
 *
 * Copyright (c) 2017-2019 Malcolm J. Smith
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
 * FITNESS RMDIR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE RMDIR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <yoripch.h>
#include <yorilib.h>

/**
 Help text to display to the user.
 */
const
CHAR strRmdirHelpText[] =
        "\n"
        "Removes directories.\n"
        "\n"
        "RMDIR [-license] [-b] [-r] [-s] <dir> [<dir>...]\n"
        "\n"
        "   -b             Use basic search criteria for directories only\n"
        "   -l             Delete links without contents\n"
        "   -r             Send directories to the recycle bin\n"
        "   -s             Remove all contents of each directory\n";

/**
 Display usage text to the user.
 */
BOOL
RmdirHelp()
{
    YoriLibOutput(YORI_LIB_OUTPUT_STDOUT, _T("Rmdir %i.%02i\n"), RMDIR_VER_MAJOR, RMDIR_VER_MINOR);
#if YORI_BUILD_ID
    YoriLibOutput(YORI_LIB_OUTPUT_STDOUT, _T("  Build %i\n"), YORI_BUILD_ID);
#endif
    YoriLibOutput(YORI_LIB_OUTPUT_STDOUT, _T("%hs"), strRmdirHelpText);
    return TRUE;
}

/**
 Context information when files are found.
 */
typedef struct _RMDIR_CONTEXT {

    /**
     If TRUE, objects should be sent to the recycle bin rather than directly
     deleted.
     */
    BOOL RecycleBin;
} RMDIR_CONTEXT, *PRMDIR_CONTEXT;

/**
 A callback that is invoked when a file is found that matches a search criteria
 specified in the set of strings to enumerate.

 @param FilePath Pointer to the file path that was found.

 @param FileInfo Information about the file.

 @param Depth Specifies the recursion depth.  Ignored in this application.

 @param Context Pointer to a RMDIR_CONTEXT.

 @return TRUE to continute enumerating, FALSE to abort.
 */
BOOL
RmdirFileFoundCallback(
    __in PYORI_STRING FilePath,
    __in PWIN32_FIND_DATA FileInfo,
    __in DWORD Depth,
    __in PVOID Context
    )
{
    DWORD Err = NO_ERROR;
    LPTSTR ErrText;
    DWORD OldAttributes;
    DWORD NewAttributes;
    BOOL FileDeleted;
    PRMDIR_CONTEXT RmdirContext = (PRMDIR_CONTEXT)Context;

    UNREFERENCED_PARAMETER(Depth);

    ASSERT(YoriLibIsStringNullTerminated(FilePath));

    FileDeleted = FALSE;

    //
    //  Try to delete it.
    //

    if (RmdirContext->RecycleBin) {
        if (YoriLibRecycleBinFile(FilePath)) {
            FileDeleted = TRUE;
        }
    }

    if (!FileDeleted) {
        if ((FileInfo->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
            if (!DeleteFile(FilePath->StartOfString)) {
                Err = GetLastError();
            }
        } else {
            if (!RemoveDirectory(FilePath->StartOfString)) {
                Err = GetLastError();
            }
        }
    }


    //
    //  If it fails with access denied, try to remove any readonly, hidden or
    //  system attributes which might be getting in the way, then try the
    //  delete again.
    //

    if (Err == ERROR_ACCESS_DENIED) {

        OldAttributes = GetFileAttributes(FilePath->StartOfString);
        NewAttributes = OldAttributes & ~(FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);

        if (OldAttributes != NewAttributes) {
            SetFileAttributes(FilePath->StartOfString, NewAttributes);

            Err = NO_ERROR;

            if ((FileInfo->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
                if (!DeleteFile(FilePath->StartOfString)) {
                    Err = GetLastError();
                }
            } else {
                if (!RemoveDirectory(FilePath->StartOfString)) {
                    Err = GetLastError();
                }
            }

            if (Err != NO_ERROR) {
                SetFileAttributes(FilePath->StartOfString, OldAttributes);
            }
        }
    }

    //
    //  If we still can't delete it, return an error.
    //

    if (Err != NO_ERROR) {
        ErrText = YoriLibGetWinErrorText(Err);
        if ((FileInfo->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
            YoriLibOutput(YORI_LIB_OUTPUT_STDERR, _T("rmdir: delete failed: %y: %s"), FilePath, ErrText);
        } else {
            YoriLibOutput(YORI_LIB_OUTPUT_STDERR, _T("rmdir: rmdir failed: %y: %s"), FilePath, ErrText);
        }
        YoriLibFreeWinErrorText(ErrText);
    }
    return TRUE;
}

/**
 A callback that is invoked when a directory cannot be successfully enumerated.

 @param FilePath Pointer to the file path that could not be enumerated.

 @param ErrorCode The Win32 error code describing the failure.

 @param Depth Recursion depth, ignored in this application.

 @param Context Context, ignored in this function.

 @return TRUE to continute enumerating, FALSE to abort.
 */
BOOL
RmdirFileEnumerateErrorCallback(
    __in PYORI_STRING FilePath,
    __in DWORD ErrorCode,
    __in DWORD Depth,
    __in PVOID Context
    )
{
    YORI_STRING UnescapedFilePath;
    BOOL Result = FALSE;

    UNREFERENCED_PARAMETER(Depth);
    UNREFERENCED_PARAMETER(Context);

    YoriLibInitEmptyString(&UnescapedFilePath);
    if (!YoriLibUnescapePath(FilePath, &UnescapedFilePath)) {
        UnescapedFilePath.StartOfString = FilePath->StartOfString;
        UnescapedFilePath.LengthInChars = FilePath->LengthInChars;
    }

    if (ErrorCode == ERROR_FILE_NOT_FOUND || ErrorCode == ERROR_PATH_NOT_FOUND) {
        if (Depth == 0) {
            YoriLibOutput(YORI_LIB_OUTPUT_STDERR, _T("File or directory not found: %y\n"), &UnescapedFilePath);
        }
        Result = TRUE;
    } else {
        LPTSTR ErrText = YoriLibGetWinErrorText(ErrorCode);
        YORI_STRING DirName;
        LPTSTR FilePart;
        YoriLibInitEmptyString(&DirName);
        DirName.StartOfString = UnescapedFilePath.StartOfString;
        FilePart = YoriLibFindRightMostCharacter(&UnescapedFilePath, '\\');
        if (FilePart != NULL) {
            DirName.LengthInChars = (DWORD)(FilePart - DirName.StartOfString);
        } else {
            DirName.LengthInChars = UnescapedFilePath.LengthInChars;
        }
        YoriLibOutput(YORI_LIB_OUTPUT_STDERR, _T("Enumerate of %y failed: %s"), &DirName, ErrText);
        YoriLibFreeWinErrorText(ErrText);
    }
    YoriLibFreeStringContents(&UnescapedFilePath);
    return Result;
}


#ifdef YORI_BUILTIN
/**
 The main entrypoint for the rmdir builtin command.
 */
#define ENTRYPOINT YoriCmd_YRMDIR
#else
/**
 The main entrypoint for the rmdir standalone application.
 */
#define ENTRYPOINT ymain
#endif

/**
 The main entrypoint for the rmdir cmdlet.

 @param ArgC The number of arguments.

 @param ArgV An array of arguments.

 @return Exit code of the process indicating success or failure.
 */
DWORD
ENTRYPOINT(
    __in DWORD ArgC,
    __in YORI_STRING ArgV[]
    )
{
    BOOL ArgumentUnderstood;
    BOOL Recursive;
    BOOL BasicEnumeration;
    BOOL DeleteLinks;
    DWORD MatchFlags;
    DWORD StartArg = 0;
    DWORD i;
    RMDIR_CONTEXT RmdirContext;
    YORI_STRING Arg;

    ZeroMemory(&RmdirContext, sizeof(RmdirContext));

    Recursive = FALSE;
    BasicEnumeration = FALSE;
    DeleteLinks = FALSE;

    for (i = 1; i < ArgC; i++) {

        ArgumentUnderstood = FALSE;
        ASSERT(YoriLibIsStringNullTerminated(&ArgV[i]));

        if (YoriLibIsCommandLineOption(&ArgV[i], &Arg)) {

            if (YoriLibCompareStringWithLiteralInsensitive(&Arg, _T("?")) == 0) {
                RmdirHelp();
                return EXIT_SUCCESS;
            } else if (YoriLibCompareStringWithLiteralInsensitive(&Arg, _T("license")) == 0) {
                YoriLibDisplayMitLicense(_T("2017-2019"));
                return EXIT_SUCCESS;
            } else if (YoriLibCompareStringWithLiteralInsensitive(&Arg, _T("b")) == 0) {
                BasicEnumeration = TRUE;
                ArgumentUnderstood = TRUE;
            } else if (YoriLibCompareStringWithLiteralInsensitive(&Arg, _T("l")) == 0) {
                DeleteLinks = TRUE;
                ArgumentUnderstood = TRUE;
            } else if (YoriLibCompareStringWithLiteralInsensitive(&Arg, _T("q")) == 0) {
                ArgumentUnderstood = TRUE;
            } else if (YoriLibCompareStringWithLiteralInsensitive(&Arg, _T("r")) == 0) {
                ArgumentUnderstood = TRUE;
                RmdirContext.RecycleBin = TRUE;
            } else if (YoriLibCompareStringWithLiteralInsensitive(&Arg, _T("s")) == 0) {
                Recursive = TRUE;
                ArgumentUnderstood = TRUE;
            } else if (YoriLibCompareStringWithLiteralInsensitive(&Arg, _T("s/q")) == 0) {
                Recursive = TRUE;
                ArgumentUnderstood = TRUE;
            } else if (YoriLibCompareStringWithLiteralInsensitive(&Arg, _T("-")) == 0) {
                StartArg = i + 1;
                ArgumentUnderstood = TRUE;
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

    if (StartArg == 0 || StartArg == ArgC) {
        YoriLibOutput(YORI_LIB_OUTPUT_STDERR, _T("rmdir: missing argument\n"));
        return EXIT_FAILURE;
    }

    MatchFlags = YORILIB_FILEENUM_RETURN_DIRECTORIES;
    if (Recursive) {
        MatchFlags |= YORILIB_FILEENUM_RECURSE_BEFORE_RETURN | YORILIB_FILEENUM_RETURN_FILES;
    }
    if (BasicEnumeration) {
        MatchFlags |= YORILIB_FILEENUM_BASIC_EXPANSION;
    }
    if (DeleteLinks) {
        MatchFlags |= YORILIB_FILEENUM_NO_LINK_TRAVERSE;
    }

    for (i = StartArg; i < ArgC; i++) {
        YoriLibForEachFile(&ArgV[i],
                           MatchFlags,
                           0,
                           RmdirFileFoundCallback,
                           RmdirFileEnumerateErrorCallback,
                           &RmdirContext);
    }

    return EXIT_SUCCESS;
}

// vim:sw=4:ts=4:et:
