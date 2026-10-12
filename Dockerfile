FROM ubuntu:24.04
ARG BUILD_JOBS=2
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential wget ca-certificates curl git python3 python3-pip python3-venv \
    libbz2-dev libzstd-dev liblzma-dev libssl-dev pkg-config ccache lsb-release \
    gnupg software-properties-common unzip gdb doxygen graphviz && \
    rm -rf /var/lib/apt/lists/*

RUN apt-get update && apt-get install -y --no-install-recommends \
    software-properties-common && \
    add-apt-repository ppa:ubuntu-toolchain-r/test -y && apt-get update && \
    apt-get install -y gcc-13 g++-13 libstdc++-13-dev && \
    rm -rf /var/lib/apt/lists/*

# 安装 clang-20
RUN wget https://apt.llvm.org/llvm.sh -O /tmp/llvm.sh && chmod +x /tmp/llvm.sh && \
    /tmp/llvm.sh 20 && \
    apt-get update && apt-get install -y --no-install-recommends \
    llvm-20 clang-20 clang-tidy-20 clang-format-20 clang-tools-20 clangd-20 lld-20 && \
    # ensure llvm coverage tools (llvm-cov, llvm-profdata) are available
    update-alternatives --install /usr/bin/cc cc /usr/bin/clang-20 100 && \
    update-alternatives --install /usr/bin/c++ c++ /usr/bin/clang++-20 100 && \
    update-alternatives --install /usr/bin/llvm-cov llvm-cov /usr/bin/llvm-cov-20 100 && \
    update-alternatives --install /usr/bin/llvm-profdata llvm-profdata /usr/bin/llvm-profdata-20 100 && \
    update-alternatives --set cc /usr/bin/clang-20 && update-alternatives --set c++ /usr/bin/clang++-20 && \
    ln -sf /usr/bin/clang-20 /usr/bin/clang && \
    ln -sf /usr/bin/clang++-20 /usr/bin/clang++ && \
    ln -sf /usr/bin/clang-tidy-20 /usr/bin/clang-tidy && \
    ln -sf /usr/bin/clang-format-20 /usr/local/bin/clang-format

# 设置编译器环境变量为 LLVM 20
ENV CC=/usr/bin/clang-20
ENV CXX=/usr/bin/clang++-20

RUN apt-get update && apt-get install -y --no-install-recommends \
    libc6-dev linux-headers-generic && \
    rm -rf /var/lib/apt/lists/*

# 构建并安装最新版本的 CMake
RUN wget https://github.com/Kitware/CMake/archive/refs/tags/v4.1.0.tar.gz -O /tmp/cmake.tar.gz && \
    mkdir -p /tmp/cmake && tar -xzf /tmp/cmake.tar.gz -C /tmp/cmake --strip-components=1 && \
    cd /tmp/cmake && ./bootstrap --parallel=${BUILD_JOBS} && make -j${BUILD_JOBS} && make install && \
    rm -rf /tmp/cmake /tmp/cmake.tar.gz

# 编译并安装 Boost 1.89，使用 Clang 默认的 libstdc++
RUN wget https://sourceforge.net/projects/boost/files/boost/1.89.0/boost_1_89_0.tar.gz -O /tmp/boost_1_89_0.tar.gz && \
    mkdir -p /tmp/boost && tar -xzf /tmp/boost_1_89_0.tar.gz -C /tmp/boost --strip-components=1 && \
    cd /tmp/boost && ./bootstrap.sh && \
    ./b2 toolset=clang install --prefix=/usr/local -j${BUILD_JOBS} && \
    rm -rf /tmp/boost /tmp/boost_1_89_0.tar.gz

# 编译并安装 GoogleTest 1.17.0，使用相同的 libstdc++
RUN wget -O /tmp/googletest-1.17.0.tar.gz https://github.com/google/googletest/releases/download/v1.17.0/googletest-1.17.0.tar.gz && \
    mkdir -p /tmp/gtest && tar -xzf /tmp/googletest-1.17.0.tar.gz -C /tmp/gtest --strip-components=1 && \
    mkdir -p /tmp/gtest/build && cd /tmp/gtest/build && \
    cmake .. -DCMAKE_CXX_COMPILER=clang++-20 -DCMAKE_BUILD_TYPE=Release && \
    cmake --build . --target install --parallel ${BUILD_JOBS} && \
    ldconfig && \
    rm -rf /tmp/gtest /tmp/googletest-1.17.0.tar.gz

# 安装 Ninja 1.11.1
RUN wget https://github.com/ninja-build/ninja/releases/download/v1.11.1/ninja-linux.zip -O /tmp/ninja-linux.zip && \
    unzip /tmp/ninja-linux.zip -d /usr/local/bin && \
    chmod +x /usr/local/bin/ninja && \
    rm /tmp/ninja-linux.zip

# 项目使用 Benchmark 1.9.4 的接口，不能使用发行版中的旧版本。
RUN curl -fsSL --retry 3 \
        https://codeload.github.com/google/benchmark/tar.gz/refs/tags/v1.9.4 \
        -o /tmp/benchmark.tar.gz && \
    mkdir /tmp/benchmark-source && \
    tar -xzf /tmp/benchmark.tar.gz -C /tmp/benchmark-source --strip-components=1 && \
    cmake -S /tmp/benchmark-source -B /tmp/benchmark-build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release -DBENCHMARK_ENABLE_TESTING=OFF && \
    cmake --build /tmp/benchmark-build --parallel ${BUILD_JOBS} && \
    cmake --install /tmp/benchmark-build && \
    rm -rf /tmp/benchmark-source /tmp/benchmark-build /tmp/benchmark.tar.gz

WORKDIR /opt/memseek
COPY . .
RUN cmake -S . -B /tmp/memseek-build -G Ninja \
        -DCMAKE_CXX_COMPILER=clang++-20 \
        -DCMAKE_BUILD_TYPE=Release \
        -DMEMSEEK_BUILD_BENCHMARKS=ON \
        -DBUILD_TESTING=ON && \
    cmake --build /tmp/memseek-build --parallel ${BUILD_JOBS} && \
    ctest --test-dir /tmp/memseek-build --output-on-failure

CMD ["bash"]
