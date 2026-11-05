#!/usr/bin/env python3
"""Check a release candidate and write its release notes (make release-gate).

docs/releases/<version>.txt says what the release needs. The stable channel
passes only when, for the EBOOT.PBP of the candidate's manifest:
- every required test has a pass row in docs/HARDWARE-RESULTS.md on every
  model class the test catalog asks for;
- the newest <version>-rc.N tag is at least 7 days old, or 72 hours for a
  release that fixes data loss (hotfix-data: yes);
- nothing under source/ changed since that tag;
- if the link code changed since the previous release, the release file
  declares the link status, and a changed protocol constant has a link test
  that passed.
The prerelease channel always passes. Both write
dist/RELEASE-NOTES-<version>.md; the CHANGELOG stays hand-written.
"""

import argparse
import importlib.util
import pathlib
import re
import subprocess
import sys
import time

import release_manifest


SCRIPTS = pathlib.Path(__file__).resolve().parent
ROOT = SCRIPTS.parent

# The script name has a dash, so load it as a module by path.
_SPEC = importlib.util.spec_from_file_location(
 "hardware_session", SCRIPTS / "hardware-session.py")
hardware_session = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(hardware_session)

CHANNELS = ("stable", "prerelease")
RELEASE_KEYS = ("tests", "hotfix-data", "known-limit", "link", "mixed-pair")
LINK_STATES = ("wire-unchanged", "wire-extended", "bump")
LINK_FILES = ("source/src/link_net.*", "source/src/link_cable.*",
              "source/src/gba_sio.*", "source/src/adhoc_peer.*",
              "source/src/psp_adhoc.c")
# What two PSPs compare before they link.
LINK_CONSTANTS = (("source/src/link_net.h", "LINK_NET_VERSION"),
                  ("source/src/link_cable.h", "LINK_CABLE_PROTOCOL"))
SOAK_HOURS = 7 * 24
HOTFIX_SOAK_HOURS = 72
COMPAT_LINE = re.compile(r"(\S+) with (\S+): (.+)")


def version_key(name):
    """(X, Y, Z) for a release name X.Y.Z, None for anything else."""
    match = re.fullmatch(r"(\d+)\.(\d+)\.(\d+)", name)
    return tuple(map(int, match.groups())) if match else None


def git(repo, *args, check=True):
    result = subprocess.run(["git", "-C", str(repo), *args],
                            capture_output=True, encoding="utf-8",
                            errors="replace")
    if check and result.returncode:
        raise ValueError(f"git {' '.join(args)}: {result.stderr.strip()}")
    return result


def parse_release(text):
    """docs/releases/<version>.txt as {key: [values]}; # starts a comment."""
    release = {key: [] for key in RELEASE_KEYS}
    for number, line in enumerate(text.splitlines(), 1):
        if not line.strip() or line.startswith("#"):
            continue
        key, colon, value = line.partition(":")
        if not colon or key.strip() not in release:
            raise ValueError(f"line {number}: expected one of "
                             f"{', '.join(RELEASE_KEYS)} and a colon")
        release[key.strip()].append(value.strip())
    for key, allowed in (("hotfix-data", ("yes", "no")), ("link", LINK_STATES)):
        if len(release[key]) > 1 or not set(release[key]) <= set(allowed):
            raise ValueError(f"{key}: give one of {', '.join(allowed)}")
    return release


def parse_releases(text):
    """(low, high) keys of 1.4.0, of the range 1.4.1-1.4.2, or of 1.4.1+
    (high None: every later release)."""
    if text.endswith("+"):
        low, high = version_key(text[:-1]), None
        valid = low is not None
    else:
        first, dash, last = text.partition("-")
        low = version_key(first)
        high = version_key(last) if dash else low
        valid = low is not None and high is not None
    if not valid:
        raise ValueError(f"'{text}' is not a release, a range or a release+")
    return low, high


def covers(bounds, key):
    return bounds[0] <= key and (bounds[1] is None or key <= bounds[1])


