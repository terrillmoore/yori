/**
 * @file sh/complete.c
 *
 * Yori shell tab completion
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

#include "yori.h"

/**
 Add a new match to the list of matches and add the match to the hash table
 to check for duplicates.

 @param TabContext Pointer to the tab context to add the match to.

 @param EntryToInsertBefore If non-NULL, the new match should be inserted
        before this entry in the list.  If NULL, the new match is inserted
        at the end of the list.

 @param Match Pointer to the match to insert.
 */
VOID
YoriShAddMatchToTabContext(
    __inout PYORI_SH_TAB_COMPLETE_CONTEXT TabContext,
    __in_opt PYORI_LIST_ENTRY EntryToInsertBefore,
    __inout PYORI_SH_TAB_COMPLETE_MATCH Match
    )
{
    ASSERT(TabContext->MatchHashTable != NULL);
    ASSERT(Match->Value.MemoryToFree != NULL);
    YoriLibHashInsertByKey(TabContext->MatchHashTable, &Match->Value, Match, &Match->HashEntry);
    if (EntryToInsertBefore == NULL) {
        YoriLibAppendList(&TabContext->MatchList, &Match->ListEntry);
    } else {
        YoriLibAppendList(EntryToInsertBefore, &Match->ListEntry);
    }
}

/**
 Remove an item that is currently in the list of matches and the hash table
 of matches.  Note this routine should not be used unless the match has
 been inserted into the list/hash table.

 @param TabContext Pointer to the tab context to remove the match from.

 @param Match Pointer to the match to remove.
 */
VOID
YoriShRemoveMatchFromTabContext(
    __inout PYORI_SH_TAB_COMPLETE_CONTEXT TabContext,
    __in PYORI_SH_TAB_COMPLETE_MATCH Match
    )
{
    UNREFERENCED_PARAMETER(TabContext);
    ASSERT(TabContext->MatchHashTable != NULL);
    ASSERT(Match->Value.MemoryToFree != NULL);
    YoriLibHashRemoveByEntry(&Match->HashEntry);
    YoriLibRemoveListItem(&Match->ListEntry);
    YoriLibFreeStringContents(&Match->Value);
    YoriLibDereference(Match);
}

/**
 Populates the list of matches for a command history tab completion.  This
 function searches the history for matching commands in MRU order and
 populates the list with the result.

 @param TabContext Pointer to the tab completion context.  This provides
        the search criteria and has its match list populated with results
        on success.

 @param ExpandFullPath Specifies if full path expansion should be performed.
        For executable matches, full path expansion is always performed.
 */
VOID
YoriShPerformHistoryTabCompletion(
    __inout PYORI_SH_TAB_COMPLETE_CONTEXT TabContext,
    __in BOOL ExpandFullPath
    )
{
    LPTSTR FoundPath;
    DWORD CompareLength;
    PYORI_LIST_ENTRY ListEntry;
    PYORI_SH_HISTORY_ENTRY HistoryEntry;
    PYORI_SH_TAB_COMPLETE_MATCH Match;

    UNREFERENCED_PARAMETER(ExpandFullPath);

    //
    //  Set up state necessary for different types of searching.
    //

    FoundPath = _tcschr(TabContext->SearchString.StartOfString, '*');
    if (FoundPath != NULL) {
        CompareLength = (DWORD)(FoundPath - TabContext->SearchString.StartOfString);
    } else {
        CompareLength = TabContext->SearchString.LengthInChars;
    }
    FoundPath = NULL;

    //
    //  Search the list of history.
    //

    ListEntry = YoriLibGetPreviousListEntry(&YoriShGlobal.CommandHistory, NULL);
    while (ListEntry != NULL) {
        HistoryEntry = CONTAINING_RECORD(ListEntry, YORI_SH_HISTORY_ENTRY, ListEntry);

        if (YoriLibCompareStringInsensitiveCount(&HistoryEntry->CmdLine, &TabContext->SearchString, CompareLength) == 0) {

            //
            //  Allocate a match entry for this file.
            //

            Match = YoriLibReferencedMalloc(sizeof(YORI_SH_TAB_COMPLETE_MATCH) + (HistoryEntry->CmdLine.LengthInChars + 1) * sizeof(TCHAR));
            if (Match == NULL) {
                return;
            }

            //
            //  Populate the file into the entry.
            //

            YoriLibInitEmptyString(&Match->Value);
            Match->Value.StartOfString = (LPTSTR)(Match + 1);
            YoriLibReference(Match);
            Match->Value.MemoryToFree = Match;
            YoriLibSPrintf(Match->Value.StartOfString, _T("%y"), &HistoryEntry->CmdLine);
            Match->Value.LengthInChars = HistoryEntry->CmdLine.LengthInChars;

            //
            //  Append to the list.
            //

            YoriShAddMatchToTabContext(TabContext, NULL, Match);

        }
        ListEntry = YoriLibGetPreviousListEntry(&YoriShGlobal.CommandHistory, ListEntry);
    }
}

/**
 A context passed between the initiator of executable tab completion and each
 callback invoked when an executable match is found.
 */
typedef struct _YORI_SH_EXEC_TAB_COMPLETE_CONTEXT {

    /**
     The TabContext to populate any matches into.
     */
    PYORI_SH_TAB_COMPLETE_CONTEXT TabContext;

    /**
     The string to search for.
     */
    PYORI_STRING SearchString;

    /**
     The number of characters in the SearchString until the final slash.
     This is used to distinguish where to search from what to search for.
     */
    DWORD CharsToFinalSlash;

    /**
     If TRUE, the resulting tab completion should expand the entire path,
     if FALSE it should only expand the file name.
     */
    BOOL ExpandFullPath;
} YORI_SH_EXEC_TAB_COMPLETE_CONTEXT, *PYORI_SH_EXEC_TAB_COMPLETE_CONTEXT;

/**
 Find the final seperator or colon in event of a drive letter colon
 prefix string, such that the criteria being searched for can be
 seperated from the location of the search.

 @param String Pointer to the string to locate the final seperator
        in.

 @return The index of the seperator, which may be zero to indicate
         one was not found.
 */
DWORD
YoriShFindFinalSlashIfSpecified(
    __in PYORI_STRING String
    )
{
    DWORD CharsInFileName;

    CharsInFileName = String->LengthInChars;

    while (CharsInFileName > 0) {
        if (YoriLibIsSep(String->StartOfString[CharsInFileName - 1])) {

            break;
        }

        if (CharsInFileName == 2 &&
            YoriLibIsDriveLetterWithColon(String)) {

            break;
        }

        CharsInFileName--;
    }

    return CharsInFileName;
}


/**
 A callback function that is invoked by the path resolver to add any
 candidate programs to the tab completion list.

 @param FoundPath A match that was found when enumerating through the path.

 @param Context Pointer to the tab complete context to populate with the new
        match.
 */
BOOL
YoriShAddExecutableToTabList(
    __in PYORI_STRING FoundPath,
    __in PVOID Context
    )
{
    PYORI_SH_TAB_COMPLETE_MATCH Match;
    PYORI_SH_EXEC_TAB_COMPLETE_CONTEXT ExecTabContext = (PYORI_SH_EXEC_TAB_COMPLETE_CONTEXT)Context;
    PYORI_HASH_ENTRY PriorEntry;
    YORI_STRING PathToReturn;
    YORI_STRING StringToFinalSlash;

    YoriLibInitEmptyString(&PathToReturn);
    YoriLibInitEmptyString(&StringToFinalSlash);

    PathToReturn.StartOfString = FoundPath->StartOfString;
    PathToReturn.LengthInChars = FoundPath->LengthInChars;

    //
    //  If not expanding the full path, trim off any path found in the match
    //  and add back any path specified by the user.
    //

    if (!ExecTabContext->ExpandFullPath) {
        DWORD PathOffset;
        PathOffset = YoriShFindFinalSlashIfSpecified(FoundPath);

        PathToReturn.StartOfString += PathOffset;
        PathToReturn.LengthInChars -= PathOffset;

        StringToFinalSlash.StartOfString = ExecTabContext->SearchString->StartOfString;
        StringToFinalSlash.LengthInChars = ExecTabContext->CharsToFinalSlash;
    }

    //
    //  Allocate a match entry for this file.
    //

    Match = YoriLibReferencedMalloc(sizeof(YORI_SH_TAB_COMPLETE_MATCH) + (StringToFinalSlash.LengthInChars + PathToReturn.LengthInChars + 1) * sizeof(TCHAR));
    if (Match == NULL) {
        return FALSE;
    }

    //
    //  Populate the file into the entry.
    //

    YoriLibInitEmptyString(&Match->Value);
    Match->Value.StartOfString = (LPTSTR)(Match + 1);
    YoriLibReference(Match);
    Match->Value.MemoryToFree = Match;
    Match->Value.LengthInChars = YoriLibSPrintf(Match->Value.StartOfString, _T("%y%y"), &StringToFinalSlash, &PathToReturn);
    Match->Value.LengthAllocated = Match->Value.LengthInChars + 1;

    //
    //  Insert into the list if no duplicate is found.
    //

    PriorEntry = YoriLibHashLookupByKey(ExecTabContext->TabContext->MatchHashTable, &Match->Value);
    if (PriorEntry == NULL) {
        YoriShAddMatchToTabContext(ExecTabContext->TabContext, NULL, Match);
    } else {
        YoriLibFreeStringContents(&Match->Value);
        YoriLibDereference(Match);
    }

    return TRUE;
}


/**
 Populates the list of matches for an executable tab completion.  This
 function searches the path for matching binaries in execution order
 and populates the list with the result.

 @param TabContext Pointer to the tab completion context.  This provides
        the search criteria and has its match list populated with results
        on success.

 @param ExpandFullPath Specifies if full path expansion should be performed.
        For executable matches, full path expansion is always performed.

 @param IncludeBuiltins If TRUE, include any builtin commands as part of
        the match.
 */
VOID
YoriShPerformExecutableTabCompletion(
    __inout PYORI_SH_TAB_COMPLETE_CONTEXT TabContext,
    __in BOOL ExpandFullPath,
    __in BOOL IncludeBuiltins
    )
{
    LPTSTR FoundPath;
    YORI_STRING FoundExecutable;
    BOOL Result;
    PYORI_SH_TAB_COMPLETE_MATCH Match;
    YORI_STRING AliasStrings;
    DWORD CompareLength;
    YORI_SH_EXEC_TAB_COMPLETE_CONTEXT ExecTabContext;
    YORI_STRING SearchString;

    YoriLibInitEmptyString(&SearchString);
    SearchString.StartOfString = TabContext->SearchString.StartOfString;
    SearchString.LengthInChars = TabContext->SearchString.LengthInChars;
    SearchString.LengthAllocated = TabContext->SearchString.LengthAllocated;

    ExecTabContext.TabContext = TabContext;
    ExecTabContext.ExpandFullPath = ExpandFullPath;
    ExecTabContext.CharsToFinalSlash = YoriShFindFinalSlashIfSpecified(&SearchString);
    ExecTabContext.SearchString = &SearchString;

    //
    //  Set up state necessary for different types of searching.
    //

    FoundPath = YoriLibFindLeftMostCharacter(&SearchString, '*');
    if (FoundPath != NULL) {
        CompareLength = (DWORD)(FoundPath - SearchString.StartOfString);
    } else {
        CompareLength = SearchString.LengthInChars;
    }

    //
    //  Firstly, search the table of aliases.
    //

    YoriLibInitEmptyString(&AliasStrings);
    if (IncludeBuiltins && YoriShGetAliasStrings(YORI_SH_GET_ALIAS_STRINGS_INCLUDE_INTERNAL | YORI_SH_GET_ALIAS_STRINGS_INCLUDE_USER, &AliasStrings)) {
        LPTSTR ThisAlias;
        LPTSTR AliasValue;
        DWORD AliasLength;
        DWORD AliasNameLength;


        ThisAlias = AliasStrings.StartOfString;
        while (*ThisAlias != '\0') {
            AliasNameLength = 
            AliasLength = _tcslen(ThisAlias);

            //
            //  Look at the alias name only, not what it maps to.
            //

            AliasValue = _tcschr(ThisAlias, '=');
            ASSERT(AliasValue != NULL);
            if (AliasValue != NULL) {
                *AliasValue = '\0';
                AliasNameLength = (DWORD)((PUCHAR)AliasValue - (PUCHAR)ThisAlias);
            }

            if (YoriLibCompareStringWithLiteralInsensitiveCount(&SearchString, ThisAlias, CompareLength) == 0) {

                //
                //  Allocate a match entry for this file.
                //

                Match = YoriLibReferencedMalloc(sizeof(YORI_SH_TAB_COMPLETE_MATCH) + (AliasNameLength + 1) * sizeof(TCHAR));
                if (Match == NULL) {
                    YoriLibFreeStringContents(&AliasStrings);
                    return;
                }

                //
                //  Populate the file into the entry.
                //

                YoriLibInitEmptyString(&Match->Value);
                Match->Value.StartOfString = (LPTSTR)(Match + 1);
                YoriLibReference(Match);
                Match->Value.MemoryToFree = Match;
                Match->Value.LengthInChars = YoriLibSPrintf(Match->Value.StartOfString, _T("%s"), ThisAlias);
                Match->Value.LengthAllocated = Match->Value.LengthInChars + 1;

                //
                //  Append to the list.
                //

                YoriShAddMatchToTabContext(TabContext, NULL, Match);
            }

            //
            //  Move to the next alias
            //

            ThisAlias += AliasLength;
            ThisAlias++;
        }
        YoriLibFreeStringContents(&AliasStrings);
    }

    //
    //  Secondly, search for the object in the PATH, resuming after the
    //  previous search
    //

    YoriLibInitEmptyString(&FoundExecutable);
    Result = YoriLibLocateExecutableInPath(&SearchString,
                                           YoriShAddExecutableToTabList,
                                           &ExecTabContext,
                                           &FoundExecutable);
    ASSERT(FoundExecutable.StartOfString == NULL);

    //
    //  Thirdly, search the table of builtins.
    //

    if (IncludeBuiltins && YoriShGlobal.BuiltinCallbacks.Next != NULL) {
        PYORI_LIST_ENTRY ListEntry;
        PYORI_SH_BUILTIN_CALLBACK Callback;

        ListEntry = YoriLibGetNextListEntry(&YoriShGlobal.BuiltinCallbacks, NULL);
        while (ListEntry != NULL) {
            Callback = CONTAINING_RECORD(ListEntry, YORI_SH_BUILTIN_CALLBACK, ListEntry);
            if (YoriLibCompareStringInsensitiveCount(&SearchString, &Callback->BuiltinName, CompareLength) == 0) {

                //
                //  Allocate a match entry for this file.
                //

                Match = YoriLibReferencedMalloc(sizeof(YORI_SH_TAB_COMPLETE_MATCH) + (Callback->BuiltinName.LengthInChars + 1) * sizeof(TCHAR));
                if (Match == NULL) {
                    return;
                }

                //
                //  Populate the file into the entry.
                //

                YoriLibInitEmptyString(&Match->Value);
                Match->Value.StartOfString = (LPTSTR)(Match + 1);
                YoriLibReference(Match);
                Match->Value.MemoryToFree = Match;
                Match->Value.LengthInChars = YoriLibSPrintf(Match->Value.StartOfString, _T("%y"), &Callback->BuiltinName);
                Match->Value.LengthAllocated = Match->Value.LengthInChars + 1;

                //
                //  Append to the list.
                //

                YoriShAddMatchToTabContext(TabContext, NULL, Match);
            }
            ListEntry = YoriLibGetNextListEntry(&YoriShGlobal.BuiltinCallbacks, ListEntry);
        }
    }
}

/**
 Context information for a file based tab completion.
 */
typedef struct _YORI_SH_FILE_COMPLETE_CONTEXT {

    /**
     The tab completion context to populate with any matches.
     */
    PYORI_SH_TAB_COMPLETE_CONTEXT TabContext;

    /**
     Extra characters to include at the beginning of any found match.
     */
    YORI_STRING Prefix;

    /**
     The string to search for.
     */
    LPTSTR SearchString;

    /**
     The number of characters in the SearchString until the final slash.
     This is used to distinguish where to search from what to search for.
     */
    DWORD CharsToFinalSlash;

    /**
     The number of files that have been found.
     */
    DWORD FilesFound;

    /**
     If TRUE, the resulting tab completion should expand the entire path,
     if FALSE it should only expand the file name (inside the specified
     directory, if present.)
     */
    BOOL ExpandFullPath;

    /**
     If TRUE, keep the list of completion options sorted.  This is generally
     useful for file completion, and matches what CMD does.  It's FALSE if
     file completions are being added after executable completion, so the
     goal is to preserve the executable completion items first.  We could
     sort the file completion options after that at some point.
     */
    BOOL KeepCompletionsSorted;

} YORI_SH_FILE_COMPLETE_CONTEXT, *PYORI_SH_FILE_COMPLETE_CONTEXT;

/**
 Populates the list of matches for environment variable based tab completion.
 This function searches the string for indications of an environment variable,
 and if so matches all environment variables whose prefix match.

 @param TabContext Pointer to the tab completion context.  This has its
        match list populated with results on success.

 @param SearchString Pointer to the string containing the argument to search
        for.  The environment variable to search for is extracted from this
        argument.
 */
VOID
YoriShPerformEnvironmentTabCompletion(
    __inout PYORI_SH_TAB_COMPLETE_CONTEXT TabContext,
    __in PYORI_STRING SearchString
    )
{
    DWORD Index;
    DWORD MatchCount;
    DWORD VarNameLength;
    YORI_STRING EnvVarPrefix;
    YORI_STRING EnvironmentStrings;
    LPTSTR ThisVar;
    LPTSTR Equals;
    PYORI_SH_TAB_COMPLETE_MATCH Match;

    YoriLibInitEmptyString(&EnvVarPrefix);


    //
    //  Count the number of environment variable delimiters in the string.
    //  If there is an even number, then any variable has already been
    //  completed, so this routine has no value to add.  An odd number
    //  indicates the final % is the beginning of an unterminated variable.
    //

    MatchCount = 0;
    for (Index = 0; Index < SearchString->LengthInChars; Index++) {
        if (SearchString->StartOfString[Index] == '%') {
            MatchCount++;
        }
    }

    if ((MatchCount % 2) == 0) {
        return;
    }

    //
    //  Look backwards for the final unterminated variable.  When it is
    //  found create a string describing the variable prefix we're looking
    //  for.
    //

    for (Index = SearchString->LengthInChars; Index > 0; Index--) {
        if (SearchString->StartOfString[Index - 1] == '%') {
            EnvVarPrefix.StartOfString = &SearchString->StartOfString[Index];
            EnvVarPrefix.LengthInChars = SearchString->LengthInChars - (DWORD)(EnvVarPrefix.StartOfString - SearchString->StartOfString);

            if (EnvVarPrefix.LengthInChars > 0 && EnvVarPrefix.StartOfString[EnvVarPrefix.LengthInChars - 1] == '*') {
                EnvVarPrefix.LengthInChars--;
            }

            break;
        }
    }

    if (EnvVarPrefix.LengthInChars == 0) {
        return;
    }

    if (!YoriLibGetEnvironmentStrings(&EnvironmentStrings)) {
        return;
    }

    ThisVar = EnvironmentStrings.StartOfString;
    while (*ThisVar != 0) {

        if (YoriLibCompareStringWithLiteralInsensitiveCount(&EnvVarPrefix, ThisVar, EnvVarPrefix.LengthInChars) == 0) {
            Equals = _tcschr(ThisVar, '=');
            if (Equals != NULL) {

                VarNameLength = (DWORD)(Equals - ThisVar);

                //
                //  Allocate a match entry for this variable.  This includes
                //  text up to an including the '%', the name of the variable,
                //  a trailing '%', and a NULL.
                //
        
                Match = YoriLibReferencedMalloc(sizeof(YORI_SH_TAB_COMPLETE_MATCH) + (VarNameLength + Index + 1 + 1) * sizeof(TCHAR));
                if (Match == NULL) {
                    break;
                }
        
                //
                //  Populate the variable into the entry.
                //
        
                YoriLibInitEmptyString(&Match->Value);
                Match->Value.StartOfString = (LPTSTR)(Match + 1);
                YoriLibReference(Match);
                Match->Value.MemoryToFree = Match;
        
                Match->Value.LengthInChars = (DWORD)(Equals - ThisVar) + Index + 1;
                Match->Value.LengthAllocated = Match->Value.LengthInChars + 1;
                memcpy(Match->Value.StartOfString, SearchString->StartOfString, Index * sizeof(TCHAR));
                memcpy(&Match->Value.StartOfString[Index], ThisVar, VarNameLength * sizeof(TCHAR));
                Match->Value.StartOfString[Match->Value.LengthInChars - 1] = '%';
                Match->Value.StartOfString[Match->Value.LengthInChars] = '\0';

                //
                //  MSFIX: Apply the KeepEntriesSorted logic? The environment
                //  will always be sorted, so without this environment matches
                //  come before file matches, which doesn't seem so bad...
                //

                YoriShAddMatchToTabContext(TabContext, NULL, Match);
            }
        }

        ThisVar += _tcslen(ThisVar);
        ThisVar++;
    }

    YoriLibFreeStringContents(&EnvironmentStrings);
}

/**
 Invoked for each file matching a file based tab completion pattern.

 @param Filename Pointer to a string containing the full file name.

 @param FileInfo Pointer to the block of information returned by directory
        enumeration describing the entry.

 @param Depth Specifies the recursion depth.  Ignored in this function.

 @param Context Pointer to a context block describing the tab completion
        context to populate with the entry and properties for the population.

 @return TRUE to continue enumerating, FALSE to stop.
 */
BOOL
YoriShFileTabCompletionCallback(
    __in PYORI_STRING Filename,
    __in PWIN32_FIND_DATA FileInfo,
    __in DWORD Depth,
    __in PVOID Context
    )
{
    PYORI_LIST_ENTRY ListEntry;
    PYORI_SH_TAB_COMPLETE_MATCH Match;
    PYORI_SH_TAB_COMPLETE_MATCH Existing;
    INT CompareResult;
    PYORI_SH_FILE_COMPLETE_CONTEXT FileCompleteContext = (PYORI_SH_FILE_COMPLETE_CONTEXT)Context;

    UNREFERENCED_PARAMETER(Depth);

    if (FileCompleteContext->ExpandFullPath) {

        //
        //  Allocate a match entry for this file.
        //

        Match = YoriLibReferencedMalloc(sizeof(YORI_SH_TAB_COMPLETE_MATCH) + (FileCompleteContext->Prefix.LengthInChars + Filename->LengthInChars + 1) * sizeof(TCHAR));
        if (Match == NULL) {
            return FALSE;
        }

        //
        //  Populate the file into the entry.
        //

        YoriLibInitEmptyString(&Match->Value);
        Match->Value.StartOfString = (LPTSTR)(Match + 1);
        YoriLibReference(Match);
        Match->Value.MemoryToFree = Match;

        Match->Value.LengthInChars = YoriLibSPrintf(Match->Value.StartOfString, _T("%y%y"), &FileCompleteContext->Prefix, Filename);

        Match->Value.LengthAllocated = Match->Value.LengthInChars + 1;
    } else {
        YORI_STRING StringToFinalSlash;
        YORI_STRING SearchAfterFinalSlash;
        YORI_STRING LongFileName;
        YORI_STRING ShortFileName;
        PYORI_STRING FileNameToUse = NULL;

        YoriLibConstantString(&LongFileName, FileInfo->cFileName);
        YoriLibConstantString(&ShortFileName, FileInfo->cAlternateFileName);

        if (ShortFileName.LengthInChars == 0) {
            FileNameToUse = &LongFileName;
        } else {
            YoriLibConstantString(&SearchAfterFinalSlash, &FileCompleteContext->SearchString[FileCompleteContext->CharsToFinalSlash]);
            ASSERT(SearchAfterFinalSlash.LengthInChars > 0);
            if (YoriLibDoesFileMatchExpression(&LongFileName, &SearchAfterFinalSlash)) {
                FileNameToUse = &LongFileName;
            } else if (YoriLibDoesFileMatchExpression(&ShortFileName, &SearchAfterFinalSlash)) {
                FileNameToUse = &ShortFileName;
            } else {

                //
                //  If we can't match the long or the short name, it can be
                //  because the expression contains extended path operators
                //  such as {} or [].  In order to use these with suggestions
                //  this code would need to be a lot smarter.  We could use
                //  them for tab completion, but this piece of code can't
                //  currently tell the difference.
                //

                return TRUE;
            }
        }

        //
        //  Allocate a match entry for this file.
        //

        Match = YoriLibReferencedMalloc(sizeof(YORI_SH_TAB_COMPLETE_MATCH) + (FileCompleteContext->Prefix.LengthInChars + FileCompleteContext->CharsToFinalSlash + FileNameToUse->LengthInChars + 1) * sizeof(TCHAR));
        if (Match == NULL) {
            return FALSE;
        }

        //
        //  Populate the file into the entry.
        //

        YoriLibInitEmptyString(&Match->Value);
        Match->Value.StartOfString = (LPTSTR)(Match + 1);
        YoriLibReference(Match);
        Match->Value.MemoryToFree = Match;

        YoriLibInitEmptyString(&StringToFinalSlash);
        StringToFinalSlash.StartOfString = FileCompleteContext->SearchString;
        StringToFinalSlash.LengthInChars = FileCompleteContext->CharsToFinalSlash;


        Match->Value.LengthInChars = YoriLibSPrintf(Match->Value.StartOfString, _T("%y%y%y"), &FileCompleteContext->Prefix, &StringToFinalSlash, FileNameToUse);

        Match->Value.LengthAllocated = Match->Value.LengthInChars + 1;
    }

    //
    //  Insert into the list.  Don't insert if an entry with the same
    //  string is found.  If maintaining sorting, insert before an entry
    //  that is greater than this one.
    //

    if (!FileCompleteContext->KeepCompletionsSorted) {
        PYORI_HASH_ENTRY PriorEntry;
        PriorEntry = YoriLibHashLookupByKey(FileCompleteContext->TabContext->MatchHashTable, &Match->Value);
        if (PriorEntry == NULL) {
            YoriShAddMatchToTabContext(FileCompleteContext->TabContext, NULL, Match);
        } else {
            YoriLibFreeStringContents(&Match->Value);
            YoriLibDereference(Match);
            Match = NULL;
        }
    } else {
        ListEntry = YoriLibGetNextListEntry(&FileCompleteContext->TabContext->MatchList, NULL);
        do {
            if (ListEntry == NULL) {
                YoriShAddMatchToTabContext(FileCompleteContext->TabContext, NULL, Match);
                break;
            }
            Existing = CONTAINING_RECORD(ListEntry, YORI_SH_TAB_COMPLETE_MATCH, ListEntry);
            CompareResult = YoriLibCompareStringInsensitive(&Match->Value, &Existing->Value);
            if (CompareResult < 0) {
                YoriShAddMatchToTabContext(FileCompleteContext->TabContext, ListEntry, Match);
                break;
            } else if (CompareResult == 0) {
                YoriLibFreeStringContents(&Match->Value);
                YoriLibDereference(Match);
                Match = NULL;
                break;
            }
            ListEntry = YoriLibGetNextListEntry(&FileCompleteContext->TabContext->MatchList, ListEntry);
        } while(TRUE);
    }

    if (Match != NULL) {
        FileCompleteContext->FilesFound++;
    }

    return TRUE;
}

/**
 A structure describing a string which when encountered in a string used for
 file tab completion may indicate the existence of a file.
 */
typedef struct _YORI_SH_TAB_FILE_HEURISTIC_MATCH {

    /**
     The string to match against.
     */
    LPCTSTR MatchString;

    /**
     The offset, from the beginning of the matched string, to where the file
     name would be.  Note this value can be negative, indicating a match of a
     string within a file name.
     */
    INT CharsToSkip;
} YORI_SH_TAB_FILE_HEURISTIC_MATCH, *PYORI_SH_TAB_FILE_HEURISTIC_MATCH;

/**
 A list of strings which if found indicate no further file name matching
 should take place.
 */
YORI_SH_TAB_FILE_HEURISTIC_MATCH YoriShTabHeuristicMismatches[] = {
    {_T("://"), 0}
};

/**
 A list of strings which may, heuristically, indicate a good place to look for
 file names.
 */
YORI_SH_TAB_FILE_HEURISTIC_MATCH YoriShTabHeuristicMatches[] = {
    {_T(":\\"), -1},
    {_T("\\\\"), 0},
    {_T(">>"), 2},
    {_T(">"), 1},
    {_T(":"), 1},
    {_T("="), 1},
    {_T("'"), 1}
};

/**
 Populates the list of matches for a file based tab completion.  This
 function searches the path for matching files in lexicographic order
 and populates the list with the result.

 @param TabContext Pointer to the tab completion context.  This provides
        the search criteria and has its match list populated with results
        on success.

 @param ExpandFullPath Specifies if full path expansion should be performed.

 @param IncludeDirectories TRUE if directories should be included in results,
        FALSE if they should be ommitted.

 @param IncludeFiles TRUE if files should be included in results, FALSE if
        they should be ommitted (used for directory only results.)

 @param KeepCompletionsSorted TRUE to keep the list of completion options
        sorted.  This is generally useful for file completion, and matches
        what CMD does.  It's FALSE if file completions are being added after
        executable completion, so the goal is to preserve the executable
        completion items first.

 */
VOID
YoriShPerformFileTabCompletion(
    __inout PYORI_SH_TAB_COMPLETE_CONTEXT TabContext,
    __in BOOL ExpandFullPath,
    __in BOOL IncludeDirectories,
    __in BOOL IncludeFiles,
    __in BOOL KeepCompletionsSorted
    )
{
    YORI_SH_FILE_COMPLETE_CONTEXT EnumContext;
    YORI_STRING SearchString;
    DWORD PrefixLen;
    DWORD MatchFlags = 0;

    YoriLibInitEmptyString(&SearchString);
    SearchString.StartOfString = TabContext->SearchString.StartOfString;
    SearchString.LengthInChars = TabContext->SearchString.LengthInChars;
    SearchString.LengthAllocated = TabContext->SearchString.LengthAllocated;

    //
    //  Strip off any file:/// prefix
    //

    PrefixLen = sizeof("file:///") - 1;

    if (YoriLibCompareStringWithLiteralInsensitiveCount(&SearchString, _T("file:///"), PrefixLen) == 0) {
        SearchString.StartOfString += PrefixLen;
        SearchString.LengthInChars -= PrefixLen;
        SearchString.LengthAllocated -= PrefixLen;
    }

    YoriLibInitEmptyString(&EnumContext.Prefix);
    EnumContext.KeepCompletionsSorted = KeepCompletionsSorted;

    EnumContext.ExpandFullPath = ExpandFullPath;
    EnumContext.CharsToFinalSlash = YoriShFindFinalSlashIfSpecified(&SearchString);
    EnumContext.SearchString = SearchString.StartOfString;
    EnumContext.TabContext = TabContext;
    EnumContext.FilesFound = 0;

    //
    //  Set flags indicating what to find
    //

    if (IncludeFiles) {
        MatchFlags |= YORILIB_FILEENUM_RETURN_FILES;
    }
    if (IncludeDirectories) {
        MatchFlags |= YORILIB_FILEENUM_RETURN_DIRECTORIES;
    }

    //
    //  If there's nothing to find, we're done
    //

    if (MatchFlags == 0) {
        YoriLibFreeStringContents(&SearchString);
        return;
    }

    //
    //  Before looking for files, look for environment matches.  This won't
    //  do anything unless '%' is found in the string and there are variables
    //  whose prefix is the string following it.
    //

    YoriShPerformEnvironmentTabCompletion(TabContext, &SearchString);

    //
    //  > and < are actually obscure wildcard characters in NT that nobody
    //  uses for that purpose, but people do use them on shells to redirect
    //  commands.  If the string starts with these, don't bother with a
    //  wildcard match, fall through to below where we'll take note of the
    //  redirection prefix and perform matches on paths that follow it.
    //

    if (SearchString.LengthInChars < 1 ||
        (SearchString.StartOfString[0] != '>' && SearchString.StartOfString[0] != '<')) {

        YoriLibForEachStream(&SearchString, MatchFlags, 0, YoriShFileTabCompletionCallback, NULL, &EnumContext);
    }

    //
    //  If we haven't found any matches against the literal file name, strip
    //  off common prefixes and continue searching for files.  On matches these
    //  prefixes are added back.  This is used for commands which include a
    //  file name but it's prefixed with some other string.
    //

    if (EnumContext.FilesFound == 0) {
        DWORD MatchCount = sizeof(YoriShTabHeuristicMatches)/sizeof(YoriShTabHeuristicMatches[0]);
        DWORD MismatchCount = sizeof(YoriShTabHeuristicMismatches)/sizeof(YoriShTabHeuristicMismatches[0]);
        DWORD AllocCount;
        DWORD Index;
        DWORD StringOffsetOfMatch;
        PYORI_STRING MatchArray;
        PYORI_STRING FoundMatch;

        AllocCount = MatchCount;
        if (AllocCount < MismatchCount) {
            AllocCount = MismatchCount;
        }

        MatchArray = YoriLibMalloc(sizeof(YORI_STRING) * AllocCount);
        if (MatchArray == NULL) {
            YoriLibFreeStringContents(&SearchString);
            return;
        }

        //
        //  First check for any mismatch, indicating we shouldn't try for
        //  a heuristic match.
        //

        for (Index = 0; Index < MismatchCount; Index++) {
            YoriLibConstantString(&MatchArray[Index], YoriShTabHeuristicMismatches[Index].MatchString);
        }

        FoundMatch = YoriLibFindFirstMatchingSubstring(&SearchString, MismatchCount, MatchArray, &StringOffsetOfMatch);
        if (FoundMatch != NULL) {
            YoriLibFree(MatchArray);
            YoriLibFreeStringContents(&SearchString);
            return;
        }

        //
        //  Now look for any heuristic matches.
        //

        for (Index = 0; Index < MatchCount; Index++) {
            YoriLibConstantString(&MatchArray[Index], YoriShTabHeuristicMatches[Index].MatchString);
        }

        FoundMatch = YoriLibFindFirstMatchingSubstring(&SearchString, MatchCount, MatchArray, &StringOffsetOfMatch);
        if (FoundMatch == NULL) {
            YoriLibFree(MatchArray);
            YoriLibFreeStringContents(&SearchString);
            return;
        }

        for (Index = 0; Index < MatchCount; Index++) {
            if (FoundMatch->StartOfString == MatchArray[Index].StartOfString) {
                break;
            }
        }

        if (Index == MatchCount) {
            ASSERT(Index != MatchCount);
            YoriLibFree(MatchArray);
            YoriLibFreeStringContents(&SearchString);
            return;
        }

        //
        //  If the file would begin before the beginning of the string, stop.
        //

        if (YoriShTabHeuristicMatches[Index].CharsToSkip < 0 &&
            (DWORD)(-1 * YoriShTabHeuristicMatches[Index].CharsToSkip) > StringOffsetOfMatch) {

            YoriLibFree(MatchArray);
            YoriLibFreeStringContents(&SearchString);
            return;
        }

        //
        //  If the file would begin beyond the end of the string, stop.
        //

        if (YoriShTabHeuristicMatches[Index].CharsToSkip > 0 &&
            StringOffsetOfMatch + (DWORD)YoriShTabHeuristicMatches[Index].CharsToSkip >= SearchString.LengthInChars) {

            YoriLibFree(MatchArray);
            YoriLibFreeStringContents(&SearchString);
            return;
        }

        //
        //  Seperate the string between the file portion (that we're looking
        //  for) and a prefix to append to any match.
        //

        EnumContext.Prefix.StartOfString = SearchString.StartOfString;
        EnumContext.Prefix.LengthInChars = StringOffsetOfMatch + YoriShTabHeuristicMatches[Index].CharsToSkip;

        SearchString.StartOfString += EnumContext.Prefix.LengthInChars;
        SearchString.LengthInChars -= EnumContext.Prefix.LengthInChars;

        EnumContext.CharsToFinalSlash = YoriShFindFinalSlashIfSpecified(&SearchString);
        EnumContext.SearchString = SearchString.StartOfString;

        YoriLibForEachStream(&SearchString, MatchFlags, 0, YoriShFileTabCompletionCallback, NULL, &EnumContext);

        YoriLibFree(MatchArray);
    }
    YoriLibFreeStringContents(&SearchString);
    return;
}

/**
 A context describing the actions that can be performed in response to a
 completion within a command argument.
 */
typedef struct _YORI_SH_ARG_TAB_COMPLETION_ACTION {

    /**
     The type of action to perform for argument completion.
     */
    enum {
        CompletionActionTypeFilesAndDirectories = 1,
        CompletionActionTypeFiles = 2,
        CompletionActionTypeDirectories = 3,
        CompletionActionTypeExecutables = 4,
        CompletionActionTypeExecutablesAndBuiltins = 5,
        CompletionActionTypeInsensitiveList = 6,
        CompletionActionTypeSensitiveList = 7
    } CompletionAction;

    /**
     For sensitive and insensitive lists, the list of matches.  Note these
     aren't guaranteed to match the specified criteria.
     */
    YORI_LIST_ENTRY List;
} YORI_SH_ARG_TAB_COMPLETION_ACTION, *PYORI_SH_ARG_TAB_COMPLETION_ACTION;

