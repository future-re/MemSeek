# Development image

The image includes Clang 20, GCC 13, clang-tidy, clang-format, clangd, LLVM
coverage tools, CMake 4.1, Ninja, ccache, Boost 1.89, GoogleTest 1.17, Google
Benchmark 1.9.4, GDB, Python with venv support, Doxygen, and Graphviz.
Project sources and configuration files are in `/opt/memseek`.

```sh
docker pull ghcr.io/future-re/memseek:dev-env
docker run --rm -it ghcr.io/future-re/memseek:dev-env
```

To develop against a local checkout, mount it at `/workspace`:

```sh
docker run --rm -it -v "$PWD:/workspace" -w /workspace \
  ghcr.io/future-re/memseek:dev-env
cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER=clang++-20
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

Use `g++-13` in a separate build directory to test GCC. Formatting and lint
tools use the checked-in `.clang-format` and `.clang-tidy` configurations.
Debugging another process may require `--cap-add=SYS_PTRACE`.

`Dockerfile` is the single development-image recipe. It builds the toolchain
from Ubuntu 24.04 and verifies the project, including tests and benchmarks:

```sh
docker build -t memseek-dev:latest .
```

The build requires access to Docker Hub and upstream downloads. It does not
depend on a locally named toolchain image. Parallel builds default to two jobs;
use `--build-arg BUILD_JOBS=4` to change this.

The image-publishing workflow uses this same Dockerfile for `latest` and
`dev-env`. These recipe changes affect future builds; they do not alter an
image that has already been uploaded.
