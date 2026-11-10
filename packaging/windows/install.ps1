# EX-GF installer for Windows: VST3 -> C:\Program Files\Common Files\VST3, apps -> %LOCALAPPDATA%\EX-GF (+ Start menu)
# Run from an administrator PowerShell:  powershell -ExecutionPolicy Bypass -File install.ps1
$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot
$vst3 = Join-Path $env:CommonProgramFiles "VST3"
New-Item -ItemType Directory -Force -Path $vst3 | Out-Null
Get-ChildItem VST3 -Directory | ForEach-Object {
  $dest = Join-Path $vst3 $_.Name
  if (Test-Path $dest) { Remove-Item -Recurse -Force $dest }
  Copy-Item -Recurse $_.FullName $dest
}
$apps = Join-Path $env:LOCALAPPDATA "EX-GF"
New-Item -ItemType Directory -Force -Path $apps | Out-Null
$menu = Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs\EX-GF"
New-Item -ItemType Directory -Force -Path $menu | Out-Null
$shell = New-Object -ComObject WScript.Shell
Get-ChildItem Apps -Filter *.exe | ForEach-Object {
  Copy-Item -Force $_.FullName $apps
  $lnk = $shell.CreateShortcut((Join-Path $menu ($_.BaseName + ".lnk")))
  $lnk.TargetPath = Join-Path $apps $_.Name
  $lnk.Save()
}
Write-Host "Installed EX-GF: VST3 plug-ins in $vst3, apps in $apps (Start menu > EX-GF)."
Write-Host "Optional: download EX-SD-Card.zip from the release page and unzip it into your Music folder (giving Music\EX SD Card)."
