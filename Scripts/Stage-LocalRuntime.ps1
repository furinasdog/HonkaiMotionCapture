param([Parameter(Mandatory = $true)][string]$RuntimeSource)
$ErrorActionPreference = 'Stop'
$pluginRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$sourceRoot = (Resolve-Path -LiteralPath $RuntimeSource).Path
$manifest = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'runtime-artifacts.json') -Raw | ConvertFrom-Json

function Test-ArtifactHash([string]$Path, [string]$Expected) {
    return (Test-Path -LiteralPath $Path -PathType Leaf) -and ((Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash -eq $Expected)
}

foreach ($artifact in $manifest.artifacts) {
    $sourcePath = Join-Path $sourceRoot $artifact.source
    if (-not (Test-ArtifactHash $sourcePath $artifact.sha256)) { throw "Missing or mismatched local MediaPipe artifact: $sourcePath" }
}
foreach ($artifact in $manifest.artifacts) {
    $sourcePath = Join-Path $sourceRoot $artifact.source
    $targetPath = [IO.Path]::GetFullPath((Join-Path $pluginRoot $artifact.target))
    if (-not $targetPath.StartsWith($pluginRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Runtime staging target is outside this plugin.'
    }
    if (Test-ArtifactHash $targetPath $artifact.sha256) { continue }
    New-Item -ItemType Directory -Path (Split-Path $targetPath -Parent) -Force | Out-Null
    $temporaryPath = $targetPath + '.stage-' + [Guid]::NewGuid().ToString('N')
    Copy-Item -LiteralPath $sourcePath -Destination $temporaryPath
    if (-not (Test-ArtifactHash $temporaryPath $artifact.sha256)) { throw "Copied artifact failed SHA256 verification: $temporaryPath" }
    Move-Item -LiteralPath $temporaryPath -Destination $targetPath -Force
}
Write-Output "Staged verified MediaPipe $($manifest.mediapipe_version) runtime into $pluginRoot"
Write-Output 'Only local files were copied. No executable, network download, firewall or SDK configuration was invoked.'
