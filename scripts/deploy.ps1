<#
.SYNOPSIS
    Bundles a fully standalone TsimCAT release into a distributable folder.

.DESCRIPTION
    Builds the Release preset, then collects the executable, all required DLLs,
    Qt plugins, QML modules, MSVC runtime, and config into a self-contained
    directory that runs on any Windows 10/11 x64 machine.

.PARAMETER OutputDir
    Destination folder for the bundle (default: .\dist\TsimCAT).

.PARAMETER SkipBuild
    Skip the CMake build step (use existing Release build).

.PARAMETER CreateZip
    Also produce a .zip archive of the bundle.

.EXAMPLE
    .\deploy.ps1
    .\deploy.ps1 -SkipBuild -CreateZip
    .\deploy.ps1 -OutputDir "C:\Releases\TsimCAT-v0.1"
#>
[CmdletBinding()]
param(
    [string]$OutputDir,
    [switch]$SkipBuild,
    [switch]$CreateZip
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Resolve-ToolPath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$CommandName,

        [string[]]$CandidatePaths = @()
    )

    $command = Get-Command $CommandName -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    foreach ($candidate in $CandidatePaths) {
        if (Test-Path $candidate) {
            return $candidate
        }
    }

    return $null
}

function Resolve-VsDevCmdPath {
    $vswhereExe = Resolve-ToolPath -CommandName 'vswhere.exe' -CandidatePaths @(
        'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe',
        'C:\Program Files\Microsoft Visual Studio\Installer\vswhere.exe'
    )

    if ($vswhereExe) {
        $installationPath = & $vswhereExe -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null
        if ($LASTEXITCODE -eq 0 -and $installationPath) {
            $candidate = Join-Path $installationPath 'Common7\Tools\VsDevCmd.bat'
            if (Test-Path $candidate) {
                return $candidate
            }
        }
    }

    $candidatePaths = @(
        'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat',
        'C:\Program Files\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat',
        'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat',
        'C:\Program Files (x86)\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat',
        'C:\Program Files (x86)\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat',
        'C:\Program Files (x86)\Microsoft Visual Studio\2022\Enterprise\Common7\Tools\VsDevCmd.bat'
    )

    foreach ($candidate in $candidatePaths) {
        if (Test-Path $candidate) {
            return $candidate
        }
    }

    return $null
}

function Enter-VisualStudioDevShell {
    param(
        [Parameter(Mandatory = $true)]
        [string]$VsDevCmdPath
    )

    $vsToolsDir = Split-Path $VsDevCmdPath -Parent
    $devShellModule = Join-Path $vsToolsDir 'Microsoft.VisualStudio.DevShell.dll'
    if (-not (Test-Path $devShellModule)) {
        throw "Visual Studio dev shell module not found at $devShellModule"
    }

    Import-Module $devShellModule -ErrorAction Stop | Out-Null

    $vsInstallPath = [System.IO.Path]::GetFullPath((Join-Path $vsToolsDir '..\..'))
    Enter-VsDevShell -VsInstallPath $vsInstallPath -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
    if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
        throw 'Visual Studio developer shell did not make cl.exe available.'
    }
}

# ── Paths ─────────────────────────────────────────────────────────────────
$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$buildDir = Join-Path $projectRoot 'build\release'
$buildCachePath = Join-Path $buildDir 'CMakeCache.txt'

# Resolve OutputDir default here (not in param block) so $projectRoot is available
if (-not $OutputDir) {
    $OutputDir = Join-Path $projectRoot 'dist\TsimCAT'
}
else {
    $OutputDir = [System.IO.Path]::GetFullPath($OutputDir)
}

$vcpkgRoot = "C:\Dev\vcpkg"
$vcpkgBin = "$vcpkgRoot\installed\x64-windows\bin"
$vcpkgPlugins = "$vcpkgRoot\installed\x64-windows\Qt6\plugins"
$vcpkgQml = "$vcpkgRoot\installed\x64-windows\Qt6\qml"
$vsDevCmd = Resolve-VsDevCmdPath

$cmakeExe = Resolve-ToolPath -CommandName 'cmake.exe' -CandidatePaths @(
    'C:\Program Files\CMake\bin\cmake.exe',
    'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe',
    'C:\Program Files\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe',
    'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
)

if (-not $cmakeExe) {
    throw 'cmake.exe was not found. Install CMake or Visual Studio CMake tools, or add cmake.exe to PATH.'
}

if ($vsDevCmd) {
    Enter-VisualStudioDevShell -VsDevCmdPath $vsDevCmd
}
elseif (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    throw 'Visual Studio developer tools were not found. Install MSVC Build Tools or make cl.exe available in the task environment.'
}

