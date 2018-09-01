/**
 * @file lib/filecomp.c
 *
 * Yori lib perform transparent individual file compression on background
 * threads
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
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, COPYESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <yoripch.h>
#include <yorilib.h>

/**
 A single item to compress.
 */
typedef struct _YORILIB_PENDING_COMPRESS {

    /**
     A list of files requiring compression.
     */
    YORI_LIST_ENTRY CompressList;

    /**
     A handle to the file to compress.
     */
    HANDLE hFile;
} YORILIB_PENDING_COMPRESS, *PYORILIB_PENDING_COMPRESS;

/**
 Set up the compress context to contain support for the compression thread pool.

 @param CompressContext Pointer to the compress context.

 @param CompressionAlgorithm The compression algorithm to use for this set of
        compressed files.

 @return TRUE if the context was successfully initialized for compression,
         FALSE if it was not.
 */
BOOL
YoriLibInitializeCompressContext(
    __in PYORILIB_COMPRESS_CONTEXT CompressContext,
    __in YORILIB_COMPRESS_ALGORITHM CompressionAlgorithm
    )
{
    SYSTEM_INFO SystemInfo;
    GetSystemInfo(&SystemInfo);

    CompressContext->CompressionAlgorithm = CompressionAlgorithm;

    //
    //  Use 1/3rd of the CPUs to initiate compression on.  The system can
    //  compress chunks of data on background threads, so this is just
    //  the number of threads initiating work.
    //

    CompressContext->MaxThreads = SystemInfo.dwNumberOfProcessors / 3;
    if (CompressContext->MaxThreads < 1) {
        CompressContext->MaxThreads = 1;
    }
    if (CompressContext->MaxThreads > 32) {
        CompressContext->MaxThreads = 32;
    }

    YoriLibInitializeListHead(&CompressContext->PendingList);
    CompressContext->WorkerWaitEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (CompressContext->WorkerWaitEvent == NULL) {
        return FALSE;
    }

    CompressContext->WorkerShutdownEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (CompressContext->WorkerShutdownEvent == NULL) {
        return FALSE;
    }

    CompressContext->Mutex = CreateMutex(NULL, FALSE, NULL);
    if (CompressContext->Mutex == NULL) {
        return FALSE;
    }

    CompressContext->Threads = YoriLibMalloc(sizeof(HANDLE) * CompressContext->MaxThreads);
    if (CompressContext->Threads == NULL) {
        return FALSE;
    }

    return TRUE;
}

/**
 Free the internal allocations and state of a compress context.  This
 also includes waiting for all outstanding compression tasks to complete.
 Note the CompressContext allocation itself is not freed, since this is
 typically on the stack.

 @param CompressContext Pointer to the compress context to clean up.
 */
VOID
YoriLibFreeCompressContext(
    __in PYORILIB_COMPRESS_CONTEXT CompressContext
    )
{
    if (CompressContext->ThreadsAllocated > 0) {
        DWORD Index;
        SetEvent(CompressContext->WorkerShutdownEvent);
        WaitForMultipleObjects(CompressContext->ThreadsAllocated, CompressContext->Threads, TRUE, INFINITE);
        for (Index = 0; Index < CompressContext->ThreadsAllocated; Index++) {
            CloseHandle(CompressContext->Threads[Index]);
            CompressContext->Threads[Index] = NULL;
        }
        ASSERT(YoriLibIsListEmpty(&CompressContext->PendingList));
    }
    if (CompressContext->WorkerWaitEvent != NULL) {
        CloseHandle(CompressContext->WorkerWaitEvent);
        CompressContext->WorkerWaitEvent = NULL;
    }
    if (CompressContext->WorkerShutdownEvent != NULL) {
        CloseHandle(CompressContext->WorkerShutdownEvent);
        CompressContext->WorkerShutdownEvent = NULL;
    }
    if (CompressContext->Mutex != NULL) {
        CloseHandle(CompressContext->Mutex);
        CompressContext->Mutex = NULL;
    }
    if (CompressContext->Threads != NULL) {
        YoriLibFree(CompressContext->Threads);
        CompressContext->Threads = NULL;
    }
}

