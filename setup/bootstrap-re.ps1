param (
    [switch]$automated = $false
    [string]$arch = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$PSDefaultParameterValues['*:ErrorAction']='Stop'

function ThrowOnNativeFailure {
    if (-not $?)
    {
        throw 'Command failed'
    }
}

Write-Host ""
Write-Host -ForegroundColor Green  " * Re Build System - Bootstrapper"
Write-Host                         "   This will download, build and install the latest version of Re."
Write-Host ""
Write-Host -ForegroundColor Yellow " NOTE: This script is used to build and install Re from scratch."
Write-Host -ForegroundColor Yellow "       If your system has an official Re release, consider downloading it instead."
Write-Host ""
Write-Host -ForegroundColor Yellow " NOTE: PLEASE make sure CMake and Ninja are available in your PATH before proceeding!"
Write-Host -ForegroundColor Yellow "       The build requires those two to be present."
Write-Host ""
Write-Host -ForegroundColor Magenta " Press ENTER to continue bootstrapping Re or CTRL+C to quit... " -NoNewline

if (-not $automated) {
    Read-Host
}

$src_dir = "re-bootstrap-source"
$repo_url = "https://github.com/osdeverr/rebs.git"
$bootstrap_branch = "bootstrap"
$installed_prefix = "re-boostrap-installed"
$main_src_dir = Resolve-Path "."

mkdir re-latest-build -ea 0
$final_out_dir = Resolve-Path "./re-latest-build"

Write-Host -ForegroundColor Yellow " * Downloading Re bootstrap sources from $repo_url@$bootstrap_branch"

if (-not (Test-Path $src_dir)) {
    git clone $repo_url --recursive --branch $bootstrap_branch $src_dir
    ThrowOnNativeFailure
}

Set-Location $src_dir

mkdir out -ea 0
Set-Location out

Write-Host -ForegroundColor Yellow " * Generating CMake files"
cmake .. -G Ninja -DCMAKE_BUILD_TYPE="RelWithDebInfo"
ThrowOnNativeFailure

Write-Host -ForegroundColor Yellow " * Building the bootstrap source"
cmake --build .
ThrowOnNativeFailure

Write-Host -ForegroundColor Yellow " * Installing the bootstrap source"
cmake --install . --prefix $installed_prefix
ThrowOnNativeFailure

Set-Location ../..

Write-Host -ForegroundColor Yellow " * Downloading latest Re files"

if(-not (Test-Path $main_src_dir)) {
    git clone $repo_url --recursive --branch $bootstrap_branch $src_dir
    Set-Location $main_src_dir
}
else {
    Set-Location $main_src_dir
    git pull
    ThrowOnNativeFailure
}

Write-Host -ForegroundColor Yellow " * Setting up build parameters"

Write-Output "re-dev-deploy-path: $final_out_dir" > re.user.yml

if (-not $automated) {
    if (-not $arch) {
        Write-Host -ForegroundColor Magenta " > Which architecture do you want to build Re for? (x86/x64/etc, leave empty for default): " -NoNewline
        $arch = Read-Host
    }
}

if ($arch) {
    Write-Output "arch: $arch" >> re.user.yml
}

Write-Host -ForegroundColor Yellow " * Building the latest Re"
..\re-bootstrap-source-test\out\re-boostrap-installed\bin\re.exe do deploy
ThrowOnNativeFailure

Set-Location ..

Write-Host -ForegroundColor Green  " * Re has succesfully been built and installed to:"
Write-Host                         "     $final_out_dir"
Write-Host ""
Write-Host -ForegroundColor Green  " * You should set up symlinks and/or PATH entries for the directory above, or move all of its contents"
Write-Host -ForegroundColor Green  "   somewhere else and do the same to wherever you moved it to, to start building projects with Re."
Write-Host ""
Write-Host -ForegroundColor Green  "   Enjoy using Re!"
Write-Host ""

