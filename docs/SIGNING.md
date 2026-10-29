# 코드 서명 가이드

서명하지 않은 빌드는 Windows에서 SmartScreen 경고가 뜨고, macOS에서는 **아예 실행이 막힌다.** 두 OS 모두 서명이 필요하며 절차와 비용이 각각 다르다.

이 문서는 상호 **SEIZIA**, 한국 개인사업자, D-U-N-S 번호 보유 상태를 전제로 한다.

---

## 먼저 알아야 할 것

**서명은 "안전한 프로그램"이라는 뜻이 아니다.** 이 파일을 누가 만들었고 그 뒤로 변조되지 않았다는 것만 증명한다. OS가 경고를 거두는 이유는 프로그램을 검사해서가 아니라 **책임질 주체가 확인됐기** 때문이다.

**인증서를 사도 코드는 계속 수정할 수 있다.** 고칠 때마다 다시 서명하면 된다. 서명은 빌드 산출물에 붙는 것이지 소스에 붙는 것이 아니다.

**타임스탬프는 선택이 아니다.** 타임스탬프 없이 서명하면 인증서가 만료되는 날 이미 배포된 모든 파일의 서명이 한꺼번에 무효가 된다. 타임스탬프가 있으면 "유효하던 시점에 서명했다"가 증명되어 만료 후에도 유지된다.

---

## Windows

### 인증서 종류

| | OV (Organization Validation) | EV (Extended Validation) |
|---|---|---|
| 신원 확인 | 사업자 확인 | 사업자 확인 + 강화 심사 |
| 개인 키 보관 | 하드웨어 필수 | 하드웨어 필수 |
| 연간 비용 | 대략 USD 200~400 | 대략 USD 400~700 |
| SmartScreen | 평판 쌓아야 함 | **평판 쌓아야 함** |

**EV를 살 이유는 거의 없다.** 예전에는 EV 인증서에 즉시 SmartScreen 평판이 부여됐지만 **Microsoft가 2024년에 그 특혜를 없앴다.** 지금은 EV도 OV와 똑같이 다운로드·설치 실적으로 평판을 쌓아야 한다. 커널 드라이버를 서명할 게 아니라면 OV로 충분하다.

### 개인 키는 이제 받을 수 없다

2023년 6월 CA/Browser Forum 규정 변경으로 **OV 코드 서명 인증서의 개인 키는 반드시 하드웨어에 있어야 한다** (FIPS 140-2 Level 2 이상). 그래서 예전처럼 `.pfx` 파일을 내려받는 방식은 사라졌다. 두 가지 중 하나를 고른다.

| 방식 | 내용 | 비고 |
|---|---|---|
| **USB 토큰** | CA가 물리 토큰을 국제우편으로 보냄 | 배송 1~2주, 분실하면 재발급 |
| **클라우드 HSM** | CA 서버의 HSM에 키를 두고 API로 서명 | 배송 없음, CI에서 쓰기 쉬움 |

**클라우드 HSM을 권한다.** 토큰은 그 PC에 물리적으로 꽂혀 있어야만 서명할 수 있어서, 나중에 빌드 서버나 다른 장비로 옮길 때 곤란해진다.

주요 CA의 클라우드 서명 서비스: DigiCert **KeyLocker**, SSL.com **eSigner**, Sectigo, GlobalSign. Certum은 상대적으로 저렴해 소규모 사업자가 많이 쓴다.

### 가장 저렴한 선택지: Azure Trusted Signing

Microsoft가 직접 운영하는 서명 서비스로, 일반 CA보다 눈에 띄게 싸다.

| | 일반 CA (OV) | Azure Trusted Signing |
|---|---|---|
| 비용 | 연 USD 200~400 | **월 USD 9.99** (연 약 120) |
| 개인 키 | 직접 관리 (토큰/HSM) | Microsoft가 관리 |
| 토큰 배송 | 1~2주 | 없음 |