/**
 Compress a single file.  This can be called on worker threads, or occasionally
 on the main thread if the worker threads are backlogged.

 @param PendingCompress Pointer to the object that needs to be compressed.
        This structure is deallocated within this function.

 @param CompressionAlgorithm Specifies the compression algorithm to compress
        the file with.
 
 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOL
YoriLibCompressSingleFile(
    __in PYORILIB_PENDING_COMPRESS PendingCompress,
    __in YORILIB_COMPRESS_ALGORITHM CompressionAlgorithm
    )
{
    DWORD BytesReturned;
    BOOL Result = FALSE;

    if (CompressionAlgorithm.NtfsAlgorithm != 0) {
        USHORT Algorithm = (USHORT)CompressionAlgorithm.NtfsAlgorithm;

        Result = DeviceIoControl(PendingCompress->hFile,
                                 FSCTL_SET_COMPRESSION,
                                 &Algorithm,
                                 sizeof(Algorithm),
                                 NULL,
                                 0,
                                 &BytesReturned,
                                 NULL);

    } else {
        struct {
            WOF_EXTERNAL_INFO WofInfo;
            FILE_PROVIDER_EXTERNAL_INFO FileInfo;
        } CompressInfo;

        ZeroMemory(&CompressInfo, sizeof(CompressInfo));
        CompressInfo.WofInfo.Version = 1;
        CompressInfo.WofInfo.Provider = WOF_PROVIDER_FILE;
        CompressInfo.FileInfo.Version = 1;
        CompressInfo.FileInfo.Algorithm = CompressionAlgorithm.WofAlgorithm;

        Result = DeviceIoControl(PendingCompress->hFile,
                                 FSCTL_SET_EXTERNAL_BACKING,
                                 &CompressInfo,
                                 sizeof(CompressInfo),
                                 NULL,
                                 0,
                                 &BytesReturned,
                                 NULL);
    }

    CloseHandle(PendingCompress->hFile);
    YoriLibFree(PendingCompress);
    return Result;

}

/**
 A background thread which will attempt to compress any items that it finds on
 a list of files requiring compression.

 @param Context Pointer to the compress context.

 @return TRUE to indicate success, FALSE to indicate one or more compression
         operations failed.
 */
DWORD WINAPI
YoriLibCompressWorker(
    __in LPVOID Context
    )
{
    PYORILIB_COMPRESS_CONTEXT CompressContext = (PYORILIB_COMPRESS_CONTEXT)Context;
    DWORD FoundEvent;
    PYORILIB_PENDING_COMPRESS PendingCompress;
    BOOL Result = TRUE;

    while (TRUE) {

        //
        //  Wait for an indication of more work or shutdown.
        //

        FoundEvent = WaitForMultipleObjects(2, &CompressContext->WorkerWaitEvent, FALSE, INFINITE);

        //
        //  Process any queued work.
        //

        while (TRUE) {
            WaitForSingleObject(CompressContext->Mutex, INFINITE);
            if (!YoriLibIsListEmpty(&CompressContext->PendingList)) {
                PendingCompress = CONTAINING_RECORD(CompressContext->PendingList.Next, YORILIB_PENDING_COMPRESS, CompressList);
                ASSERT(CompressContext->ItemsQueued > 0);
                CompressContext->ItemsQueued--;
                YoriLibRemoveListItem(&PendingCompress->CompressList);
                ReleaseMutex(CompressContext->Mutex);

                if (!YoriLibCompressSingleFile(PendingCompress, CompressContext->CompressionAlgorithm)) {
                    Result = FALSE;
                }

            } else {
                ASSERT(CompressContext->ItemsQueued == 0);
                ReleaseMutex(CompressContext->Mutex);
                break;
            }
        }

        //
        //  If shutdown was requested, terminate the thread.
        //

        if (FoundEvent == (WAIT_OBJECT_0 + 1)) {
            break;
        }
    }

    return Result;
}