# ── Resolve MSVC Redist ──────────────────────────────────────────────────
$vsBase = "C:\Program Files (x86)\Microsoft Visual Studio"
$redistRoot = $null
foreach ($edition in @('18', '2022', '2019')) {
    foreach ($sku in @('BuildTools', 'Enterprise', 'Professional', 'Community')) {
        $candidate = "$vsBase\$edition\$sku\VC\Redist\MSVC"
        if (Test-Path $candidate) {
            $redistRoot = $candidate
            break
        }
    }
    if ($redistRoot) { break }
}

$vcRedistDir = $null
if ($redistRoot) {
    $latest = Get-ChildItem $redistRoot -Directory | Where-Object { $_.Name -match '^\d' } |
    Sort-Object Name -Descending | Select-Object -First 1
    if ($latest) {
        $vcRedistDir = "$($latest.FullName)\x64\Microsoft.VC145.CRT"
        if (-not (Test-Path $vcRedistDir)) {
            # Fallback: try VC143
            $vcRedistDir = "$($latest.FullName)\x64\Microsoft.VC143.CRT"
        }
    }
}

# ── Banner ────────────────────────────────────────────────────────────────
Write-Host ""
Write-Host "===============================================" -ForegroundColor Cyan
Write-Host "       TsimCAT Standalone Deploy Script        " -ForegroundColor Cyan
Write-Host "===============================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "  Project:    $projectRoot"
Write-Host "  Build:      $buildDir"
Write-Host "  Output:     $OutputDir"
Write-Host "  vcpkg bin:  $vcpkgBin"
Write-Host "  VC Redist:  $vcRedistDir"
Write-Host "  CMake:      $cmakeExe"
Write-Host "  VsDevCmd:   $vsDevCmd"
Write-Host ""

# ── Step 1: Build ─────────────────────────────────────────────────────────
if (-not $SkipBuild) {
    Write-Host "[1/5] Building Release..." -ForegroundColor Yellow
    Push-Location $projectRoot
    try {
        & $cmakeExe --preset release
        if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }

        if ($env:NUMBER_OF_PROCESSORS) {
            & $cmakeExe --build --preset release --parallel $env:NUMBER_OF_PROCESSORS
        }
        else {
            & $cmakeExe --build --preset release
        }
        if ($LASTEXITCODE -ne 0) { throw "Release build failed." }
    }
    finally { Pop-Location }
    Write-Host "      Build OK" -ForegroundColor Green
}
else {
    Write-Host "[1/5] Skipping build (using existing)" -ForegroundColor DarkGray
}

$exePath = "$buildDir\appTsimCAT.exe"
if (-not (Test-Path $exePath)) {
    throw "Executable not found at $exePath. Run without -SkipBuild first."
}

$adsDriver = 'AdsLib'
if (Test-Path $buildCachePath) {
    $adsDriverLine = Select-String -Path $buildCachePath -Pattern '^TSIMCAT_ADS_DRIVER:STRING=' -SimpleMatch:$false | Select-Object -First 1
    if ($adsDriverLine) {
        $adsDriver = ($adsDriverLine.Line -split '=', 2)[1]
    }
}

$tcAdsDllSource = $null
if ($adsDriver -eq 'TcAdsDll') {
    $tcAdsDllSource = Join-Path $projectRoot 'third_party\TcAdsDll\x64\TcAdsDll.dll'
    if (-not (Test-Path $tcAdsDllSource)) {
        $tcAdsDllSource = Join-Path $projectRoot 'third_party\TcAdsDll\TcAdsDll.dll'
    }
}

# ── Step 2: Prepare output directory ──────────────────────────────────────
Write-Host "[2/5] Preparing output directory..." -ForegroundColor Yellow
if (Test-Path $OutputDir) {
    Remove-Item $OutputDir -Recurse -Force
}
New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null

# ── Step 3: Collect DLLs via dependency walking ───────────────────────────
Write-Host "[3/5] Collecting dependencies..." -ForegroundColor Yellow

# Copy the executable
Copy-Item $exePath $OutputDir

if ($tcAdsDllSource -and (Test-Path $tcAdsDllSource)) {
    Copy-Item $tcAdsDllSource $OutputDir
}

# Walk DLL dependencies recursively using dumpbin
$dumpbinExe = $null
# Try PATH first
$found = Get-Command dumpbin.exe -ErrorAction SilentlyContinue
if ($found) {
    $dumpbinExe = $found.Source
}
else {
    # Search VS installations
    $vsSearchPaths = @(
        "$vsBase\18\BuildTools\VC\Tools\MSVC",
        "$vsBase\2022\BuildTools\VC\Tools\MSVC",
        "$vsBase\2022\Enterprise\VC\Tools\MSVC",
        "$vsBase\2022\Professional\VC\Tools\MSVC",
        "$vsBase\2022\Community\VC\Tools\MSVC"
    )
    foreach ($vsPath in $vsSearchPaths) {
        if (Test-Path $vsPath) {
            $found = Get-ChildItem $vsPath -Recurse -Filter "dumpbin.exe" |
            Where-Object { $_.FullName -match 'Hostx64[\\/]x64' } |
            Select-Object -First 1
            if ($found) {
                $dumpbinExe = $found.FullName
                break
            }
        }
    }
}

