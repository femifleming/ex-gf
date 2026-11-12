# Builds and packages the Windows release (VST3 + standalone apps) as a .zip.
#   powershell -ExecutionPolicy Bypass -File packaging\package-windows.ps1 [version]
param([string]$Version = "")
$ErrorActionPreference = "Stop"
Set-Location (Join-Path $PSScriptRoot "..")
if (-not $Version) { $Version = ((Select-String -Path CMakeLists.txt -Pattern 'project\(EXGF VERSION ([0-9.]+)').Matches[0].Groups[1].Value) }
$Build = "build-release"
cmake -B $Build -DEX_COPY_AFTER_BUILD=OFF -DEX_BUILD_TESTS=ON
cmake --build $Build --config Release --parallel
$Name = "EX-GF-$Version-Windows-x64"
$Stage = "dist\$Name"
if (Test-Path $Stage) { Remove-Item -Recurse -Force $Stage }
New-Item -ItemType Directory -Force -Path "$Stage\VST3", "$Stage\Apps" | Out-Null
foreach ($t in "EXMultisample", "EXPolyWavetable", "EXPolyFM", "EXPolyExciter", "EXGranulator", "EXMultiFX", "EXResonator", "EXSpectralFreeze", "EXMacroOsc2") {
  $A = "$Build\${t}_artefacts\Release"
  Copy-Item -Recurse "$A\VST3\*.vst3" "$Stage\VST3\"
  Copy-Item "$A\Standalone\*.exe" "$Stage\Apps\"
}
Copy-Item packaging\windows\install.ps1, packaging\INSTALL.txt, docs\EX-GF-manual.pdf $Stage
Compress-Archive -Force -Path $Stage -DestinationPath "dist\$Name.zip"
(Get-FileHash "dist\$Name.zip" -Algorithm SHA256).Hash
