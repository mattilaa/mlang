#!/usr/bin/env sh
set -eu

# Large self-hosted tools can exhaust Linux's default 8 MiB stack while the
# LLVM 15/17 bootstrap compiler lowers their combined AST and IR module.
if ! ulimit -s unlimited 2>/dev/null; then
    # A container may impose a finite hard limit; use all of it when possible.
    hard_stack_limit=$(ulimit -H -s 2>/dev/null || true)
    if [ -n "$hard_stack_limit" ]; then
        ulimit -s "$hard_stack_limit" 2>/dev/null || true
    fi
fi

exec "$@"