def parse_compat(text):
    """docs/releases/link-compat.txt as (releases, releases, text, line)."""
    facts = []
    for number, line in enumerate(text.splitlines(), 1):
        if not line.strip() or line.startswith("#"):
            continue
        match = COMPAT_LINE.fullmatch(line.strip())
        if not match:
            raise ValueError(f"link-compat.txt line {number}: expected "
                             "'<releases> with <releases>: <text>'")
        try:
            facts.append((parse_releases(match.group(1)),
                          parse_releases(match.group(2)), match.group(3),
                          line.strip()))
        except ValueError as error:
            raise ValueError(f"link-compat.txt line {number}: {error}") \
             from None
    return facts


def release_tags(repo):
    """The X.Y.Z tags, oldest release first."""
    names = git(repo, "tag", "--list").stdout.split()
    return sorted(filter(version_key, names), key=version_key)


def link_constants(repo, ref):
    """The values of LINK_CONSTANTS at ref, or None before the link cable."""
    values = []
    for path, name in LINK_CONSTANTS:
        shown = git(repo, "show", f"{ref}:{path}", check=False)
        if shown.returncode:
            return None
        match = re.search(rf"^#define\s+{name}\s+(\d+)u?\b", shown.stdout,
                          re.MULTILINE)
        if not match:
            raise ValueError(f"{ref}:{path} does not define {name}")
        values.append(int(match.group(1)))
    return tuple(values)


def rc_tags(repo, version):
    """(tag, unix time, object type) of each <version>-rc.N tag, newest
    first. Only an annotated tag (type "tag") has a date of its own; a
    lightweight one (type "commit") gives its commit's date."""
    listed = git(repo, "for-each-ref", "--sort=-creatordate",
                 "--format=%(creatordate:unix) %(objecttype) "
                 "%(refname:lstrip=2)",
                 f"refs/tags/{version}-rc.*").stdout.splitlines()
    tags = []
    for line in listed:
        created, kind, tag = line.split(" ", 2)
        if re.fullmatch(re.escape(version) + r"-rc\.\d+", tag):
            tags.append((tag, int(created), kind))
    return tags


def model_classes(model):
    """The catalog's model classes a results row's Model cell covers."""
    return {"PSP-1000" if name == "PSP-1000" else "second model"
            for name in model.split(" + ")}


def missing_classes(test_id, catalog, passed):
    """The model classes the catalog asks of test_id that no pass row
    covers."""
    covered = set().union(*(model_classes(row["Model"]) for row in passed
                            if row["Test id"] == test_id))
    return [c for c in catalog[test_id]["Models"] if c not in covered]


def required_test_problems(tests, catalog, passed, digest):
    problems = []
    for test_id in tests:
        missing = missing_classes(test_id, catalog, passed)
        if missing:
            problems.append(f"{test_id}: no pass row for EBOOT {digest} on "
                            f"{', '.join(missing)}")
    return problems


def candidate_problems(repo, version, release, now):
    """The soak since the newest rc tag, and source/ unchanged since it."""
    tags = rc_tags(repo, version)
    if not tags:
        return [f"no {version}-rc.N tag: a stable release is a prerelease "
                "that went through its soak"]
    lightweight = [tag for tag, _, kind in tags if kind != "tag"]
    if lightweight:
        return [f"{', '.join(lightweight)}: a lightweight tag has only its "
                "commit's date; recreate it with git tag -a, whose date "
                "starts the soak"]
    tag, created, _ = tags[0]
    hotfix = release["hotfix-data"] == ["yes"]
    needed = HOTFIX_SOAK_HOURS if hotfix else SOAK_HOURS
    problems = []
    elapsed = now - created
    if elapsed < needed * 3600:
        problems.append(f"soak: {elapsed // 3600} h since {tag}, {needed} h "
                        "needed" + (" for a data hotfix" if hotfix else ""))
    diff = git(repo, "diff", "--quiet", tag, "HEAD", "--", "source/",
               check=False)
    if diff.returncode == 1:
        problems.append(f"source/ changed since {tag}")
    elif diff.returncode:
        raise ValueError(f"git diff {tag} HEAD: {diff.stderr.strip()}")
    return problems


