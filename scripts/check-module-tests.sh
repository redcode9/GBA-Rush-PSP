#!/bin/sh

set -eu

# Part of make tree-check: every pure module, a source/src/*.c that includes
# neither common.h nor a psp* or kubridge header, has its host test
# tests/test_<module>.c, listed in HOST_TESTS and HOST_SANITIZE_TESTS and run
# by the test and test-sanitize recipes. scripts/makefile_tests.py reads the
# Makefile. The argument is the repository root, "." by default; PYTHON names
# the interpreter, python3 by default.

cd "${1:-.}"
entries=$("${PYTHON:-python3}" scripts/makefile_tests.py Makefile)

# The test that covers a module: test_<module> unless listed here, and "-"
# for a file that is not a module of its own.
covering_test()
{
  case $1 in
    # test_arm_core builds all three through ARM_CORE_SOURCES.
    arm_decode|arm_interp|arm_mips) echo arm_core ;;
    # test_runtime_log builds it with runtime_log.c.
    runtime_log_store) echo runtime_log ;;
    # The main menu categories, checked by test_menu_model.
    menu_catalog) echo menu_model ;;
    # Vendored font tables, #included by ch_print.c and mh_print.c.
    gbkfont|shinonomefont) echo - ;;
    # Icon data #included by psp_video.c.
    volume_icon) echo - ;;
    # Builds video.c, which includes common.h, as the old renderer.
    video_old_wrap) echo - ;;
    *) echo "$1" ;;
  esac
}

include='^[[:space:]]*#[[:space:]]*include[[:space:]]*'
newline='
'
failed=0
for source in source/src/*.c
do
  if grep -Eq "$include"'["<](common\.h|psp|ku_?bridge)' "$source"; then
    continue
  fi
  module=${source##*/}
  module=${module%.c}
  test=$(covering_test "$module")
  if [ "$test" = - ]; then
    continue
  fi
  missing=
  if [ ! -f "tests/test_$test.c" ]; then
    missing=" tests/test_$test.c"
  fi
  for place in HOST_TESTS HOST_SANITIZE_TESTS test test-sanitize
  do
    case "$newline$entries$newline" in
      *"${newline}$place test_$test$newline"*) ;;
      *) missing="$missing $place" ;;
    esac
  done
  if [ -n "$missing" ]; then
    echo "Pure module $module has no complete host test test_$test," \
     "missing:$missing" >&2
    failed=1
  fi
done

if [ "$failed" -ne 0 ]; then
  echo "HOST_TESTS and HOST_SANITIZE_TESTS are Makefile variables, test" \
   "and test-sanitize its recipes; the Makefile header says how to add" \
   "a C test." >&2
  exit 1
fi
echo "Module test checks passed"
