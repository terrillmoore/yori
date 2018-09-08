/**
 * @file more/init.c
 *
 * Yori shell more initialization
 *
 * Copyright (c) 2017-2018 Malcolm J. Smith
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

#include "more.h"

/**
 Initialize a MoreContext with settings indicating where the data should come
 from, and launch a background thread to commence ingesting the data.

 @param MoreContext Pointer to the more context whose state should be
        initialized.

 @param ArgCount The number of elements in the ArgStrings array.  This can be
        zero if input should come from a pipe, not from a set of files.

 @param ArgStrings Pointer to an array of YORI_STRINGs for each argument
        that represents a criteria of one or more files to ingest.  This can
        be NULL if ingestion should come from a pipe, not from files.

 @param Recursive TRUE if the set of files should be enumerated recursively.
        FALSE if they should be interpreted as files and not recursed.

 @param BasicEnumeration TRUE if enumeration should not expand {}, [], or
        similar operators.  FALSE if these should be expanded.

 @param DebugDisplay TRUE if the program should use the debug display which
        clears the screen and writes internal buffers on each change.  This
        is much slower than just telling the console about changes but helps
        to debug the state of the program.

 @return TRUE to indicate successful completion, meaning a background thread
         is executing and this should be drained with @ref MoreGracefulExit.
         FALSE to indicate initialization was unsuccessful, and the
         MoreContext should be cleaned up with @ref MoreCleanupContext.
 */
BOOL
MoreInitContext(
    __inout PMORE_CONTEXT MoreContext,
    __in DWORD ArgCount,
    __in PYORI_STRING ArgStrings,
    __in BOOL Recursive,
    __in BOOL BasicEnumeration,
    __in BOOL DebugDisplay
    )
{
    CONSOLE_SCREEN_BUFFER_INFO ScreenInfo;
    DWORD ThreadId;

    ZeroMemory(MoreContext, sizeof(MORE_CONTEXT));

    MoreContext->Recursive = Recursive;
    MoreContext->BasicEnumeration = BasicEnumeration;
    MoreContext->DebugDisplay = DebugDisplay;
    MoreContext->TabWidth = 4;

    YoriLibInitializeListHead(&MoreContext->PhysicalLineList);
    MoreContext->PhysicalLineMutex = CreateMutex(NULL, FALSE, NULL);
    if (MoreContext->PhysicalLineMutex == NULL) {
        return FALSE;
    }

    MoreContext->PhysicalLineAvailableEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (MoreContext->PhysicalLineAvailableEvent == NULL) {
        return FALSE;
    }

    MoreContext->ShutdownEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (MoreContext->ShutdownEvent == NULL) {
        return FALSE;
    }

    if (!GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &ScreenInfo)) {
        return FALSE;
    }

    MoreContext->ViewportWidth = ScreenInfo.srWindow.Right - ScreenInfo.srWindow.Left + 1;
    MoreContext->ViewportHeight = ScreenInfo.srWindow.Bottom - ScreenInfo.srWindow.Top;

    MoreContext->DisplayViewportLines = YoriLibMalloc(sizeof(MORE_LOGICAL_LINE) * MoreContext->ViewportHeight);
    if (MoreContext->DisplayViewportLines == NULL) {
        return FALSE;
    }
    ZeroMemory(MoreContext->DisplayViewportLines, sizeof(MORE_LOGICAL_LINE) * MoreContext->ViewportHeight);

    MoreContext->StagingViewportLines = YoriLibMalloc(sizeof(MORE_LOGICAL_LINE) * MoreContext->ViewportHeight);
    if (MoreContext->StagingViewportLines == NULL) {
        return FALSE;
    }

    ZeroMemory(MoreContext->StagingViewportLines, sizeof(MORE_LOGICAL_LINE) * MoreContext->ViewportHeight);

    MoreContext->InputSourceCount = ArgCount;
    MoreContext->InputSources = ArgStrings;

    MoreContext->IngestThread = CreateThread(NULL, 0, MoreIngestThread, MoreContext, 0, &ThreadId);
    if (MoreContext->IngestThread == NULL) {
        return FALSE;
    }

    return TRUE;
}

/**
 Clean up any state on the MoreContext.

 @param MoreContext Pointer to the more context whose state should be cleaned
        up.
 */
VOID
MoreCleanupContext(
    __inout PMORE_CONTEXT MoreContext
    )
{
    if (MoreContext->DisplayViewportLines != NULL) {
        YoriLibFree(MoreContext->DisplayViewportLines);
        MoreContext->DisplayViewportLines = NULL;
    }

    if (MoreContext->StagingViewportLines != NULL) {
        YoriLibFree(MoreContext->StagingViewportLines);
        MoreContext->StagingViewportLines = NULL;
    }

    if (MoreContext->PhysicalLineAvailableEvent != NULL) {
        CloseHandle(MoreContext->PhysicalLineAvailableEvent);
        MoreContext->PhysicalLineAvailableEvent = NULL;
    }

    if (MoreContext->ShutdownEvent != NULL) {
        CloseHandle(MoreContext->ShutdownEvent);
        MoreContext->ShutdownEvent = NULL;
    }

    if (MoreContext->PhysicalLineMutex != NULL) {
        CloseHandle(MoreContext->PhysicalLineMutex);
        MoreContext->PhysicalLineMutex = NULL;
    }

    if (MoreContext->IngestThread != NULL) {
        CloseHandle(MoreContext->IngestThread);
        MoreContext->IngestThread = NULL;
    }
}

/**
 Indicate that the ingest thread should terminate, wait for it to die, and
 clean up any state.

 @param MoreContext Pointer to the more context whose state should be cleaned
        up.
 */
VOID
MoreGracefulExit(
    __inout PMORE_CONTEXT MoreContext
    )
{
    PYORI_LIST_ENTRY ListEntry;
    PMORE_PHYSICAL_LINE PhysicalLine;
    DWORD Index;

    SetEvent(MoreContext->ShutdownEvent);
    WaitForSingleObject(MoreContext->IngestThread, INFINITE);
    for (Index = 0; Index < MoreContext->ViewportHeight; Index++) {
        YoriLibFreeStringContents(&MoreContext->DisplayViewportLines[Index].Line);
    }
    ListEntry = YoriLibGetNextListEntry(&MoreContext->PhysicalLineList, NULL);
    while (ListEntry != NULL) {
        PhysicalLine = CONTAINING_RECORD(ListEntry, MORE_PHYSICAL_LINE, LineList);
        YoriLibRemoveListItem(ListEntry);
        YoriLibFreeStringContents(&PhysicalLine->LineContents);
        YoriLibDereference(PhysicalLine);
        ListEntry = YoriLibGetNextListEntry(&MoreContext->PhysicalLineList, NULL);
    }

    MoreCleanupContext(MoreContext);
}

// vim:sw=4:ts=4:et:
