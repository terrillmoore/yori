/**
 * @file lib/builtin.c
 *
 * Yori routines that are specific to builtin modules
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "yoripch.h"
#include "yorilib.h"
#include "yoricall.h"

/**
 Retore a set of environment strings into the current environment.  This
 implies removing all currently defined variables and replacing them with
 the specified set.  This version of the routine is specific to builtin
 modules because it manipulates the environment through the YoriCall
 interface.  Note that the input buffer is modified temporarily (ie.,
 it is not immutable.)

 @param NewEnvironment Pointer to the new environment strings to apply.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOL
YoriLibBuiltinSetEnvironmentStrings(
    __in PYORI_STRING NewEnvironment
    )
{
    YORI_STRING CurrentEnvironment;
    YORI_STRING VariableName;
    YORI_STRING ValueName;
    LPTSTR ThisVar;
    LPTSTR ThisValue;
    DWORD VarLen;

    if (!YoriLibGetEnvironmentStrings(&CurrentEnvironment)) {
        return FALSE;
    }

    YoriLibInitEmptyString(&VariableName);
    YoriLibInitEmptyString(&ValueName);

    ThisVar = CurrentEnvironment.StartOfString;
    while (*ThisVar != '\0') {
        VarLen = _tcslen(ThisVar);

        //
        //  We know there's at least one char.  Skip it if it's equals since
        //  that's how drive current directories are recorded.
        //

        ThisValue = _tcschr(&ThisVar[1], '=');
        if (ThisValue != NULL) {
            ThisValue[0] = '\0';
            VariableName.StartOfString = ThisVar;
            VariableName.LengthInChars = (DWORD)(ThisValue - ThisVar);
            VariableName.LengthAllocated = VariableName.LengthInChars + 1;
            YoriCallSetEnvironmentVariable(&VariableName, NULL);
        }

        ThisVar += VarLen;
        ThisVar++;
    }
    YoriLibFreeStringContents(&CurrentEnvironment);

    //
    //  Now restore the saved environment.
    //

    ThisVar = NewEnvironment->StartOfString;
    while (*ThisVar != '\0') {
        VarLen = _tcslen(ThisVar);

        //
        //  We know there's at least one char.  Skip it if it's equals since
        //  that's how drive current directories are recorded.
        //

        ThisValue = _tcschr(&ThisVar[1], '=');
        if (ThisValue != NULL) {
            ThisValue[0] = '\0';
            VariableName.StartOfString = ThisVar;
            VariableName.LengthInChars = (DWORD)(ThisValue - ThisVar);
            VariableName.LengthAllocated = VariableName.LengthInChars + 1;
            ThisValue++;
            ValueName.StartOfString = ThisValue;
            ValueName.LengthInChars = VarLen - VariableName.LengthInChars - 1;
            ValueName.LengthAllocated = ValueName.LengthInChars + 1;
            YoriCallSetEnvironmentVariable(&VariableName, &ValueName);
            ThisValue--;
            ThisValue[0] = '=';
        }

        ThisVar += VarLen;
        ThisVar++;
    }

    return TRUE;
}


// vim:sw=4:ts=4:et:
