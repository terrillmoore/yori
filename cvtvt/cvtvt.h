/**
 * @file cvtvt/cvtvt.h
 *
 * Convert VT100/ANSI escape sequences into HTML, native Win32 console or
 * remove them completely.
 *
 * Copyright (c) 2015-2017 Malcolm J. Smith
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

#ifndef COMMON_LVB_UNDERSCORE
/**
 A private definition for underscore in case the compilation environment
 doesn't provide it.
 */
#define COMMON_LVB_UNDERSCORE 0x8000
#endif

BOOL CvtvtHtml4SetFunctions(__out PYORI_LIB_VT_CALLBACK_FUNCTIONS CallbackFunctions);
BOOL CvtvtHtml5SetFunctions(__out PYORI_LIB_VT_CALLBACK_FUNCTIONS CallbackFunctions);
BOOL CvtvtRtfSetFunctions(__out PYORI_LIB_VT_CALLBACK_FUNCTIONS CallbackFunctions);

// vim:sw=4:ts=4:et:
