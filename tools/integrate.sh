#!/usr/bin/env bash
# Integration gate for the police drone GCS branches.
#
# The three view branches (feat/main, feat/plan, feat/configuration) are developed
# independently, so nothing guarantees they still fit together. This checks that they
# do, in four escalating stages -- each one slower and more conclusive than the last:
#
#   1. drift    which branches have fallen behind main
#   2. conflict whether the merges apply, without touching the working tree
#   3. build    whether the merged result compiles
#   4. smoke    whether the merged app actually starts
#
# Stage 4 exists because a QML error does not fail the build and does not set a
# non-zero exit code -- the window simply never appears. Compiling is not evidence
# that the merge worked.
#
#   tools/integrate.sh              # all four stages
#   tools/integrate.sh --fast       # stages 1-2 only (seconds)
#   tools/integrate.sh --keep       # leave the integration branch behind to inspect
#
set -uo pipefail

BRANCHES=(feat/main feat/plan feat/configuration)
BASE=main
BUILD_DIR=build/Windows-integration
SMOKE_SECONDS=25

VS=/c/Program\ Files/Microsoft\ Visual\ Studio/2022/Community
CMAKE="$VS/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
CTEST="$VS/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/ctest.exe"
QT_PREFIX=/c/Qt/6.11.1/msvc2022_64

FAST=0
KEEP=0
for arg in "$@"; do
    case "$arg" in
        --fast) FAST=1 ;;
        --keep) KEEP=1 ;;
        *) echo "unknown option: $arg" >&2; exit 2 ;;
    esac
done

cd "$(dirname "$0")/.." || exit 1

red()   { printf '\033[31m%s\033[0m\n' "$*"; }
green() { printf '\033[32m%s\033[0m\n' "$*"; }
bold()  { printf '\033[1m%s\033[0m\n' "$*"; }

FAILURES=()
fail() { FAILURES+=("$1"); red "  x $1"; }

# ---------------------------------------------------------------- stage 1: drift
bold "[1/4] 브랜치 최신성"

if [ -n "$(git status --porcelain)" ]; then
    red "작업 트리에 커밋되지 않은 변경이 있습니다. 커밋하거나 stash 후 다시 실행하세요."
    git status --short
    exit 1
fi

START_BRANCH=$(git rev-parse --abbrev-ref HEAD)

for b in "${BRANCHES[@]}"; do
    if ! git rev-parse --verify --quiet "$b" >/dev/null; then
        echo "  - $b 없음 (건너뜀)"
        continue
    fi
    behind=$(git rev-list --count "$b..$BASE")
    ahead=$(git rev-list --count "$BASE..$b")
    if [ "$behind" -gt 0 ]; then
        printf '  ! %-20s %s커밋 앞섬 / %s커밋 뒤처짐  → git rebase %s %s 권장\n' \
               "$b" "$ahead" "$behind" "$BASE" "$b"
    else
        printf '  o %-20s %s커밋 앞섬 (최신)\n' "$b" "$ahead"
    fi
done

# ------------------------------------------------------------- stage 2: conflict
bold "[2/4] 병합 충돌 검사 (작업 트리 미변경)"

# merge-tree resolves the merge in memory, so a conflicting branch can be reported
# without leaving the repository half-merged.
CLEAN=()
for b in "${BRANCHES[@]}"; do
    git rev-parse --verify --quiet "$b" >/dev/null || continue
    if [ "$(git rev-list --count "$BASE..$b")" -eq 0 ]; then
        printf '  o %-20s 변경 없음\n' "$b"
        continue
    fi
    if out=$(git merge-tree --write-tree --name-only "$BASE" "$b" 2>&1); then
        printf '  o %-20s 충돌 없음\n' "$b"
        CLEAN+=("$b")
    else
        fail "$b 충돌"
        echo "$out" | tail -n +2 | sed 's/^/      /'
    fi
done

# Pairwise: two branches can each merge into main cleanly and still collide with
# each other on the same line.
for i in "${!CLEAN[@]}"; do
    for j in "${!CLEAN[@]}"; do
        [ "$i" -lt "$j" ] || continue
        a=${CLEAN[$i]}; b=${CLEAN[$j]}
        if ! out=$(git merge-tree --write-tree --name-only "$a" "$b" 2>&1); then
            fail "$a <-> $b 상호 충돌"
            echo "$out" | tail -n +2 | sed 's/^/      /'
        fi
    done
done

