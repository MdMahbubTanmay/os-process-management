#!/usr/bin/env bash
# Test suite for procmon. Each test checks procmon's own exit code.
cd "$(dirname "$0")/.." || exit 1
mkdir -p logs
LOG=logs/test_log.txt
: > "$LOG"
pass=0; fail=0

check() {  # name expected_rc procmon-args...
  local name="$1" expect="$2"; shift 2
  echo "=== TEST: $name ==="
  ./procmon -l "$LOG" "$@"
  local rc=$?
  if [ "$rc" -eq "$expect" ]; then echo ">>> PASS ($name): rc=$rc"; pass=$((pass+1))
  else echo ">>> FAIL ($name): rc=$rc expected=$expect"; fail=$((fail+1)); fi
  echo
}

check "T1 normal exit (ls)"                  0   ls -l /tmp
check "T2 non-zero exit status (exit 3)"     3   sh -c 'exit 3'
check "T3 exec failure (bad command)"        127 no_such_program_xyz
check "T4 killed by SIGSEGV"                 139 sh -c 'kill -SEGV $$'
check "T5 killed by SIGKILL"                 137 sh -c 'kill -KILL $$'
check "T6 boundary: command, no arguments"   0   true
check "T7 timeout kills long child"          143 -t 2 sleep 30
check "T8 child ignores SIGTERM -> SIGKILL"  137 -t 1 sh -c 'trap "" TERM; sleep 30'
check "T9 child finishes before timeout"     0   -t 5 sleep 1

echo "=== TEST: T10 invalid usage (no command) ==="
./procmon 2>/dev/null; rc=$?
if [ "$rc" -eq 2 ]; then echo ">>> PASS (T10): rc=$rc"; pass=$((pass+1)); else echo ">>> FAIL (T10): rc=$rc"; fail=$((fail+1)); fi

echo
echo "SUMMARY: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
