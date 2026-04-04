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
 * comma-separated TOML arrays. Both `"double-quoted"` and `'single-quoted'`
 * TOML strings are supported in these task fields. TOML `#` comments are
 * supported on their own line and at the end of an assignment line as long as
 * the `#` appears outside quoted string content.
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