def link_problems(repo, version, release, passed):
    """The link status a change of the link code since the previous release
    must declare, and the link test a changed protocol constant needs."""
    earlier = [t for t in release_tags(repo)
               if version_key(t) < version_key(version)]
    if not earlier:
        return []
    previous = earlier[-1]
    changed = git(repo, "diff", "--name-only", previous, "HEAD", "--",
                  *LINK_FILES).stdout.split()
    if not changed:
        return []
    problems = []
    name = f"docs/releases/{version}.txt"
    if not release["link"]:
        problems.append(f"link code changed since {previous} "
                        f"({', '.join(changed)}): {name} must declare "
                        f"link: {', '.join(LINK_STATES)}")
    elif release["link"] == ["wire-extended"] and not any(release["mixed-pair"]):
        problems.append(f"link: wire-extended needs a mixed-pair: line in "
                        f"{name} saying what a mixed pair loses")
    old = link_constants(repo, previous) or (None,) * len(LINK_CONSTANTS)
    new = link_constants(repo, "HEAD") or (None,) * len(LINK_CONSTANTS)
    bumped = [constant for (_, constant), a, b in zip(LINK_CONSTANTS, old, new)
              if a != b]
    if bumped and not any(row["Test id"] == "ppsspp-link-pair"
                          or row["Test id"].startswith("link-")
                          for row in passed):
        problems.append(f"{' and '.join(bumped)} changed since {previous}: "
                        "needs a pass row of ppsspp-link-pair or of a link-* "
                        "test for this EBOOT")
    return problems


def performance_problems(repo, digest):
    """PRF-1.6 runs compare-benchmarks' group check here; nothing yet."""
    return []


def link_section(repo, version, release, facts):
    """The "Links with" lines: the earlier releases whose protocol constants
    match the candidate's and that no "no link" fact excludes, the others,
    and every fact that names a release the candidate links with."""
    mine, key = link_constants(repo, "HEAD"), version_key(version)
    linked, unlinked = [], []
    for tag in release_tags(repo):
        theirs, their_key = link_constants(repo, tag), version_key(tag)
        if theirs is None or their_key >= key:
            continue
        excluded = any(
         text.startswith("no link")
         and (covers(a, key) and covers(b, their_key)
              or covers(b, key) and covers(a, their_key))
         for a, b, text, _ in facts)
        (unlinked if excluded or theirs != mine else linked).append(tag)
    lines = [f"- Links with {', '.join(linked) or 'no earlier release'} "
             f"(link version {mine[0]}, cable protocol {mine[1]})."]
    if unlinked:
        lines.append(f"- Does not link with {', '.join(unlinked)}.")
    lines += [f"- {line}" for a, b, _, line in facts
              if any(covers(a, version_key(t)) or covers(b, version_key(t))
                     for t in linked)]
    if release["mixed-pair"]:
        lines.append(f"- With an earlier release: "
                     f"{' '.join(release['mixed-pair'])}")
    return lines


def notes(version, channel, digest, release, tests, catalog, passed, links):
    lines = [f"# GBA Rush PSP {version}", "", f"Channel: {channel}."]
    if digest:
        lines.append(f"EBOOT.PBP sha256: {digest}.")
    lines += ["", "## Validated on", ""]
    validated = [test_id for test_id in tests
                 if any(row["Test id"] == test_id for row in passed)]
    for test_id in validated:
        rows = [row for row in passed if row["Test id"] == test_id]
        line = f"- {test_id}: " + ", ".join(dict.fromkeys(
         f"{row['Model']} ({row['Firmware/CFW']})" for row in rows))
        if test_id == "perf-baseline-5":
            line += "; compare-benchmarks: " + "; ".join(
             row["Evidence"] for row in rows)
        lines.append(line)
    if not validated:
        lines.append("Nothing yet: this build has not been through its "
                     "hardware tests.")
    lines += ["", "## Not validated", ""]
    missing = [(test_id, missing_classes(test_id, catalog, passed))
               for test_id in tests]
    missing = [(test_id, classes) for test_id, classes in missing if classes]
    if missing:
        lines += ["The stable release needs:", ""]
        lines += [f"- {test_id} on {', '.join(classes)}"
                  for test_id, classes in missing]
        lines.append("")
    others = [test_id for test_id in catalog if test_id not in tests]
    if others:
        lines.append(f"Not required for this release: {', '.join(others)}.")
    lines += ["", "## Known limits", ""]
    lines += [f"- {limit}" for limit in release["known-limit"]] \
     or ["None recorded."]
    lines += ["", "## Links with", ""] + links
    return "\n".join(lines) + "\n"


