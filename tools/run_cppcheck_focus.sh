#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

PROFILE="${1:-gui-timeline}"
BUILD_DIR="${BUILD_DIR:-build}"
CACHE_DIR="${CACHE_DIR:-.cppcheck-cache}"
OUTPUT_FILE="${OUTPUT_FILE:-/tmp/friction_cppcheck_${PROFILE}.txt}"
RAW_FILE="${OUTPUT_FILE%.txt}.raw.txt"
SUPPRESSIONS_FILE="tools/cppcheck/suppressions.txt"

if [[ ! -f "$BUILD_DIR/compile_commands.json" ]]; then
    echo "Missing $BUILD_DIR/compile_commands.json" >&2
    echo "Run: cmake -S . -B $BUILD_DIR -DCMAKE_EXPORT_COMPILE_COMMANDS=ON" >&2
    exit 1
fi

mkdir -p "$CACHE_DIR"

declare -a FILTERS
case "$PROFILE" in
    gui-timeline)
        FILTERS=(
            "src/app/GUI/mainwindow.cpp"
            "src/app/GUI/timelinedockwidget.cpp"
            "src/app/GUI/timelinewidget.cpp"
            "src/app/GUI/keysview.cpp"
            "src/app/GUI/graphboxeslist.cpp"
            "src/app/GUI/BoxesList/boxscrollwidget.cpp"
            "src/app/GUI/BoxesList/boxsinglewidget.cpp"
        )
        ;;
    gui)
        FILTERS=("src/app/GUI/*")
        ;;
    firstparty)
        FILTERS=("src/app/*" "src/core/*" "src/ui/*")
        ;;
    *)
        echo "Unknown profile: $PROFILE" >&2
        echo "Available profiles: gui-timeline, gui, firstparty" >&2
        exit 1
        ;;
esac

CMD=(
    cppcheck
    "--project=${BUILD_DIR}/compile_commands.json"
    "--enable=warning,style,performance,portability"
    "--check-level=normal"
    "--inline-suppr"
    "--quiet"
    "--cppcheck-build-dir=${CACHE_DIR}"
    "--suppressions-list=${SUPPRESSIONS_FILE}"
    "--output-file=${RAW_FILE}"
)

for filter in "${FILTERS[@]}"; do
    CMD+=("--file-filter=${filter}")
done

"${CMD[@]}" || true

if ! rg '^src/(app|core|ui)/' "$RAW_FILE" > "$OUTPUT_FILE"; then
    : > "$OUTPUT_FILE"
fi

echo "cppcheck profile: $PROFILE"
echo "filtered report: $OUTPUT_FILE"
echo "raw report: $RAW_FILE"
echo "issue count: $(wc -l < "$OUTPUT_FILE")"
