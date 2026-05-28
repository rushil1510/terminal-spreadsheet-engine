# terminal-spreadsheet-engine

This was a course project I undertook for [COP290](https://abhilash-jindal.com/teaching/2024-2-cop-290/) at IIT Delhi as part of my minor degree requirements. The objective of the course was to introduce software design practices and principles in C and Rust, and this repo contains my submission for the C lab.

## Layout

```
core/           # config knobs, helpers (clamp, time deltas, address math)
containers/    # int stack, state-memo stack, AVL set + iterator
workbook/      # Cell[] table, dependants graph, packed<->unpacked formulas
engine/        # cycle detection, topological recompute, eval
parser/        # cell-label codec, command grammar + formatter
ui/cli/         # plain stdin/stdout REPL
ui/tui/         # ncurses interactive front-end
apps/           # entry points: main_cli, main_gui, main_test, tester
testcases/      # input scripts for `make test`
expected_outputs/text_files/   # canonical model outputs
outputs/        # populated by `make test`
```

## Building and running

The build emits a binary named `sheet` regardless of front-end so the
invocation is always `./sheet R C [options]`.

```bash
make            # build the CLI (default target)
./sheet R C     # R rows, C columns, plain-text REPL

make gui        # build the ncurses TUI (requires libncurses)
./sheet R C
```

### CLI commands

* `wasd`                 -- pan the viewport by the configured scroll amount.
* `<CELL>=<expr>`        -- assign a formula (see grammar below).
* `scroll_to <CELL>`     -- jump the viewport to a cell.
* `disable_output`/`enable_output` -- toggle grid rendering.
* `q`                    -- quit.

### TUI keybindings

| Key             | Action                                       |
|-----------------|----------------------------------------------|
| `w` `a` `s` `d` | Pan the viewport                             |
| `+` / `-`       | Resize column width                          |
| `Enter`         | Edit the focused cell inline                 |
| `Backspace`     | Clear the focused cell to 0                  |
| `Tab`           | Switch between Interactive and Command modes |
| `q`             | Quit                                         |

### Formula grammar

```
<CELL> = <int>
<CELL> = <CELL>
<CELL> = <int>  <op> <int>     where <op> in { + - * / }
<CELL> = <int>  <op> <CELL>
<CELL> = <CELL> <op> <int>
<CELL> = <CELL> <op> <CELL>
<CELL> = SUM|AVG|MIN|MAX|STDEV( <CELL> : <CELL> )
<CELL> = SLEEP( <int> )
<CELL> = SLEEP( <CELL> )
```

A `<CELL>` is the usual `A1`, `BC42`, `AAA999` style label.

## Running the tests

```bash
make test
```

This compiles the headless test driver, the comparator, and replays every
script under `testcases/` against a 1000x1000 workbook. The comparator
(`tester`) prints `All values match` when each cell matches the canonical
expected output and lists any divergences otherwise.

## How the compute engine works

When a formula is assigned to a cell (`A1=B2+C3`):

1. **Detach old inputs.** Walk the cell's previously-stored formula and
   remove `A1` from each input's dependants set. (Inputs were tracked when
   the formula was first installed.)
2. **Install the new formula in bit-packed form** and re-register `A1` as a
   dependant of each new input. Dependants are kept in an inline array up
   to size 4 and promoted to an AVL set above that threshold -- a 50%+ memory
   win on the typical workbook where most cells have few dependants.
3. **Detect cycles.** An iterative DFS from `A1` walks the *forward*
   dependants graph. Touched cells are pushed onto a memo stack so that, if a
   back-edge is found, we can roll their walk-state back to what it was
   before the DFS started -- and we restore the previously installed formula.
4. **Recompute downstream.** A Kahn-style traversal evaluates cells in
   topological order: a dependant is only queued once *all* of its inputs
   have been resolved. This guarantees we never recompute a cell whose
   inputs are still pending.

The original implementation duplicated this graph walk: once for the inline
array form and once for the AVL set form. This refactor introduces a unified
`DepIter` in `workbook/workbook.h` so the engine writes the algorithm once
and the iterator hides the underlying storage.

## Test mode input format

```
T                 -- number of test cases
N M               -- N edits, M queries (per test case)
... N edit lines (formulas or scroll_to commands)
... M query lines (bare cell references like B42)
```

Each edit emits a `[i] > <command> --> (<msg>) [<sec>s]` line; each query
emits `<CELL> : <expr> --> Value: <int>, State: <state>`.
