/**
 * @file yui/yui.h
 *
 * Yori shell display lightweight graphical UI master header
 *
 * Copyright (c) 2019 Malcolm J. Smith
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

#ifndef BS_LEFT
/**
 If not defined by the compilation environment, the flag indicating button
 text should be left aligned.
 */
#define BS_LEFT 0x100
#endif

#ifndef BS_CENTER
/**
 If not defined by the compilation environment, the flag indicating button
 text should be centered.
 */
#define BS_CENTER 0x300
#endif

#ifndef HSHELL_REDRAW
/**
 If not defined by the compilation environment, the flag indicating a top
 level window title has changed.
 */
#define HSHELL_REDRAW 6
#endif

#ifndef HSHELL_WINDOWACTIVATED
/**
 If not defined by the compilation environment, the flag indicating the active
 top level window has changed.
 */
#define HSHELL_WINDOWACTIVATED 4
#endif

#ifndef SS_SUNKEN
/**
 If not defined by the compilation environment, the flag indicating a static
 control should have a sunken appearance.
 */
#define SS_SUNKEN 0x1000
#endif

#ifndef TPM_BOTTOMALIGN
/**
 If not defined by the compilation environment, the flag indicating a popup
 menu should be bottom aligned.
 */
#define TPM_BOTTOMALIGN 0x0020
#endif

#ifndef TPM_NONOTIFY
/**
 If not defined by the compilation environment, the flag indicating a popup
 menu should not generate notification messages.
 */
#define TPM_NONOTIFY    0x0080
#endif

#ifndef TPM_RETURNCMD
/**
 If not defined by the compilation environment, the flag indicating a popup
 menu should indicate the selected option as a return value.
 */
#define TPM_RETURNCMD   0x0100
#endif

#ifndef WM_DISPLAYCHANGE
/**
 If not defined by the compilation environment, the message indicating that
 the screen resolution has changed.
 */
#define WM_DISPLAYCHANGE 0x007e
#endif

#ifndef WS_EX_TOOLWINDOW
/**
 If not defined by the compilation environment, a flag indicating a window is
 a helper window that should not be included in the taskbar.
 */
#define WS_EX_TOOLWINDOW 0x0080
#endif

#ifndef WS_EX_STATICEDGE
/**
 If not defined by the compilation environment, a flag indicating a window 
 should have a 3D border indicating it does not accept user input.
 */
#define WS_EX_STATICEDGE 0x20000
#endif


/**
 A structure describing a directory within the start menu.
 */
typedef struct _YUI_MENU_DIRECTORY {

    /**
     The entry for this directory within the children of its parent.  This is
     associated with ChildDirectories, below.
     */
    YORI_LIST_ENTRY ListEntry;

    /**
     The name of this directory.  This is the final path component only, not
     a fully qualified path.
     */
    YORI_STRING DirName;

    /**
     A list of child directories within this directory.  This is paired with
     ListEntry above.
     */
    YORI_LIST_ENTRY ChildDirectories;

    /**
     A list of child files (launchable applications) underneath this
     directory.  This is paired with @ref YUI_MENU_FILE::ListEntry .
     */
    YORI_LIST_ENTRY ChildFiles;

    /**
     A handle to the menu that contains subdirectories and files within this
     directory.
     */
    HMENU MenuHandle;

    /**
     The depth of this directory.  The root is zero, and all subitems start
     from 1.
     */
    DWORD Depth;
} YUI_MENU_DIRECTORY, *PYUI_MENU_DIRECTORY;

/**
 A structure describing a launchable program within the start menu.
 */
typedef struct _YUI_MENU_FILE {

    /**
     The list linkage associating this program with its parent directory.
     This is paired with @ref YUI_MENU_DIRECTORY::ChildFiles .
     */
    YORI_LIST_ENTRY ListEntry;

    /**
     A fully qualified path to this file (typically a .lnk file.)
     */
    YORI_STRING FilePath;

    /**
     The name to display for this file within the start menu.
     */
    YORI_STRING FriendlyName;

    /**
     The depth of this entry.  All objects underneath the root start at 1.
     */
    DWORD Depth;

    /**
     The unique identifier for this menu item.
     */
    DWORD MenuId;
} YUI_MENU_FILE, *PYUI_MENU_FILE;

