#!/usr/bin/env bash
set -euo pipefail

BINARY="$(cd "$(dirname "$0")/.." && pwd)/src/jsonbench"
TESTS_DIR="$(cd "$(dirname "$0")" && pwd)"
TMPDIR_LOCAL=$(mktemp -d)
trap 'rm -rf "$TMPDIR_LOCAL"' EXIT

if [[ ! -x "$BINARY" ]]; then
    echo "ERROR: binary not found or not executable: $BINARY"
    echo "       Run './autogen.sh && ./configure && make' first"
    exit 1
fi

# Discover available engines from jsonbench -h output
ENGINES=()
while IFS= read -r line; do
    if [[ "$line" =~ [[:space:]]-[[:space:]]([A-Z]+)$ ]]; then
        ENGINES+=("${BASH_REMATCH[1]}")
    fi
done < <("$BINARY" -h 2>&1 || true)

if [[ ${#ENGINES[@]} -eq 0 ]]; then
    echo "ERROR: no engines found in '$BINARY -h' output"
    exit 1
fi

# Optional: filter to a single engine
if [[ "${1:-}" != "" ]]; then
    ENGINE_FILTER="$1"
    FOUND=0
    for e in "${ENGINES[@]}"; do
        [[ "$e" == "$ENGINE_FILTER" ]] && FOUND=1
    done
    if [[ "$FOUND" -eq 0 ]]; then
        echo "ERROR: engine '$ENGINE_FILTER' is not available (available: ${ENGINES[*]})"
        exit 1
    fi
    ENGINES=("$ENGINE_FILTER")
fi

PASS=0
FAIL=0
SKIP=0

# Valid JSON fixtures; test05.json is intentionally invalid and excluded here
VALID_FIXTURES=(test01.json test02.json test03.json test04.json test06.json 512KB.json)
INVALID_FIXTURES=(test05.json)

# Use YAJL as the reference engine for output consistency; fall back to first available
REFERENCE_ENGINE="${ENGINES[0]}"
for e in "${ENGINES[@]}"; do
    if [[ "$e" == "YAJL" ]]; then
        REFERENCE_ENGINE="YAJL"
        break
    fi
done

# Generate reference output for each valid fixture
echo "=== Reference engine: $REFERENCE_ENGINE ==="
for fixture in "${VALID_FIXTURES[@]}"; do
    "$BINARY" -e "$REFERENCE_ENGINE" -a 9999 "$TESTS_DIR/$fixture" \
        > "$TMPDIR_LOCAL/ref_${fixture}.raw" 2>/dev/null
    grep -Ev "^(Time:|$)" "$TMPDIR_LOCAL/ref_${fixture}.raw" \
        > "$TMPDIR_LOCAL/ref_${fixture}.out" || true
done
echo ""

# --- Helpers ---

pass() { echo "PASS  $1"; PASS=$((PASS + 1)); }
fail() { echo "FAIL  $1"; FAIL=$((FAIL + 1)); }
skip() { echo "SKIP  $1"; SKIP=$((SKIP + 1)); }

# Run a parse that must succeed (exit 0)
check_valid() {
    local engine="$1" fixture="$2"
    local label="[$engine] valid parse: $fixture"
    local rc=0
    "$BINARY" -e "$engine" -s -a 9999 "$TESTS_DIR/$fixture" \
        > "$TMPDIR_LOCAL/out.txt" 2>&1 || rc=$?
    if [[ "$rc" -eq 0 ]]; then pass "$label"; else fail "$label"; fi
}

# Check that non-silent output matches the reference engine's output
check_consistency() {
    local engine="$1" fixture="$2"
    local label="[$engine] output consistency: $fixture"
    local rc=0
    "$BINARY" -e "$engine" -a 9999 "$TESTS_DIR/$fixture" \
        > "$TMPDIR_LOCAL/cmp_${engine}_${fixture}.raw" 2>/dev/null || rc=$?
    grep -Ev "^(Time:|$)" "$TMPDIR_LOCAL/cmp_${engine}_${fixture}.raw" \
        > "$TMPDIR_LOCAL/cmp_${engine}_${fixture}.out" || true
    if [[ "$rc" -ne 0 ]]; then
        fail "$label (parse failed)"
        return
    fi
    if [[ ! -s "$TMPDIR_LOCAL/cmp_${engine}_${fixture}.out" ]]; then
        skip "$label (engine produces no field output)"
        return
    fi
    if diff -q "$TMPDIR_LOCAL/ref_${fixture}.out" \
              "$TMPDIR_LOCAL/cmp_${engine}_${fixture}.out" > /dev/null 2>&1; then
        pass "$label"
    else
        fail "$label"
        diff "$TMPDIR_LOCAL/ref_${fixture}.out" \
             "$TMPDIR_LOCAL/cmp_${engine}_${fixture}.out" | head -20
    fi
}

# Run a parse that must fail (non-zero exit); handles "does not support" gracefully
# Usage: check_rejected engine label [extra args...]
check_rejected() {
    local engine="$1" label="$2"
    shift 2
    local rc=0
    "$BINARY" -e "$engine" -s "$@" \
        > "$TMPDIR_LOCAL/out.txt" 2>"$TMPDIR_LOCAL/err.txt" || rc=$?
    if [[ "$rc" -ne 0 ]]; then
        pass "[$engine] $label"
    elif grep -q "does not support" "$TMPDIR_LOCAL/err.txt" 2>/dev/null; then
        skip "[$engine] $label (engine does not support this limit)"
    else
        fail "[$engine] $label (expected non-zero exit, got 0)"
    fi
}

# --- Tests ---

for engine in "${ENGINES[@]}"; do
    # 1. Valid parse
    for fixture in "${VALID_FIXTURES[@]}"; do
        check_valid "$engine" "$fixture"
    done

    # 2. Output consistency vs reference (skip self-comparison)
    if [[ "$engine" != "$REFERENCE_ENGINE" ]]; then
        for fixture in "${VALID_FIXTURES[@]}"; do
            check_consistency "$engine" "$fixture"
        done
    fi

    # 3. Invalid JSON must be rejected
    for fixture in "${INVALID_FIXTURES[@]}"; do
        check_rejected "$engine" "rejects invalid JSON: $fixture" \
            "$TESTS_DIR/$fixture"
    done

    # 4. Depth limit enforcement
    check_rejected "$engine" "depth limit (-d 1) on test06.json" \
        -d 1 "$TESTS_DIR/test06.json"

    # 5. Arg limit enforcement
    check_rejected "$engine" "arg limit (-a 1) on test06.json" \
        -a 1 "$TESTS_DIR/test06.json"
done

# --- Summary ---
echo ""
echo "=== Results: $PASS passed, $FAIL failed, $SKIP skipped ==="

# --- Timing table ---
BENCH_N=20
echo ""
echo "=== Timing: mean of ${BENCH_N} runs (seconds, silent mode) ==="
echo ""

printf "%-20s" "Fixture"
for engine in "${ENGINES[@]}"; do
    printf "%12s" "$engine"
done
printf "\n"
printf "%-20s" "--------------------"
for engine in "${ENGINES[@]}"; do
    printf "%12s" "------------"
done
printf "\n"

for fixture in "${VALID_FIXTURES[@]}"; do
    printf "%-20s" "$fixture"
    for engine in "${ENGINES[@]}"; do
        raw=$("$BINARY" -e "$engine" -s -a 9999 -n "$BENCH_N" \
            "$TESTS_DIR/$fixture" 2>/dev/null) || true
        mean=$(printf '%s\n' "$raw" | grep "^Time:" | grep -oE "mean=[0-9.]+" | sed 's/mean=//')
        printf "%12s" "${mean:-ERR}"
    done
    printf "\n"
done
echo ""

[[ "$FAIL" -eq 0 ]]
