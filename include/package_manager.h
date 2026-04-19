#ifndef PACKAGE_MANAGER_H
#define PACKAGE_MANAGER_H

/**
 * \brief Implements the `mlang pkg` command family.
 *
 * Supported package workflows include manifest initialization, dependency
 * fetching, package builds, task execution, and cleanup. Task execution honors
 * manifest dependency edges such as `depends_on`, `join_on`, and phase-based
 * scheduling. The pkg CLI also accepts `--config <file>` (or
 * `--config=<file>`) before the subcommand to target an alternate manifest
 * instead of the default `mlang.toml`. `mlang pkg run <task> --tasks` prints
 * an ASCII task tree and the resolved linear execution order without running
 * commands. `mlang pkg [--tasks] [--color] <manifest.toml>...` prints runnable
 * task entrypoints for one or more manifests. `mlang pkg --tests [--tasks]
 * [--color] <manifest.toml>...`
 * resolves every `phase = "test"` task root from each listed manifest and
 * runs those test workflows after their dependencies. The `--tests` options
 * are parsed before the manifest path list so multiple manifests can be passed
 * in one command. The top-level manifest-overview shorthand accepts the same
 * option-first ordering, and `--tasks` / `--color` may appear in any order
 * before the manifest path list. Passing `--color` additionally colorizes
 * parallel branches in the tree view. `mlang pkg run` also accepts
 * `--option key=value` overrides for values declared under
 * `[tool.mlang.options]`, which are exposed to task text through placeholders
 * such as `{{option.userspace}}`. Tasks may also opt into
 * `inline_output = true`, which keeps command output on a single live status
 * row with task numbering and a truncated tail of the latest output line.
 * Declarative task builds are also supported via keys such as `language`,
 * `source`, `output`, `inputs`, `compile_only`, `libs`, `lib_paths`,
 * `compiler_flags`, and `linker_flags`. Tasks may also request a recursive
 * permission fixup after they complete with `chmod` plus `chmod_path` or
 * `chmod_paths`.
 *
 * Example manifest excerpt:
 * \code{.toml}
 * [package]
 * name = "multilang_demo"
 * version = "0.1.0"
 *
 * [dependencies]
 * miniaudio = { url = "https://github.com/mackron/miniaudio/archive/refs/heads/master.tar.gz", archive = "tar.gz", strip_components = "1", build = "none" }
 * vst3sdk = { git = "https://github.com/steinbergmedia/vst3sdk.git", submodules = true, build = "none" }
 *
 * [[task]]
 * name = "compile-c-bridge"
 * phase = "compile"
 * language = "c"
 * source = "src/player.c"
 * output = "{{build_dir}}/obj/player.o"
 * compile_only = true
 * compiler_flags = [
 *   "-std=c11",
 *   "-O2",
 *   "-I{{deps_dir}}/miniaudio",
 * ]
 *
 * [[task]]
 * name = "link-demo"
 * phase = "build"
 * language = "c++"
 * output = "{{build_dir}}/demo"
 * inputs = [
 *   "{{build_dir}}/obj/main_mlang.o",
 *   "{{build_dir}}/obj/player.o",
 * ]
 * libs = ["mlang_std"]
 *
 * # Comments are supported too.
 * [tool.mlang]
 * build_dir = "build-release" # End-of-line comments also work.
 *
 * [tool.mlang.options]
 * userspace = "busybox"
 * \endcode
 *
 * Dedicated test-manifest example:
 * \code{.toml}
 * [package]
 * name = "mla_tests"
 * version = "0.1.0"
 *
 * [tool.mlang.options]
 * suite_dir = "tests"
 *
 * [[task]]
 * name = "build-mlang-test-runner"
 * phase = "build"
 * workdir = "{{root}}/.."
 * shell = [
 *   "cmake --build build --target mlang -j4",
 * ]
 *
 * [[task]]
 * name = "run-mla-tests"
 * phase = "test"
 * depends_on = ["build-mlang-test-runner"]
 * workdir = "{{root}}/.."
 * shell = [
 *   "./build/mlang --tests {{option.suite_dir}}",
 * ]
 * \endcode
 *
 * Task array values such as `inputs`, `libs`, `compiler_flags`,
 * `linker_flags`, `commands`, `shell`, and `path_entries` accept multiline
 * comma-separated TOML arrays. `command` may also be written as a token array
 * such as `["sh", "-c", "echo hi"]`, `commands` may contain nested token
 * arrays, and `commands += [ ... ]` appends more command entries later in the
 * same task block. Both `"double-quoted"` and `'single-quoted'` TOML strings
 * are supported in these task fields. TOML `#` comments are supported on
 * their own line and at the end of an assignment line as long as the `#`
 * appears outside quoted string content.
 *
 * Tasks may also declare `supported_hosts = ["darwin", "linux", "windows"]`
 * and an optional `unsupported_message = "..."`. When the current host is not
 * listed, `mlang pkg` stops before running task commands and reports that
 * message directly. This is useful for examples that only support one runtime
 * stack so far, such as a CoreAudio-only macOS workflow.
 *
 * Git source dependencies may additionally declare `submodules = true`. When
 * present, `mlang pkg fetch` runs `git submodule update --init --recursive`
 * after cloning or updating that dependency. This is useful for repositories
 * such as the Steinberg VST3 SDK that keep required source trees in git
 * submodules.
 *
 * Git dependency example with submodules:
 * \code{.toml}
 * [dependencies]
 * vst3sdk = {
 *   git = "https://github.com/steinbergmedia/vst3sdk.git",
 *   submodules = true,
 *   build = "none",
 * }
 * \endcode
 *
 * Command-token example:
 * \code{.toml}
 * [[task]]
 * name = "toolchain-check"
 * commands = [
 *   [
 *     'sh',
 *     '-c',
 *     'if [ ! -x ../../build/mlang ]; then echo Missing ../../build/mlang.; exit 1; fi',
 *   ],
 * ]
 * commands += [
 *   [
 *     'sh',
 *     '-c',
 *     'for tool in cc c++ ar python3; do if ! command -v $tool >/dev/null 2>&1; then echo Missing required tool in PATH: $tool; exit 1; fi; done',
 *   ],
 * ]
 * \endcode
 *
 * Post-task chmod example:
 * \code{.toml}
 * [[task]]
 * name = "fix-source-perms"
 * commands = [
 *   [ "tar", "-xzf", "{{build_dir}}/archive.tar.gz", "-C", "{{build_dir}}/src" ],
 * ]
 * chmod = "644"
 * chmod_paths = [
 *   "{{build_dir}}/src",
 * ]
 * \endcode
 *
 * `chmod` currently accepts octal modes such as `644` or `755`. The mode is
 * applied recursively to regular files, and directories keep traverse bits so
 * a source tree remains usable after `chmod = "644"`.
 *
 * Linux initramfs example using BusyBox as the real `/init` while optionally
 * overlaying a wider GNU userspace selected from the command line:
 * \code{.toml}
 * [tool.mlang.options]
 * userspace = "busybox"
 *
 * [[task]]
 * name = "busybox-fetch"
 * commands = [
 *   [ "mkdir", "-p", "{{build_dir}}" ],
 *   [ "sh", "-c", "[ -x {{build_dir}}/busybox-armv8l ] || curl -L --fail https://busybox.net/downloads/binaries/1.31.0-defconfig-multiarch-musl/busybox-armv8l -o {{build_dir}}/busybox-armv8l" ],
 *   [ "chmod", "+x", "{{build_dir}}/busybox-armv8l" ],
 * ]
 *
 * [[task]]
 * name = "gnu-userspace-fetch"
 * commands = [
 *   [ "sh", "-c", "if [ \"{{option.userspace}}\" != \"gnu\" ]; then exit 0; fi" ],
 *   [ "mkdir", "-p", "{{build_dir}}" ],
 *   [ "sh", "-c", "[ -f {{build_dir}}/ubuntu-base-24.04.3-base-arm64.tar.gz ] || curl -L --fail https://cdimage.ubuntu.com/ubuntu-base/releases/noble/release/ubuntu-base-24.04.3-base-arm64.tar.gz -o {{build_dir}}/ubuntu-base-24.04.3-base-arm64.tar.gz" ],
 *   [ "rm", "-rf", "{{build_dir}}/gnu-rootfs" ],
 *   [ "mkdir", "-p", "{{build_dir}}/gnu-rootfs" ],
 *   [ "tar", "-xzf", "{{build_dir}}/ubuntu-base-24.04.3-base-arm64.tar.gz", "-C", "{{build_dir}}/gnu-rootfs" ],
 * ]
 *
 * [[task]]
 * name = "initramfs"
 * depends_on = ["busybox-fetch", "gnu-userspace-fetch"]
 * shell = [
 *   "rm -rf {{build_dir}}/initramfs {{build_dir}}/initramfs.cpio.gz",
 *   "if [ '{{option.userspace}}' = 'gnu' ]; then cp -a {{build_dir}}/gnu-rootfs/. {{build_dir}}/initramfs/; CONSOLE_CMD='/bin/cttyhack /bin/bash --login'; else mkdir -p {{build_dir}}/initramfs/bin {{build_dir}}/initramfs/usr/bin; CONSOLE_CMD='/bin/cttyhack /bin/sh'; fi",
 *   "mkdir -p {{build_dir}}/initramfs/dev {{build_dir}}/initramfs/etc {{build_dir}}/initramfs/proc {{build_dir}}/initramfs/sys {{build_dir}}/initramfs/tmp",
 *   "cp {{build_dir}}/busybox-armv8l {{build_dir}}/initramfs/bin/busybox",
 *   "# Use BusyBox as the real PID 1 init process and precreate applet links on the host.",
 *   "cd {{build_dir}}/initramfs/bin && for applet in init cttyhack sh mount mkdir uname ls cat echo dmesg ps; do ln -sf busybox $applet; done",
 *   "ln -sf bin/busybox {{build_dir}}/initramfs/init",
 *   "cat > {{build_dir}}/initramfs/etc/inittab <<EOF",
 *   "::sysinit:/bin/mount -t proc proc /proc",
 *   "::sysinit:/bin/mount -t sysfs sysfs /sys",
 *   "::sysinit:/bin/mount -t devtmpfs devtmpfs /dev",
 *   "::sysinit:/bin/mkdir -p /dev/pts",
 *   "ttyAMA0::respawn:$CONSOLE_CMD",
 *   "::ctrlaltdel:/bin/umount -a -r",
 *   "::shutdown:/bin/umount -a -r",
 *   "EOF",
 *   "cd {{build_dir}}/initramfs && find . -print | cpio -o -H newc | gzip -9 > ../initramfs.cpio.gz",
 * ]
 * \endcode
 *
 * Build and run the example from
 * `examples/package_manager_linux_aarch64_qemu/` with:
 * \code{.sh}
 * ../../build/mlang pkg run qemu-run --option userspace=busybox
 * ../../build/mlang pkg run qemu-run --option userspace=gnu
 * \endcode
 *
 * The first command boots a minimal BusyBox shell. The second command overlays
 * an Ubuntu Base ARM64 rootfs, stages the official Neovim ARM64 tarball as
 * `nvim` / `vim` / `vi`, and starts a real GNU-mode serial login prompt on
 * `ttyAMA0`. Both modes fetch dependencies on demand, build the kernel image,
 * pack the initramfs, and then launch QEMU. The BusyBox applet links are
 * created explicitly during packing because the host may not be able to
 * execute the target-architecture BusyBox binary. Writing `/etc/inittab` lets
 * BusyBox `init` mount the basic pseudo filesystems and respawn either a
 * BusyBox shell or the GNU login wrapper, depending on `userspace`.
 *
 * The GNU userspace path seeds demo accounts `admin/admin`, `user/user`, and
 * `root/root`. The example initramfs also seeds `/etc/passwd`, `/etc/group`,
 * and `/etc/shadow`, then installs small helper commands for `addgroup`,
 * `adduser`, `passwd`, and `sudo`. `adduser` creates `/home/<user>` and a
 * basic `.profile` so the guest can switch into that user cleanly. A guest
 * session can use them like:
 * \code{.sh}
 * login: admin
 * Password: admin
 * pwd
 * sudo ls --color=auto /
 * addgroup demo
 * adduser alice demo
 * passwd alice
 * grep '^alice:' /etc/passwd
 * grep '^demo:' /etc/group
 * su alice
 * pwd
 * \endcode
 */
class PackageManager
{
public:
    /**
     * \brief Runs `mlang pkg` using the process command-line arguments.
     *
     * \param argc Argument count from `main`.
     * \param argv Argument vector from `main`.
     *
     * The expected CLI shape is:
     * `mlang pkg [--config FILE] <subcommand> [args...]`.
     * Runtime task invocations may additionally pass `--option key=value`
     * after `run <task>` to override values declared under
     * `[tool.mlang.options]`.
     *
     * \return Process-style exit code. Returns `0` on success and non-zero on
     *         failure.
     */
    int run(int argc, char** argv);
};

#endif
