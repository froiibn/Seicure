<#
.SYNOPSIS
    Authenticode-signs the Seicure binaries and installer.

.DESCRIPTION
    signtool comes from the Microsoft.Windows.SDK.BuildTools NuGet package,
    extracted under tools\winsdk. That avoids installing the Windows SDK, which
    matters on a machine short of space on C:.

    Timestamping is not optional. Without it every signature stops validating
    the day the certificate expires; with it they stay valid indefinitely,
    because the timestamp proves the file was signed while the certificate was
    still good.

.PARAMETER CertPath
    Path to the signing certificate (.pfx). With a cloud HSM this is replaced by
    the CA's own signing tool or a CSP option -- see the notes at the bottom.

.PARAMETER Files
    Files to sign. Defaults to both executables and the installer.

.EXAMPLE
    tools\sign.ps1 -CertPath D:\keys\seicure.pfx
#>
param(
    [Parameter(Mandatory = $true)][string]$CertPath,
    [securestring]$Password,
    [string[]]$Files,
    [string]$TimestampUrl = 'http://timestamp.digicert.com'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

$signtool = Get-ChildItem "$root\tools\winsdk" -Recurse -Filter signtool.exe -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -match '\\x64\\' } | Select-Object -First 1
if (-not $signtool) {
    throw "signtool not found under tools\winsdk. See tools\fetch-signtool.ps1."
}

if (-not $Files) {
    $Files = @(
        "$root\build\src\cli\seicure.exe",
        "$root\build\src\gui\seicure_gui.exe",
        "$root\build\Seicure-0.1.0-x64-setup.exe"
    ) | Where-Object { Test-Path $_ }
}
if (-not $Files) { throw "nothing to sign; build first" }

# The installer embeds the executables, so those must be signed before it is
# built -- signing the setup afterwards does not reach inside it.
foreach ($f in $Files) {
    $args = @('sign', '/fd', 'sha256', '/f', $CertPath)
    if ($Password) {
        $plain = [System.Runtime.InteropServices.Marshal]::PtrToStringAuto(
            [System.Runtime.InteropServices.Marshal]::SecureStringToBSTR($Password))
        $args += @('/p', $plain)
    }
    $args += @('/tr', $TimestampUrl, '/td', 'sha256', '/v', $f)

    Write-Host "signing $(Split-Path -Leaf $f)"
    & $signtool.FullName @args
    if ($LASTEXITCODE -ne 0) { throw "signing failed for $f" }
}

Write-Host "`n=== verification ==="
foreach ($f in $Files) {
    & $signtool.FullName verify /pa /v $f 2>&1 | Select-String 'Successfully verified|Signing Certificate|Timestamp' |
        ForEach-Object { "  $($_.Line.Trim())" }
}

<#
Cloud HSM note
--------------
Since June 2023 the CA/Browser Forum requires OV code-signing keys to live on
an HSM, so a plain .pfx is no longer issued for production certificates. Each
CA provides its own path:

  DigiCert KeyLocker   smctl sign --keypair <name> --input <file>
  SSL.com eSigner      CodeSignTool sign -input_file_path=<file>
  Sectigo / GlobalSign signtool with /csp and /kc pointing at their provider

The rest of the pipeline is unchanged: same SHA-256 file digest, same
timestamping, same verification step.
#>