**결제 전에 자격 요건을 확인해야 한다.** 사업자 신원 검증에 **설립 후 3년 이상** 조건이 걸려 있고, 지원 국가 목록도 확인이 필요하다. 최근에 등록한 사업자는 이 조건에서 막힐 수 있다.

여기서 걸리면 일반 CA로 가되, **3년 계약**으로 연 환산 단가를 낮추는 것이 현실적이다.

### 심사에 필요한 것

CA가 사업자의 실재를 확인하는 절차다. 보통 이렇게 진행된다.

1. **사업자등록증** 제출 (영문 상호가 필요하면 영문 병기 서류)
2. **제3자 데이터베이스 대조** — D-U-N-S 번호가 여기서 쓰인다. 없으면 변호사·회계사 확인서를 요구한다
3. **전화 확인** — 공개된 사업장 번호로 CA가 전화를 건다

D-U-N-S가 이미 있으므로 2번이 해결된다. 다만 **최근에 등록했다면 D&B 데이터베이스에 반영되기까지 시간이 걸릴 수 있다.** 신청 전에 D&B 조회 사이트에서 SEIZIA가 검색되는지 먼저 확인하는 편이 낫다. 안 나오면 CA 심사가 거기서 막힌다.

상호는 인증서에 박히는 이름이므로 **사업자등록증의 영문 표기와 정확히 일치**해야 한다.

### 서명 순서 — 이 순서를 지켜야 한다

**설치 파일보다 실행 파일을 먼저 서명한다.** 인스톨러가 exe를 안에 담기 때문에, 순서를 뒤집으면 설치된 프로그램에는 서명이 없다.

```
1. cmake --build build
2. seicure.exe, seicure_gui.exe 서명      <-- 먼저
3. cmake --install build --prefix build/stage
4. makensis installer\seicure.nsi
5. Seicure-0.1.0-x64-setup.exe 서명        <-- 나중
6. powershell -File tools\package.ps1      (zip + 체크섬)
```

`tools/sign.ps1`이 2번과 5번을 담당한다. `signtool`은 Windows SDK 전체를 설치하지 않고 NuGet 패키지에서 꺼내 쓴다.

```bash
powershell -File tools\fetch-signtool.ps1
```

타임스탬프 서버는 CA와 무관하게 아무거나 써도 되지만, 발급받은 CA 것을 쓰는 편이 무난하다.

```
http://timestamp.digicert.com
http://timestamp.sectigo.com
http://ts.ssl.com
```

### 평판은 서명 후에도 시간이 걸린다

인증서를 붙였다고 경고가 바로 사라지지 않는다. SmartScreen은 **그 인증서로 서명된 파일들의 다운로드·설치 실적**을 누적해 평판을 만든다. 초기에는 여전히 경고가 뜰 수 있다.

그래서 **인증서를 바꾸지 않는 것이 중요하다.** 갱신할 때 같은 CA에서 같은 상호로 이어가야 쌓은 평판이 유지된다.

---

## macOS

### 필요한 것

**Apple Developer Program, 연 USD 99.** 이것 말고는 선택지가 없다. Gatekeeper는 서명·공증되지 않은 앱의 실행을 차단하며, Windows처럼 "경고를 무시하고 실행"하는 일반 사용자용 경로가 없다.

### 현재 상태: 이미 가입되어 있음 — 추가 비용 없음

Developer Program에 **Individual 자격으로 이미 가입되어 있다.** 멤버십 하나로 앱 개수 제한 없이 서명할 수 있으므로 **이 프로그램 때문에 더 낼 돈은 없다.** 인증서를 만들어 바로 서명하면 된다.

다만 Individual 등록의 결과로 **인증서와 앱 정보에 상호(SEIZIA)가 아니라 대표자 실명이 표시된다.**

| | Individual (현재) | Organization |
|---|---|---|
| D-U-N-S | 불필요 | 필수 |
| 표시되는 이름 | **개인 실명** | 상호 (SEIZIA) |
| 심사 기간 | 며칠 | 1~4주 |

상호로 바꾸려면 Organization으로 재등록해야 하는데, Apple은 "법인격 있는 실체(legal entity)"를 요구하며 상호명·영업표지(DBA)·지점은 거부한다고 명시한다. **한국의 개인사업자는 법적으로 개인**이라 거부될 수 있다. 승인·거부 사례가 모두 있어 단정할 수 없다.

실명 노출이 문제되지 않는다면 지금 자격 그대로 쓰는 편이 간단하다. 상호가 꼭 필요하다면 재등록을 시도하기 전에 Apple 개발자 지원에 먼저 문의할 것.

### 인증서 종류

배포 방식에 따라 다르다. 지금 방식(웹사이트에서 `.dmg` 직접 배포)에는 이것이 필요하다.

- **Developer ID Application** — 앱 번들과 dmg에 서명
- Developer ID Installer — `.pkg`로 배포할 경우에만 (현재는 불필요)

Mac App Store에 올릴 게 아니라면 "Mac App Distribution" 인증서는 필요 없다.

### 맥에서 한 번만 하는 준비

**둘 다 로그인 키체인이 필요하므로 맥에서 터미널을 열고 직접 해야 한다.** SSH 세션에서는 키체인이 잠긴 채라 `codesign`이 개인 키에 닿지 못한다.

**1. Developer ID Application 인증서를 키체인에 넣는다**

```bash
security find-identity -v -p codesigning
```

`Developer ID Application: ...`이 나오면 이미 준비된 것이다. 다른 맥에 있다면 그쪽 Keychain Access에서 우클릭 → `.p12`로 내보내고, 이 맥에서 더블클릭하면 된다.

없다면 Keychain Access → 인증서 지원 → 인증 기관에서 인증서 요청 → 디스크에 저장. 그 CSR을 developer.apple.com의 Certificates에 **Developer ID Application**으로 올려 발급받고, 내려받은 `.cer`을 더블클릭한다.

**2. 공증 자격 증명을 키체인에 저장한다**

```bash
xcrun notarytool store-credentials seicure --apple-id <APPLE_ID> --team-id F3Z4RU59X6
```

앱 전용 암호를 물어본다. **Apple ID 비밀번호가 아니라** appleid.apple.com → 로그인 및 보안 → 앱 암호에서 새로 발급한 것이다.

이렇게 저장해두면 스크립트는 `seicure`라는 프로필 이름만 참조한다. **암호가 스크립트에도 셸 히스토리에도 남지 않는다.**

### 실행

준비가 끝나면 맥에서 한 줄이면 된다. 스크립트가 서명·공증·스테이플까지 알아서 한다.

```bash
bash tools/package-macos.sh
```

인증서가 없으면 서명 없이 빌드만 하고 그 사실을 출력한다. 자동 빌드는 그대로 돌아간다.

### 스크립트가 실제로 하는 일

### 서명 → 공증 → 스테이플

macOS는 서명만으로 끝나지 않는다. **Apple 서버에 올려 검사를 받고(공증), 그 결과를 파일에 붙이는(스테이플)** 단계가 더 있다.

```
1. bash tools/package-macos.sh          (빌드까지)
2. Seicure.app 서명                      <-- 번들 전체
3. dmg 생성
4. dmg 서명
5. dmg 공증 제출 → 대기
6. dmg에 티켓 스테이플
```

앱에 서명할 때 두 가지가 필수다.

- `--options runtime` — 하드닝된 런타임. 없으면 공증이 거부된다
- `--timestamp` — Windows와 같은 이유

```bash
codesign --sign "Developer ID Application: SEIZIA (TEAMID)" --options runtime --timestamp --deep --verbose Seicure.app
```

공증은 `.dmg`를 통째로 올린다.

