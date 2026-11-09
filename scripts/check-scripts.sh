#!/bin/sh

set -eu

# Part of make tree-check: every shell script, Python tool and test, and
# GitHub workflow must at least parse. The argument is the repository root,
# "." by default, so tests/test_check_scripts.py can point it at a scratch
# tree. PYTHON names the interpreter, python3 by default.

cd "${1:-.}"
python=${PYTHON:-python3}

for script in scripts/*.sh
do
  sh -n "$script"
done

temporary_root=$(mktemp -d "${TMPDIR:-/tmp}/gba-rush-scripts.XXXXXX")
trap 'rm -rf "$temporary_root"' EXIT HUP INT TERM

# A Git pathspec * also crosses directories, so tests/rom_link/*.py is
# included. The bytecode goes to the temporary folder, not into the tree.
git ls-files 'scripts/*.py' 'tools/*.py' 'tests/*.py' \
 'source/single-game_launcher/tool/*.py' >"$temporary_root/python.txt"
PYTHONPYCACHEPREFIX="$temporary_root" "$python" -m py_compile - \
 <"$temporary_root/python.txt"

git ls-files '.github/*.yml' '.github/*.yaml' >"$temporary_root/yaml.txt"
if [ -s "$temporary_root/yaml.txt" ]; then
  if "$python" -c 'import yaml' 2>/dev/null; then
    "$python" -c '
import sys
import yaml
for path in sys.stdin.read().splitlines():
    try:
        with open(path, encoding="utf-8") as stream:
            yaml.safe_load(stream)
    except yaml.YAMLError as error:
        sys.exit(f"{path}: {error}")
' <"$temporary_root/yaml.txt"
  else
    echo "YAML check skipped: PyYAML missing"
  fi
fi

echo "Script syntax checks passed"
