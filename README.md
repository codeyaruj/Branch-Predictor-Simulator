# Branch Predictor Simulator

A compact, deterministic C++17 command-line simulator for studying how classic
dynamic branch predictors react to real or synthetic control-flow traces. The
project keeps trace parsing, predictor policy, simulation, statistics, and
presentation separate, making it useful both as a computer-architecture
learning tool and as a small systems-programming portfolio project.

## 1. Project overview

`branchsim` consumes a text trace containing branch program counters and actual
outcomes, predicts every branch in program order, trains the selected predictor,
and reports accuracy and misprediction statistics. It includes five predictor
models, per-branch diagnostic output, an all-predictor comparison mode, and CSV
export. Normal single-predictor runs stream the trace instead of retaining it in
memory, so trace size is not limited by available RAM.

The implementation uses only the C++17 standard library. There are no runtime or
test-framework dependencies.

## 2. Why branch prediction matters

Modern processors overlap the execution of many instructions. A conditional
branch interrupts that flow because the processor does not yet know which path
to fetch. A correct prediction keeps the pipeline supplied; a misprediction can
discard speculative work and introduce a penalty that grows with pipeline depth.

Branch predictors exploit regularity in a program's recent behavior. This
simulator makes the resulting trade-offs visible: simple static policies use no
table storage, per-address predictors learn local bias, and GShare additionally
uses global branch history to capture correlations between branches.

## 3. Implemented predictor types

| CLI name | Policy | Mutable predictor storage |
| --- | --- | --- |
| `always-taken` | Predict every branch taken | None |
| `always-not-taken` | Predict every branch not taken | None |
| `one-bit` | Remember the last outcome at each table index | 1 bit per entry |
| `two-bit` | Use a four-state saturating counter at each index | 2 bits per entry |
| `gshare` | XOR low PC bits with global history to select a two-bit counter | 2 bits per entry plus the history register |

The default predictor is `two-bit`. A table has `2^N` entries when configured
with `--table-bits N` (default: `10`). One-bit entries initially predict not
taken. Two-bit and GShare entries initially start weakly not taken. GShare uses
an 8-bit global-history register by default, initially all zeroes.

## 4. Build instructions

Requirements are a C++17 compiler, CMake 3.16 or newer for the CMake workflow,
and Make for the provided direct build. Useful warnings are enabled in both builds:
`-Wall -Wextra -Wpedantic -Wconversion -Wshadow`.

### CMake

```bash
cmake -S . -B build
cmake --build build
```

The resulting executable is `build/branchsim`.

### Make

```bash
make
```

The resulting executable is `./branchsim`. To remove generated Make artifacts:

```bash
make clean
```

## 5. Usage examples

The command-line form is:

```text
branchsim <trace-file> [options]
```

Options:

```text
--predictor <always-taken|always-not-taken|one-bit|two-bit|gshare>
--table-bits <N>
--history-bits <N>
--initial-state <taken|not-taken|strongly-taken|weakly-taken|
                 weakly-not-taken|strongly-not-taken>
--compare
--csv <path>
--verbose
--help
```

`--table-bits` controls one-bit, two-bit, and GShare table sizes.
Accepted table widths are 0 through 24 bits (a width of 0 creates one entry).
`--history-bits` controls GShare's history width and accepts 1 through 63 bits.
In comparison mode, the configured table and history sizes are used for the
applicable predictors.

`--initial-state` uses one universal set of values so the option also works in
comparison mode. One-Bit derives a direction from every value; Two-Bit and
GShare map the direction-only values to the corresponding weak state:

| `--initial-state` value | One-Bit entry | Two-Bit/GShare counter |
| --- | --- | --- |
| `taken` | taken (`1`) | weakly taken (`2`) |
| `not-taken` | not taken (`0`) | weakly not taken (`1`) |
| `strongly-taken` | taken (`1`) | strongly taken (`3`) |
| `weakly-taken` | taken (`1`) | weakly taken (`2`) |
| `weakly-not-taken` | not taken (`0`) | weakly not taken (`1`) |
| `strongly-not-taken` | not taken (`0`) | strongly not taken (`0`) |

When the option is omitted, One-Bit defaults to not taken and Two-Bit/GShare to
weakly not taken. In `--compare` mode, the selected mapping is applied to all
three table-based predictors; Always Taken and Always Not Taken have no state.

The CLI enforces these compatibility rules:

- `--compare` cannot be combined with `--predictor` or `--verbose`.
- Outside comparison mode, `--table-bits` and `--initial-state` are valid only
  for a table-based predictor, and `--history-bits` is valid only for GShare.
- `--csv` works for either a single-predictor run or comparison mode, and it may
  accompany `--verbose` in a single-predictor run.
- During normal execution, each option may be specified at most once and
  exactly one trace path is required.

`--help` prints the help text and exits before normal argument validation, so it
does not require a trace path.

