<#
.SYNOPSIS
Build and install Relay Native for the current Windows x64 user.
.DESCRIPTION
Requires Visual Studio 2022/2025 C++ Build Tools with an x64 SDK, LLVM 20,
Python 3.12, Git, GitHub CLI and 7-Zip (unless -QtDir supplies Qt 6.11.1).
Build tools and Qt are cached; Qt and the MSVC runtime are deployed with Relay.
Git and gh remain external. No administrator privileges or login are requested.
#>
[CmdletBinding()]
param(
    [string]$Source = (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)),
    [string]$QtDir = '',
    [string]$InstallDirectory = '',
    [switch]$NoOpen
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT -or
    ![Environment]::Is64BitOperatingSystem -or $env:PROCESSOR_ARCHITECTURE -ne 'AMD64') {
    throw 'Use an x64 PowerShell session on Windows x64.'
}
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
if (!(Test-Path -LiteralPath $vswhere)) {
    throw 'Install Visual Studio C++ Build Tools with the Desktop development with C++ workload and Windows SDK.'
}
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or !$vs) { throw 'An x64 Visual Studio C++ toolchain and Windows SDK are required.' }
& (Join-Path $vs 'Common7/Tools/Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
if (!(Get-Command python -ErrorAction SilentlyContinue)) {
    throw 'Install Python 3.12 and add it to PATH, then run this installer again.'
}
$arguments = @((Join-Path $PSScriptRoot 'install_windows.py'), '--source', $Source)
if ($QtDir) { $arguments += @('--qt-dir', $QtDir) }
if ($InstallDirectory) { $arguments += @('--install-directory', $InstallDirectory) }
if ($NoOpen) { $arguments += '--no-open' }
& python @arguments
if ($LASTEXITCODE -ne 0) { throw "Relay installation failed (exit $LASTEXITCODE). See the error above." }
