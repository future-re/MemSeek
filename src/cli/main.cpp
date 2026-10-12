#include <CLI/CLI.hpp>
#include <algorithm>
#include <cstddef>
#include <iostream>
#include <string>
#include <thread>

#include "memseek/cli/command.hpp"
#include "memseek/cli/interactive.hpp"
#include "memseek/utils/version.hpp"

auto main(int argc, char* argv[]) -> int {
    CLI::App app{"MemSeek interactive memory scanner"};
    app.set_version_flag("--version", version::string());

    std::size_t workers{};
    app.add_option("--workers", workers, "Number of scan workers")
        ->check(CLI::PositiveNumber);

    std::string byteOrder{"native"};
    app.add_option("--byte-order", byteOrder, "Default byte order")
        ->check(CLI::IsMember({"native", "little", "big"}));
    CLI11_PARSE(app, argc, argv);

    if (workers == 0) {
        workers = std::max<std::size_t>(1, std::thread::hardware_concurrency());
    }
    const auto parsedByteOrder = memseek::cli::parseByteOrder(byteOrder);
    if (!parsedByteOrder) {
        std::cerr << "Invalid byte order: " << byteOrder << '\n';
        return 1;
    }
    const memseek::cli::Settings settings{
        .workers = workers,
        .byteOrder = *parsedByteOrder,
    };
    memseek::cli::InteractiveCli cli{settings, std::cin, std::cout, std::cerr};
    return cli.run();
}
