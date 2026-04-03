# Task Graph Package Example

This example shows the task-graph features in `mlang pkg run`:

- `next = ["task-a", "task-b"]` to launch downstream tasks
- `parallel = true` to allow multiple downstream tasks to run concurrently
- `join_on = ["task-a", "task-b"]` to wait until specific tasks have finished

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

## Run

```sh
../../build/mlang pkg run workflow
cat build/joined.txt
```

Expected `build/joined.txt` content:

```text
left
right
```