if (-not $dumpbinExe) {
    Write-Host "      WARNING: dumpbin.exe not found, will copy all vcpkg DLLs" -ForegroundColor Red
}

function Get-DllDependencies {
    param([string]$Binary, [string]$SearchDir, [hashtable]$Visited)
    
    if (-not $dumpbinExe) { return }
    
    $output = & $dumpbinExe /dependents $Binary 2>$null
    foreach ($line in $output) {
        $trimmed = $line.Trim()
        if ($trimmed -match '^(\S+\.dll)$') {
            $dll = $Matches[1]
            if ($Visited.ContainsKey($dll.ToLower())) { continue }
            
            $fullPath = Join-Path $SearchDir $dll
            if (Test-Path $fullPath) {
                $Visited[$dll.ToLower()] = $fullPath
                # Recurse into this DLL
                Get-DllDependencies -Binary $fullPath -SearchDir $SearchDir -Visited $Visited
            }
        }
    }
}

$visited = @{}

if ($dumpbinExe) {
    Get-DllDependencies -Binary $exePath -SearchDir $vcpkgBin -Visited $visited
}
else {
    # Fallback: copy all DLLs from vcpkg bin
    foreach ($dll in (Get-ChildItem "$vcpkgBin\*.dll")) {
        $visited[$dll.Name.ToLower()] = $dll.FullName
    }
}

# Copy all found DLLs
$dllCount = 0
foreach ($entry in $visited.GetEnumerator()) {
    Copy-Item $entry.Value $OutputDir
    $dllCount++
}
Write-Host "      Copied $dllCount DLLs from vcpkg" -ForegroundColor Green

# ── MSVC Runtime ──────────────────────────────────────────────────────────
if ($vcRedistDir -and (Test-Path $vcRedistDir)) {
    $crtDlls = Get-ChildItem "$vcRedistDir\*.dll"
    foreach ($dll in $crtDlls) {
        Copy-Item $dll.FullName $OutputDir
    }
    Write-Host "      Copied $($crtDlls.Count) MSVC CRT DLLs" -ForegroundColor Green
}
else {
    Write-Host "      WARNING: MSVC Redist not found, target PC needs VC++ Redistributable" -ForegroundColor Red
}

# ── Step 4: Qt plugins & QML modules ─────────────────────────────────────
Write-Host "[4/5] Copying Qt plugins & QML modules..." -ForegroundColor Yellow

# Required Qt plugin directories for this app
$requiredPlugins = @(
    'platforms',        # qwindows.dll - essential
    'imageformats',    # PNG/JPEG support
    'iconengines',     # SVG icons
    'assetimporters',  # Quick3D asset loading
    'styles',          # QuickControls2 styles
    'tls',             # OpenSSL TLS backend
    'networkinformation'
)

foreach ($pluginDir in $requiredPlugins) {
    $src = "$vcpkgPlugins\$pluginDir"
    if (Test-Path $src) {
        $dest = "$OutputDir\plugins\$pluginDir"
        Copy-Item $src $dest -Recurse -Force
        # Also walk DLLs inside plugins for additional dependencies
        foreach ($pluginDll in (Get-ChildItem "$dest\*.dll" -ErrorAction SilentlyContinue)) {
            Get-DllDependencies -Binary $pluginDll.FullName -SearchDir $vcpkgBin -Visited $visited
        }
    }
}

# Copy any newly-discovered DLLs from plugin dependency walk
foreach ($entry in $visited.GetEnumerator()) {
    $target = Join-Path $OutputDir $entry.Key
    if (-not (Test-Path $target)) {
        Copy-Item $entry.Value $OutputDir
        $dllCount++
    }
}

# Required QML module directories
$requiredQml = @(
    'QtQuick',
    'QtQuick3D',
    'QtQml',
    'QtCore',
    'Qt'
)

foreach ($qmlDir in $requiredQml) {
    $src = "$vcpkgQml\$qmlDir"
    if (Test-Path $src) {
        $dest = "$OutputDir\qml\$qmlDir"
        Copy-Item $src $dest -Recurse -Force
    }
}

