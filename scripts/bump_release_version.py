#!/usr/bin/env python3
"""Prepare the next patch version after a published GitHub release."""

import argparse
import json
from pathlib import Path
import re
import subprocess
import sys


VERSION_FILES = ("version.txt",)


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

    version_file = root / "version.txt"
    current_text = version_file.read_text().strip()
    match = re.fullmatch(r"(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)", current_text)
    if not match:
        raise ValueError("version.txt must contain MAJOR.MINOR.BUGFIX (no leading zeros)")
    current = tuple(map(int, match.groups()))
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

    version_file.write_text(next_text + "\n")
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
