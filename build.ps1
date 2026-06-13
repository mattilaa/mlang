$ErrorActionPreference = "Stop"

$SourceDir = "."
$BuildDir = "build"
$ConfigFile = ""
$Target = ""
$Jobs = ""
$Install = ""
$Prefix = ""
$BinDir = ""

function Show-Usage {
    @"
Usage: ./build.ps1 [options]

Options:
  -S, --source-dir DIR    Source directory (default: .)
  -B, --build-dir DIR     CMake build directory (default: build)
      --config-file FILE  mlang-config.conf to import (default: BUILD_DIR/mlang-config.conf)
  -j, --jobs N            Override saved parallel build jobs
      --target NAME       Build a specific CMake target only
  -i, --install           Install after build
      --no-install        Do not install after build
      --prefix DIR        Install prefix override
      --bin-dir DIR       Tool binary install dir override
  -h, --help              Show this help
"@
}

function Need-Value($ArgsList, $Index, $Option) {
    if ($Index + 1 -ge $ArgsList.Count) {
        Write-Error "build.ps1: $Option requires a value"
        exit 2
    }
    return $ArgsList[$Index + 1]
}

function Get-ConfigValue($Path, $Key) {
    if (!(Test-Path -LiteralPath $Path)) { return "" }
    $prefix = "$Key="
    $match = Get-Content -LiteralPath $Path |
        Where-Object { $_.StartsWith($prefix) } |
        Select-Object -Last 1
    if ($null -eq $match) { return "" }
    return $match.Substring($prefix.Length)
}

