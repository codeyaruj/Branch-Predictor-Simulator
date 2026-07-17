# Branch Predictor Simulator

I made this project to understand how branch predictors work.

It supports:

- Always Taken
- Always Not Taken
- One-Bit
- Two-Bit
- GShare

## Build

This project needs a C++17 compiler and Make.

```bash
make
```

## Run

The default predictor is Two-Bit:

```bash
./branchsim traces/simple.trace
```

Choose a predictor:

```bash
./branchsim traces/simple.trace --predictor one-bit
./branchsim traces/simple.trace --predictor gshare --table-bits 10 --history-bits 8
```

Compare all five predictors:

```bash
./branchsim traces/alternating.trace --compare
```

Show every prediction and state change:

```bash
./branchsim traces/loop.trace --predictor two-bit --verbose
```

Use `./branchsim --help` to see all options.

## Trace format

Each line has a hexadecimal program counter followed by `T` for taken or `N`
for not taken:

```text
0x0040123A T
0x00401240 N
```

Blank lines and lines beginning with `#` are ignored.

## Tests

```bash
make test
```

## What I learned

A one-bit predictor changes its prediction after one different result. A
two-bit predictor uses four states, so one different result does not always
reverse its prediction. GShare combines the program counter with recent branch
history before selecting a table entry.

## Limitations

This simulator is educational and models simplified branch predictors. It only
simulates branch direction and does not attempt cycle-accurate or
implementation-specific CPU behavior.

The whole trace is loaded into memory before simulation, so it is intended for
small learning traces. Table indexing currently uses the raw lower bits of the
PC value; instruction-alignment bits are not removed first.