def gate(version, channel, repo, dist, now):
    """The problems that stop the release; with none, the notes are written
    to <dist>/RELEASE-NOTES-<version>.md. now is the time in Unix seconds."""
    repo, dist = pathlib.Path(repo), pathlib.Path(dist)
    if channel not in CHANNELS or version_key(version) is None:
        raise ValueError(f"need a version X.Y.Z and a channel among "
                         f"{', '.join(CHANNELS)}")
    docs = repo / "docs"
    catalog, rows = hardware_session.check(docs / "hardware-tests.md",
                                           docs / "HARDWARE-RESULTS.md")
    name = f"docs/releases/{version}.txt"
    if not (repo / name).is_file():
        raise ValueError(f"{name} not found")
    text = (repo / name).read_text(encoding="utf-8")
    try:
        release = parse_release(text)
    except ValueError as error:
        raise ValueError(f"{name}: {error}") from None
    tests = hardware_session.release_tests(text)
    if not tests:
        raise ValueError(f"{name} has no tests line")
    for test_id in tests:
        if test_id not in catalog:
            raise ValueError(f"{name}: unknown test id '{test_id}'")
    facts = parse_compat((docs / "releases" / "link-compat.txt")
                         .read_text(encoding="utf-8"))
    manifest = dist / f"GBA-Rush-PSP-{version}.manifest.json"
    digest = None
    if manifest.is_file():
        try:
            digest = release_manifest.read_manifest(manifest)["files"][
             "EBOOT.PBP"]["sha256"]
        except (KeyError, TypeError):
            raise ValueError(f"{manifest} has no EBOOT.PBP sha256") from None
    elif channel == "stable":
        raise ValueError(f"{manifest} not found: build the candidate with "
                         f"make package VERSION={version}")
    passed = [row for row in rows if digest and row["Result"] == "pass"
              and row["EBOOT sha256"] == digest[:12]]
    problems = []
    if channel == "stable":
        problems += required_test_problems(tests, catalog, passed, digest[:12])
        problems += candidate_problems(repo, version, release, now)
        problems += link_problems(repo, version, release, passed)
        problems += performance_problems(repo, digest)
    if not problems:
        dist.mkdir(parents=True, exist_ok=True)
        (dist / f"RELEASE-NOTES-{version}.md").write_text(
         notes(version, channel, digest, release, tests, catalog, passed,
               link_section(repo, version, release, facts)),
         encoding="utf-8", newline="\n")
    return problems


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--version", required=True)
    parser.add_argument("--channel", required=True, choices=CHANNELS)
    parser.add_argument("--repo", default=ROOT, type=pathlib.Path,
                        help="the repository (default: this one)")
    parser.add_argument("--dist", type=pathlib.Path,
                        help="the package folder (default: REPO/dist)")
    parser.add_argument("--now", type=int, default=int(time.time()),
                        help="the time in Unix seconds (default: now)")
    args = parser.parse_args(argv)
    dist = args.dist or args.repo / "dist"
    try:
        problems = gate(args.version, args.channel, args.repo, dist, args.now)
    except (OSError, ValueError) as error:
        print(f"release-gate: {error}", file=sys.stderr)
        return 1
    if problems:
        print(f"release-gate: {args.version} is not ready for the "
              f"{args.channel} channel:", file=sys.stderr)
        for problem in problems:
            print(f"  {problem}", file=sys.stderr)
        return 1
    print(f"release-gate: {args.version} passes the {args.channel} gate; "
          f"wrote {dist / f'RELEASE-NOTES-{args.version}.md'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
