$ErrorActionPreference = "Stop"

$BuildDir = "build"
$Jobs = ""

function Show-Usage {
    @"
Usage: ./bootstrap.ps1 [--build-dir DIR] [--jobs N]

Options:
  --build-dir DIR    CMake build directory (default: build)
  --build-dir=DIR    Same as --build-dir DIR
  -j, --jobs N       Parallel build jobs
  --jobs=N           Same as --jobs N
  -h, --help         Show this help
"@
}

function Need-Value($ArgsList, $Index, $Option) {
    if ($Index + 1 -ge $ArgsList.Count) {
        Write-Error "bootstrap.ps1: $Option requires a value"
        exit 2
    }
    return $ArgsList[$Index + 1]
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

function Resolve-MlangConfig($Directory) {
    $exe = Join-Path $Directory "mlang-config.exe"
    if (Test-Path -LiteralPath $exe) { return $exe }
    $plain = Join-Path $Directory "mlang-config"
    if (Test-Path -LiteralPath $plain) { return $plain }
    $releaseExe = Join-Path (Join-Path $Directory "Release") "mlang-config.exe"
    if (Test-Path -LiteralPath $releaseExe) { return $releaseExe }
    $releasePlain = Join-Path (Join-Path $Directory "Release") "mlang-config"
    if (Test-Path -LiteralPath $releasePlain) { return $releasePlain }
    return ""
}

function Confirm-YesNo($Prompt) {
    while ($true) {
        Write-Host -NoNewline "$Prompt "
        $answer = [Console]::In.ReadLine()
        if ($null -eq $answer) { return $false }
        if ($answer -eq "y" -or $answer -eq "Y") { return $true }
        if ($answer -eq "n" -or $answer -eq "N") { return $false }
    }
}

$i = 0
while ($i -lt $args.Count) {
    $arg = $args[$i]
    switch -Regex ($arg) {
        '^(-h|--help)$' { Show-Usage; exit 0 }
        '^--build-dir$' { $BuildDir = Need-Value $args $i $arg; $i += 2; continue }
        '^--build-dir=.+$' { $BuildDir = $arg.Substring("--build-dir=".Length); $i += 1; continue }
        '^(-j|--jobs)$' { $Jobs = Need-Value $args $i $arg; $i += 2; continue }
        '^--jobs=.+$' { $Jobs = $arg.Substring("--jobs=".Length); $i += 1; continue }
        default {
            Write-Error "bootstrap.ps1: unknown option: $arg"
            Show-Usage | Write-Error
            exit 2
        }
    }
}

$ConfigureArgs = @("-S", ".", "-B", $BuildDir, "-DBUILD_TESTS=OFF", "-DCMAKE_BUILD_TYPE=Release")
$ConfigureCommand = "cmake -S . -B $BuildDir -DBUILD_TESTS=OFF -DCMAKE_BUILD_TYPE=Release"
if (Test-NinjaAvailable) {
    $ConfigureArgs += @("-G", "Ninja")
    $ConfigureCommand += " -G Ninja"
}
Write-Host $ConfigureCommand
cmake @ConfigureArgs
Stop-IfNativeCommandFailed "cmake configure"

$BuildArgs = @("--build", $BuildDir, "--config", "Release", "--target", "mlang", "mlang_std", "mlang-config")
$BuildCommand = "cmake --build $BuildDir --config Release --target mlang mlang_std mlang-config"
if (![string]::IsNullOrEmpty($Jobs)) {
    $BuildArgs += @("--parallel", $Jobs)
    $BuildCommand += " --parallel $Jobs"
}
Write-Host $BuildCommand
cmake @BuildArgs
Stop-IfNativeCommandFailed "cmake build"

$MlangConfig = Resolve-MlangConfig $BuildDir
if ([string]::IsNullOrEmpty($MlangConfig)) {
    Write-Error "bootstrap.ps1: cannot find mlang-config in $BuildDir after build"
    exit 1
}

if (Confirm-YesNo "Do you want to run mlang-config? (y/n)") {
    Write-Host $MlangConfig
    & $MlangConfig
    Stop-IfNativeCommandFailed "mlang-config"

    if (Confirm-YesNo "Do you want to build the full MLang toolchain now? (y/n)") {
        $BuildScript = Join-Path "." "build.ps1"
        if (Confirm-YesNo "Do you want to install the output binaries to the configured install location? (y/n)") {
            Write-Host "$BuildScript --build-dir $BuildDir --install"
            & $BuildScript --build-dir $BuildDir --install
        } else {
            Write-Host "$BuildScript --build-dir $BuildDir --no-install"
            & $BuildScript --build-dir $BuildDir --no-install
        }
        Stop-IfNativeCommandFailed "build.ps1"
    }
}
