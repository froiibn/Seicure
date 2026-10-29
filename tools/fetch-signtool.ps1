<#
.SYNOPSIS
    Fetches signtool.exe without installing the Windows SDK.

.DESCRIPTION
    The signing tools ship inside the Microsoft.Windows.SDK.BuildTools NuGet
    package, which is just a zip. Extracting it gives a working signtool
    without an installer, without touching the registry, and without consuming
    space on the system drive -- which matters when C: is tight.

    Run once; tools\winsdk is gitignored.
#>
param(
    [string]$Version = '10.0.28000.2526',
    [string]$Destination = "$PSScriptRoot\winsdk"
)

$ErrorActionPreference = 'Stop'

if (Get-ChildItem $Destination -Recurse -Filter signtool.exe -ErrorAction SilentlyContinue) {
    Write-Host "signtool already present under $Destination"
    exit 0
}

New-Item -ItemType Directory -Force -Path $Destination | Out-Null
$zip = Join-Path $Destination 'sdk-buildtools.zip'
$url = "https://api.nuget.org/v3-flatcontainer/microsoft.windows.sdk.buildtools/$Version/microsoft.windows.sdk.buildtools.$Version.nupkg"

Write-Host "downloading $Version"
Invoke-WebRequest -Uri $url -OutFile $zip -UseBasicParsing
Expand-Archive -Path $zip -DestinationPath (Join-Path $Destination 'pkg') -Force
Remove-Item $zip -Force

$found = Get-ChildItem $Destination -Recurse -Filter signtool.exe |
         Where-Object { $_.FullName -match '\\x64\\' } | Select-Object -First 1
if (-not $found) { throw "signtool.exe not found in the package" }
Write-Host "signtool: $($found.FullName)"