/**
 Perform a list tab completion.  This walks through a list provided by a
 completion script and compares each entry to the currently input string.
 If they match, the list entry is retained (including its current allocation)
 and is transferred to the completion list.  Any entry that is not a match is
 deallocated in this routine.

 @param TabContext Pointer to the tab completion context.  This provides
        the search criteria and has its match list populated with results
        on success.

 @param CompletionAction The action to perform, including the list of
        candidates to test against the currently entered string.

 @param Insensitive If TRUE, comparisons are case insensitive; if FALSE,
        comparisons are case sensitive.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOL
YoriShPerformListTabCompletion(
    __inout PYORI_SH_TAB_COMPLETE_CONTEXT TabContext,
    __inout PYORI_SH_ARG_TAB_COMPLETION_ACTION CompletionAction,
    __in BOOL Insensitive
    )
{
    PYORI_LIST_ENTRY ListEntry;
    PYORI_LIST_ENTRY NextEntry;
    PYORI_SH_TAB_COMPLETE_MATCH Match;
    YORI_STRING SearchString;
    int MatchResult;

    //
    //  Generate the current argument being completed without any
    //  trailing '*'.
    //

    YoriLibInitEmptyString(&SearchString);
    SearchString.StartOfString = TabContext->SearchString.StartOfString;
    SearchString.LengthInChars = TabContext->SearchString.LengthInChars;

    if (SearchString.LengthInChars > 0 &&
        SearchString.StartOfString[SearchString.LengthInChars - 1] == '*') {

        SearchString.LengthInChars--;
    }

    ListEntry = YoriLibGetNextListEntry(&CompletionAction->List, NULL);
    while (ListEntry != NULL) {
        Match = CONTAINING_RECORD(ListEntry, YORI_SH_TAB_COMPLETE_MATCH, ListEntry);
        NextEntry = YoriLibGetNextListEntry(&CompletionAction->List, ListEntry);

        YoriLibRemoveListItem(&Match->ListEntry);

        //
        //  Check if the given list item matches the current string being
        //  completed.
        //

        if (Insensitive) {
            MatchResult = YoriLibCompareStringInsensitiveCount(&SearchString, &Match->Value, SearchString.LengthInChars);
        } else {
            MatchResult = YoriLibCompareStringCount(&SearchString, &Match->Value, SearchString.LengthInChars);
        }

        //
        //  If it's a match, add it to the list; if not, free it.
        //

        if (MatchResult == 0) {
            YoriShAddMatchToTabContext(TabContext, NULL, Match);
        } else {
            YoriLibFreeStringContents(&Match->Value);
            YoriLibDereference(Match);
        }

        ListEntry = NextEntry;
    }

    return TRUE;
}

/**
 Parse a string describing the actions to perform for a specific tab completion
 into a master action, possibly including a list of values associated with
 that action.

 @param TabCompletionString Pointer to the string describing the action.

 @param TabCompletionAction On successful completion, updated with the
        description for the tab completion action.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOL
YoriShResolveTabCompletionStringToAction(
    __in PYORI_STRING TabCompletionString,
    __out PYORI_SH_ARG_TAB_COMPLETION_ACTION TabCompletionAction
    )
{
    YORI_SH_CMD_CONTEXT CmdContext;

    if (!YoriShParseCmdlineToCmdContext(TabCompletionString, 0, &CmdContext)) {
        return FALSE;
    }

    if (CmdContext.ArgC == 0) {
        YoriShFreeCmdContext(&CmdContext);
        return FALSE;
    }

    if (YoriLibCompareStringWithLiteralInsensitive(&CmdContext.ArgV[0], _T("/commands")) == 0) {
        TabCompletionAction->CompletionAction = CompletionActionTypeExecutablesAndBuiltins;
    } else if (YoriLibCompareStringWithLiteralInsensitive(&CmdContext.ArgV[0], _T("/directories")) == 0) {
        TabCompletionAction->CompletionAction = CompletionActionTypeDirectories;
    } else if (YoriLibCompareStringWithLiteralInsensitive(&CmdContext.ArgV[0], _T("/executables")) == 0) {
        TabCompletionAction->CompletionAction = CompletionActionTypeExecutables;
    } else if (YoriLibCompareStringWithLiteralInsensitive(&CmdContext.ArgV[0], _T("/files")) == 0) {
        TabCompletionAction->CompletionAction = CompletionActionTypeFilesAndDirectories;
    } else if (YoriLibCompareStringWithLiteralInsensitive(&CmdContext.ArgV[0], _T("/filesonly")) == 0) {
        TabCompletionAction->CompletionAction = CompletionActionTypeFiles;
    } else if (YoriLibCompareStringWithLiteralInsensitive(&CmdContext.ArgV[0], _T("/insensitivelist")) == 0) {
        TabCompletionAction->CompletionAction = CompletionActionTypeInsensitiveList;
    } else if (YoriLibCompareStringWithLiteralInsensitive(&CmdContext.ArgV[0], _T("/sensitivelist")) == 0) {
        TabCompletionAction->CompletionAction = CompletionActionTypeSensitiveList;
    } else {
        YoriShFreeCmdContext(&CmdContext);
        return FALSE;
    }


    //
    //  If the request specifies a list of things, populate the list from
    //  the command context into the list of match candidates.
    //

    if (TabCompletionAction->CompletionAction == CompletionActionTypeInsensitiveList ||
        TabCompletionAction->CompletionAction == CompletionActionTypeSensitiveList) {

        DWORD Count;
        PYORI_SH_TAB_COMPLETE_MATCH Match;

        for (Count = 1; Count < CmdContext.ArgC; Count++) {
            //
            //  Allocate a match entry for this file.
            //

            Match = YoriLibReferencedMalloc(sizeof(YORI_SH_TAB_COMPLETE_MATCH) + (CmdContext.ArgV[Count].LengthInChars + 1) * sizeof(TCHAR));
            if (Match == NULL) {
                YoriShFreeCmdContext(&CmdContext);
                return TRUE;
            }

            //
            //  Populate the file into the entry.
            //

            YoriLibInitEmptyString(&Match->Value);
            Match->Value.StartOfString = (LPTSTR)(Match + 1);
            YoriLibReference(Match);
            Match->Value.MemoryToFree = Match;
            YoriLibSPrintf(Match->Value.StartOfString, _T("%y"), &CmdContext.ArgV[Count]);
            Match->Value.LengthInChars = CmdContext.ArgV[Count].LengthInChars;

            //
            //  Append to the list.
            //

            YoriLibAppendList(&TabCompletionAction->List, &Match->ListEntry);
        }
    }

    YoriShFreeCmdContext(&CmdContext);
    return TRUE;
}

/**
 Check for the given executable or builtin command how to expand its arguments.

 @param TabContext Pointer to the tab completion context.  This provides
        the search criteria and has its match list populated with results
        on success.
 
 @param Executable Pointer to the executable or builtin command.  Note this
        is a fully qualified path on entry.

 @param CurrentArg Specifies the index of the argument being completed for this
        command.

 @param Action On successful completion, populated with the action to perform
        for this command.
 */
BOOL
YoriShResolveTabCompletionActionForExecutable(
    __inout PYORI_SH_TAB_COMPLETE_CONTEXT TabContext,
    __in PYORI_STRING Executable,
    __in DWORD CurrentArg,
    __out PYORI_SH_ARG_TAB_COMPLETION_ACTION Action
    )
{
    YORI_STRING FilePartOnly;
    YORI_STRING ActionString;
    YORI_STRING YoriCompletePathVariable;
    YORI_STRING FoundCompletionScript;
    YORI_STRING CompletionExpression;
    YORI_STRING ArgToComplete;
    DWORD FinalSeperator;

    UNREFERENCED_PARAMETER(CurrentArg);

    YoriLibInitializeListHead(&Action->List);

    //
    //  Find just the executable name, without any prepending path.
    //

    FinalSeperator = YoriShFindFinalSlashIfSpecified(Executable);

    YoriLibInitEmptyString(&FilePartOnly);
    FilePartOnly.StartOfString = &Executable->StartOfString[FinalSeperator];
    FilePartOnly.LengthInChars = Executable->LengthInChars - FinalSeperator;

    if (FilePartOnly.LengthInChars == 0) {
        Action->CompletionAction = CompletionActionTypeFilesAndDirectories;
        return TRUE;
    }

    //
    //  Find the set of locations to search for completion scripts.  This
    //  may be empty but typically shouldn't fail except for memory.
    //

    if (!YoriShAllocateAndGetEnvironmentVariable(_T("YORICOMPLETEPATH"), &YoriCompletePathVariable, NULL)) {
        return FALSE;
    }

    if (YoriCompletePathVariable.LengthInChars == 0) {
        Action->CompletionAction = CompletionActionTypeFilesAndDirectories;
        return TRUE;
    }

    if (!YoriLibAllocateString(&FoundCompletionScript, YoriCompletePathVariable.LengthInChars + MAX_PATH)) {
        YoriLibFreeStringContents(&YoriCompletePathVariable);
        return FALSE;
    }

    //
    //  Search through the locations for a matching script name.  If there
    //  isn't one, perform a default action.
    //

    if (!YoriLibPathLocateUnknownExtensionUnknownLocation(&FilePartOnly, &YoriCompletePathVariable, NULL, NULL, &FoundCompletionScript)) {
        YoriLibFreeStringContents(&FoundCompletionScript);
        YoriLibFreeStringContents(&YoriCompletePathVariable);

        Action->CompletionAction = CompletionActionTypeFilesAndDirectories;
        return TRUE;
    }

    YoriLibFreeStringContents(&YoriCompletePathVariable);

    if (FoundCompletionScript.LengthInChars == 0) {
        YoriLibFreeStringContents(&FoundCompletionScript);
        Action->CompletionAction = CompletionActionTypeFilesAndDirectories;
        return TRUE;
    }

    //
    //  If there is one, create an expression and invoke the script.
    //

    YoriLibInitEmptyString(&ArgToComplete);
    ArgToComplete.StartOfString = TabContext->SearchString.StartOfString;
    ArgToComplete.LengthInChars = TabContext->SearchString.LengthInChars;

    if (ArgToComplete.LengthInChars > 0 &&
        ArgToComplete.StartOfString[ArgToComplete.LengthInChars - 1] == '*') {

        ArgToComplete.LengthInChars--;
    }
    if (!YoriLibAllocateString(&CompletionExpression, FoundCompletionScript.LengthInChars + 20 + ArgToComplete.LengthInChars)) {

        YoriLibFreeStringContents(&FoundCompletionScript);
        return FALSE;
    }

    CompletionExpression.LengthInChars = YoriLibSPrintf(CompletionExpression.StartOfString, _T("\"%y\" %i %y"), &FoundCompletionScript, CurrentArg, &ArgToComplete);

    YoriLibFreeStringContents(&FoundCompletionScript);

    if (!YoriShExecuteExpressionAndCaptureOutput(&CompletionExpression, &ActionString)) {

        Action->CompletionAction = CompletionActionTypeFilesAndDirectories;
        return TRUE;
    }

    YoriLibFreeStringContents(&CompletionExpression);

    //
    //  Parse the result and determine the appropriate action.
    //

    if (!YoriShResolveTabCompletionStringToAction(&ActionString, Action)) {
        YoriLibFreeStringContents(&ActionString);
        Action->CompletionAction = CompletionActionTypeFilesAndDirectories;
        return TRUE;
    }

    YoriLibFreeStringContents(&ActionString);
    return TRUE;
}

/**
 Populates the list of matches for a command argument based tab completion.
 This function uses command specific patterns to determine how to complete
 arguments.

 @param TabContext Pointer to the tab completion context.  This provides
        the search criteria and has its match list populated with results
        on success.

 @param ExpandFullPath Specifies if full path expansion should be performed.

 @param CmdContext Pointer to a caller allocated command context since
        basic arguments have already been parsed.  Note this should not be
        modified in this routine (that's for the caller.)
 */
