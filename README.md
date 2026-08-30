# Agent-Flight-Recorder
C + Qt desktop tool that acts as a black box recorder for AI coding agent sessions (like Claude Code)

## Dependencies

The UI uses Qt6 Widgets and Qt6 Charts (for the live CPU/memory graphs):

- macOS: `brew install qt6` (Charts is included).
- Linux: `sudo apt install qt6-base-dev qt6-charts-dev`.

The recorder traces the [Claude Code](https://docs.claude.com/en/docs/claude-code) CLI, so `claude` must be installed and on your `PATH`.

## Build

```sh
cmake -S . -B build
cmake --build build
```

The recorder is Linux-only (it uses `ptrace` and `/proc`). On non-Linux hosts
build just the UI: `cmake --build build --target flight_ui`.

## Run

Set your API key (or run `claude login` once beforehand so the CLI is
already authenticated):

```sh
export ANTHROPIC_API_KEY=sk-...
```

Then launch the UI from the same shell so it inherits the key:

```sh
./build/flight_ui
```

Type a query and hit Record — it launches `claude -p "<query>"` under
`ptrace` and streams file opens, exec calls, signals, and CPU/memory into
the live view, then saves a session summary under `build/sessions/`.
