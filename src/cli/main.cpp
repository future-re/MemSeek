#include <CLI/CLI.hpp>

#include "memseek/utils/version.hpp"

auto main(int argc, char* argv[]) -> int {
    CLI::App app{"MemSeek memory scanner"};
    app.set_version_flag("--version", version::string());

    CLI11_PARSE(app, argc, argv);
    return 0;
}