/**
 In memory state corresponding to a taskbar button.
 */
typedef struct _YUI_TASKBAR_BUTTON {

    /**
     The entry for this taskbar button within the list of taskbar buttons.
     Paired with @ref YUI_ENUM_CONTEXT::TaskbarButtons .
     */
    YORI_LIST_ENTRY ListEntry;

    /**
     The window handle for the button control for this taskbar button.
     */
    HWND hWndButton;

    /**
     The window to activate when this taskbar button is clicked.
     */
    HWND hWndToActivate;

    /**
     The identifier of the button control.
     */
    DWORD ControlId;

    /**
     TRUE if the button is the currently selected button, indicating the
     taskbar believes this window to be active.
     */
    BOOL WindowActive;

    /**
     TRUE if this entry has been located when syncing the current set of
     windows with the current set of taskbar buttons.
     */
    BOOL AssociatedWindowFound;

    /**
     The text to display on the taskbar button.
     */
    YORI_STRING ButtonText;
} YUI_TASKBAR_BUTTON, *PYUI_TASKBAR_BUTTON;

/**
 Context passed to the callback which is invoked for each file found.
 */
typedef struct _YUI_ENUM_CONTEXT {

    /**
     The directory object corresponding to the top level start menu directory.
     */
    YUI_MENU_DIRECTORY StartDirectory;

    /**
     The directory object corresponding to the programs directory.
     */
    YUI_MENU_DIRECTORY ProgramsDirectory;

    /**
     The directory to filter from enumerate.  This is the "Programs" directory
     when enumerating the start menu directory.  Because this is typically
     nested, but not required to be nested, we enumerate both and filter any
     matches from the start menu directory that overlap with the programs
     directory.  If this string is not empty, it implies that the start menu
     directory is being enumerated, which also implies different rules for
     where found files should be placed.
     */
    YORI_STRING FilterDirectory;

    /**
     Change notification handles to detect if the contents of the start menu
     have changed.  There are four of these, being per-user and per-system as
     well as Programs and Start Menu directories.
     */
    HANDLE StartChangeNotifications[4];

    /**
     The next identifier to allocate for subsequent menu entries.
     */
    DWORD NextMenuIdentifier;

    /**
     The window handle for the taskbar.
     */
    HWND hWnd;

    /**
     The window handle for the start button.
     */
    HWND hWndStart;

    /**
     The window handle for the clock.
     */
    HWND hWndClock;

    /**
     The message identifier used to communicate shell hook messages.  This is
     only meaningful when using SetWindowsHookEx to monitor changes.
     */
    DWORD ShellHookMsg;

    /**
     A handle to a font used to display buttons on the task bar.
     */
    HFONT hFont;

    /**
     The top level menu handle for the start menu.
     */
    HMENU StartMenu;

    /**
     The menu handle for the nested shutdown menu.
     */
    HMENU ShutdownMenu;

    /**
     The list of currently known taskbar buttons.
     */
    YORI_LIST_ENTRY TaskbarButtons;

    /**
     An identifier for a periodic timer used to refresh taskbar buttons.  This
     is only meaningful when polling is used to sync taskbar state.
     */
    DWORD_PTR SyncTimerId;

    /**
     An identifier for a periodic timer used to update the clock.
     */
    DWORD_PTR ClockTimerId;

    /**
     The string containing the current value of the clock display.  It is only
     updated if the value changes.
     */
    YORI_STRING ClockDisplayedValue;

    /**
     The buffer containing the current displayed clock value.
     */
    TCHAR ClockDisplayedValueBuffer[16];

    /**
     The number of buttons currently displayed in the task bar.
     */
    DWORD TaskbarButtonCount;

    /**
     The offset in pixels from the beginning of the taskbar window to the
     first task button.  This is to allow space for the start button.
     */
    DWORD LeftmostTaskbarOffset;

    /**
     The offset in pixels from the end of the taskbar window to the
     last task button.  This is to allow space for the clock.
     */
    DWORD RightmostTaskbarOffset;

    /**
     The next control ID to allocate for the next taskbar button.
     */
    DWORD NextTaskbarId;

    /**
     A timer frequency of how often to poll for window changes to refresh
     the taskbar.  Generally notifications are used instead and this value
     is zero.  A nonzero value implies the user forced a value.
     */
    DWORD TaskbarRefreshFrequency;

    /**
     Set to TRUE if a display resolution change message is being processed.
     This happens because moving an app bar triggers a display resolution
     change, and we don't want to recurse infinitely.
     */
    BOOL DisplayResolutionChangeInProgress;

    /**
     Set to TRUE if a menu is being displayed.  Opening a dialog under the
     menu can cause a start click to be re-posted, so this is used to
     prevent recursing indefinitely.
     */
    BOOL MenuActive;

} YUI_ENUM_CONTEXT, *PYUI_ENUM_CONTEXT;

