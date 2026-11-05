param([switch]$Test)
$ErrorActionPreference='Stop'
$taskRoot=$PSScriptRoot
$cmakeCommand=Get-Command cmake -ErrorAction SilentlyContinue
if($cmakeCommand){$cmake=$cmakeCommand.Source}else{
    $vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if(!(Test-Path -LiteralPath $vswhere)){throw 'Install Visual Studio 2022 C++ tools and CMake, or put cmake on PATH.'}
    $installation=& $vswhere -latest -products '*' -version '[17.0,18.0)' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if(!$installation){throw 'Visual Studio 2022 C++ tools not found.'}
    $cmake=Join-Path $installation 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
}
$buildRoot=Join-Path $taskRoot 'out'
$releaseRoot=Join-Path $taskRoot 'dist\XREAL-StableScreen-v0.1.1'
New-Item -ItemType Directory -Force $releaseRoot | Out-Null
& $cmake -S $taskRoot -B $buildRoot -G 'Visual Studio 17 2022' -A x64 '-DCMAKE_SYSTEM_VERSION=10.0.22621.0'
if($LASTEXITCODE -ne 0){throw 'CMake configure failed'}
& $cmake --build $buildRoot --config Release --parallel 2
if($LASTEXITCODE -ne 0){throw 'C++ build failed'}
Copy-Item -LiteralPath (Join-Path $buildRoot 'Release\XrealStableScreen.exe') -Destination $releaseRoot
Copy-Item -LiteralPath (Join-Path $taskRoot 'README.zh-CN.md') -Destination (Join-Path $releaseRoot '使用说明.md') -ErrorAction SilentlyContinue
Copy-Item -LiteralPath (Join-Path $taskRoot 'THIRD-PARTY-NOTICES.txt') -Destination $releaseRoot -ErrorAction SilentlyContinue
if($Test){
    $testProcess=Start-Process -FilePath (Join-Path $releaseRoot 'XrealStableScreen.exe') -ArgumentList '--self-test' -WindowStyle Hidden -PassThru -Wait
    Get-Content (Join-Path $releaseRoot 'self-test.json')
    if($testProcess.ExitCode -ne 0){throw 'Self test failed'}
}
Get-FileHash -LiteralPath (Join-Path $releaseRoot 'XrealStableScreen.exe') -Algorithm SHA256