# Walk ALL DLLs in plugins and QML directories for transitive dependencies
# QML plugins (like Material style) pull in additional Qt DLLs at runtime
Write-Host "      Walking transitive dependencies of plugins & QML modules..." -ForegroundColor DarkGray
$allBundleDlls = Get-ChildItem $OutputDir -Recurse -Filter "*.dll" -ErrorAction SilentlyContinue
foreach ($bundleDll in $allBundleDlls) {
    Get-DllDependencies -Binary $bundleDll.FullName -SearchDir $vcpkgBin -Visited $visited
}

# Copy any newly-discovered transitive DLLs to the bundle root
foreach ($entry in $visited.GetEnumerator()) {
    $target = Join-Path $OutputDir $entry.Key
    if (-not (Test-Path $target)) {
        Copy-Item $entry.Value $OutputDir
        $dllCount++
    }
}

$pluginCount = (Get-ChildItem "$OutputDir\plugins" -Recurse -Filter "*.dll" -ErrorAction SilentlyContinue).Count
$qmlCount = (Get-ChildItem "$OutputDir\qml" -Recurse -Filter "*.dll" -ErrorAction SilentlyContinue).Count
$rootDllCount = (Get-ChildItem "$OutputDir\*.dll").Count
Write-Host "      $pluginCount plugin DLLs, $qmlCount QML module DLLs, $rootDllCount root DLLs" -ForegroundColor Green

# Strip debug symbols from the distributable bundle.
$pdbFiles = Get-ChildItem $OutputDir -Recurse -Filter "*.pdb" -ErrorAction SilentlyContinue
if ($pdbFiles.Count -gt 0) {
    foreach ($pdb in $pdbFiles) {
        Remove-Item $pdb.FullName -Force
    }
    Write-Host "      Removed $($pdbFiles.Count) PDB files from bundle" -ForegroundColor Green
}

# ── Step 5: Config & launcher ─────────────────────────────────────────────
Write-Host "[5/5] Creating config & launcher..." -ForegroundColor Yellow

# Config directory
New-Item -ItemType Directory -Path "$OutputDir\config" -Force | Out-Null
$runtimeConfig = Join-Path $projectRoot 'config\runtime.json'
if (Test-Path $runtimeConfig) {
    Copy-Item $runtimeConfig "$OutputDir\config\"
}
else {
    Write-Host "      runtime.json not found; skipping config copy" -ForegroundColor DarkGray
}

# Create launcher script that sets the relative paths
$launcherContent = @'
@echo off
setlocal

set "APP_DIR=%~dp0"
set "PATH=%APP_DIR%;%PATH%"
set "QT_PLUGIN_PATH=%APP_DIR%plugins"
set "QML2_IMPORT_PATH=%APP_DIR%qml"

start "" "%APP_DIR%appTsimCAT.exe" %*
'@
Set-Content -Path "$OutputDir\TsimCAT.bat" -Value $launcherContent -Encoding ASCII

# Also create a qt.conf so Qt finds plugins/qml relative to the exe
$qtConfContent = "[Paths]`r`nPlugins = plugins`r`nQml2Imports = qml"
Set-Content -Path "$OutputDir\qt.conf" -Value $qtConfContent -Encoding ASCII

Write-Host "      Created launcher and qt.conf" -ForegroundColor Green

# ── Optional: Zip ─────────────────────────────────────────────────────────
if ($CreateZip) {
    $zipPath = "$OutputDir.zip"
    Write-Host ""
    Write-Host "Creating archive: $zipPath" -ForegroundColor Yellow
    if (Test-Path $zipPath) { Remove-Item $zipPath }
    Compress-Archive -Path $OutputDir -DestinationPath $zipPath -CompressionLevel Optimal
    $sizeMB = [math]::Round((Get-Item $zipPath).Length / 1MB, 1)
    Write-Host "      Archive: $zipPath ($sizeMB MB)" -ForegroundColor Green
}

# ── Summary ───────────────────────────────────────────────────────────────
$totalFiles = (Get-ChildItem $OutputDir -Recurse -File).Count
$totalSizeMB = [math]::Round((Get-ChildItem $OutputDir -Recurse -File | Measure-Object Length -Sum).Sum / 1MB, 1)

Write-Host ""
Write-Host "===============================================" -ForegroundColor Green
Write-Host "              Deploy Complete!                  " -ForegroundColor Green
Write-Host "===============================================" -ForegroundColor Green
Write-Host ""
Write-Host "  Location:  $OutputDir"
Write-Host "  Files:     $totalFiles"
Write-Host "  Size:      $totalSizeMB MB"
Write-Host ""
Write-Host "  To run:    .\TsimCAT.bat" -ForegroundColor Cyan
Write-Host "             or just double-click TsimCAT.bat" -ForegroundColor Cyan
Write-Host ""
