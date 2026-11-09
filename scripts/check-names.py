#!/usr/bin/env python3
"""Fail when a forbidden word is in the tree or in commit messages.

A word is a run of letters and digits, compared in lower case. It is
forbidden when its SHA-256 is listed in scripts/forbidden-words.sha256, so
the list never names the words, and neither does the output: it says where
a word was found, with stars in its place in file names. The check reads
the text of the tracked files, the tracked file names and the names of the
untracked files under docs/; with --commits A..B, it reads the messages of
those commits instead. Run it from the repository root.

Commit hashes and file paths of other projects are not words: new text is
still read by hand for those before a merge.
"""

import argparse
import hashlib
import pathlib
import re
import subprocess
import sys


LIST = pathlib.Path(__file__).resolve().parent / "forbidden-words.sha256"
WORD = re.compile(rb"[a-z0-9]+")
BINARY_PROBE = 8192


def load_hashes(path):
    """The hex digests of path: one per line, # starts a comment."""
    hashes = set()
    lines = pathlib.Path(path).read_text(encoding="utf-8").splitlines()
    for number, line in enumerate(lines, 1):
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        if not re.fullmatch(r"[0-9a-f]{64}", line):
            raise ValueError(f"{path} line {number}: expected a lower-case "
                             "SHA-256 in hex")
        hashes.add(line)
    return hashes


class Matcher:
    """Finds forbidden words, hashing each distinct word once."""

    def __init__(self, hashes):
        self.hashes = hashes
        self.known = {}

    def forbidden(self, word):
        if word not in self.known:
            self.known[word] = \
             hashlib.sha256(word).hexdigest() in self.hashes
        return self.known[word]

    def found(self, data):
        """The forbidden words of data (bytes), whatever their case."""
        return {word for word in set(WORD.findall(data.lower()))
                if self.forbidden(word)}

    def lines(self, data):
        """The numbers of the lines of data that hold a forbidden word."""
        if not self.found(data):
            return []
        return [number for number, line in enumerate(data.split(b"\n"), 1)
                if self.found(line)]

    def mask(self, name):
        """name with each forbidden word replaced by stars."""
        return re.sub(r"[A-Za-z0-9]+", lambda match: "*" * len(match[0])
                      if self.forbidden(match[0].lower().encode())
                      else match[0], name)


def git(repo, *args):
    result = subprocess.run(["git", "-C", str(repo), *args],
                            capture_output=True)
    if result.returncode:
        raise ValueError(f"git {' '.join(args)}: "
                         f"{result.stderr.decode(errors='replace').strip()}")
    return result.stdout


def names(repo, *args):
    return [name for name in git(repo, "ls-files", "-z", *args)
            .decode("utf-8", "replace").split("\0") if name]


def tree_hits(repo, matcher):
    """Where a forbidden word is in a tracked file, a tracked file name or
    the name of an untracked file under docs/."""
    hits = []
    tracked = names(repo)
    for name in tracked + names(repo, "--others", "--exclude-standard",
                                "--", "docs/"):
        if matcher.found(name.encode()):
            hits.append(f"{matcher.mask(name)}: in the file name")
    for name in tracked:
        path = pathlib.Path(repo) / name
        if not path.is_file():
            continue
        data = path.read_bytes()
        if b"\0" in data[:BINARY_PROBE]:
            continue
        hits += [f"{matcher.mask(name)}:{number}"
                 for number in matcher.lines(data)]
    return hits


def commit_hits(repo, revisions, matcher):
    """Where a forbidden word is in the messages of the commits revisions
    (a git log range such as 1.4.3..HEAD) selects."""
    hits = []
    log = git(repo, "log", "-z", "--format=%h%n%B", revisions)
    for entry in filter(None, log.split(b"\0")):
        commit, _, message = entry.partition(b"\n")
        hits += [f"commit {commit.decode()}: message line {number}"
                 for number in matcher.lines(message)]
    return hits


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--commits", metavar="A..B",
                        help="check these commit messages, not the tree")
    parser.add_argument("--list", default=LIST, type=pathlib.Path,
                        help="the hash list (default: %(default)s)")
    args = parser.parse_args(argv)
    try:
        matcher = Matcher(load_hashes(args.list))
        hits = (commit_hits(".", args.commits, matcher) if args.commits
                else tree_hits(".", matcher))
    except (OSError, ValueError) as error:
        print(f"check-names: {error}", file=sys.stderr)
        return 1
    if hits:
        for hit in hits:
            print(f"Forbidden word: {hit}", file=sys.stderr)
        return 1
    print("Forbidden-name check passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