function Expand-UserPath($Value) {
    if ([string]::IsNullOrEmpty($Value)) { return $Value }
    $homeDir = if (![string]::IsNullOrEmpty($env:USERPROFILE)) { $env:USERPROFILE } else { $HOME }
    if ($Value -eq "~") { return $homeDir }
    if ($Value.StartsWith("~/") -or $Value.StartsWith("~\")) {
        return (Join-Path $homeDir ($Value.Substring(2)))
    }
    return $Value
}

function Stop-IfNativeCommandFailed($CommandName) {
    if ($LASTEXITCODE -ne 0) {
        Write-Error "$CommandName failed with exit code $LASTEXITCODE"
        exit $LASTEXITCODE
    }
}

function Test-NinjaAvailable {
    return $null -ne (Get-Command ninja -ErrorAction SilentlyContinue)
}

function Resolve-Tool($Directory, $Name) {
    $exe = Join-Path $Directory "$Name.exe"
    if (Test-Path -LiteralPath $exe) { return $exe }
    $plain = Join-Path $Directory $Name
    if (Test-Path -LiteralPath $plain) { return $plain }
    $releaseExe = Join-Path (Join-Path $Directory "Release") "$Name.exe"
    if (Test-Path -LiteralPath $releaseExe) { return $releaseExe }
    $releasePlain = Join-Path (Join-Path $Directory "Release") $Name
    if (Test-Path -LiteralPath $releasePlain) { return $releasePlain }
    return ""
}

function Build-MlangTool($Mlang, $BuildDir, $Source, $Output) {
    $ReleaseLibDir = Join-Path $BuildDir "Release"
    Write-Host "$Mlang $Source --no-tests -Wno-unwrap -O0 -L $BuildDir -L $ReleaseLibDir -lmlang_std -o $Output"
    & $Mlang $Source --no-tests -Wno-unwrap -O0 -L $BuildDir -L $ReleaseLibDir -lmlang_std -o $Output
    Stop-IfNativeCommandFailed $Source
}

$i = 0
while ($i -lt $args.Count) {
    $arg = $args[$i]
    switch -Regex ($arg) {
        '^(-h|--help)$' { Show-Usage; exit 0 }
        '^(-S|--source-dir)$' { $SourceDir = Need-Value $args $i $arg; $i += 2; continue }
        '^--source-dir=.+$' { $SourceDir = $arg.Substring("--source-dir=".Length); $i += 1; continue }
        '^(-B|--build-dir)$' { $BuildDir = Need-Value $args $i $arg; $i += 2; continue }
        '^--build-dir=.+$' { $BuildDir = $arg.Substring("--build-dir=".Length); $i += 1; continue }
        '^--config-file$' { $ConfigFile = Need-Value $args $i $arg; $i += 2; continue }
        '^--config-file=.+$' { $ConfigFile = $arg.Substring("--config-file=".Length); $i += 1; continue }
        '^(-j|--jobs)$' { $Jobs = Need-Value $args $i $arg; $i += 2; continue }
        '^--jobs=.+$' { $Jobs = $arg.Substring("--jobs=".Length); $i += 1; continue }
        '^--target$' { $Target = Need-Value $args $i $arg; $i += 2; continue }
        '^--target=.+$' { $Target = $arg.Substring("--target=".Length); $i += 1; continue }
        '^(-i|--install)$' { $Install = "true"; $i += 1; continue }
        '^--no-install$' { $Install = "false"; $i += 1; continue }
        '^--prefix$' { $Prefix = Need-Value $args $i $arg; $i += 2; continue }
        '^--prefix=.+$' { $Prefix = $arg.Substring("--prefix=".Length); $i += 1; continue }
        '^--bin-dir$' { $BinDir = Need-Value $args $i $arg; $i += 2; continue }
        '^--bin-dir=.+$' { $BinDir = $arg.Substring("--bin-dir=".Length); $i += 1; continue }
        default {
            Write-Error "build.ps1: unknown option: $arg"
            Show-Usage | Write-Error
            exit 2
        }
    }
}

if ([string]::IsNullOrEmpty($ConfigFile)) {
    $ConfigFile = Join-Path $BuildDir "mlang-config.conf"
}
if (!(Test-Path -LiteralPath $ConfigFile)) {
    Write-Error "build.ps1: missing config file: $ConfigFile"
    Write-Error "Run ./bootstrap.ps1 and ./build/mlang-config first, or pass --config-file."
    exit 1
}

$MlangConfig = Resolve-Tool $BuildDir "mlang-config"
if ([string]::IsNullOrEmpty($MlangConfig)) {
    Write-Host "build.ps1: mlang-config is missing; bootstrapping first"
    & (Join-Path "." "bootstrap.ps1") --build-dir $BuildDir
    Stop-IfNativeCommandFailed "bootstrap.ps1"
    $MlangConfig = Resolve-Tool $BuildDir "mlang-config"
}
if ([string]::IsNullOrEmpty($MlangConfig)) {
    Write-Error "build.ps1: cannot find mlang-config in $BuildDir after bootstrap"
    exit 1
}

if ([string]::IsNullOrEmpty($Jobs)) { $Jobs = Get-ConfigValue $ConfigFile "jobs" }
if ([string]::IsNullOrEmpty($Prefix)) { $Prefix = Get-ConfigValue $ConfigFile "install_prefix" }
if ([string]::IsNullOrEmpty($Prefix)) {
    $homeDir = if (![string]::IsNullOrEmpty($env:USERPROFILE)) { $env:USERPROFILE } else { $HOME }
    $Prefix = Join-Path $homeDir ".local"
}
$Prefix = Expand-UserPath $Prefix
if ([string]::IsNullOrEmpty($BinDir)) { $BinDir = Get-ConfigValue $ConfigFile "bin_dir" }
if ([string]::IsNullOrEmpty($BinDir)) { $BinDir = Join-Path $Prefix "bin" }
$BinDir = Expand-UserPath $BinDir

$CacheFile = Join-Path $BuildDir "mlang_config_cache.cmake"
Write-Host "$MlangConfig --import $ConfigFile --build-dir $BuildDir --install-prefix $Prefix --bin-dir $BinDir --write"
& $MlangConfig --import $ConfigFile --build-dir $BuildDir --install-prefix $Prefix --bin-dir $BinDir --write
Stop-IfNativeCommandFailed "mlang-config"

$ConfigureArgs = @("-C", $CacheFile, "-S", $SourceDir, "-B", $BuildDir)
$ConfigureCommand = "cmake -C $CacheFile -S $SourceDir -B $BuildDir"
if (Test-NinjaAvailable) {
    $ConfigureArgs += @("-G", "Ninja")
    $ConfigureCommand += " -G Ninja"
}
Write-Host $ConfigureCommand
cmake @ConfigureArgs
Stop-IfNativeCommandFailed "cmake configure"

if (![string]::IsNullOrEmpty($Target)) {
    $TargetArgs = @("--build", $BuildDir, "--config", "Release", "--target", $Target)
    $TargetCommand = "cmake --build $BuildDir --config Release --target $Target"
    if (![string]::IsNullOrEmpty($Jobs)) {
        $TargetArgs += @("--parallel", $Jobs)
        $TargetCommand += " --parallel $Jobs"
    }
    Write-Host $TargetCommand
    cmake @TargetArgs
    Stop-IfNativeCommandFailed "cmake build"
    exit 0
}

$BuildArgs = @("--build", $BuildDir, "--config", "Release", "--target", "mlang", "mlang_std", "mlang-config")
$BuildCommand = "cmake --build $BuildDir --config Release --target mlang mlang_std mlang-config"
if (![string]::IsNullOrEmpty($Jobs)) {
    $BuildArgs += @("--parallel", $Jobs)
    $BuildCommand += " --parallel $Jobs"
}
Write-Host $BuildCommand
cmake @BuildArgs
Stop-IfNativeCommandFailed "cmake build"

$Mlang = Resolve-Tool $BuildDir "mlang"
if ([string]::IsNullOrEmpty($Mlang)) {
    Write-Error "build.ps1: cannot find mlang in $BuildDir"
    exit 1
}

Build-MlangTool $Mlang $BuildDir "tools/mlangd-mla/main.mla" (Join-Path $BuildDir "mlangd-mla.exe")
Build-MlangTool $Mlang $BuildDir "tools/mlang-format-mla/main.mla" (Join-Path $BuildDir "mlang-format.exe")
Build-MlangTool $Mlang $BuildDir "tools/mlang-frontend-mla/main.mla" (Join-Path $BuildDir "mlang-frontend-mla.exe")
Build-MlangTool $Mlang $BuildDir "tools/mlangpkg/mlangpkg.mla" (Join-Path $BuildDir "mlangpkg.exe")

$Launcher = Join-Path $BuildDir "mlang-frontend.cmd"
@"
@echo off
setlocal
set BIN_DIR=%~dp0
"%BIN_DIR%mlang-frontend-mla.exe" --backend "%BIN_DIR%mlang.exe" %*
"@ | Set-Content -LiteralPath $Launcher -Encoding ASCII

if ($Install -eq "true" -or $Install -eq "ON" -or $Install -eq "1") {
    Write-Host "cmake --install $BuildDir --config Release --prefix $Prefix"
    cmake --install $BuildDir --config Release --prefix $Prefix
    Stop-IfNativeCommandFailed "cmake install"

    Write-Host "installing MLang-built tools to $BinDir"
    New-Item -ItemType Directory -Force -Path $BinDir | Out-Null
    Copy-Item -Force (Join-Path $BuildDir "mlangd-mla.exe") (Join-Path $BinDir "mlangd-mla.exe")
    Copy-Item -Force (Join-Path $BuildDir "mlang-format.exe") (Join-Path $BinDir "mlang-format.exe")
    Copy-Item -Force (Join-Path $BuildDir "mlang-frontend-mla.exe") (Join-Path $BinDir "mlang-frontend-mla.exe")
    Copy-Item -Force (Join-Path $BuildDir "mlang-frontend.cmd") (Join-Path $BinDir "mlang-frontend.cmd")
    Copy-Item -Force (Join-Path $BuildDir "mlangpkg.exe") (Join-Path $BinDir "mlangpkg.exe")
}
