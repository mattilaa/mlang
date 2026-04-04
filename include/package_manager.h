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
 * instead of the default `mlang.toml`. Tasks may also opt into
 * `inline_output = true`, which keeps command output on a single live status
 * row with task numbering and a truncated tail of the latest output line.
 * Declarative task builds are also supported via keys such as `language`,
 * `source`, `output`, `inputs`, `compile_only`, `libs`, `lib_paths`,
 * `compiler_flags`, and `linker_flags`.
 *
 * Example manifest excerpt:
 * \code{.toml}
 * [package]
 * name = "multilang_demo"
 * version = "0.1.0"
 *
 * [dependencies]
 * miniaudio = { url = "https://github.com/mackron/miniaudio/archive/refs/heads/master.tar.gz", archive = "tar.gz", strip_components = "1", build = "none" }
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
 * Linux initramfs example using BusyBox as the real `/init`:
 * \code{.toml}
 * [[task]]
 * name = "busybox-fetch"
 * commands = [
 *   "mkdir -p {{build_dir}}",
 *   "sh -c '[ -x {{build_dir}}/busybox-armv8l ] || curl -L --fail https://busybox.net/downloads/binaries/1.31.0-defconfig-multiarch-musl/busybox-armv8l -o {{build_dir}}/busybox-armv8l'",
 *   "chmod +x {{build_dir}}/busybox-armv8l",
 * ]
 *
 * [[task]]
 * name = "initramfs"
 * depends_on = ["busybox-fetch"]
 * shell = [
 *   "rm -rf {{build_dir}}/initramfs {{build_dir}}/initramfs.cpio.gz",
 *   "mkdir -p {{build_dir}}/initramfs/bin {{build_dir}}/initramfs/dev {{build_dir}}/initramfs/etc {{build_dir}}/initramfs/proc {{build_dir}}/initramfs/sys {{build_dir}}/initramfs/tmp {{build_dir}}/initramfs/usr/bin",
 *   "cp {{build_dir}}/busybox-armv8l {{build_dir}}/initramfs/bin/busybox",
 *   "# Use BusyBox as the real PID 1 init process and precreate applet links on the host.",
 *   "cd {{build_dir}}/initramfs/bin && for applet in sh init ls cat echo uname mount mkdir dmesg ps pwd sleep clear true false head tail grep env which cp mv rm ln chmod sync cttyhack; do ln -sf busybox $applet; done",
 *   "ln -sf bin/busybox {{build_dir}}/initramfs/init",
 *   "cat > {{build_dir}}/initramfs/etc/inittab <<'EOF'",
 *   "::sysinit:/bin/mount -t proc proc /proc",
 *   "::sysinit:/bin/mount -t sysfs sysfs /sys",
 *   "::sysinit:/bin/mount -t devtmpfs devtmpfs /dev",
 *   "::sysinit:/bin/mkdir -p /dev/pts",
 *   "ttyAMA0::respawn:/bin/cttyhack /bin/sh",
 *   "::ctrlaltdel:/bin/umount -a -r",
 *   "::shutdown:/bin/umount -a -r",
 *   "EOF",
 *   "cd {{build_dir}}/initramfs && find . -print | cpio -o -H newc | gzip -9 > ../initramfs.cpio.gz",
 * ]
 * \endcode
 *
 * The BusyBox applet links are created explicitly during packing because the
 * host may not be able to execute the target-architecture BusyBox binary.
 * Writing `/etc/inittab` lets BusyBox `init` mount the basic pseudo
 * filesystems and respawn a shell on `ttyAMA0`.
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
     *
     * \return Process-style exit code. Returns `0` on success and non-zero on
     *         failure.
     */
    int run(int argc, char** argv);
};

#endif
