# Task Graph Package Example

This example shows the task-graph features in `mlang pkg run`:

- `next = ["task-a", "task-b"]` to launch downstream tasks
- `parallel = true` to allow multiple downstream tasks to run concurrently
- `join_on = ["task-a", "task-b"]` to wait until specific tasks have finished
- `phase = "compile"` to tag tasks into a named phase
- `next_phases = ["compile"]` to launch every task in a phase
- `phase_join_on = ["compile"]` to wait for all tasks in a phase

## Manifest Highlights

```toml
[[task]]
name = "workflow"
parallel = true
next = ["left", "right", "merge"]

[[task]]
name = "merge"
join_on = ["left", "right"]
```

Running `workflow` starts the two branch tasks and also schedules `merge`.
Because `merge` declares `join_on = ["left", "right"]`, it waits until both
branch tasks are complete before concatenating their outputs.

## Example 1: Named Task Join

Run:

```sh
../../build/mlang pkg run workflow
cat build/joined.txt
```

Expected `build/joined.txt` content:

```text
left
right
```

## Example 2: Phase Barrier

The same example also includes a phase-based barrier:

```toml
[[task]]
name = "phase-workflow"
parallel = true
next_phases = ["compile"]

[[task]]
name = "compile-left"
phase = "compile"
commands = [
  "mkdir -p {{build_dir}}",
  "sh -c 'echo phase-left > {{build_dir}}/phase-left.txt'"
]

[[task]]
name = "compile-right"
phase = "compile"
commands = [
  "mkdir -p {{build_dir}}",
  "sh -c 'echo phase-right > {{build_dir}}/phase-right.txt'"
]

[[task]]
name = "phase-link"
phase_join_on = ["compile"]
commands = [
  "mkdir -p {{build_dir}}",
  "sh -c 'cat {{build_dir}}/phase-left.txt {{build_dir}}/phase-right.txt > {{build_dir}}/phase-joined.txt'"
]
```

Run it with:

```sh
../../build/mlang pkg run phase-workflow
cat build/phase-joined.txt
```

Expected `build/phase-joined.txt` content:

```text
phase-left
phase-right
```

`phase-workflow` launches the whole `compile` phase and also schedules
`phase-link`. Because `phase-link` declares `phase_join_on = ["compile"]`, it
does not run its own command until both compile tasks are complete.
