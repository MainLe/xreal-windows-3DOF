param([switch]$SkipBuild)
$ErrorActionPreference='Stop'
if(!$SkipBuild){& (Join-Path $PSScriptRoot 'build.ps1')}
$output=Join-Path $PSScriptRoot 'dist'
$application=Join-Path $output 'XREAL-StableScreen-v0.1.1'
$files=@('XrealStableScreen.exe','使用说明.md','THIRD-PARTY-NOTICES.txt') | ForEach-Object {
    $entry=Join-Path $application $_
    if(!(Test-Path -LiteralPath $entry)){throw ('Missing Release file: '+$entry)}
    $entry
}
$zip=Join-Path $output 'XREAL-StableScreen-v0.1.1-win-x64.zip'
Compress-Archive -LiteralPath $files -DestinationPath $zip -CompressionLevel Optimal -Force
$digest=(Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText((Join-Path $output 'SHA256SUMS'),($digest+'  '+(Split-Path $zip -Leaf)+"`n"),[Text.UTF8Encoding]::new($false))
Get-Item -LiteralPath $zip | Select-Object FullName,Length
