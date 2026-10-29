# Seicure

파일 암호화 도구. 내용뿐 아니라 **파일명, 타임스탬프, 속성, 크기 정보까지 은닉**하고 복호화 시 원본 그대로 복원한다.

네이티브 C++20, .NET 비의존. 외부 런타임 DLL 없이 단일 실행 파일로 배포된다.

## 현황

core 엔진과 CLI는 Windows·Linux·macOS 세 곳에서, GUI는 Windows와 macOS에서 동작한다. 남은 것은 하드닝과 코드 서명.

## GUI

Windows는 `seicure_gui.exe`, macOS는 `Seicure.app` — 파일을 끌어다 놓고 비밀번호를 입력한 뒤 Encrypt를 누른다. 실행 파일에 파일을 드롭하거나 "연결 프로그램"으로 열어도 바로 목록에 담긴다.

두 플랫폼이 **같은 화면 코드를 쓴다.** 레이아웃·팔레트·페인트·히트테스트는 `src/gui/ui/`에 한 벌만 있고, 각 OS는 그리기 구현체와 창 코드만 따로 갖는다.

OS 기본 타이틀바 대신 커스텀 프레임을 쓴다. 헤더가 곧 제목 표시줄이며, 드래그·더블클릭 최대화·스냅 레이아웃·모서리 리사이즈는 네이티브 그대로 동작한다. macOS에서는 신호등 버튼이 그 헤더 안에 그대로 남고, 앱 마크가 그 옆으로 비켜 앉는다.

Windows는 설치하면 우클릭 메뉴가 붙는다. 아무 파일이나 우클릭 → **Seicure로 암호화**, `.sef` 파일 우클릭 → **Seicure로 복호화**. 비밀번호 칸에 커서가 놓인 채로 열리므로 입력 후 Enter만 누르면 된다. 여러 파일을 선택해도 창은 하나만 뜬다.

```bash
seicure_gui.exe --encrypt <file>...
```

```bash
seicure_gui.exe --decrypt <container>...
```

아이콘은 `tools\make_icon.ps1`이 생성한다. 16~256px 10개 크기를 담으며 작은 크기는 축소가 아니라 따로 그린다 — 128px 미만은 문서 없이 열쇠구멍만 그린다.

```bash
powershell -ExecutionPolicy Bypass -File tools\make_icon.ps1 -IcoPath assets\seicure.ico -PreviewPath preview.png
```

macOS용 `.icns`는 같은 스크립트가 그린 PNG 10장을 `iconutil`로 묶어 만든다. `iconutil`은 macOS에만 있으므로 결과물인 `assets/Seicure.icns`를 저장소에 넣어 둔다.

```bash
powershell -ExecutionPolicy Bypass -File tools\make_icon.ps1 -IconsetPath Seicure.iconset
```

```bash
iconutil -c icns Seicure.iconset -o assets/Seicure.icns
```

**macOS 아이콘은 가장자리까지 채우지 않는다.** Apple 규격은 둥근 타일을 캔버스의 약 80%에 두고 나머지를 비우며, 그래야 Dock에서 아이콘끼리 붙지 않는다. 꽉 채우면 옆 앱들보다 눈에 띄게 커 보인다. 그리는 그림 자체는 `.ico`와 동일하고 액자만 다르다.

```bash
build\src\gui\seicure_gui.exe
```

CLI와 완전히 같은 엔진(`job` 계층)을 쓰므로 한쪽으로 암호화한 파일을 다른 쪽으로 복호화할 수 있다.

## CLI

```
seicure enc [options] <file>...   암호화
seicure dec [options] <file>...   복호화

  -o, --out <dir>   출력 디렉토리 (기본: 입력 파일 옆)
      --archive     입력 전체를 하나의 컨테이너로 묶음
      --individual  입력마다 컨테이너 하나씩 (기본)
      --force       기존 파일 덮어쓰기 허용
  -h, --help
```

암호화된 파일은 랜덤 hex 이름(`67ffc7ca165eb65cc65994e2b00ced92.sef`)으로 저장된다. 안에서 파일명을 암호화해도 컨테이너가 `보고서.hwp.sef`라면 의미가 없기 때문이다.

## 예시

```bash
seicure enc "2026 사업계획.hwp" -o D:\vault
```

```bash
seicure enc --archive report.docx salaries.xlsx notes.txt -o D:\vault
```

```bash
seicure dec D:\vault\67ffc7ca165eb65cc65994e2b00ced92.sef -o D:\restored
```

## 암호 구성

| 항목 | 선택 |
|---|---|
| 대칭 암호 | **XChaCha20-Poly1305** (기본) / AES-256-GCM (선택) |
| KDF | Argon2id, 256 MiB / t=3 / p=1 |
| 난수 | libsodium `randombytes_buf` |
| 패딩 | Padmé (크기 유출 최소화, 오버헤드 12% 이내) |

프리미티브는 직접 구현하지 않았다. **libsodium 1.0.22를 벤더링해 정적 링크**하므로 외부 DLL 없는 단일 exe는 그대로다.

두 알고리즘을 모두 지원하고 컨테이너 헤더의 `cipher_id`가 어느 쪽인지 기록한다. 기본값은 XChaCha20-Poly1305 — 어느 CPU에서든 상수 시간으로 동작한다. FIPS 승인 알고리즘이 요구되는 배포에서는 기본값만 AES로 바꾸면 되고 포맷은 그대로다.

## 보호 범위

**막는 것**

- 분실·도난 디스크, USB의 내용 노출
- 클라우드 스토리지 사업자 및 침해 사고
- 암호문 변조 — 1비트만 바뀌어도 복호화가 실패한다
- 메타데이터 노출 — 파일명, 작성·수정 시각, 파일 개수

**막지 못하는 것**

- 이미 감염된 PC의 악성코드, 키로거
- 약한 비밀번호 — KDF는 시간을 벌 뿐이다
- 비밀번호를 아는 사람
- **파일 크기의 근사치.** 완전 은닉은 불가능하다. 10GB 파일을 1KB처럼 보이게 할 방법은 없다. Padmé는 유출 정보를 성긴 구간으로 뭉갤 뿐이다. 완전한 은닉은 고정 크기 컨테이너로만 가능하며 아직 미구현이다.

## 빌드

CMake 3.25+ 와 Ninja, C++20 컴파일러가 필요하다. Windows는 MinGW-w64 GCC 15 또는 MSVC, Linux는 GCC 13+ 또는 Clang 16+, macOS는 Command Line Tools의 Apple clang 21에서 확인했다.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
```

```bash
cmake --build build
```

산출물은 `build/src/cli/seicure`(Windows에서는 `.exe`)와 `build/tests/seicure_tests`. GUI는 Windows에서 `seicure_gui.exe`, macOS에서 `Seicure.app`으로 나오며 Linux에서는 자동으로 빠진다.

**플랫폼 지원 현황**

| | 엔진 + CLI | GUI |
|---|---|---|
| Windows x64 | 검증됨 | 검증됨 |
| Linux x64 | 검증됨 (Ubuntu 24.04 / GCC 13.3) | 미구현 |
| macOS (arm64+x86_64) | 검증됨 (macOS 26.5 / Apple clang 21) | 검증됨 |

세 플랫폼이 **컨테이너를 서로 열 수 있다.** 파일명·시각·권한이 양방향으로 보존되며, 읽기 전용 속성은 POSIX `0444`와 상호 변환된다. macOS는 stat이 생성 시각까지 주므로 맥에서 만든 컨테이너를 윈도우에서 풀면 생성 시각도 복원된다. 반대 방향은 복원되지 않는데, POSIX에는 생성 시각을 지정하는 API가 없기 때문이다.

macOS 유니버설 바이너리는 arm64 슬라이스만 실행 검증했다. x86_64 슬라이스는 빌드되고 최소 버전도 11.0으로 올바르게 붙지만, 실행하려면 Rosetta 2가 필요해 시험 장비에서는 돌려보지 못했다.

## 설치 프로그램

NSIS 3.x가 필요하다 (`winget install NSIS.NSIS`). 먼저 두 실행 파일을 한 폴더로 모은 뒤 스크립트를 컴파일한다.

```bash
cmake --install build --prefix build/stage
```

```bash
"%ProgramFiles(x86)%\NSIS\makensis.exe" installer\seicure.nsi
```

결과물은 `build/Seicure-0.1.0-x64-setup.exe`. 시스템 전체 설치(Program Files)이며 `.sef` 파일 연결과 우클릭 메뉴를 등록한다.

`/DTESTMODE`를 주면 관리자 권한 없이 HKCU에 쓰는 시험용 빌드가 나온다. 실제 시스템을 건드리지 않고 설치·업그레이드·제거 전 과정을 검증할 때 쓴다.

```bash
"%ProgramFiles(x86)%\NSIS\makensis.exe" /DTESTMODE installer\seicure.nsi
```

**스크립트는 BOM이 있는 UTF-8로 저장해야 한다.** BOM이 없으면 makensis가 한글 문자열에서 `Bad text encoding`으로 실패한다.

## 배포본 만들기

### Windows

```bash
powershell -File tools\package.ps1
```

`build/release/`에 세 가지가 생긴다.

| 파일 | 용도 |
|---|---|
| `Seicure-0.1.0-portable-x64.zip` | 설치 없이 실행. 레지스트리를 건드리지 않는다 |
| `Seicure-0.1.0-x64-setup.exe` | 설치본. 파일 연결과 우클릭 메뉴 포함 |
| `SHA256SUMS.txt` | 두 파일의 SHA-256 |

**체크섬은 zip 안이 아니라 배포 페이지에 올려야 한다.** zip을 바꿔치기할 수 있는 공격자는 그 안의 체크섬도 같이 바꿀 수 있다. HTTPS로 서비스되는 페이지에 따로 게시해야 의미가 있다.

코드 서명 전까지는 이것이 사용자가 받은 파일의 진위를 확인할 수 있는 유일한 수단이다.

### Linux

```bash
bash tools/package-linux.sh
```

`build-linux/release/`에 `.deb`와 포터블 `.tar.gz`가 생긴다. **두 개를 다르게 링크한다.**

| 산출물 | 링크 | 이유 |
|---|---|---|
| `.deb` | 동적 | glibc 보안 업데이트가 이 바이너리에도 적용되어야 한다 |
| `.tar.gz` | 완전 정적 | 배포판·glibc 버전과 무관하게 실행된다 |

정적 링크된 glibc는 보통 권장되지 않는다. NSS가 호스트·사용자 조회를 `dlopen`으로 처리하는데 정적 바이너리는 그 모듈을 못 읽기 때문이다. **이 프로그램은 네트워크도 계정 조회도 하지 않아** 해당되지 않으며, 빌드에서 NSS 경고도 나오지 않는다.

`.deb`는 빌드한 배포판의 glibc·libstdc++ 버전을 하한으로 물려받는다. 현재 패키지는 Ubuntu 24.04에서 빌드되어 `libstdc++6 (>= 13)`을 요구하므로 22.04에는 설치되지 않는다. **더 넓은 배포판을 지원하려면 더 오래된 배포판에서 빌드해야 한다.** 그때까지는 정적 tarball이 보편적 대안이다.

### macOS

```bash
bash tools/package-macos.sh
```

Apple Silicon과 Intel 양쪽에서 네이티브 실행되는 유니버설 바이너리를 만든다. `build-macos/release/`에 두 가지가 나온다.

| 파일 | 용도 |
|---|---|
| `Seicure-0.1.0-macos.dmg` | GUI. 열어서 `Seicure.app`을 Applications로 끌어다 놓는다 |
| `seicure-0.1.0-macos-universal.tar.gz` | 명령줄 버전 |

**GUI를 `.dmg`로 내보내는 데는 이유가 있다.** 공증 티켓은 번들·디스크 이미지·설치 패키지에만 붙일 수 있고, tarball 안의 맨 실행 파일에는 붙지 않는다. 그래서 서명을 붙이는 날 `.dmg`는 오프라인에서도 검증되고 tarball은 그렇지 못한다.

Linux와 달리 정적 링크 옵션이 없다. Apple이 정적 libSystem을 제공하지 않기 때문이며, 그래서 macOS에서는 정적 링크 대신 **배포 타깃(11.0)**이 하위 호환을 담당한다. 의존성은 `libSystem`과 `libc++` 둘뿐이다.

macOS는 Gatekeeper가 서명 없는 다운로드를 아예 차단해서, Windows처럼 "경고를 넘기고 실행"하는 경로가 없다. 서명과 공증이 둘 다 필수이며 Apple Developer Program(연 $99)이 필요하다. 절차는 `tools/package-macos.sh` 하단 주석 참조.

## 코드 서명

`signtool`은 Windows SDK를 설치하지 않고 NuGet 패키지에서 꺼내 쓴다. 시스템 드라이브를 건드리지 않고 레지스트리도 남기지 않는다.

```bash
powershell -File tools\fetch-signtool.ps1
```

```bash
powershell -File tools\sign.ps1 -CertPath D:\keys\seicure.pfx
```

**인스톨러보다 실행 파일을 먼저 서명해야 한다.** 인스톨러가 exe를 안에 담기 때문에, 설치 파일만 나중에 서명하면 내부 파일에는 서명이 붙지 않는다.

타임스탬프는 선택이 아니다. 없으면 인증서가 만료되는 날 기존 서명이 전부 무효가 된다.

2023년 6월부터 OV 인증서의 개인 키는 HSM에 보관해야 하므로 실제 발급에는 `.pfx`가 나오지 않는다. CA별 서명 도구를 쓰게 되며, 파일 다이제스트·타임스탬프·검증 절차는 동일하다.

인증서 종류 선택, 심사 준비물, Windows와 macOS의 서명 순서는 [docs/SIGNING.md](docs/SIGNING.md)에 정리했다.

계층은 `core → platform → job → { cli, gui }` 순이다. `job`이 "파일들을 암호화한다"를 담당하고 두 프론트엔드가 이를 공유한다.

GUI는 다시 둘로 나뉜다. `src/gui/ui/`는 팔레트·메트릭·레이아웃·페인트·히트테스트를 담으며 **플랫폼 타입이 하나도 없다.** 그 위에 `src/gui/win32/`가 GDI로, `src/gui/mac/`이 Core Graphics로 `Canvas`의 12개 연산을 구현한다.

분리 직후 Windows 리팩터링 전후 스크린샷을 픽셀 단위로 비교해 936,752개 중 0개 차이를 확인했다. 이후 macOS 프론트엔드를 붙일 때 **레이아웃·페인트 코드는 한 줄도 다시 쓰지 않았다** — 새로 쓴 것은 렌더러와 창 코드뿐이다.

MSVC로도 빌드된다. 배포 단계에서는 Control Flow Guard(`/guard:cf`)와 코드 서명(`signtool`)을 쓸 수 있는 MSVC + Windows SDK를 권장한다. 둘 다 MinGW에서는 불가능하다.

## 테스트

```bash
build\tests\seicure_tests.exe
```

65개 테스트. Windows·Linux·macOS 세 곳에서 모두 통과한다. 정확성은 자체 기댓값이 아니라 **공식 벡터**로 검증한다 — GCM 규격 원문의 AES-256 케이스와 Argon2 레퍼런스 구현의 테스트 벡터를 그대로 쓴다.

기능 통과보다 **실패해야 할 때 실패하는지**에 무게를 뒀다. 비트 반전, 헤더 파라미터 하향 조작, 청크 순서 교환, 청크 중복, 파일 절단, 바이트 덧붙이기, 다른 파일 헤더 이식, 아카이브 항목의 경로 탈출 시도가 모두 거부되는지 확인한다.

## 문서

설계 근거와 파일 포맷 명세는 [docs/DESIGN.md](docs/DESIGN.md)에, 코드 서명 절차는 [docs/SIGNING.md](docs/SIGNING.md)에 있다.

## 라이선스

**Apache License 2.0.** 전문은 [LICENSE](LICENSE)에, 저작권 및 제3자 고지는 [NOTICE](NOTICE)에 있다.

상업적 이용을 포함해 자유롭게 쓰고 고치고 재배포할 수 있다. MIT 대신 Apache-2.0을 고른 이유는 **특허 사용권을 명시적으로 부여**하기 때문이다. 암호 소프트웨어는 특허 분쟁 이력이 있는 분야라, 쓰는 쪽이 그 위험을 따로 계산하지 않아도 되게 하는 편이 낫다.

재배포할 때는 LICENSE 사본을 함께 제공하고, 고친 파일에는 고쳤다는 표시를 남기고, NOTICE의 내용을 유지해야 한다.

벤더링한 의존성은 별개의 조건을 따른다.

| 경로 | 출처 | 라이선스 |
|---|---|---|
| `third_party/libsodium` | [jedisct1/libsodium](https://github.com/jedisct1/libsodium) 1.0.22 | ISC |

libsodium은 상류 그대로이며 수정하지 않았다. 고정 커밋과 검증 방법은 `third_party/libsodium/VENDORED.txt`에 있다.
