/**
 * @file pkglib/yoripkg.h
 *
 * Master header for Yori package routines
 *
 * Copyright (c) 2018-2019 Malcolm J. Smith
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

BOOL
YoriPkgCreateBinaryPackage(
    __in PYORI_STRING FileName,
    __in PYORI_STRING PackageName,
    __in PYORI_STRING Version,
    __in PYORI_STRING Architecture,
    __in PYORI_STRING FileListFile,
    __in_opt PYORI_STRING MinimumOSBuild,
    __in_opt PYORI_STRING PackagePathForOlderBuilds,
    __in_opt PYORI_STRING UpgradePath,
    __in_opt PYORI_STRING SourcePath,
    __in_opt PYORI_STRING SymbolPath,
    __in_opt PYORI_STRING Replaces,
    __in DWORD ReplaceCount
    );

BOOL
YoriPkgCreateSourcePackage(
    __in PYORI_STRING FileName,
    __in PYORI_STRING PackageName,
    __in PYORI_STRING Version,
    __in PYORI_STRING FileRoot
    );

BOOL
YoriPkgDeletePackage(
    __in_opt PYORI_STRING TargetDirectory,
    __in PYORI_STRING PackageName,
    __in BOOL WarnIfNotInstalled
    );

BOOL
YoriPkgInstallSinglePackage(
    __in PYORI_STRING PackagePath,
    __in_opt PYORI_STRING TargetDirectory
    );

BOOL
YoriPkgUpgradeInstalledPackages(
    __in_opt PYORI_STRING NewArchitecture
    );

BOOL
YoriPkgUpgradeSinglePackage(
    __in PYORI_STRING PackageName,
    __in_opt PYORI_STRING NewArchitecture
    );

BOOL
YoriPkgInstallSourceForInstalledPackages(
    );

BOOL
YoriPkgInstallSourceForSinglePackage(
    __in PYORI_STRING PackageName
    );

BOOL
YoriPkgInstallSymbolsForInstalledPackages(
    );

BOOL
YoriPkgInstallSymbolForSinglePackage(
    __in PYORI_STRING PackageName
    );

BOOL
YoriPkgDisplayAvailableRemotePackages();

BOOL
YoriPkgDisplaySources();

BOOL
YoriPkgAddNewSource(
    __in PYORI_STRING SourcePath,
    __in BOOLEAN InstallAsFirst
    );

BOOL
YoriPkgDeleteSource(
    __in PYORI_STRING SourcePath
    );

BOOL
YoriPkgDisplayMirrors();

BOOL
YoriPkgAddNewMirror(
    __in PYORI_STRING SourceName,
    __in PYORI_STRING TargetName,
    __in BOOLEAN InstallAsFirst
    );

BOOL
YoriPkgDeleteMirror(
    __in PYORI_STRING SourceName
    );

BOOL
YoriPkgInstallRemotePackages(
    __in PYORI_STRING PackageNames,
    __in DWORD PackageNameCount,
    __in_opt PYORI_STRING NewDirectory,
    __in_opt PYORI_STRING MatchVersion,
    __in_opt PYORI_STRING MatchArch
    );

DWORD
YoriPkgGetRemotePackageUrls(
    __in PYORI_STRING PackageNames,
    __in DWORD PackageNameCount,
    __in_opt PYORI_STRING NewDirectory,
    __out PYORI_STRING * PackageUrls
    );

BOOL
YoriPkgListInstalledPackages(
    __in BOOL Verbose
    );

// vim:sw=4:ts=4:et:
