# Run only in a disposable Windows CI user profile.
param([Parameter(Mandatory)][string]$Compiler, [Parameter(Mandatory)][string]$Payload,
      [Parameter(Mandatory)][string]$Version, [Parameter(Mandatory)][string]$Output)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$source = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$script = Join-Path $source 'native/packaging/preview/windows.iss'
New-Item -ItemType Directory -Force $Output | Out-Null
function Build-Installer([string]$number) {
    & $Compiler "/DPayloadDir=$Payload" "/DRelayVersion=$number" "/DOutputDir=$Output" $script | Out-Host
    if ($LASTEXITCODE -ne 0) { throw 'Inno Setup compilation failed' }
    return Join-Path $Output "Relay-Native-Setup-$number-x64.exe"
}
function Install([string]$installer, [string[]]$extra, [bool]$success = $true) {
    $arguments = @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/SP-', "/LOG=`"$Output/install-$([guid]::NewGuid()).log`"") + $extra
    $process = Start-Process -FilePath $installer -ArgumentList $arguments -Wait -PassThru
    if (($process.ExitCode -eq 0) -ne $success) { throw "Unexpected installer exit: $($process.ExitCode)" }
}
$old = Build-Installer '0.5.1'
$new = Build-Installer $Version
$testRoot = Join-Path $env:RUNNER_TEMP 'relay-installer-tests'
New-Item -ItemType Directory -Force $testRoot | Out-Null
$destination = Join-Path $testRoot 'custom location/Relay Native'
$desktopLink = Join-Path ([Environment]::GetFolderPath('Desktop')) 'Relay Native.lnk'
# A native profile fixture is outside the install tree and must survive all steps.
$profile = Join-Path $env:APPDATA 'relay-desktop'
New-Item -ItemType Directory -Force $profile | Out-Null
$settings = Join-Path $profile 'installer-preservation-fixture.txt'
Set-Content $settings 'preserve accounts and settings'
$before = (Get-FileHash $settings).Hash
Install $old @("/DIR=`"$destination`"", '/TASKS=')
if (Test-Path $desktopLink) { throw 'Unselected desktop shortcut was created' }
# Update without /DIR must reuse the prior custom directory.
Install $new @('/TASKS=desktopicon')
if (!(Test-Path "$destination/Relay.exe") -or !(Test-Path $desktopLink)) { throw 'Upgrade or desktop shortcut missing' }
if ((Get-Content "$destination/relay-native-install.ini" -Raw) -notmatch "Version=$([regex]::Escape($Version))") { throw 'Upgrade version was not recorded' }
$link = (New-Object -ComObject WScript.Shell).CreateShortcut($desktopLink)
if ($link.TargetPath -ne (Join-Path $destination 'Relay.exe')) { throw 'Shortcut targets wrong installation' }
& "$destination/Relay.exe" --smoke-test
if ($LASTEXITCODE -ne 0) { throw 'Installed app smoke failed' }
# Existing Electron/unrelated folders are refused without touching their content.
$unrelated = Join-Path $testRoot 'unrelated'
New-Item -ItemType Directory -Force "$unrelated/resources" | Out-Null
Set-Content "$unrelated/Relay.exe" 'electron fixture'
Set-Content "$unrelated/resources/app.asar" 'preserve'
Install $new @("/DIR=`"$unrelated`"") $false
if ((Get-Content "$unrelated/Relay.exe" -Raw).Trim() -ne 'electron fixture') { throw 'Electron was modified' }
# Exercise adoption of the marker shipped with the 0.5.1 portable/source build.
$portable = Join-Path $testRoot 'portable'
$archive = Join-Path $testRoot 'native-0.5.1.zip'
Invoke-WebRequest 'https://github.com/ka-capek/relay-desktop/releases/download/v0.5.1/Relay-Native-0.5.1-windows-x64.zip' -OutFile $archive
if ((Get-FileHash $archive).Hash -ne '0a9cf52079814728300d034955101ec6660834e49bb6c3d43c25bf6eacc5c2f8') { throw 'Legacy artifact digest mismatch' }
Expand-Archive $archive $portable
$locked = [IO.File]::Open("$portable/Relay.exe", 'Open', 'ReadWrite', 'None')
try { Install $new @("/DIR=`"$portable`"", '/TASKS=') $false }
finally { $locked.Dispose() }
Install $new @("/DIR=`"$portable`"", '/TASKS=')
if (!(Test-Path "$portable/unins000.exe")) { throw 'Portable native adoption failed' }
if ((Get-FileHash $settings).Hash -ne $before) { throw 'Profile changed' }
# The synthetic older installer is a test fixture, never a published download.
Remove-Item $old
Write-Output 'Installer upgrade, custom path, optional desktop shortcut, portable adoption and Electron preservation passed.'
