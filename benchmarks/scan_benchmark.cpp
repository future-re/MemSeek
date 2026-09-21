#include <benchmark/benchmark.h>

int main(int argc, char** argv) {
    benchmark::MaybeReenterWithoutASLR(argc, argv);

    benchmark::Initialize(&argc, argv);
    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();
    return 0;
}
