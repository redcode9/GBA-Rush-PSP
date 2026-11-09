#!/usr/bin/env python3
"""Read the host C tests that the top-level Makefile lists.

A C test is named in four places: the HOST_TESTS and HOST_SANITIZE_TESTS
variables and the commands of the test and test-sanitize recipes. entries()
gives each place's tests as test_<name>, found by expanding the Makefile's
own variables: a variable's name need not match its binary's
(HOST_TEST_FAST_FORWARD builds test_fast_forward_policy).

Only what this Makefile uses is understood: continued lines, NAME = value
assignments (=, :=, ::=, ?=, +=), $(NAME), ${NAME} and $$, and rules whose
recipe lines start with a tab. Functions such as $(shell ...) stay as they
are. check-module-tests.sh runs it as a script, which prints one
"<place> <test>" line per entry; standard library only.
"""

import argparse
import pathlib
import posixpath
import re
import sys


LISTS = ("HOST_TESTS", "HOST_SANITIZE_TESTS")
RECIPES = ("test", "test-sanitize")
ASSIGNMENT = re.compile(r"([A-Za-z_][\w.-]*)\s*(\?=|::=|:=|\+=|=)\s*(.*)")
REFERENCE = re.compile(r"\$(?:(\$)|\(([\w.-]+)\)|\{([\w.-]+)\})")


def parse(text):
    """The Makefile's variables, unexpanded, and each target's recipe, as
    {name: value} and {target: [command, ...]}."""
    variables = {}
    recipes = {}
    recipe = None
    # A backslash-newline and the blanks around it read as one space.
    for line in re.sub(r"[ \t]*\\\n[ \t]*", " ", text).splitlines():
        if line.startswith("\t"):
            if recipe is not None and line.strip():
                recipe.append(line.strip())
            continue
        # As in make, blank and comment lines do not end a recipe.
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        recipe = None
        assignment = ASSIGNMENT.fullmatch(line.strip())
        if assignment:
            name, operator, value = assignment.groups()
            if operator == "?=":
                variables.setdefault(name, value)
            elif operator == "+=" and name in variables:
                variables[name] += " " + value
            else:
                variables[name] = value
            continue
        targets, colon, _ = line.partition(":")
        if colon:
            recipe = []
            for target in expand(targets, variables).split():
                recipes[target] = recipe
    return variables, recipes


def expand(value, variables, depth=0):
    """value with every variable reference replaced, recursively. An unknown
    variable is empty, as in make."""
    if depth > 50:
        raise ValueError(f"a variable refers to itself: {value}")

    def replace(match):
        if match.group(1):
            return "$"
        name = match.group(2) or match.group(3)
        return expand(variables.get(name, ""), variables, depth + 1)

    return REFERENCE.sub(replace, value)


def entries(text):
    """{place: [test_<name>, ...]} for the two lists and the two recipes, in
    Makefile order. A recipe's test is its command's first word."""
    variables, recipes = parse(text)
    found = {}
    for name in LISTS:
        if name not in variables:
            raise ValueError(f"the Makefile does not set {name}")
        found[name] = expand(variables[name], variables).split()
    for target in RECIPES:
        if target not in recipes:
            raise ValueError(f"the Makefile has no {target} rule")
        found[target] = [word for command in recipes[target]
                         for word in expand(command, variables)
                         .lstrip("@+-").split()[:1]]
    return {place: [posixpath.basename(word) for word in words]
            for place, words in found.items()}


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("makefile", nargs="?", default="Makefile")
    arguments = parser.parse_args()
    try:
        found = entries(pathlib.Path(arguments.makefile)
                        .read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        print(f"makefile_tests: {error}", file=sys.stderr)
        return 1
    # LF on Windows too: the shell matches whole lines.
    sys.stdout.reconfigure(newline="\n")
    for place, tests in found.items():
        for test in tests:
            print(place, test)
    return 0


if __name__ == "__main__":
    sys.exit(main())