VOID
YoriShPerformArgumentTabCompletion(
    __inout PYORI_SH_TAB_COMPLETE_CONTEXT TabContext,
    __in BOOL ExpandFullPath,
    __in PYORI_SH_CMD_CONTEXT CmdContext
    )
{
    YORI_SH_ARG_TAB_COMPLETION_ACTION CompletionAction;
    YORI_SH_EXEC_PLAN ExecPlan;
    PYORI_SH_SINGLE_EXEC_CONTEXT CurrentExecContext;
    DWORD CurrentExecContextArg;
    BOOL ActiveExecContextArg;
    BOOL ExecutableFound;
    PYORI_LIST_ENTRY ListEntry;
    PYORI_SH_TAB_COMPLETE_MATCH Match;

    //
    //  This function needs to implement a more substantial parser
    //  than many others.
    //
    //  1. Take the active character and locate which backquote scope
    //     it's in, and calculate cursor position within the substring.
    //
    //  2. Resolve the current string into a command context, capturing
    //     the current argument.
    //
    //  3. Parse the command context into an exec plan, and capture which
    //     exec context contains the current argument.  Note the answer
    //     may be none, since the argument might be a seperator.  This
    //     returns which argument within the exec context is the thing
    //     we're trying to complete too, which may be none due to things
    //     like redirection, so we're in one command but not in its
    //     arguments.
    //
    //  4. If we're looking at the first thing in an exec context, perform
    //     executable completion.
    //
    //  5. If we're looking at something that's not a command argument,
    //     perform file completion.
    //
    //  6. If we're an arg within an exec context, take the first thing
    //     from the exec context, resolve aliases, resolve path, and
    //     we should then either be looking at a located executable or
    //     be searching for a builtin.  Look for a matching script to
    //     handle this command.
    //
    //  7. The parent is responsible for reassembling everything and doesn't
    //     need all of this state.  This function is only using the state to
    //     populate match criteria.
    //

    //
    //  Currently the caller won't call here for argument zero
    //

    ASSERT(CmdContext->CurrentArg > 0);

    //
    //  Parse the command context into an exec plan (series of programs to
    //  run), and find which program is the one the argument is for
    //

    ActiveExecContextArg = FALSE;
    CurrentExecContextArg = 0;
    CurrentExecContext = NULL;

    if (!YoriShParseCmdContextToExecPlan(CmdContext, &ExecPlan, &CurrentExecContext, &ActiveExecContextArg, &CurrentExecContextArg)) {
        return;
    }

    if (!ActiveExecContextArg) {

        //
        //  The active argument isn't for the receiving program.  Default to
        //  handing it to file completion.
        //

        CompletionAction.CompletionAction = CompletionActionTypeFilesAndDirectories;
        YoriLibInitializeListHead(&CompletionAction.List);
    } else if (CurrentExecContextArg == 0) {

        //
        //  The active argument is the first one, to launch a program.  Default
        //  to handing it to executable completion.
        //

        CompletionAction.CompletionAction = CompletionActionTypeExecutablesAndBuiltins;
        YoriLibInitializeListHead(&CompletionAction.List);
    } else {

        ASSERT(CurrentExecContext != NULL);

        //
        //  The active argument is for the program.  Resolve the program
        //  aliases and path to an unambiguous thing to execute.
        //

        if (!YoriShResolveCommandToExecutable(&CurrentExecContext->CmdToExec, &ExecutableFound)) {
            YoriShFreeExecPlan(&ExecPlan);
            return;
        }

        //
        //  Determine the action to perform for this particular executable.
        //

        if (!YoriShResolveTabCompletionActionForExecutable(TabContext, &CurrentExecContext->CmdToExec.ArgV[0], CurrentExecContextArg, &CompletionAction)) {
            YoriShFreeExecPlan(&ExecPlan);
            return;
        }
    }

    //
    //  Perform the requested completion action.
    //

    switch(CompletionAction.CompletionAction) {
        case CompletionActionTypeFilesAndDirectories:
            YoriShPerformFileTabCompletion(TabContext, ExpandFullPath, TRUE, TRUE, TRUE);
            break;
        case CompletionActionTypeFiles:
            YoriShPerformFileTabCompletion(TabContext, ExpandFullPath, FALSE, TRUE, TRUE);
            break;
        case CompletionActionTypeDirectories:
            YoriShPerformFileTabCompletion(TabContext, ExpandFullPath, TRUE, FALSE, TRUE);
            break;
        case CompletionActionTypeExecutables:
            YoriShPerformExecutableTabCompletion(TabContext, ExpandFullPath, FALSE);
            break;
        case CompletionActionTypeExecutablesAndBuiltins:
            YoriShPerformExecutableTabCompletion(TabContext, ExpandFullPath, TRUE);
            break;
        case CompletionActionTypeInsensitiveList:
            YoriShPerformListTabCompletion(TabContext, &CompletionAction, TRUE);
            break;
        case CompletionActionTypeSensitiveList:
            YoriShPerformListTabCompletion(TabContext, &CompletionAction, FALSE);
            TabContext->CaseSensitive = TRUE;
            break;
    }

    //
    //  Free any items that completion scripts have populated for list
    //  completion.
    //

    ListEntry = YoriLibGetNextListEntry(&CompletionAction.List, NULL);
    while (ListEntry != NULL) {
        Match = CONTAINING_RECORD(ListEntry, YORI_SH_TAB_COMPLETE_MATCH, ListEntry);
        ListEntry = YoriLibGetNextListEntry(&CompletionAction.List, ListEntry);

        YoriLibFreeStringContents(&Match->Value);
        YoriLibDereference(Match);
    }

    YoriShFreeExecPlan(&ExecPlan);
}

/**
 Populate tab completion matches.

 @param Buffer Pointer to the input buffer.

 @param CmdContext Pointer to the parsed command context.

 @param TabFlags Specifies the tab behavior to exercise, including whether
        to include full path or relative path, whether to match command
        history or files and arguments.
 */
VOID
YoriShPopulateTabCompletionMatches(
    __inout PYORI_SH_INPUT_BUFFER Buffer,
    __inout PYORI_SH_CMD_CONTEXT CmdContext,
    __in DWORD TabFlags
    )
{
    YORI_STRING CurrentArgString;
    DWORD SearchLength;
    BOOL KeepSorted;
    BOOL SearchHistory = FALSE;
    BOOL ExpandFullPath = FALSE;

    if ((TabFlags & YORI_SH_TAB_COMPLETE_FULL_PATH) != 0) {
        ExpandFullPath = TRUE;
    }

    if ((TabFlags & YORI_SH_TAB_COMPLETE_HISTORY) != 0) {
        SearchHistory = TRUE;
    }

    YoriLibInitEmptyString(&CurrentArgString);

    if (Buffer->TabContext.MatchHashTable == NULL) {
        Buffer->TabContext.MatchHashTable = YoriLibAllocateHashTable(250);
        if (Buffer->TabContext.MatchHashTable == NULL) {
            return;
        }
    }
    YoriLibInitializeListHead(&Buffer->TabContext.MatchList);
    Buffer->TabContext.PreviousMatch = NULL;

    if (CmdContext->CurrentArg < CmdContext->ArgC) {
        memcpy(&CurrentArgString, &CmdContext->ArgV[CmdContext->CurrentArg], sizeof(YORI_STRING));
    }

    if (SearchHistory) {
        SearchLength = Buffer->String.LengthInChars + 1;
        if (!YoriLibAllocateString(&Buffer->TabContext.SearchString, SearchLength + 1)) {
            return;
        }

        KeepSorted = TRUE;

        Buffer->TabContext.SearchString.LengthInChars = YoriLibSPrintfS(Buffer->TabContext.SearchString.StartOfString, SearchLength + 1, _T("%y*"), &Buffer->String);
    } else {
        SearchLength = CurrentArgString.LengthInChars + 1;
        if (!YoriLibAllocateString(&Buffer->TabContext.SearchString, SearchLength + 1)) {
            return;
        }

        KeepSorted = TRUE;

        Buffer->TabContext.SearchString.LengthInChars = YoriLibSPrintfS(Buffer->TabContext.SearchString.StartOfString, SearchLength + 1, _T("%y*"), &CurrentArgString);
    }

    if (SearchHistory) {
        Buffer->TabContext.SearchType = YoriTabCompleteSearchHistory;

    } else if (CmdContext->CurrentArg == 0) {

        YoriShPerformExecutableTabCompletion(&Buffer->TabContext, ExpandFullPath, TRUE);
        Buffer->TabContext.SearchType = YoriTabCompleteSearchFiles;
        if (YoriLibGetNextListEntry(&Buffer->TabContext.MatchList, NULL) != NULL) {
            KeepSorted = FALSE;
        }

    } else {
        Buffer->TabContext.SearchType = YoriTabCompleteSearchArguments;
    }

    if (Buffer->TabContext.SearchType == YoriTabCompleteSearchExecutables) {
        YoriShPerformExecutableTabCompletion(&Buffer->TabContext, ExpandFullPath, TRUE);
    } else if (Buffer->TabContext.SearchType == YoriTabCompleteSearchHistory) {
        YoriShPerformHistoryTabCompletion(&Buffer->TabContext, ExpandFullPath);
    } else if (Buffer->TabContext.SearchType == YoriTabCompleteSearchArguments) {
        YoriShPerformArgumentTabCompletion(&Buffer->TabContext, ExpandFullPath, CmdContext);
    } else {
        YoriShPerformFileTabCompletion(&Buffer->TabContext, ExpandFullPath, TRUE, TRUE, KeepSorted);
    }

    Buffer->TabContext.TabFlagsUsedCreatingList = TabFlags;
}

/**
 Free any matches collected as a result of a prior tab completion operation.

 @param Buffer The input buffer containing tab completion results to free.
 */
VOID
YoriShClearTabCompletionMatches(
    __inout PYORI_SH_INPUT_BUFFER Buffer
    )
{
    PYORI_LIST_ENTRY ListEntry = NULL;
    PYORI_SH_TAB_COMPLETE_MATCH Match;

    YoriLibFreeStringContents(&Buffer->TabContext.SearchString);

    ListEntry = YoriLibGetNextListEntry(&Buffer->TabContext.MatchList, NULL);
    while (ListEntry != NULL) {
        Match = CONTAINING_RECORD(ListEntry, YORI_SH_TAB_COMPLETE_MATCH, ListEntry);
        ListEntry = YoriLibGetNextListEntry(&Buffer->TabContext.MatchList, ListEntry);

        YoriShRemoveMatchFromTabContext(&Buffer->TabContext, Match);
    }

    if (Buffer->TabContext.MatchHashTable != NULL) {
        YoriLibFreeEmptyHashTable(Buffer->TabContext.MatchHashTable);
    }
    ZeroMemory(&Buffer->TabContext, sizeof(Buffer->TabContext));
}

/**
 Given a string that could be comprised of backquote regions, find the
 substring that should have tab completion matching applied to it.

 @param String Pointer to a string that potentially contains backquoted
        regions.

 @param CurrentOffset The currently selected offset within String, in
        characters.

 @param SearchType The type of tab completion matching to perform.

 @param BackquoteSubset On completion, updated to contain the subset of the
        string to perform tab completion matches against.

 @param OffsetInSubstring On completion, updated to contain the offset within
        the substring that is currently selected.

 @param PrefixBeforeBackquoteSubstring On completion, updated to contain the
        string before BackquoteSubset.

 @param SuffixAfterBackquoteSubstring On completion, updated to contain the
        string after BackquoteSubset.
 */
