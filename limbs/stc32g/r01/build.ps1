param([Parameter(Mandatory=$true)][string]$UV4)
$ErrorActionPreference = 'Stop'
$compiler = (Get-Item -LiteralPath $UV4).FullName
Push-Location $PSScriptRoot
try {
    foreach ($dir in 'Objects', 'Listings') {
        New-Item -ItemType Directory -Force -Path $dir | Out-Null
    }
    $hex = 'Objects/limb.hex'
    $log = 'Objects/rebuild.log'
    $start = [DateTime]::UtcNow.AddSeconds(-2)
    $build = Start-Process $compiler -ArgumentList '-r limb.uvproj -t "STC32G12K128 R01" -o Objects/rebuild.log' -Wait -PassThru
    if (Test-Path $log) { Get-Content $log }
    if ($build.ExitCode -gt 1) { throw "uVision failed with exit code $($build.ExitCode)" }
    $image = Get-Item $hex
    if ($image.LastWriteTimeUtc -lt $start -or !(Select-String -LiteralPath $hex -SimpleMatch ':00000001FF')) {
        throw 'Missing or stale Intel HEX output'
    }
    $map = Get-ChildItem Listings,Objects -File | Where-Object {
        $_.Extension -in '.map','.m51','.m25','.m251' -and $_.LastWriteTimeUtc -ge $start
    } | Select-Object -First 1
    if (!$map) { throw 'Enable the linker memory map in the target listing settings' }
    $map.FullName
    Get-FileHash $hex -Algorithm SHA256
} finally { Pop-Location }
