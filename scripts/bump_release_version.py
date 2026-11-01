#!/usr/bin/env python3
"""Prepare the next patch version after a published GitHub release."""

import argparse
import json
from pathlib import Path
import re
import subprocess
import sys


VERSION_FILES = ("CMakeLists.txt", "src/main.cpp", "src/package_manager.cpp")


def run(*args, cwd):
    return subprocess.check_output(args, cwd=cwd, text=True).strip()


def bump(root, tag, assume_yes=False):
    match = re.fullmatch(r"v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)", tag)
    if not match:
        raise ValueError("release tag must use vMAJOR.MINOR.BUGFIX (no leading zeros)")
    released = tuple(map(int, match.groups()))
    next_version = (*released[:2], released[2] + 1)
    next_text = ".".join(map(str, next_version))

    # Resolve an actual local tag, and reject bumping an unrelated checkout.
    run("git", "rev-parse", "--verify", f"refs/tags/{tag}^{{commit}}", cwd=root)
    run("git", "merge-base", "--is-ancestor", f"refs/tags/{tag}", "HEAD", cwd=root)
    release = json.loads(run("gh", "release", "view", tag, "--json",
                             "tagName,isDraft,isPrerelease,publishedAt", cwd=root))
    if (release["tagName"] != tag or release["isDraft"] or
            release["isPrerelease"] or not release["publishedAt"]):
        raise ValueError("the tag must have a published, non-prerelease GitHub Release")
    if run("git", "status", "--porcelain", "--", *VERSION_FILES, cwd=root):
        raise ValueError("version files have local changes; commit or stash them first")

    contents = {name: (root / name).read_text() for name in VERSION_FILES}
    cmake = contents["CMakeLists.txt"]
    components = []
    for component in ("MAJOR", "MINOR", "BUGFIX"):
        values = re.findall(rf"^set\(MLANG_VERSION_{component} ([0-9]+)\)$", cmake, re.M)
        if len(values) != 1:
            raise ValueError(f"expected one numeric MLANG_VERSION_{component} in CMakeLists.txt")
        components.append(int(values[0]))
    current = tuple(components)
    current_text = ".".join(map(str, current))
    for name in VERSION_FILES[1:]:
        versions = re.findall(r'^#define MLANG_VERSION "([^"]+)"$', contents[name], re.M)
        if versions != [current_text]:
            raise ValueError(f"{name}: fallback version does not match {current_text}")
    if current > released:
        print(f"Version is already {current_text}; no bump needed after {tag}.")
        return False
    if current != released:
        raise ValueError(f"source version {current_text} does not match released {tag}")

    print(f"Published release: {tag}\nProposed source version: {current_text} -> {next_text}")
    print("Files: " + ", ".join(VERSION_FILES))
    if not assume_yes:
        try:
            confirmed = input("Apply this patch version bump? [y/N] ").strip().lower() in ("y", "yes")
        except EOFError:
            confirmed = False
        if not confirmed:
            print("Cancelled; no files changed.")
            return False

    contents["CMakeLists.txt"] = re.sub(
        r"^set\(MLANG_VERSION_BUGFIX [0-9]+\)$",
        f"set(MLANG_VERSION_BUGFIX {next_version[2]})", cmake, flags=re.M)
    for name in VERSION_FILES[1:]:
        contents[name] = contents[name].replace(
            f'#define MLANG_VERSION "{current_text}"', f'#define MLANG_VERSION "{next_text}"')
    for name, content in contents.items():
        (root / name).write_text(content)
    print(f"Updated source version to {next_text}. Review and commit the diff.")
    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--release-tag", required=True, help="published release, e.g. v0.4.3")
    parser.add_argument("--yes", action="store_true",
                        help="skip local prompt; automation must obtain confirmation by PR review")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    try:
        bump(root, args.release_tag, args.yes)
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