VOID
YoriShFindStringSubsetForCompletion(
    __in PYORI_STRING String,
    __in DWORD CurrentOffset,
    __in YORI_SH_TAB_COMPLETE_SEARCH_TYPE SearchType,
    __out PYORI_STRING BackquoteSubset,
    __out PDWORD OffsetInSubstring,
    __out PYORI_STRING PrefixBeforeBackquoteSubstring,
    __out PYORI_STRING SuffixAfterBackquoteSubstring
    )
{
    YoriLibInitEmptyString(BackquoteSubset);
    YoriLibInitEmptyString(PrefixBeforeBackquoteSubstring);
    YoriLibInitEmptyString(SuffixAfterBackquoteSubstring);

    if (SearchType != YoriTabCompleteSearchHistory &&
        YoriShFindBestBackquoteSubstringAtOffset(String, CurrentOffset, BackquoteSubset)) {

        PrefixBeforeBackquoteSubstring->StartOfString = String->StartOfString;
        PrefixBeforeBackquoteSubstring->LengthInChars = (DWORD)(BackquoteSubset->StartOfString - String->StartOfString);

        SuffixAfterBackquoteSubstring->StartOfString = BackquoteSubset->StartOfString + BackquoteSubset->LengthInChars;
        SuffixAfterBackquoteSubstring->LengthInChars = String->LengthInChars - PrefixBeforeBackquoteSubstring->LengthInChars - BackquoteSubset->LengthInChars;

        *OffsetInSubstring = CurrentOffset - PrefixBeforeBackquoteSubstring->LengthInChars;

    } else {
        YoriLibInitEmptyString(BackquoteSubset);
        BackquoteSubset->StartOfString = String->StartOfString;
        BackquoteSubset->LengthInChars = String->LengthInChars;
        *OffsetInSubstring = CurrentOffset;
    }
}



/**
 A subset of flags that determine the composition of the match set.  If these
 flags change between two calls to YoriShTabCompletion, it implies the existing
 results are stale and invalid.
 */
#define YORI_SH_TAB_COMPLETE_COMPAT_MASK (YORI_SH_TAB_COMPLETE_FULL_PATH | YORI_SH_TAB_COMPLETE_HISTORY)

/**
 Perform tab completion processing.  On error the buffer is left unchanged.

 @param Buffer Pointer to the current input context.

 @param TabFlags Specifies the tab behavior to exercise, including whether
        to include full path or relative path, whether to match command
        history or files and arguments, and the direction to navigate through
        any matches.
 */
VOID
YoriShTabCompletion(
    __inout PYORI_SH_INPUT_BUFFER Buffer,
    __in DWORD TabFlags
    )
{
    YORI_SH_CMD_CONTEXT CmdContext;
    PYORI_LIST_ENTRY ListEntry;
    PYORI_SH_TAB_COMPLETE_MATCH Match;
    YORI_STRING BackquoteSubset;
    DWORD OffsetInSubstring;
    YORI_STRING PrefixBeforeBackquoteSubstring;
    YORI_STRING SuffixAfterBackquoteSubstring;

    if (Buffer->String.LengthInChars == 0) {
        return;
    }

    //
    //  If there's an existing list, check that it's a list for the same
    //  type of query as the current one.  If it's a different query,
    //  throw it away and start over.
    //

    if (Buffer->TabContext.MatchList.Next != NULL) {
        if ((TabFlags & YORI_SH_TAB_COMPLETE_COMPAT_MASK) != Buffer->TabContext.TabFlagsUsedCreatingList) {
            if (Buffer->SuggestionString.LengthInChars > 0) {
                YoriLibFreeStringContents(&Buffer->SuggestionString);
            }
            YoriShClearTabCompletionMatches(Buffer);
            Buffer->PriorTabCount = 0;
        }
    }

    YoriShFindStringSubsetForCompletion(&Buffer->String,
                                        Buffer->CurrentOffset,
                                        Buffer->TabContext.SearchType,
                                        &BackquoteSubset,
                                        &OffsetInSubstring,
                                        &PrefixBeforeBackquoteSubstring,
                                        &SuffixAfterBackquoteSubstring);

    ASSERT(Buffer->CurrentOffset >= PrefixBeforeBackquoteSubstring.LengthInChars);

    if (!YoriShParseCmdlineToCmdContext(&BackquoteSubset, OffsetInSubstring, &CmdContext)) {
        return;
    }

    if (CmdContext.ArgC == 0) {
        YoriShFreeCmdContext(&CmdContext);
        return;
    }

    Buffer->TabContext.TabCount++;

    //
    //  If we're searching for the first time, set up the search
    //  criteria and populate the list of matches.
    //

    if (Buffer->TabContext.TabCount == 1 && Buffer->TabContext.MatchList.Next == NULL) {
        YoriShPopulateTabCompletionMatches(Buffer,
                                           &CmdContext,
                                           TabFlags & YORI_SH_TAB_COMPLETE_COMPAT_MASK);
    }

    //
    //  Check if we have any match.  If we do, try to use it.  If not, leave
    //  the buffer unchanged.
    //

    if ((TabFlags & YORI_SH_TAB_COMPLETE_BACKWARDS) == 0) {
        ListEntry = YoriLibGetNextListEntry(&Buffer->TabContext.MatchList, &Buffer->TabContext.PreviousMatch->ListEntry);
        if (ListEntry == NULL) {
            if (Buffer->TabContext.TabCount != 1) {
                ListEntry = YoriLibGetNextListEntry(&Buffer->TabContext.MatchList, NULL);
            }
        }
    } else {
        ListEntry = YoriLibGetPreviousListEntry(&Buffer->TabContext.MatchList, &Buffer->TabContext.PreviousMatch->ListEntry);
        if (ListEntry == NULL) {
            if (Buffer->TabContext.TabCount != 1) {
                ListEntry = YoriLibGetPreviousListEntry(&Buffer->TabContext.MatchList, NULL);
            }
        }
    }
    if (ListEntry == NULL) {
        YoriShFreeCmdContext(&CmdContext);
        return;
    }

    YoriLibFreeStringContents(&Buffer->SuggestionString);
    Buffer->TabContext.CurrentArgLength = 0;
    Buffer->TabContext.CaseSensitive = FALSE;

    Match = CONTAINING_RECORD(ListEntry, YORI_SH_TAB_COMPLETE_MATCH, ListEntry);
    Buffer->TabContext.PreviousMatch = Match;

    {
        DWORD BeginCurrentArg;
        DWORD EndCurrentArg;
        LPTSTR NewString;
        BOOL FreeNewString = FALSE;
        DWORD NewStringLen;

        //
        //  MSFIX This isn't updating the referenced memory.  This works because
        //  we'll free the "correct" one and not the one we just put here, but
        //  it seems dodgy.
        //

        if (Buffer->TabContext.SearchType != YoriTabCompleteSearchHistory) {
            PYORI_STRING OldArgv = NULL;
            PYORI_SH_ARG_CONTEXT OldArgContext = NULL;
            DWORD OldArgCount = 0;

            if (CmdContext.CurrentArg >= CmdContext.ArgC) {
                DWORD Count;

                OldArgCount = CmdContext.ArgC;
                OldArgv = CmdContext.ArgV;
                OldArgContext = CmdContext.ArgContexts;

                CmdContext.ArgV = YoriLibMalloc((CmdContext.CurrentArg + 1) * (sizeof(YORI_STRING) + sizeof(YORI_SH_ARG_CONTEXT)));
                if (CmdContext.ArgV == NULL) {
                    YoriShFreeCmdContext(&CmdContext);
                    return;
                }

                CmdContext.ArgC = CmdContext.CurrentArg + 1;
                ZeroMemory(CmdContext.ArgV, CmdContext.ArgC * (sizeof(YORI_STRING) + sizeof(YORI_SH_ARG_CONTEXT)));
                CmdContext.ArgContexts = (PYORI_SH_ARG_CONTEXT)YoriLibAddToPointer(CmdContext.ArgV, CmdContext.ArgC * sizeof(YORI_STRING));

                memcpy(CmdContext.ArgV, OldArgv, OldArgCount * sizeof(YORI_STRING));
                for (Count = 0; Count < OldArgCount; Count++) {
                    CmdContext.ArgContexts[Count] = OldArgContext[Count];
                }

                YoriLibInitEmptyString(&CmdContext.ArgV[CmdContext.CurrentArg]);
            }

            YoriLibFreeStringContents(&CmdContext.ArgV[CmdContext.CurrentArg]);
            YoriLibCloneString(&CmdContext.ArgV[CmdContext.CurrentArg], &Match->Value);
            CmdContext.ArgContexts[CmdContext.CurrentArg].Quoted = FALSE;
            YoriShCheckIfArgNeedsQuotes(&CmdContext, CmdContext.CurrentArg);
            NewString = YoriShBuildCmdlineFromCmdContext(&CmdContext, FALSE, &BeginCurrentArg, &EndCurrentArg);

            if (OldArgv != NULL) {
                YoriLibFreeStringContents(&CmdContext.ArgV[CmdContext.CurrentArg]);
                YoriLibFree(CmdContext.ArgV);
                CmdContext.ArgC = OldArgCount;
                CmdContext.ArgV = OldArgv;
                CmdContext.ArgContexts = OldArgContext;
            }

            if (NewString == NULL) {
                YoriShFreeCmdContext(&CmdContext);
                return;
            }

            FreeNewString = TRUE;
            Buffer->CurrentOffset = PrefixBeforeBackquoteSubstring.LengthInChars + EndCurrentArg + 1;
            NewStringLen = _tcslen(NewString);

        } else {
            NewString = Match->Value.StartOfString;
            NewStringLen = Match->Value.LengthInChars;
            Buffer->CurrentOffset = PrefixBeforeBackquoteSubstring.LengthInChars + NewStringLen;
        }

        //
        //  Assemble the prefix (before backquote start), new string, and
        //  suffix into the input buffer.  Because the prefix and suffix
        //  are described within the buffer being written, these need to
        //  be reallocated and copied out first.
        //

        if (NewString != NULL) {
            if (!YoriShEnsureStringHasEnoughCharacters(&Buffer->String, PrefixBeforeBackquoteSubstring.LengthInChars + NewStringLen + SuffixAfterBackquoteSubstring.LengthInChars)) {
                YoriShFreeCmdContext(&CmdContext);
                if (FreeNewString) {
                    YoriLibDereference(NewString);
                }
                return;
            }

            if (PrefixBeforeBackquoteSubstring.LengthInChars > 0 &&
                !YoriLibReallocateString(&PrefixBeforeBackquoteSubstring, PrefixBeforeBackquoteSubstring.LengthInChars + 1)) {

                YoriShFreeCmdContext(&CmdContext);
                if (FreeNewString) {
                    YoriLibDereference(NewString);
                }
                return;
            }

            if (SuffixAfterBackquoteSubstring.LengthInChars > 0 &&
                !YoriLibReallocateString(&SuffixAfterBackquoteSubstring, SuffixAfterBackquoteSubstring.LengthInChars + 1)) {

                YoriLibFreeStringContents(&PrefixBeforeBackquoteSubstring);
                YoriShFreeCmdContext(&CmdContext);
                if (FreeNewString) {
                    YoriLibDereference(NewString);
                }
                return;
            }

            YoriLibFreeStringContents(&Buffer->SuggestionString);
            YoriLibYPrintf(&Buffer->String, _T("%y%s%y"), &PrefixBeforeBackquoteSubstring, NewString, &SuffixAfterBackquoteSubstring);
            if (Buffer->CurrentOffset > Buffer->String.LengthInChars) {
                Buffer->CurrentOffset = Buffer->String.LengthInChars;
            }

            if (FreeNewString) {
                YoriLibDereference(NewString);
            }
            YoriLibFreeStringContents(&PrefixBeforeBackquoteSubstring);
            YoriLibFreeStringContents(&SuffixAfterBackquoteSubstring);

            //
            //  For successful tab completion, redraw everything.  It's rare
            //  and plenty of changes are possible.
            //

            Buffer->DirtyBeginOffset = 0;
            Buffer->DirtyLength = Buffer->String.LengthInChars;
        }
    }

    YoriShFreeCmdContext(&CmdContext);
}

