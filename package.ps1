param(
    [string]$Configuration = 'Release',
    [switch]$SkipBuild,
    [Parameter(Mandatory=$true)][string]$LoaderPath
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$output = Join-Path $root 'dist'
$packageName = 'JishsVirtualTV-SelectiveSync5-Install'
$stage = Join-Path $output $packageName
if (Test-Path -LiteralPath $stage) { throw "Stage already exists: $stage. Choose a fresh dist directory before packaging." }
if (-not $SkipBuild) { & (Join-Path $root 'build.ps1') -Configuration $Configuration }
$dll = Join-Path $root "build/$Configuration/JishsVirtualTV.dll"
foreach ($file in @($dll, $LoaderPath, (Join-Path $root 'resolver/yt-dlp.exe'), (Join-Path $root 'resolver/deno.exe'))) {
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "Missing required file: $file" }
}
$custom = Join-Path $stage 'Scripts/Custom'
$bridge = Join-Path $stage 'Mods/JishsVirtualTVBridge'
New-Item -ItemType Directory -Force -Path $custom,$bridge | Out-Null
Copy-Item -LiteralPath $dll -Destination $custom
Copy-Item -LiteralPath $LoaderPath -Destination (Join-Path $stage 'dinput8.dll')
Copy-Item -LiteralPath (Join-Path $root 'JishsVirtualTV.defaults') -Destination $custom
Copy-Item -LiteralPath (Join-Path $root 'resolver') -Destination $custom -Recurse
Copy-Item -Path (Join-Path $root 'CompanionPlugin/JishsVirtualTVBridge/*') -Destination $bridge -Recurse
foreach ($name in @('README.md','LICENSE','NOTICE.md')) { Copy-Item -LiteralPath (Join-Path $root $name) -Destination $stage }
$licenses = Join-Path $stage 'licenses'
New-Item -ItemType Directory -Path $licenses | Out-Null
Copy-Item -LiteralPath (Join-Path $root 'vendor/multi-mod-loader/LICENSE.txt') -Destination (Join-Path $licenses 'MultiModLoader-LICENSE.txt')
Get-ChildItem -LiteralPath (Join-Path $root 'vendor') -Recurse -File | Where-Object { $_.Name -like '*LICENSE*' } | ForEach-Object {
    $relative = $_.FullName.Substring((Join-Path $root 'vendor').Length + 1)
    $destination = Join-Path $licenses $relative
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
    Copy-Item -LiteralPath $_.FullName -Destination $destination
}
Copy-Item -LiteralPath (Join-Path $root 'vendor/streamline211/sl_consts.h') -Destination (Join-Path $licenses 'Streamline-header-notice.h')
$archive = Join-Path $output "$packageName.zip"
Compress-Archive -LiteralPath $stage -DestinationPath $archive
Write-Host "Install package created: $archive"
