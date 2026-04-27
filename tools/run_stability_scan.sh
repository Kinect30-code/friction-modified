#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

BUILD_DIR="${BUILD_DIR:-build}"
ASAN_BUILD_DIR="${ASAN_BUILD_DIR:-build_asan_clang}"
LOG_DIR="${LOG_DIR:-/tmp/friction_stability_scan}"
JOBS="${JOBS:-4}"
CLANG_TIDY_CHECKS="${CLANG_TIDY_CHECKS:--*,clang-analyzer-*}"

CLANG_TIDY_LOG="${LOG_DIR}/clang_tidy_focus.log"
CLANG_TIDY_FIRSTPARTY_LOG="${LOG_DIR}/clang_tidy_firstparty.log"
CPP_CHECK_LOG="${LOG_DIR}/cppcheck_firstparty.txt"
SUMMARY_LOG="${LOG_DIR}/summary.txt"

mkdir -p "$LOG_DIR"

require_file() {
    local path="$1"
    if [[ ! -f "$path" ]]; then
        echo "Missing required file: $path" >&2
        exit 1
    fi
}

run_logged() {
    local name="$1"
    shift
    local log_file="${LOG_DIR}/${name}.log"
    echo "==> ${name}" | tee "$log_file"
    "$@" 2>&1 | tee -a "$log_file"
}

require_file "${BUILD_DIR}/compile_commands.json"
require_file "${ASAN_BUILD_DIR}/compile_commands.json"

run_logged build_release \
    cmake --build "${BUILD_DIR}" --target friction -j"${JOBS}"

run_logged build_asan \
    cmake --build "${ASAN_BUILD_DIR}" --target vecb -j"${JOBS}"

if command -v clang-tidy >/dev/null 2>&1; then
    : > "${CLANG_TIDY_LOG}"
    declare -a TIDY_FILES=(
        "src/app/renderhandler.cpp"
        "src/app/memorychecker.cpp"
        "src/core/canvas.cpp"
        "src/core/videoencoder.cpp"
        "src/core/hardwareinfo.cpp"
        "src/core/CacheHandlers/imagecachecontainer.cpp"
        "src/core/CacheHandlers/sceneframecontainer.cpp"
        "src/core/FileCacheHandlers/videocachehandler.cpp"
        "src/core/FileCacheHandlers/videoframeloader.cpp"
        "src/core/smartPointers/eobject.h"
        "src/core/smartPointers/selfref.h"
        "src/core/smartPointers/stdselfref.h"
    )

    for file in "${TIDY_FILES[@]}"; do
        echo "==> clang-tidy ${file}" | tee -a "${CLANG_TIDY_LOG}"
        clang-tidy \
            -quiet \
            -p "${BUILD_DIR}" \
            "${ROOT_DIR}/${file}" \
            -header-filter='.*src/(app|core|ui)/' \
            -checks="${CLANG_TIDY_CHECKS}" \
            >> "${CLANG_TIDY_LOG}" 2>&1 || true
    done
else
    echo "clang-tidy not found, skipping focused analyzer pass" \
        | tee "${CLANG_TIDY_LOG}"
fi

if [[ -f "${CLANG_TIDY_LOG}" ]]; then
    awk -v root="${ROOT_DIR}" '
        index($0, root "/src/") == 1 &&
        $0 ~ /warning:/ &&
        $0 !~ /\/skia\// {
            print
        }
    ' "${CLANG_TIDY_LOG}" > "${CLANG_TIDY_FIRSTPARTY_LOG}" || true
fi

if command -v cppcheck >/dev/null 2>&1; then
    OUTPUT_FILE="${CPP_CHECK_LOG}" BUILD_DIR="${BUILD_DIR}" \
        tools/run_cppcheck_focus.sh firstparty || true
else
    echo "cppcheck not found, skipping first-party cppcheck pass" \
        | tee "${CPP_CHECK_LOG}"
fi

release_warning_count="$(rg -c 'warning:' "${LOG_DIR}/build_release.log" || echo 0)"
asan_warning_count="$(rg -c 'warning:' "${LOG_DIR}/build_asan.log" || echo 0)"
tidy_warning_count="$(wc -l < "${CLANG_TIDY_FIRSTPARTY_LOG}" 2>/dev/null || echo 0)"
cppcheck_issue_count="$(wc -l < "${CPP_CHECK_LOG}" 2>/dev/null || echo 0)"

cat > "${SUMMARY_LOG}" <<EOF
Stability scan completed.

Build directory: ${BUILD_DIR}
ASan build directory: ${ASAN_BUILD_DIR}
Log directory: ${LOG_DIR}

Release build warnings: ${release_warning_count}
ASan build warnings: ${asan_warning_count}
Focused clang-tidy first-party warnings: ${tidy_warning_count}
Filtered cppcheck issues: ${cppcheck_issue_count}

Interpretation:
- build steps are hard blockers
- clang-tidy and cppcheck counts are advisory and must be reviewed manually
- full clang-tidy log still contains Qt and Skia note stacks
- first-party clang-tidy count excludes Skia/system-header warning noise

Logs:
- ${LOG_DIR}/build_release.log
- ${LOG_DIR}/build_asan.log
- ${CLANG_TIDY_LOG}
- ${CLANG_TIDY_FIRSTPARTY_LOG}
- ${CPP_CHECK_LOG}
EOF

cat "${SUMMARY_LOG}"