```bash
xcrun notarytool submit Seicure-0.1.0-macos.dmg --apple-id <id> --team-id <TEAMID> --password <앱 암호> --wait
```

`<앱 암호>`는 Apple ID 비밀번호가 아니라 **앱 전용 암호(app-specific password)** 다. appleid.apple.com에서 별도로 발급한다.

```bash
xcrun stapler staple Seicure-0.1.0-macos.dmg
```

### 왜 dmg에 스테이플하는가

**공증 티켓은 tarball 안의 맨 실행 파일에는 붙지 않는다.** 번들·디스크 이미지·설치 패키지만 스테이플할 수 있다.

스테이플하지 않아도 공증 자체는 유효하지만, 그러면 사용자가 처음 실행할 때 macOS가 **Apple 서버에 조회를 시도한다.** 오프라인이거나 Apple 쪽이 느리면 실행이 지연되거나 막힌다. 스테이플하면 파일 안에 티켓이 들어 있어 네트워크 없이 검증된다.

CLI tarball은 스테이플할 수 없어서, 받는 쪽에서 격리 속성을 직접 지워야 하는 경우가 생긴다.

```bash
xattr -d com.apple.quarantine ./seicure
```

---

## 만료돼도 배포본은 살아 있다 — 비용을 줄이는 지점

**타임스탬프를 찍은 서명은 인증서가 만료돼도 유효하다.** "유효하던 시점에 서명했다"가 증명되기 때문이다. 인증서가 만료되면 이미 배포한 파일이 무효가 되는 것이 아니라, **새로 서명을 할 수 없게 될 뿐이다.**

그래서 이런 운영이 가능하다.

- 1년치를 사서 그 해의 릴리스를 몰아서 서명
- 다음 릴리스 계획이 없으면 갱신하지 않음
- 새 버전을 낼 때 다시 구매

기존 사용자에게는 아무 영향이 없다.

**단, SmartScreen 평판은 인증서 단위로 쌓인다.** 끊었다가 다른 인증서로 재개하면 평판이 초기화되어 경고가 다시 뜬다. 자주 릴리스한다면 이어가는 편이 낫고, 연 1~2회라면 끊어 쓰는 편이 싸다.

## 비용 정리

| 항목 | 연간 | 비고 |
|---|---|---|
| Apple Developer Program | **USD 0 추가** | 이미 가입되어 있고, 멤버십 하나가 앱 수와 무관하다 |
| Azure Trusted Signing | 약 USD 120 | 자격 요건 확인 필요 |
| 일반 CA OV 인증서 | USD 200~400 | 위에서 걸릴 경우의 대안 |

3년 계약으로 사면 Windows 쪽 연 환산 단가가 내려간다. 다만 **개인 키는 여전히 매년 갱신 절차를 거쳐야 하는 CA가 있으니** 계약 조건을 확인할 것.

---

## 권하는 순서

1. **macOS부터 서명해 배포를 시작한다** — 멤버십이 이미 있으므로 추가 비용도 대기 시간도 없다
2. **Azure Trusted Signing 자격 요건 확인** — 설립 연수와 지원 국가
3. 걸리면 **D&B에서 SEIZIA가 조회되는지 확인** 후 일반 CA로 (조회가 안 되면 심사가 막힌다)
4. Windows 인증서가 준비될 때까지는 체크섬 공개와 설치 안내로 버틴다
5. 인증서를 받으면 `tools/sign.ps1`과 `tools/package-macos.sh`에 실제 서명 단계를 끼워 넣는다

---

## 서명 전까지

체크섬이 유일한 진위 확인 수단이다. **`SHA256SUMS.txt`는 배포 파일 안이 아니라 HTTPS 웹페이지에 따로 게시해야 의미가 있다.** zip을 바꿔치기할 수 있는 공격자는 그 안의 체크섬도 같이 바꿀 수 있기 때문이다.