/**
 The number of pixels to include in the start button.
 */
#define YUI_START_BUTTON_WIDTH (50)

/**
 The number of pixels to include in the clock.
 */
#define YUI_CLOCK_WIDTH (60)

/**
 The control identifier for the start button.
 */
#define YUI_START_BUTTON (1)

/**
 The control identifier for the first taskbar button.  Later taskbar buttons
 have identifiers higher than this one.
 */
#define YUI_FIRST_TASKBAR_BUTTON (100)

/**
 The timer identifier of the timer that polls for window change events on
 systems that do not support notifications.
 */
#define YUI_WINDOW_POLL_TIMER (1)

/**
 The timer identifier of the timer that updates the clock in the task bar.
 */
#define YUI_CLOCK_TIMER (2)

BOOL
YuiMenuPopulate(
    __in PYUI_ENUM_CONTEXT YuiContext
    );

BOOL
YuiMenuDisplayAndExecute(
    __in PYUI_ENUM_CONTEXT YuiContext,
    __in HWND hWnd
    );

VOID
YuiMenuFreeAll(
    __in PYUI_ENUM_CONTEXT YuiContext
    );

BOOL
YuiTaskbarPopulateWindows(
    __in PYUI_ENUM_CONTEXT YuiContext,
    __in HWND TaskbarHwnd
    );

VOID
YuiTaskbarSwitchToTask(
    __in PYUI_ENUM_CONTEXT YuiContext,
    __in DWORD CtrlId
    );

VOID
YuiTaskbarNotifyResolutionChange(
    __in PYUI_ENUM_CONTEXT YuiContext
    );

VOID
YuiTaskbarNotifyNewWindow(
    __in PYUI_ENUM_CONTEXT YuiContext,
    __in HWND hWnd
    );

VOID
YuiTaskbarNotifyDestroyWindow(
    __in PYUI_ENUM_CONTEXT YuiContext,
    __in HWND hWnd
    );

VOID
YuiTaskbarNotifyActivateWindow(
    __in PYUI_ENUM_CONTEXT YuiContext,
    __in HWND hWnd
    );

VOID
YuiTaskbarNotifyTitleChange(
    __in PYUI_ENUM_CONTEXT YuiContext,
    __in HWND hWnd
    );

VOID
YuiTaskbarFreeButtons(
    __in PYUI_ENUM_CONTEXT YuiContext
    );

VOID
YuiTaskbarSyncWithCurrent(
    __in PYUI_ENUM_CONTEXT YuiContext
    );

VOID
YuiTaskbarUpdateClock(
    __in PYUI_ENUM_CONTEXT YuiContext
    );

// vim:sw=4:ts=4:et:
