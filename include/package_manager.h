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
