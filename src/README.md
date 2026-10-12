# Source layout

The build consumes the traditional headers under
`include/memseek/` and the ordinary translation units under `src/`.

- `src/` contains `.cpp` implementation files
- `include/memseek/` contains `.hpp` header files
- The current scanner is implemented in `memory/`, with session state in
  `app/scan_session.cpp` and values in `include/memseek/value.hpp`.
- The old `core/` engine, `scan/` implementations, CLI, app services, and CLI UI
  have been removed. `ScanSession` is the entry point for a future CLI or app.
- The build produces `memseek_lib`, tests, and optional benchmarks. There is
  currently no `memseek` CLI executable target.
- Standalone `utils/endianness` and `utils/logging` remain available as sources.
  `utils/version.hpp` provides the version string directly; there is no
  generated version header or version-generation step.