Invalid, missing, duplicate, out-of-range, or incompatible arguments are
rejected with a clear error.

Run the default two-bit predictor:

```bash
./branchsim traces/simple.trace
```

Run GShare with explicit dimensions:

```bash
./branchsim traces/mostly_taken.trace \
  --predictor gshare --history-bits 8 --table-bits 10
```

Inspect every state transition:

```bash
./branchsim traces/loop.trace --predictor two-bit --verbose
```

Compare all five predictors:

```bash
./branchsim traces/alternating.trace --compare
```

Write comparison results to CSV:

```bash
./branchsim traces/alternating.trace --compare --csv results.csv
```

CSV also works for a single predictor and produces one data row:

```bash
./branchsim traces/simple.trace --predictor one-bit --csv results.csv
```

Use `--help` for the complete command summary:

```bash
./branchsim --help
```

For a CMake build, replace `./branchsim` above with `./build/branchsim`.

## 6. Trace-file format

Each non-comment line contains a hexadecimal branch instruction address and one
case-sensitive outcome token:

```text
0x0040123A T
0x00401240 N
0x00401258 T
```

`T` means taken and `N` means not taken. Blank lines and lines whose first
non-whitespace character is `#` are ignored. Every other line must contain
exactly one valid hexadecimal address and one valid outcome. Malformed input is
rejected with a message containing the source line number.

The `traces/` directory contains four deterministic examples:

- `simple.trace`: a short mixed workload over several branch addresses
- `alternating.trace`: the repeating outcome pattern T, N, T, N
- `mostly_taken.trace`: exactly 80% taken outcomes
- `loop.trace`: one loop-closing branch, taken repeatedly and then not taken once

## 7. Algorithm explanations

Every valid trace record is processed in three steps: predict using the state
that existed before the branch, compare the prediction with the actual outcome,
then train the predictor with that outcome.

### Always Taken and Always Not Taken

The two static baselines return a constant prediction and keep no state. They are
useful reference points for judging whether a learning predictor adds value.

### One-Bit

The table index is formed from the **raw lower PC bits**:

```text
table_mask = (1 << table_bits) - 1
index      = pc & table_mask
```

No alignment bits are discarded or shifted away. An entry value of `0` predicts
not taken and `1` predicts taken. After resolution, the selected entry is simply
replaced by the actual outcome.

### Two-Bit

Each selected entry is a counter from 0 through 3. States 0 and 1 predict not
taken; states 2 and 3 predict taken. A taken outcome increments the counter and
a not-taken outcome decrements it, saturating at the endpoints. Requiring two
opposing observations to cross the prediction threshold prevents one unusual
iteration from immediately reversing a strongly learned direction.

### GShare

GShare uses the same two-bit counter behavior, but its table index combines the
raw low PC bits with recent global outcomes. The history is zero-extended when it
is narrower than the table index; when it is wider, only the low index-width
history bits participate in the XOR:

```text
table_mask = (1 << table_bits) - 1
pc_index   = pc & table_mask
index      = (pc_index ^ global_history) & table_mask
```

The configured history register itself retains exactly `history_bits` bits.
Most importantly, the update order is:

1. Use the pre-update PC/history index to update the selected counter with the
   actual outcome.
2. Shift that actual outcome into the global-history register.
3. Mask the register back to the configured history width.

With `T = 1` and `N = 0`, the history update is:

```text
global_history = ((global_history << 1) | actual) & history_mask
```

The newest outcome therefore occupies the least-significant history bit.

### Storage accounting and streaming

Reported table memory is logical predictor storage, not the size of C++
containers: one-bit uses `entries x 1` bits, while two-bit and GShare use
`entries x 2` bits. Normal execution parses and simulates one record at a time.
Comparison mode deliberately **reparses the trace once per predictor** rather
than retaining the whole trace. That trades additional file I/O for bounded
memory even on very large traces.

## 8. Two-bit state-machine diagram

```text
Saturating endpoint:  [SNT:0] --N--> [SNT:0]

Taken transitions:    [SNT:0] --T--> [WNT:1] --T--> [WT:2] --T--> [ST:3]
Not-taken transitions:[SNT:0] <--N-- [WNT:1] <--N-- [WT:2] <--N-- [ST:3]

Saturating endpoint:  [ST:3]  --T--> [ST:3]
Predictions:          SNT/WNT = N; WT/ST = T
```

Equivalently, each taken transition moves one state to the right and each
not-taken transition moves one state to the left. SNT saturates on not taken; ST
saturates on taken.

## 9. GShare indexing example

Assume `table_bits = 4`, `history_bits = 4`, PC `0x003A`, and a pre-update global
history of `0110`:

```text
raw low PC bits   = 1010   (0x003A & 0xF)
global history    = 0110
                     ---- XOR
selected index    = 1100   (decimal 12)
```

If counter 12 is weakly not taken (`1`) and the actual outcome is taken, the
prediction is not taken and the counter changes `1 -> 2`. Only after that update
does the history change from `0110` to `1101`. The new history affects the next
branch, never the branch currently being trained.

