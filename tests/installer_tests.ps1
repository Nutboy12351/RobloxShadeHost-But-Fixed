param(
    [string]$Compiler = "$env:LOCALAPPDATA/Programs/Inno Setup 6/ISCC.exe",
    [switch]$DownloadDLSS,
    [string]$PresetsBaseUrl
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$testRoot = Join-Path $repo ('build/installer-tests/' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testRoot -Force | Out-Null

function Build-TestInstaller([string]$Name, [string]$ManifestUrl = '') {
    $arguments = @('/Q', '/DTestMode', "/O$testRoot", "/F$Name")
    if ($ManifestUrl) { $arguments += "/DDownloadManifestUrl=$ManifestUrl" }
    if ($PresetsBaseUrl) { $arguments += "/DPresetsBaseUrl=$PresetsBaseUrl" }
    & $Compiler @arguments "$repo/installer/RobloxShadeHost.iss"
    if ($LASTEXITCODE -ne 0) { throw 'Installer compilation failed.' }
    return Join-Path $testRoot "$Name.exe"
}

function Invoke-TestInstaller(
    [string]$Setup, [string]$Name, [string]$Components, [bool]$AcceptLicense = $true
) {
    $destination = Join-Path $testRoot $Name
    $arguments = @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/SP-', '/NOICONS',
        "/COMPONENTS=$Components", "/DIR=`"$destination`"", "/LOG=`"$testRoot/$Name.log`"")
    if ($AcceptLicense) { $arguments += '/ACCEPTRESHADELICENSE=1' }
    $process = Start-Process $Setup -ArgumentList $arguments -WindowStyle Hidden -Wait -PassThru
    if ($AcceptLicense -and $process.ExitCode -ne 0) {
        throw "$Name failed with exit code $($process.ExitCode). See $testRoot/$Name.log"
    }
    if (-not $AcceptLicense -and ($process.ExitCode -eq 0 -or (Test-Path $destination))) {
        throw 'ReShade installation proceeded without license acceptance.'
    }
    return $destination
}

function Assert-File([string]$Directory, [string]$Name, [bool]$Expected = $true) {
    if ((Test-Path (Join-Path $Directory $Name)) -ne $Expected) {
        throw "Unexpected file state: $Directory/$Name; expected present=$Expected"
    }
}

$setup = Build-TestInstaller 'Setup'
$hostOnly = Invoke-TestInstaller $setup 'host-only' 'host'
Assert-File $hostOnly 'RobloxShadeHost.exe'
Assert-File $hostOnly 'CREDITS.txt'
Assert-File $hostOnly 'dxgi.dll' $false
Assert-File $hostOnly 'nvngx_dlssnr.dll' $false
Assert-File $hostOnly 'unins000.exe' $false

$null = Invoke-TestInstaller $setup 'no-license' 'host,reshade' $false
$reshade = Invoke-TestInstaller $setup 'reshade' 'host,reshade,reshade\presets'
Assert-File $reshade 'dxgi.dll'
Assert-File $reshade 'ReShade-LICENSE.txt'
Assert-File $reshade 'renodx-dlss.addon64' $false
Assert-File $reshade 'reshade-shaders/Shaders/ReShade.fxh'
Assert-File $reshade 'reshade-shaders/Shaders/FXShaders/AdaptiveTonemapper.fx'
Assert-File $reshade 'reshade-shaders/Shaders/qUINT/qUINT_common.fxh'
if (-not (Get-ChildItem "$reshade/reshade-shaders/Textures" -Filter *.png -Recurse | Select-Object -First 1)) {
    throw 'No textures were installed.'
}
$reshadeIni = Get-Content "$reshade/ReShade.ini" -Raw
if ($reshadeIni -notmatch '(?m)^EffectSearchPaths=.*\.\\reshade-shaders\\Shaders\\\*\*' -or
    $reshadeIni -notmatch '(?m)^TextureSearchPaths=.*\.\\reshade-shaders\\Textures\\\*\*') {
    throw 'ReShade.ini is missing the effect search paths.'
}
if (([regex]::Matches($reshadeIni, '(?m)^\[GENERAL\]')).Count -ne 1) {
    throw 'ReShade.ini has a duplicated GENERAL section.'
}
foreach ($preset in Get-ChildItem "$repo/presets/*.ini" -Exclude downloads.ini) {
    if ((Get-FileHash "$reshade/presets/$($preset.Name)").Hash -ne (Get-FileHash $preset.FullName).Hash) {
        throw "Installed preset $($preset.Name) does not match the repository."
    }
}
if ((Get-Content "$reshade/CREDITS.txt" -Raw) -notmatch 'tiago@mouta.me') {
    throw 'Removal contact is missing from installed credits.'
}

Add-Content "$reshade/ReShade.ini" "`n[InstallerTest]`nPreserve=1"
$originalHash = (Get-FileHash "$reshade/ReShade.ini").Hash
Set-Content "$reshade/presets/GenericPreset1.ini" 'Techniques=Edited@Edited.fx'
$null = Invoke-TestInstaller $setup 'reshade' 'host,reshade,reshade\presets'
if ((Get-FileHash "$reshade/ReShade.ini").Hash -ne $originalHash) {
    throw 'Reinstall changed the existing ReShade configuration.'
}
if ((Get-Content "$reshade/presets/GenericPreset1.ini" -Raw) -notmatch 'Edited') {
    throw 'Reinstall overwrote an edited preset.'
}

$missingSetup = Build-TestInstaller 'Setup-Missing' 'https://github.com/OMouta/RobloxShadeHost/releases/download/dlss5-assets/not-present.ini'
$missing = Invoke-TestInstaller $missingSetup 'missing-dlss' 'host,reshade,reshade\dlss5'
Assert-File $missing 'RobloxShadeHost.exe'
Assert-File $missing 'dxgi.dll'
Assert-File $missing 'nvngx_dlssnr.dll' $false
Assert-File $missing 'renodx-dlss.addon64' $false
if ((Get-Content "$testRoot/missing-dlss.log" -Raw) -notmatch 'DLSS5 skipped:') {
    throw 'Missing DLSS5 downloads were not reported.'
}

if ($DownloadDLSS) {
    $full = Invoke-TestInstaller $setup 'full' 'host,reshade,reshade\dlss5'
    Assert-File $full 'nvngx_dlssnr.dll'
    Assert-File $full 'renodx-dlss.addon64'
    $manifest = Get-Content "$repo/vendor/dlss5/downloads.ini" -Raw
    foreach ($file in @('nvngx_dlssnr.dll', 'renodx-dlss.addon64')) {
        $pattern = '(?ms)^\[' + [regex]::Escape($file) + '\]\r?\n.*?^sha256=([a-f0-9]{64})'
        $expectedHash = [regex]::Match($manifest, $pattern).Groups[1].Value
        if ((Get-FileHash "$full/$file").Hash -ne $expectedHash) {
            throw "$file does not match the repository manifest."
        }
    }
}

Write-Output "Installer checks passed. Test files and logs: $testRoot"
