# 경찰드론 GCS — 새 작업자 안내

QGroundControl 포크입니다. SIYI ZT30 짐벌, AI 트래킹 모듈, 기체 탑재 스피커를 붙였습니다.

작업은 화면 단위로 나뉩니다. 본인이 맡은 브랜치에서만 작업하세요.

| 브랜치 | 화면 | 담당 디렉터리 |
|---|---|---|
| `main` | 통합·배포 | **직접 커밋 금지.** 병합만 합니다 |
| `feat/main` | 메인(비행) 화면 | `src/FlyView/`, `src/SiyiCamera/`, `src/Speaker/`, `src/VideoManager/`, `src/Gimbal/` |
| `feat/plan` | 미션 계획 화면 | `src/PlanView/`, `src/MissionManager/` |
| `feat/configuration` | 설정·기체 구성 화면 | `src/AppSettings/`, `src/AutoPilotPlugins/`, `src/Settings/` |

---

## 1. 접근 권한

**비공개 저장소입니다.** 초대를 받아야 `clone`이 됩니다. 초대 없이 시도하면
`Repository not found` 가 뜨는데, 저장소가 없는 게 아니라 안 보이는 것입니다.

저장소 관리자(`gunhee2330`)에게 **본인 GitHub 아이디**를 알려주고 초대를 요청하세요.
초대 메일이 오면 수락합니다. GitHub 계정이 없으면 먼저 만듭니다.

초대는 권한이고, 아래 3번의 로그인은 신원 확인입니다. **둘 다 필요합니다.**
로그인 방식을 바꿔도 초대 없이는 접근할 수 없습니다.

## 2. 설치

| | 버전 | 비고 |
|---|---|---|
| Git | 아무거나 | https://git-scm.com |
| GitHub CLI | 아무거나 | https://cli.github.com — 로그인에 씁니다 |
| Visual Studio 2022 | Community 가능 | **C++를 사용한 데스크톱 개발** 워크로드 필수. CMake·Ninja가 여기 들어 있습니다 |
| Qt | **6.11.1** msvc2022_64 | 약 3GB |

Qt는 최소 6.11.0 이상이어야 하고, 아래 모듈을 **반드시 체크**해야 합니다. 하나라도 빠지면
CMake 구성 단계에서 멈춥니다.

```
qtgraphs  qtlocation  qtpositioning  qtspeech  qtmultimedia  qtserialport
qtimageformats  qtshadertools  qtconnectivity  qtquick3d  qtsensors
qtscxml  qtwebsockets  qthttpserver
```

Qt 온라인 설치 관리자에서 `Qt 6.11.1 > MSVC 2022 64-bit` 와 위 모듈들을 선택합니다.
설치 경로는 기본값(`C:\Qt`)을 권장합니다 — 아래 명령들이 그 경로를 가정합니다.

## 3. 로그인

두 가지 방법이 있습니다. **A를 권장합니다** — 한 번 하면 이후 신경 쓸 게 없습니다.

### A. GitHub CLI (권장)

```bash
gh auth login --hostname github.com --git-protocol https --web
```

브라우저가 열립니다. 터미널에 뜬 8자리 코드를 붙여넣고 승인하면 끝입니다.
자격증명이 Windows 자격 증명 관리자에 저장돼서 이후 `git push`/`pull`에 다시 묻지 않습니다.

### B. 토큰을 비밀번호처럼 입력

GitHub은 2021년 8월에 **계정 비밀번호로 git 인증하는 것을 폐지**했습니다.
지금은 비밀번호 자리에 **Personal Access Token(PAT)** 을 넣습니다.

1. https://github.com/settings/tokens → **Generate new token (classic)**
2. 권한은 **`repo`** 하나만 체크
3. 만료일을 정하고 생성 → **토큰 문자열을 그 자리에서 복사** (다시 못 봅니다)

이후 `git clone` 하면 아이디·비밀번호를 묻는데, 비밀번호 자리에 토큰을 붙여넣습니다.

```
Username: <본인 GitHub 아이디>
Password: ghp_xxxxxxxxxxxxxxxxxxxx   ← 계정 비밀번호가 아니라 토큰
```

매번 묻지 않게 하려면:

```bash
git config --global credential.helper manager
```

> 토큰은 비밀번호와 같습니다. 카카오톡·메일로 주고받지 말고 각자 발급하세요.

## 4. 내려받기

```bash
cd /c/Users/<본인계정>/Desktop
git clone https://github.com/gunhee2330/GCS_davincilabs.git
cd GCS_davincilabs
```

첫 clone은 538MB라 10~30분 걸립니다. 서브모듈은 없으므로 `--recursive`는 필요 없습니다.

본가 업데이트를 받아올 원격도 걸어둡니다.

```bash
git remote add upstream https://github.com/mavlink/qgroundcontrol.git
```

## 5. 브랜치 선택

본인이 맡은 것 하나만 받으면 됩니다.

```bash
git checkout feat/plan            # 미션 계획 담당
git checkout feat/configuration   # 설정 화면 담당
```

## 6. 빌드

```bash
CMAKE="/c/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"

"$CMAKE" -S . -B build/Windows -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_PREFIX_PATH=C:/Qt/6.11.1/msvc2022_64

"$CMAKE" --build build/Windows
```

첫 빌드는 30분~1시간, 이후 증분 빌드는 수십 초입니다. 결과물은
`build/Windows/staging/bin/QGroundControl.exe` 입니다.

**빌드 디렉터리는 사람마다 다른 이름을 쓰세요.** 같은 디렉터리를 두 명이 동시에 쓰면
Ninja 파일 잠금이 충돌하고, 산출물이 섞여 원인을 알 수 없는 크래시가 납니다.

---

## 작업 규칙

### 공용 파일은 건드리지 마세요

아래는 세 브랜치가 모두 쓰는 파일입니다. 각자 고치면 합칠 때 세 갈래로 갈라져 수습이 어렵습니다.

- `src/Settings/SettingsManager.{cc,h}`
- `src/QmlControls/**`
- `src/MainWindow/MainWindow.qml`
- 최상위 `CMakeLists.txt`

**꼭 고쳐야 하면 `main`에 먼저 반영하고 각 브랜치가 rebase로 받아갑니다.**
`Desktop/경찰드론_GCS.md` §3 표에 점유를 선언한 뒤 진행하세요.

CMake 목록 파일 세 개(`src/CMakeLists.txt`, `src/Settings/CMakeLists.txt`,
`test/CMakeLists.txt`)는 예외입니다. `.gitattributes`에 union 병합을 걸어둬서
양쪽 추가분이 자동으로 합쳐집니다.

### 자주 합치세요

브랜치가 오래 갈라져 있을수록 충돌이 급격히 늘어납니다. 하루 한 번은 맞춥니다.

```bash
git fetch origin
git rebase origin/main
```

### 올리기 전에 검사

```bash
bash tools/integrate.sh --fast    # 충돌 검사, 수 초
```

이건 본인 브랜치가 `main`과 충돌하는지뿐 아니라 **다른 브랜치와 충돌하는지도** 봅니다.
각자 `main` 기준으로는 멀쩡한데 서로 부딪히는 경우가 실제로 가장 흔합니다.

```bash
git push
```

### `main`에 병합하기 전에는 전체 검사

```bash
bash tools/integrate.sh           # 빌드 + 단위 테스트 + 실제 기동, 수십 분
```

마지막 단계에서 앱을 실제로 25초 띄워봅니다. **QML 오류는 빌드를 통과하고 종료 코드도
0으로 나오면서 창만 안 뜨기 때문입니다.** 컴파일이 됐다는 건 병합이 잘 됐다는 증거가
아닙니다.

### 커밋 메시지

Conventional Commits를 씁니다. 자세한 규칙은 [AGENTS.md](AGENTS.md), 코드 스타일은
[CODING_STYLE.md](CODING_STYLE.md)를 보세요.

```
feat(PlanView): add per-leg speed control to mission items
fix(FlyView): guard null activeVehicle in RTL altitude picker
```

---

## 자주 겪는 문제

**`Repository not found`** — 초대를 아직 수락하지 않았습니다. 1번을 다시 보세요.

**CMake가 Qt를 못 찾음** — `-DCMAKE_PREFIX_PATH` 경로가 실제 설치 위치와 다르거나,
필요한 모듈이 빠졌습니다. 2번의 모듈 목록을 확인하세요.

**앱이 실행되자마자 조용히 종료됨** — 거의 항상 QML 오류입니다. 종료 코드가 0이라
정상 종료처럼 보이니 속지 마세요. 원인을 보려면:

```bash
QT_FORCE_STDERR_LOGGING=1 QT_LOGGING_RULES="*.warning=true" \
    build/Windows/staging/bin/QGroundControl.exe
```

**빌드 중 `Permission denied`** — 다른 사람이 같은 빌드 디렉터리를 쓰고 있거나,
QGroundControl이 이미 실행 중입니다. 프로세스를 먼저 종료하세요.