## 10. Output example

A single-predictor summary includes the common counts and rates. Table-based
predictors also report their entry count and logical table storage; GShare adds
its history width and final register value. For example,
`./branchsim traces/simple.trace --predictor gshare` prints:

```text
Predictor: GShare
Total branches: 12
Taken branches: 7
Not-taken branches: 5
Correct predictions: 5
Incorrect predictions: 7
Prediction accuracy: 41.67%
Misprediction rate: 58.33%
Table entries: 1024
Table memory usage: 2048 bits (256 bytes)
History bits: 8
Final global history: 00110101
```

Verbose GShare output includes the pre-update index/state and both history
values; fields that do not apply to a predictor are omitted. The first line from
this run:

```bash
./branchsim traces/loop.trace --predictor gshare \
  --table-bits 4 --history-bits 4 --verbose
```

is:

```text
PC=0x00403010 actual=T predicted=N result=MISS index=0 state=1->2 history=0000->0001
```

`./branchsim traces/simple.trace --compare` prints this compact table:

```text
Predictor                  Correct     Incorrect      Accuracy
Always Taken                     7             5        58.33%
Always Not Taken                 5             7        41.67%
One-Bit                          6             6        50.00%
Two-Bit                          7             5        58.33%
GShare                           5             7        41.67%
```

When `--csv <path>` is supplied, the file begins with this stable header and has
one row per reported predictor:

```csv
predictor,total,correct,incorrect,accuracy,misprediction_rate
GShare,12,5,7,41.67,58.33
```

Terminal percentages include `%`; CSV percentages are numeric values without the
symbol. Both use two decimal places. CSV creation and write failures are
reported as errors, and terminal output is still printed when CSV export is
requested successfully. To protect the source trace, the CSV destination may
not resolve to the same file through a direct path, symbolic link, or hard link.

## 11. Testing instructions

The tests use a small in-tree harness and require no third-party framework.

With Make:

```bash
make test
```

With CMake:

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

The suite covers both static predictors, one-bit and two-bit transitions,
counter saturation at both endpoints, GShare indexing and update order, valid
and invalid trace parsing, ignored comments and blank lines, statistics, and a
deterministic end-to-end simulation. Executable-level tests also assert exact
summary, comparison, and verbose output; CSV contents; CLI validation; and
input-trace overwrite protection.

## 12. Project structure

```text
branch-predictor-simulator/
|-- CMakeLists.txt
|-- Makefile
|-- .gitignore
|-- README.md
|-- LICENSE
|-- include/
|   |-- branch_predictor.hpp
|   |-- predictors.hpp
|   |-- simulator.hpp
|   |-- statistics.hpp
|   `-- trace_parser.hpp
|-- src/
|   |-- main.cpp
|   |-- predictors.cpp
|   |-- simulator.cpp
|   |-- statistics.cpp
|   `-- trace_parser.cpp
|-- tests/
|   |-- test_cli.cpp
|   |-- test_predictors.cpp
|   |-- test_trace_parser.cpp
|   `-- test_simulator.cpp
`-- traces/
    |-- simple.trace
    |-- alternating.trace
    |-- mostly_taken.trace
    `-- loop.trace
```

Predictor classes own policy state, `TraceParser` validates and streams records,
`Simulator` coordinates prediction/training and verbose events, and `Statistics`
owns aggregate results. Polymorphic predictors are managed with RAII and
`std::unique_ptr`; there is no global mutable state.

## 13. Limitations

- Input is an outcome trace, not an instruction-set simulator; it does not
  derive branches, targets, or timing from executable code.
- The simulator measures direction-prediction accuracy only. It does not model
  pipeline depth, cycle penalties, alias classification, warm-up intervals, or
  context switches.
- Table indexing intentionally uses raw low address bits. Real processors may
  discard known alignment bits, hash more address bits, or tag predictor state.
- Predictor tables are conceptually bit-packed for reported memory, but the C++
  representation favors clarity over physical packing.
- Comparison mode performs five streaming passes over the input. Its memory use
  stays bounded, but it can be slower for traces on high-latency storage.
- The global history begins at zero and there is no separate configurable
  warm-up phase before statistics collection.

## 14. Ideas for future extensions

- Add a per-address local-history predictor.
- Combine predictors with a tournament selector.
- Explore perceptron or other neural branch predictors.
- Model a branch target buffer (BTB) and target-prediction accuracy.
- Simulate a return-address stack for function returns.
- Add branch-trace visualization and state/alias heat maps.
- Extend comparison mode with user-selected predictor sets and repeated trials.
- Add JSON export alongside the existing CSV output.
- Support warm-up windows, region-based statistics, and misprediction penalty
  estimates.
- Read compressed traces or a compact binary trace format.

## License

This project is available under the MIT License. See [LICENSE](LICENSE).
