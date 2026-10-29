<#
.SYNOPSIS
    Assembles the release artifacts and their checksums.

.DESCRIPTION
    Produces two things from an existing build:

      Seicure-<ver>-portable-x64.zip   run from a folder, nothing installed
      Seicure-<ver>-x64-setup.exe      copied in if the installer was built

    plus SHA256SUMS.txt covering both.

    The checksums are not decoration. Until the binaries are code-signed they
    are the only way a user can tell that what they downloaded is what was
    published, so they belong next to the download, on a page served over
    HTTPS -- not inside the zip, where an attacker who could swap the zip could
    swap them too.
#>
param(
    [string]$Version = '0.1.0',
    [string]$BuildDir = "$(Split-Path -Parent $PSScriptRoot)\build"
)

$ErrorActionPreference = 'Stop'
$root    = Split-Path -Parent $PSScriptRoot
$release = Join-Path $BuildDir 'release'
$name    = "Seicure-$Version-portable-x64"
$staging = Join-Path $release $name

$gui = Join-Path $BuildDir 'src\gui\seicure_gui.exe'
$cli = Join-Path $BuildDir 'src\cli\seicure.exe'
foreach ($f in $gui, $cli) {
    if (-not (Test-Path $f)) { throw "missing $f -- build first" }
}

if (Test-Path $release) { Remove-Item -LiteralPath $release -Recurse -Force }
New-Item -ItemType Directory -Force -Path $staging | Out-Null

Copy-Item $gui, $cli -Destination $staging

# Short, practical, and honest about what the portable build does not do.
$readme = @"
Seicure $Version — 포터블 버전
================================

설치 없이 실행하는 배포본입니다. 압축을 풀고 바로 실행하세요.

  seicure_gui.exe   그래픽 버전
  seicure.exe       명령줄 버전

설치본과 다른 점
----------------
레지스트리를 건드리지 않고, 시스템에 아무것도 남기지 않습니다.
USB에 넣어 다녀도 됩니다.

대신 우클릭 메뉴와 .sef 파일 연결은 없습니다. 그 기능이 필요하면
설치본을 쓰세요.

명령줄 사용법
-------------
  seicure enc <파일>...            암호화
  seicure dec <컨테이너>...        복호화

  -o, --out <폴더>   출력 위치 (기본: 원본 파일 옆)
      --archive      여러 파일을 컨테이너 하나로 묶음
      --force        기존 파일 덮어쓰기 허용

암호화된 파일은 랜덤 hex 이름으로 저장됩니다. 원본 파일명·크기·
시각·권한은 모두 암호문 안에 들어가고 복호화할 때 복원됩니다.

무결성 확인
-----------
이 배포본은 아직 코드 서명이 되어 있지 않습니다. Windows가 실행 시
경고를 띄울 수 있습니다.

받으신 파일이 배포된 것과 같은지는 배포 페이지의 SHA256SUMS.txt와
대조해 확인하세요.

  certutil -hashfile Seicure-$Version-portable-x64.zip SHA256

비밀번호를 잊으면 복구할 방법이 없습니다. 설계상 그렇습니다.
"@
[System.IO.File]::WriteAllText((Join-Path $staging 'README.txt'), $readme,
                               (New-Object System.Text.UTF8Encoding($true)))

$zip = Join-Path $release "$name.zip"
Compress-Archive -Path $staging -DestinationPath $zip -CompressionLevel Optimal
Remove-Item -LiteralPath $staging -Recurse -Force

$setup = Join-Path $BuildDir "Seicure-$Version-x64-setup.exe"
if (Test-Path $setup) { Copy-Item $setup -Destination $release }

$lines = @("# Seicure $Version", "# SHA-256")
Get-ChildItem $release -File | Sort-Object Name | ForEach-Object {
    $h = (Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLower()
    $lines += "$h  $($_.Name)"
}
[System.IO.File]::WriteAllLines((Join-Path $release 'SHA256SUMS.txt'), $lines)

Write-Host "`n=== release ==="
Get-ChildItem $release -File | ForEach-Object {
    "{0,-40} {1,10:N0} bytes" -f $_.Name, $_.Length
}
