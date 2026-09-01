# Builds novusc on Windows from the checked-in C snapshot.
# Needs a C compiler in PATH: gcc (MinGW-w64 / MSYS2), clang or "zig cc".
#   powershell -ExecutionPolicy Bypass -File scripts\bootstrap.ps1
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$out = if ($env:NOVUS_OUT) { $env:NOVUS_OUT } else { Join-Path $root "build" }
$cc = if ($env:NOVUS_CC) { $env:NOVUS_CC } else { "gcc" }
$env:NOVUS_CC = $cc
New-Item -ItemType Directory -Force -Path $out | Out-Null

Write-Host "stage0: $cc bootstrap\novusc.c -> $out\novusc0.exe"
& $cc -O2 (Join-Path $root "bootstrap\novusc.c") -o (Join-Path $out "novusc0.exe") -lm
if ($LASTEXITCODE -ne 0) { throw "stage0 failed" }

Write-Host "stage1: compiling compiler\main.nv with the snapshot"
& (Join-Path $out "novusc0.exe") build (Join-Path $root "compiler\main.nv") -o (Join-Path $out "novusc1.exe") | Out-Null
if ($LASTEXITCODE -ne 0) { throw "stage1 failed" }

Write-Host "stage2: compiling compiler\main.nv with stage1"
& (Join-Path $out "novusc1.exe") build (Join-Path $root "compiler\main.nv") -o (Join-Path $out "novusc.exe") | Out-Null
if ($LASTEXITCODE -ne 0) { throw "stage2 failed" }

Write-Host "ok: $out\novusc.exe ($(& (Join-Path $out 'novusc.exe') version))"