/**
 Take a previously populated suggestion list and remove any entries that are
 no longer consistent with a newly added string.  This may mean the currently
 active suggestion needs to be updated.

 @param Buffer Pointer to the input buffer containing the tab context
        indicating previous matches and the current suggestion.

 @param NewString Pointer to a new string being appended to the input buffer.
 */
VOID
YoriShTrimSuggestionList(
    __inout PYORI_SH_INPUT_BUFFER Buffer,
    __in PYORI_STRING NewString
    )
{
    PYORI_LIST_ENTRY ListEntry = NULL;
    PYORI_SH_TAB_COMPLETE_MATCH Match;
    YORI_STRING CompareString;
    BOOL TrimItem;

    if (Buffer->SuggestionString.LengthInChars == 0) {
        return;
    }

    //
    //  Find any match that's not consistent with the newly entered text
    //  and discard it.
    //

    ListEntry = YoriLibGetNextListEntry(&Buffer->TabContext.MatchList, NULL);
    while (ListEntry != NULL) {
        Match = CONTAINING_RECORD(ListEntry, YORI_SH_TAB_COMPLETE_MATCH, ListEntry);
        ListEntry = YoriLibGetNextListEntry(&Buffer->TabContext.MatchList, ListEntry);

        YoriLibInitEmptyString(&CompareString);

        //
        //  Assumption is that anything in the list currently matches the
        //  argument, so we're only looking for mismatches in the new text.
        //

        ASSERT(Match->Value.LengthInChars >= Buffer->TabContext.CurrentArgLength);

        CompareString.StartOfString = &Match->Value.StartOfString[Buffer->TabContext.CurrentArgLength];
        CompareString.LengthInChars = Match->Value.LengthInChars - Buffer->TabContext.CurrentArgLength;

        //
        //  If the new characters don't match, remove it.
        //

        TrimItem = FALSE;

        if (CompareString.LengthInChars <= NewString->LengthInChars) {
            TrimItem = TRUE;
        } else if (Buffer->TabContext.CaseSensitive) {
            if (YoriLibCompareStringCount(&CompareString,
                                          NewString,
                                          NewString->LengthInChars) != 0) {
                TrimItem = TRUE;
            }
        } else {
            if (YoriLibCompareStringInsensitiveCount(&CompareString,
                                                     NewString,
                                                     NewString->LengthInChars) != 0) {
                TrimItem = TRUE;
            }
        }

        if (TrimItem) {
            YoriShRemoveMatchFromTabContext(&Buffer->TabContext, Match);
        }
    }

    if (Buffer->SuggestionString.LengthInChars != 0) {
        Buffer->TabContext.CurrentArgLength += NewString->LengthInChars;

        TrimItem = FALSE;

        //
        //  If the existing suggestion isn't consistent with the newly entered
        //  text, discard it and look for a new match.
        //

        if (Buffer->SuggestionString.LengthInChars <= NewString->LengthInChars) {
            TrimItem = TRUE;
        } else if (Buffer->TabContext.CaseSensitive) {
            if (YoriLibCompareStringCount(&Buffer->SuggestionString,
                                          NewString,
                                          NewString->LengthInChars) != 0) {
                TrimItem = TRUE;
            }
        } else {
            if (YoriLibCompareStringInsensitiveCount(&Buffer->SuggestionString,
                                                     NewString,
                                                     NewString->LengthInChars) != 0) {
                TrimItem = TRUE;
            }
        }

        if (TrimItem) {

            YoriLibFreeStringContents(&Buffer->SuggestionString);

            //
            //  Check if we have any match.  If we do, try to use it.  If not, leave
            //  the buffer unchanged.
            //

            ListEntry = YoriLibGetNextListEntry(&Buffer->TabContext.MatchList, NULL);
            if (ListEntry == NULL) {
                Buffer->TabContext.CurrentArgLength = 0;
                return;
            }

            Match = CONTAINING_RECORD(ListEntry, YORI_SH_TAB_COMPLETE_MATCH, ListEntry);

            if (Match->Value.LengthInChars > Buffer->TabContext.CurrentArgLength) {
                YoriLibCloneString(&Buffer->SuggestionString, &Match->Value);
                Buffer->SuggestionString.StartOfString += Buffer->TabContext.CurrentArgLength;
                Buffer->SuggestionString.LengthInChars -= Buffer->TabContext.CurrentArgLength;
            }
        } else {
            Buffer->SuggestionString.StartOfString += NewString->LengthInChars;
            Buffer->SuggestionString.LengthInChars -= NewString->LengthInChars;
            if (Buffer->SuggestionString.LengthInChars == 0) {
                YoriLibFreeStringContents(&Buffer->SuggestionString);
            }
        }
    }
}

/**
 Perform suggestion completion processing.

 @param Buffer Pointer to the current input context.
 */
VOID
YoriShCompleteSuggestion(
    __inout PYORI_SH_INPUT_BUFFER Buffer
    )
{
    YORI_SH_CMD_CONTEXT CmdContext;
    PYORI_LIST_ENTRY ListEntry;
    PYORI_SH_TAB_COMPLETE_MATCH Match;
    PYORI_STRING Arg;
    DWORD Index;
    YORI_STRING BackquoteSubset;
    DWORD OffsetInSubstring;
    YORI_STRING PrefixBeforeBackquoteSubstring;
    YORI_STRING SuffixAfterBackquoteSubstring;

    if (Buffer->String.LengthInChars == 0) {
        return;
    }
    if (Buffer->TabContext.MatchList.Next != NULL) {
        return;
    }

    YoriShFindStringSubsetForCompletion(&Buffer->String,
                                        Buffer->CurrentOffset,
                                        Buffer->TabContext.SearchType,
                                        &BackquoteSubset,
                                        &OffsetInSubstring,
                                        &PrefixBeforeBackquoteSubstring,
                                        &SuffixAfterBackquoteSubstring);

    if (SuffixAfterBackquoteSubstring.LengthInChars > 0) {
        return;
    }

    if (!YoriShParseCmdlineToCmdContext(&BackquoteSubset, OffsetInSubstring, &CmdContext)) {
        return;
    }

    if (CmdContext.ArgC == 0) {
        YoriShFreeCmdContext(&CmdContext);
        return;
    }

    if (CmdContext.CurrentArg != CmdContext.ArgC - 1 ||
        CmdContext.TrailingChars) {

        YoriShFreeCmdContext(&CmdContext);
        return;
    }

    if (CmdContext.ArgV[CmdContext.CurrentArg].LengthInChars < YoriShGlobal.MinimumCharsInArgBeforeSuggesting) {
        YoriShFreeCmdContext(&CmdContext);
        return;
    }


    //
    //  Check if the argument has a wildcard like '*' or '?' in it, and don't
    //  suggest if so.  Suggestions get really messed up when the first part
    //  of a name contains a wild and we're attachings proposed suffixes to it.
    //  It's not great to have this check here because it implicitly disables
    //  matching of arguments, but the alternative is pushing the distinction
    //  between regular tab and suggestion throughout all the above code.
    //

    Arg = &CmdContext.ArgV[CmdContext.CurrentArg];
    for (Index = 0; Index < Arg->LengthInChars; Index++) {
        if (Arg->StartOfString[Index] == '*' || Arg->StartOfString[Index] == '?') {
            YoriShFreeCmdContext(&CmdContext);
            return;
        }
    }

    Index = YoriShFindFinalSlashIfSpecified(Arg);

    if (Arg->LengthInChars - Index < YoriShGlobal.MinimumCharsInArgBeforeSuggesting) {
        YoriShFreeCmdContext(&CmdContext);
        return;
    }

    //
    //  If we're searching for the first time, set up the search
    //  criteria and populate the list of matches.
    //

    YoriShPopulateTabCompletionMatches(Buffer, &CmdContext, 0);

    //
    //  Check if we have any match.  If we do, try to use it.  If not, leave
    //  the buffer unchanged.
    //

    ListEntry = YoriLibGetNextListEntry(&Buffer->TabContext.MatchList, NULL);
    if (ListEntry == NULL) {
        YoriShFreeCmdContext(&CmdContext);
        return;
    }

    Match = CONTAINING_RECORD(ListEntry, YORI_SH_TAB_COMPLETE_MATCH, ListEntry);

    ASSERT(Buffer->SuggestionString.MemoryToFree == NULL);

    //
    //  MSFIX CurrentArgLength needs some rethinking since there may be a
    //  prefix attached to it
    //

    Buffer->TabContext.CurrentArgLength = CmdContext.ArgV[CmdContext.CurrentArg].LengthInChars;

    if (Match->Value.LengthInChars > Buffer->TabContext.CurrentArgLength) {
        YoriLibCloneString(&Buffer->SuggestionString, &Match->Value);
        Buffer->SuggestionString.StartOfString += Buffer->TabContext.CurrentArgLength;
        Buffer->SuggestionString.LengthInChars -= Buffer->TabContext.CurrentArgLength;
    }

    YoriShFreeCmdContext(&CmdContext);
}

// vim:sw=4:ts=4:et:
