param([switch]$VerifyOnly)
$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$manifest = Get-Content -LiteralPath (Join-Path $repoRoot 'distribution\manifest.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$localRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot '.local'))
if (-not $localRoot.StartsWith($repoRoot.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Invalid output directory' }
[IO.Directory]::CreateDirectory($localRoot) | Out-Null
$archive = Join-Path $localRoot $manifest.fileName
$stream = [IO.File]::Open($archive, [IO.FileMode]::Create, [IO.FileAccess]::Write, [IO.FileShare]::None)
try {
    foreach ($part in $manifest.parts) {
        $source = [IO.Path]::GetFullPath((Join-Path $repoRoot $part.path))
        if (-not $source.StartsWith($repoRoot.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Part path outside repository' }
        if ((Get-Item -LiteralPath $source).Length -ne $part.bytes -or (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant() -ne $part.sha256) { throw ('Part checksum failed: ' + $part.path) }
        $partStream = [IO.File]::OpenRead($source)
        try { $partStream.CopyTo($stream) } finally { $partStream.Dispose() }
    }
} finally { $stream.Dispose() }
if ((Get-Item -LiteralPath $archive).Length -ne $manifest.bytes -or (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne $manifest.sha256) { throw 'Complete archive checksum failed' }
if ($VerifyOnly) { Write-Output 'Archive checksum passed'; exit 0 }
$destination = Join-Path $localRoot 'v0.1.0-preview'
Expand-Archive -LiteralPath $archive -DestinationPath $destination -Force
$entry = Join-Path $destination 'LvmengModManager.exe'
if ((Get-FileHash -LiteralPath $entry -Algorithm SHA256).Hash.ToLowerInvariant() -ne $manifest.managerSha256) { throw 'Manager checksum failed' }
Start-Process -FilePath $entry -WorkingDirectory $destination