if [ "$FAST" -eq 1 ]; then
    echo
    if [ ${#FAILURES[@]} -eq 0 ]; then green "빠른 검사 통과 (빌드는 생략)"; exit 0
    else red "충돌 ${#FAILURES[@]}건"; exit 1; fi
fi

if [ ${#FAILURES[@]} -gt 0 ]; then
    echo
    red "충돌을 먼저 해결하세요. 빌드는 진행하지 않습니다."
    exit 1
fi

# ---------------------------------------------------------------- stage 3: build
bold "[3/4] 통합 빌드"

INTEGRATION="integration/$(date +%Y%m%d-%H%M%S)"
cleanup() {
    git merge --abort 2>/dev/null
    git checkout -q "$START_BRANCH" 2>/dev/null
    if [ "$KEEP" -eq 0 ]; then
        git branch -D "$INTEGRATION" 2>/dev/null >/dev/null
    else
        echo "통합 브랜치 유지: $INTEGRATION"
    fi
}
trap cleanup EXIT

git checkout -q -b "$INTEGRATION" "$BASE" || exit 1
for b in "${CLEAN[@]}"; do
    if git merge --no-edit -q "$b" 2>/dev/null; then
        echo "  o $b 병합"
    else
        fail "$b 병합 실패 (순차 병합에서 발생 — 2단계는 통과했음)"
        git merge --abort 2>/dev/null
        exit 1
    fi
done

[ -x "$CMAKE" ] || { red "cmake 없음: $CMAKE"; exit 1; }

if [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
    echo "  구성 중 (최초 1회, 수 분 소요)..."
    "$CMAKE" -S . -B "$BUILD_DIR" -G Ninja \
        -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_PREFIX_PATH="$QT_PREFIX" >/dev/null 2>&1 \
        || { red "구성 실패"; "$CMAKE" -S . -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH="$QT_PREFIX" 2>&1 | tail -30; exit 1; }
fi

echo "  빌드 중..."
if "$CMAKE" --build "$BUILD_DIR" > /tmp/integrate-build.log 2>&1; then
    green "  o 빌드 성공"
else
    fail "빌드 실패"
    grep -E "error|Error" /tmp/integrate-build.log | head -20 | sed 's/^/      /'
    exit 1
fi

# ---------------------------------------------------------------- stage 4: smoke
bold "[4/4] 단위 테스트 + 기동 확인"

if [ -x "$CTEST" ]; then
    if (cd "$BUILD_DIR" && "$CTEST" --output-on-failure -L Unit) > /tmp/integrate-test.log 2>&1; then
        green "  o 단위 테스트 통과"
    else
        fail "단위 테스트 실패"
        grep -E "Failed|\*\*\*" /tmp/integrate-test.log | head -15 | sed 's/^/      /'
    fi
fi

APP=$(find "$BUILD_DIR" -name QGroundControl.exe -path "*bin*" 2>/dev/null | head -1)
if [ -n "$APP" ]; then
    # A QML load failure exits 0 with no window, so the exit code proves nothing.
    # Watch stderr for the error text instead, and require the app to still be alive.
    pkill -f QGroundControl.exe 2>/dev/null
    QT_FORCE_STDERR_LOGGING=1 QT_LOGGING_RULES="*.warning=true" \
        "$APP" > /tmp/integrate-smoke.log 2>&1 &
    APP_PID=$!
    sleep "$SMOKE_SECONDS"

    if ! kill -0 "$APP_PID" 2>/dev/null; then
        fail "앱이 ${SMOKE_SECONDS}초 안에 종료됨 (QML 로드 실패 가능성 높음)"
    elif grep -qE "is not a type|Cannot assign|ReferenceError|Unable to assign|non-existent property" /tmp/integrate-smoke.log; then
        fail "QML 오류 발견"
        grep -E "is not a type|Cannot assign|ReferenceError|Unable to assign|non-existent property" \
            /tmp/integrate-smoke.log | head -10 | sed 's/^/      /'
    else
        green "  o 기동 정상"
    fi
    kill "$APP_PID" 2>/dev/null
    wait "$APP_PID" 2>/dev/null
else
    fail "실행 파일을 찾지 못함"
fi

# --------------------------------------------------------------------- summary
echo
if [ ${#FAILURES[@]} -eq 0 ]; then
    green "통합 검증 통과 — $BASE 에 병합해도 안전합니다."
    echo
    echo "  git checkout $BASE"
    for b in "${CLEAN[@]}"; do echo "  git merge --no-ff $b"; done
    exit 0
else
    red "통합 검증 실패 (${#FAILURES[@]}건)"
    printf '  - %s\n' "${FAILURES[@]}"
    echo
    echo "로그: /tmp/integrate-build.log  /tmp/integrate-test.log  /tmp/integrate-smoke.log"
    exit 1
fi
