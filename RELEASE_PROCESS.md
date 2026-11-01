# Release process

MLang releases use tags named `vMAJOR.MINOR.BUGFIX`, for example `v0.4.3`.
The release workflow builds Linux x86_64 and macOS arm64 archives, an unsigned
macOS installer, and SHA-256 checksums. After publication, it opens a pull
request to increment `BUGFIX` by one. **Reviewing and merging that PR is the
user confirmation for the version bump.**

## One-time GitHub setup

1. Merge the release workflow and scripts into the repository's default branch.
2. In GitHub, open **Settings > Actions > General > Workflow permissions**.
   Enable **Allow GitHub Actions to create and approve pull requests** and save.
   Organization policy may control this setting. The workflow requests
   `contents: write` and `pull-requests: write` only for the jobs that need them.
   See [GitHub's workflow permissions documentation](https://docs.github.com/en/repositories/managing-your-repositorys-settings-and-features/enabling-features-for-your-repository/managing-github-actions-settings-for-a-repository).

## Make a release

1. On GitHub, merge the intended changes into the default branch. Confirm the
   **Build and Test** workflow passes for the commit you intend to release.
2. Update your checkout of the default branch and fetch tags:

   ```sh
   git switch <default-branch>
   git pull --ff-only
   git fetch origin --tags
   git status --short
   ```

   Replace `<default-branch>` with the repository's default branch name. Commit
   or stash any local changes before proceeding.
3. Check the root `version.txt`. Its `MAJOR.MINOR.BUGFIX` value forms the next
   release tag; the fourth, automatically incremented build component is omitted.
   Add release notes to `RELEASE_NOTES.md`, commit and push them, and wait for
   CI. For a deliberate major/minor change, edit only `version.txt` before
   tagging. CMake reads it and supplies the version to the compiler and runtime;
   changing it also triggers CMake reconfiguration on the next build.
4. Tag that commit and push **only the intended tag**. For example, if the
   source version is `0.4.3` and `v0.4.3` does not already exist:

   ```sh
   git tag -a v0.4.3 -m "MLang v0.4.3"
   git push origin v0.4.3
   ```

   Use the actual source version. Never move an existing published release tag.
5. On GitHub, open **Actions > Release binaries** and open the run for the tag.
   Wait for both platform builds and **Attach assets to GitHub Release** to
   succeed. The workflow creates the GitHub Release and uploads the assets.
6. Open **Releases**, select the release, and verify that it contains both
   platform `.tar.gz` archives, the macOS `.pkg`, and their `.sha256` files.
   Download an archive for your platform and verify it, for example:

   ```sh
   shasum -a 256 -c mlang-v0.4.3-macos-arm64.tar.gz.sha256
   tar -xzf mlang-v0.4.3-macos-arm64.tar.gz
   ./mlang-v0.4.3-macos-arm64/bin/mlang --version
   ```

   The reported version must match the tag without `v`. The macOS installer
   is unsigned. Native compilation still requires the host dependencies listed
   in the README and the archive's `INSTALL.txt`.
7. Use **Edit** on the release to review the generated notes, incorporate the
   relevant `RELEASE_NOTES.md` entry, and save.
8. Open **Pull requests > Bump patch version after vX.Y.Z**. Review the
   change to `version.txt`. For `v0.4.3`, the proposed source version is `0.4.4`.
   Approve any pending workflow runs in the PR's merge box, wait for required
   checks, and merge the PR to confirm the bump. The workflow leaves this
   confirmation to a maintainer; it does not enable automatic merging.
   GitHub documents [workflow runs created using GITHUB_TOKEN](https://docs.github.com/en/actions/how-tos/write-workflows/choose-when-workflows-run/trigger-a-workflow).

The release tag and its binaries retain the released version. Development
builds after the bump use the next patch version plus their build number.
No new release tag is created by the bump.

## Retry or build an existing tag from GitHub

Open **Actions > Release binaries > Run workflow**, select the default branch,
enter the exact existing tag in **release_tag**, and select **Run workflow**.
The build checks out that tag, regardless of the selected workflow branch.
Matching release assets are replaced. Rerunning after an accepted version bump
does not increment again; an existing bump PR is reused without overwriting it.

If you created a tag and release through GitHub's **Releases > Draft a new
release** interface, use this same manual workflow to build and attach assets
if no tag-triggered run appears. Publish a normal release, rather than leaving
it as a draft or marking it as a prerelease, to enable the patch bump.

If builds or asset upload fail, fix the failure and rerun the failed jobs.
The version bump job runs only after asset upload succeeds. If only the bump
job fails (for example because Actions cannot create PRs), the release remains
published; fix the repository setting and rerun that job, or use the local
fallback below. A tag outside the default branch's history or a mismatched
source version requires maintainer review before bumping.

## Confirm the bump locally

For an existing release, or when automated PR creation is unavailable, install
Python 3 and the GitHub CLI, authenticate with `gh auth login`, and update your
checkout of the default branch. Fetch tags, then run:

```sh
git fetch origin --tags
python3 scripts/bump_release_version.py --release-tag v0.4.3
```

The script verifies the local tag is in the current checkout's history and the
GitHub Release is published and is not a prerelease. It checks that the
version in `version.txt` is valid and has no local changes, shows the next
patch version, and asks `Apply this patch version bump? [y/N]`. Enter `y` to
change the files. Any other answer, including end of input, cancels the change.
The script leaves committing and pushing to you:

```sh
git diff -- version.txt
git switch -c release/bump-after-v0.4.3
git add version.txt
git commit -m "Bump patch version after v0.4.3"
git push -u origin release/bump-after-v0.4.3
gh pr create --fill
```

Use the actual released tag in these commands. If the source version is already
newer than the release, the script exits without changing it. Automation uses
`--yes` to prepare the PR; maintainer review and merge provide confirmation.
