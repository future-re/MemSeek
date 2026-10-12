# Source layout

The build consumes the traditional headers under
`include/memseek/` and the ordinary translation units under `src/`.

- `src/` contains `.cpp` implementation files
- `include/memseek/` contains `.hpp` header files
- The current scanner is implemented in `memory/`, with session state in
  `app/scan_session.cpp` and values in `include/memseek/value.hpp`.
- `cli/` contains the command-line entry point, while `app/` contains
  CLI-independent application use cases such as `ScanSession`.
- The build produces `memseek_lib`, the `memseek` CLI entry point, tests, and
  optional benchmarks. The CLI currently supports `--help` and `--version`;
  scanning commands will use `ScanSession`.
- Standalone `utils/endianness` and `utils/logging` remain available as sources.
  `utils/version.hpp` provides the version string directly; there is no
  generated version header or version-generation step.
