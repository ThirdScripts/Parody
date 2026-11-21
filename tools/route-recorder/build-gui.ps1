param([string]$OutputDirectory='route-recorder-app',[string]$CoreBuild='cpp/build-routes')
$ErrorActionPreference='Stop'
$compiler=Join-Path $env:WINDIR 'Microsoft.NET/Framework64/v4.0.30319/csc.exe'
if(!(Test-Path $compiler)){throw '.NET Framework 4 C# compiler is required.'}
foreach($exe in 'parody.exe','route_replay.exe'){if(!(Test-Path (Join-Path $CoreBuild $exe))){throw "Build the core first: missing $exe"}}
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$target=(Resolve-Path $OutputDirectory).Path
Copy-Item "$PSScriptRoot/RouteWindow.cs" $target -Force
Push-Location $target
try { & $compiler /nologo /target:winexe /out:Routes.exe /reference:System.Windows.Forms.dll /reference:System.Drawing.dll RouteWindow.cs } finally { Pop-Location }
if($LASTEXITCODE){throw 'GUI build failed.'}
foreach($exe in 'parody.exe','route_replay.exe'){Copy-Item (Join-Path $CoreBuild $exe) $target -Force}
Copy-Item (Join-Path $CoreBuild 'assets') $target -Recurse -Force
Copy-Item "$PSScriptRoot/README.md" $target -Force
Write-Output "Ready: $target/Routes.exe. Validate profiles before live use."