/**
 Compress a given file with a specified algorithm.  This routine will skip
 small files that do not benefit from compression.

 @param CompressContext Pointer to the compress context specifying where to
        queue compression tasks and which compression algorithm to use.

 @param FileName Pointer to the file name to compress.

 @return TRUE to indicate the file was successfully compressed, FALSE if it
         was not.
 */
BOOL
YoriLibCompressFileInBackground(
    __in PYORILIB_COMPRESS_CONTEXT CompressContext,
    __in PYORI_STRING FileName
    )
{
    HANDLE DestFileHandle;
    DWORD ThreadId;
    BOOL Result = FALSE;
    BY_HANDLE_FILE_INFORMATION FileInfo;
    PYORILIB_PENDING_COMPRESS PendingCompress;

    ASSERT(YoriLibIsStringNullTerminated(FileName));

    DestFileHandle = CreateFile(FileName->StartOfString,
                                FILE_READ_DATA | FILE_READ_ATTRIBUTES | FILE_WRITE_DATA | FILE_WRITE_ATTRIBUTES | SYNCHRONIZE,
                                FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
                                NULL,
                                OPEN_EXISTING,
                                0,
                                NULL);

    if (DestFileHandle == INVALID_HANDLE_VALUE) {
        return FALSE;
    }

    //
    //  File system compression works by storing the data in fewer allocation
    //  units.  What this means is for files that are very small the
    //  possibility and quantity of allocation units reclaimed can't justify
    //  the overhead, so just skip them.
    //

    if (!GetFileInformationByHandle(DestFileHandle, &FileInfo)) {
        goto Exit;
    }

    if (FileInfo.nFileSizeHigh == 0 &&
        FileInfo.nFileSizeLow < 10 * 1024) {

        goto Exit;
    }

    PendingCompress = YoriLibMalloc(sizeof(YORILIB_PENDING_COMPRESS));
    if (PendingCompress == NULL) {
        goto Exit;
    }

    PendingCompress->hFile = DestFileHandle;
    DestFileHandle = NULL;
    WaitForSingleObject(CompressContext->Mutex, INFINITE);
    if (CompressContext->ThreadsAllocated == 0 ||
        (CompressContext->ItemsQueued > CompressContext->ThreadsAllocated * 2 &&
         CompressContext->ThreadsAllocated < CompressContext->MaxThreads)) {

        CompressContext->Threads[CompressContext->ThreadsAllocated] = CreateThread(NULL, 0, YoriLibCompressWorker, CompressContext, 0, &ThreadId);
        if (CompressContext->Threads[CompressContext->ThreadsAllocated] != NULL) {
            CompressContext->ThreadsAllocated++;
            if (CompressContext->Verbose) {
                YoriLibOutput(YORI_LIB_OUTPUT_STDOUT, _T("Created compression thread %i\n"), CompressContext->ThreadsAllocated);
            }
        }
    }

    if (CompressContext->ThreadsAllocated > 0 &&
        CompressContext->ItemsQueued < CompressContext->MaxThreads * 2) {

        YoriLibAppendList(&CompressContext->PendingList, &PendingCompress->CompressList);
        CompressContext->ItemsQueued++;
        PendingCompress = NULL;
    }

    ReleaseMutex(CompressContext->Mutex);

    SetEvent(CompressContext->WorkerWaitEvent);

    Result = TRUE;

    //
    //  If the threads in the pool are all busy (we have more than 6 items
    //  waiting) do the compression on the main thread.  This is mainly done
    //  to prevent the main thread from continuing to pile in more items
    //  that the pool can't get to.
    //

    if (PendingCompress != NULL) {
        if (CompressContext->Verbose) {
            YoriLibOutput(YORI_LIB_OUTPUT_STDOUT, _T("Compressing %y on main thread for back pressure\n"), FileName);
        }
        if (!YoriLibCompressSingleFile(PendingCompress, CompressContext->CompressionAlgorithm)) {
            Result = FALSE;
        }
    }

Exit:
    if (DestFileHandle != NULL) {
        CloseHandle(DestFileHandle);
    }
    return Result;
}

// vim:sw=4:ts=4:et:
