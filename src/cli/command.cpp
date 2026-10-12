#include "memseek/cli/command.hpp"

#include <cctype>
#include <charconv>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "memseek/cli/detail/value.hpp"

namespace memseek::cli {

namespace {

constexpr auto allRegionTypes() noexcept -> std::uint8_t {
    return static_cast<std::uint8_t>(MemoryRegionType::HEAP) |
           static_cast<std::uint8_t>(MemoryRegionType::STACK) |
           static_cast<std::uint8_t>(MemoryRegionType::ANONYMOUS) |
           static_cast<std::uint8_t>(MemoryRegionType::MODULE);
}

auto readableLevel() -> MemoryScanLevel {
    return MemoryScanLevel{
        .memoryProtection = static_cast<std::uint8_t>(MemoryProtection::READ),
        .memoryRegionType = allRegionTypes(),
    };
}

auto tokenize(const std::string_view line)
    -> std::expected<std::vector<std::string>, std::string> {
    std::vector<std::string> tokens;
    std::string token;
    char quote = '\0';
    bool escaping = false;
    bool tokenStarted = false;

    for (const char character : line) {
        if (escaping) {
            token += character;
            escaping = false;
            tokenStarted = true;
        } else if (character == '\\' && quote != '\0') {
            escaping = true;
            tokenStarted = true;
        } else if (quote != '\0') {
            if (character == quote) {
                quote = '\0';
            } else {
                token += character;
            }
        } else if (character == '\'' || character == '"') {
            quote = character;
            tokenStarted = true;
        } else if (std::isspace(static_cast<unsigned char>(character)) != 0) {
            if (tokenStarted) {
                tokens.push_back(std::move(token));
                token.clear();
                tokenStarted = false;
            }
        } else {
            token += character;
            tokenStarted = true;
        }
    }

    if (escaping || quote != '\0') {
        return std::unexpected("unterminated quote or escape");
    }
    if (tokenStarted) {
        tokens.push_back(std::move(token));
    }
    return tokens;
}

template <typename T>
auto parseInteger(const std::string_view text) -> std::optional<T> {
    T value{};
    const auto result =
        std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

auto parseRegion(const std::optional<std::string>& name, MemoryScanLevel& level)
    -> std::expected<std::string, std::string> {
    if (!name) {
        level.memoryRegionType = allRegionTypes();
        return "all";
    }
    if (*name == "heap") {
        level.memoryRegionType =
            static_cast<std::uint8_t>(MemoryRegionType::HEAP);
    } else if (*name == "stack") {
        level.memoryRegionType =
            static_cast<std::uint8_t>(MemoryRegionType::STACK);
    } else if (*name == "anonymous") {
        level.memoryRegionType =
            static_cast<std::uint8_t>(MemoryRegionType::ANONYMOUS);
    } else if (*name == "module") {
        level.memoryRegionType =
            static_cast<std::uint8_t>(MemoryRegionType::MODULE);
    } else {
        return std::unexpected("invalid region: " + *name);
    }
    return *name;
}

auto parseProtection(const std::optional<std::string>& name,
                     MemoryScanLevel& level)
    -> std::expected<std::string, std::string> {
    const auto protection = name.value_or("read");
    if (protection == "read") {
        level.memoryProtection =
            static_cast<std::uint8_t>(MemoryProtection::READ);
    } else if (protection == "write") {
        level.memoryProtection =
            static_cast<std::uint8_t>(MemoryProtection::WRITE);
    } else if (protection == "execute") {
        level.memoryProtection =
            static_cast<std::uint8_t>(MemoryProtection::EXECUTE);
    } else {
        return std::unexpected("invalid protection: " + protection);
    }
    return protection;
}

auto setOnce(std::optional<std::string>& destination, const std::string& value,
             const std::string_view option)
    -> std::expected<void, std::string> {
    if (destination) {
        return std::unexpected("duplicate option: " + std::string{option});
    }
    destination = value;
    return {};
}

struct ScanArguments {
    std::optional<std::string> type;
    std::optional<std::string> mode;
    std::optional<std::string> value;
    std::optional<std::string> minimum;
    std::optional<std::string> maximum;
    std::optional<std::string> region;
    std::optional<std::string> protection;
};

auto readScanOption(const std::vector<std::string>& tokens, std::size_t& index,
                    ScanArguments& arguments)
    -> std::expected<void, std::string> {
    const auto& option = tokens[index];
    if (index + 1 >= tokens.size()) {
        return std::unexpected("missing value for option: " + option);
    }
    const auto& value = tokens[++index];
    if (option == "--type") {
        return setOnce(arguments.type, value, option);
    }
    if (option == "--mode") {
        return setOnce(arguments.mode, value, option);
    }
    if (option == "--value") {
        return setOnce(arguments.value, value, option);
    }
    if (option == "--min") {
        return setOnce(arguments.minimum, value, option);
    }
    if (option == "--max") {
        return setOnce(arguments.maximum, value, option);
    }
    if (option == "--region") {
        return setOnce(arguments.region, value, option);
    }
    if (option == "--protection") {
        return setOnce(arguments.protection, value, option);
    }
    return std::unexpected("unknown scan option: " + option);
}

auto makeScanCommand(ScanArguments arguments)
    -> std::expected<Command, std::string> {
    if (!arguments.type) {
        return std::unexpected("scan requires --type");
    }
    const auto mode = arguments.mode.value_or("exact");
    ScanType scanType{};
    if (mode == "exact") {
        scanType = ScanType::EXACT;
    } else if (mode == "greater") {
        scanType = ScanType::GREATER;
    } else if (mode == "less") {
        scanType = ScanType::LESS;
    } else if (mode == "range") {
        scanType = ScanType::RANGE;
    } else {
        return std::unexpected("invalid scan mode: " + mode);
    }

    MemoryScanLevel level{};
    const auto regionName = parseRegion(arguments.region, level);
    if (!regionName) {
        return std::unexpected(regionName.error());
    }
    const auto protectionName = parseProtection(arguments.protection, level);
    if (!protectionName) {
        return std::unexpected(protectionName.error());
    }

    if (scanType == ScanType::RANGE) {
        if (arguments.value) {
            return std::unexpected("range scan does not accept --value");
        }
        if (!arguments.minimum || !arguments.maximum) {
            return std::unexpected("range scan requires --min and --max");
        }
        auto lower = detail::parseValue(*arguments.type, *arguments.minimum);
        if (!lower) {
            return std::unexpected(lower.error());
        }
        auto upper = detail::parseValue(*arguments.type, *arguments.maximum);
        if (!upper) {
            return std::unexpected(upper.error());
        }
        if (!lower->isNumeric() || !upper->isNumeric()) {
            return std::unexpected("range scans require a numeric type");
        }
        if (!detail::orderedBounds(*lower, *upper)) {
            return std::unexpected(
                "range lower bound must not exceed upper bound");
        }
        const auto description = "type=" + *arguments.type +
                                 " mode=range min=" + *arguments.minimum +
                                 " max=" + *arguments.maximum +
                                 " region=" + *regionName +
                                 " protection=" + *protectionName;
        return Command{ScanCommand{
            .scanType = scanType,
            .lowerBound = std::move(*lower),
            .upperBound = std::move(*upper),
            .level = level,
            .description = description,
        }};
    }

    if (arguments.minimum || arguments.maximum) {
        return std::unexpected("only range scans accept --min and --max");
    }
    if (!arguments.value) {
        return std::unexpected("scan requires --value");
    }
    auto value = detail::parseValue(*arguments.type, *arguments.value);
    if (!value) {
        return std::unexpected(value.error());
    }
    if (scanType != ScanType::EXACT && !value->isNumeric()) {
        return std::unexpected("ordering scans require a numeric type");
    }
    const auto description = "type=" + *arguments.type + " mode=" + mode +
                             " value=" + *arguments.value +
                             " region=" + *regionName +
                             " protection=" + *protectionName;
    return Command{ScanCommand{
        .scanType = scanType,
        .lowerBound = std::move(*value),
        .upperBound = std::nullopt,
        .level = level,
        .description = description,
    }};
}

auto parseScan(const std::vector<std::string>& tokens)
    -> std::expected<Command, std::string> {
    if (tokens.size() < 2) {
        return std::unexpected("usage: scan <type> <value> [options]");
    }

    ScanArguments arguments;
    if (tokens[1].starts_with("--")) {
        for (std::size_t index = 1; index < tokens.size(); ++index) {
            if (!tokens[index].starts_with("--")) {
                return std::unexpected(
                    "positional scan arguments cannot be mixed with --type");
            }
            const auto result = readScanOption(tokens, index, arguments);
            if (!result) {
                return std::unexpected(result.error());
            }
        }
        return makeScanCommand(std::move(arguments));
    }

    arguments.type = tokens[1];
    if (tokens.size() < 3 || tokens[2].starts_with("--")) {
        return std::unexpected("usage: scan <type> <value> [options]");
    }

    std::size_t index = 2;
    if (tokens[index] == "range") {
        arguments.mode = "range";
        if (index + 2 >= tokens.size() || tokens[index + 1].starts_with("--") ||
            tokens[index + 2].starts_with("--")) {
            return std::unexpected(
                "usage: scan <type> range <min> <max> [options]");
        }
        arguments.minimum = tokens[index + 1];
        arguments.maximum = tokens[index + 2];
        index += 3;
    } else if (tokens[index] == "greater" || tokens[index] == "less") {
        arguments.mode = tokens[index];
        if (index + 1 >= tokens.size() || tokens[index + 1].starts_with("--")) {
            return std::unexpected(
                "usage: scan <type> greater|less <value> [options]");
        }
        arguments.value = tokens[index + 1];
        index += 2;
    } else {
        arguments.mode = "exact";
        arguments.value = tokens[index++];
    }

    for (; index < tokens.size(); ++index) {
        if (tokens[index] != "--region" && tokens[index] != "--protection") {
            if (tokens[index].starts_with("--")) {
                return std::unexpected(
                    "positional scan syntax only accepts --region and "
                    "--protection");
            }
            return std::unexpected("unexpected scan argument: " +
                                   tokens[index]);
        }
        const auto result = readScanOption(tokens, index, arguments);
        if (!result) {
            return std::unexpected(result.error());
        }
    }
    return makeScanCommand(std::move(arguments));
}

auto parseResults(const std::vector<std::string>& tokens)
    -> std::expected<Command, std::string> {
    std::optional<std::size_t> positionalLimit;
    std::optional<std::size_t> optionLimit;
    std::optional<OutputFormat> format;

    for (std::size_t index = 1; index < tokens.size(); ++index) {
        const auto& token = tokens[index];
        if (token == "--limit") {
            if (optionLimit) {
                return std::unexpected("duplicate option: --limit");
            }
            if (index + 1 >= tokens.size()) {
                return std::unexpected("missing value for option: --limit");
            }
            const auto parsed = parseInteger<std::size_t>(tokens[++index]);
            if (!parsed) {
                return std::unexpected("invalid result limit");
            }
            optionLimit = *parsed;
        } else if (token == "--format") {
            if (format) {
                return std::unexpected("duplicate option: --format");
            }
            if (index + 1 >= tokens.size()) {
                return std::unexpected("missing value for option: --format");
            }
            const auto& name = tokens[++index];
            if (name == "text") {
                format = OutputFormat::TEXT;
            } else if (name == "json") {
                format = OutputFormat::JSON;
            } else {
                return std::unexpected("invalid result format: " + name);
            }
        } else if (token.starts_with("--")) {
            return std::unexpected("unknown results option: " + token);
        } else {
            if (positionalLimit) {
                return std::unexpected("too many positional result limits");
            }
            const auto parsed = parseInteger<std::size_t>(token);
            if (!parsed) {
                return std::unexpected("invalid result limit");
            }
            positionalLimit = *parsed;
        }
    }

    if (positionalLimit && optionLimit) {
        return std::unexpected(
            "result limit cannot be both positional and --limit");
    }
    return Command{ResultsCommand{
        .limit = optionLimit ? optionLimit : positionalLimit,
        .format = format.value_or(OutputFormat::TEXT),
    }};
}

auto parseRegions(const std::vector<std::string>& tokens)
    -> std::expected<Command, std::string> {
    std::optional<std::string> region;
    for (std::size_t index = 1; index < tokens.size(); ++index) {
        if (tokens[index] != "--region") {
            return std::unexpected("unknown regions option: " + tokens[index]);
        }
        if (index + 1 >= tokens.size()) {
            return std::unexpected("missing value for option: --region");
        }
        const auto result = setOnce(region, tokens[++index], "--region");
        if (!result) {
            return std::unexpected(result.error());
        }
    }

    auto level = readableLevel();
    const auto parsed = parseRegion(region, level);
    if (!parsed) {
        return std::unexpected(parsed.error());
    }
    return Command{RegionsCommand{.level = level}};
}

}  // namespace

auto parseByteOrder(const std::string_view text) -> std::optional<ByteOrder> {
    if (text == "native") {
        return ByteOrder::NATIVE;
    }
    if (text == "little") {
        return ByteOrder::LITTLE;
    }
    if (text == "big") {
        return ByteOrder::BIG;
    }
    return std::nullopt;
}

auto byteOrderText(const ByteOrder byteOrder) -> std::string_view {
    switch (byteOrder) {
        case ByteOrder::NATIVE:
            return "native";
        case ByteOrder::LITTLE:
            return "little";
        case ByteOrder::BIG:
            return "big";
    }
    return "unknown";
}

auto parseCommandLine(const std::string_view line)
    -> std::expected<std::optional<Command>, std::string> {
    const auto tokens = tokenize(line);
    if (!tokens) {
        return std::unexpected(tokens.error());
    }
    if (tokens->empty()) {
        return std::optional<Command>{};
    }

    const auto& name = tokens->front();
    if (name == "attach") {
        if (tokens->size() != 2) {
            return std::unexpected("usage: attach <PID>");
        }
        const auto pid = parseInteger<pid_t>((*tokens)[1]);
        if (!pid || *pid <= 0) {
            return std::unexpected("invalid PID");
        }
        return std::optional<Command>{Command{AttachCommand{.pid = *pid}}};
    }
    if (name == "scan") {
        const auto command = parseScan(*tokens);
        if (!command) {
            return std::unexpected(command.error());
        }
        return std::optional<Command>{std::move(*command)};
    }
    if (name == "filter") {
        if (tokens->size() != 2 ||
            ((*tokens)[1] != "changed" && (*tokens)[1] != "unchanged")) {
            return std::unexpected("usage: filter changed|unchanged");
        }
        return std::optional<Command>{Command{FilterCommand{
            .type = (*tokens)[1] == "changed" ? ChangeType::CHANGED
                                              : ChangeType::UNCHANGED,
        }}};
    }
    if (name == "results") {
        const auto command = parseResults(*tokens);
        if (!command) {
            return std::unexpected(command.error());
        }
        return std::optional<Command>{std::move(*command)};
    }
    if (name == "regions") {
        const auto command = parseRegions(*tokens);
        if (!command) {
            return std::unexpected(command.error());
        }
        return std::optional<Command>{std::move(*command)};
    }

    const auto noArguments = [&]() -> std::expected<void, std::string> {
        if (tokens->size() != 1) {
            return std::unexpected(name + " does not accept arguments");
        }
        return {};
    };
    if (name == "info") {
        if (const auto result = noArguments(); !result) {
            return std::unexpected(result.error());
        }
        return std::optional<Command>{Command{InfoCommand{}}};
    }
    if (name == "reset") {
        if (const auto result = noArguments(); !result) {
            return std::unexpected(result.error());
        }
        return std::optional<Command>{Command{ResetCommand{}}};
    }
    if (name == "help") {
        if (const auto result = noArguments(); !result) {
            return std::unexpected(result.error());
        }
        return std::optional<Command>{Command{HelpCommand{}}};
    }
    if (name == "quit" || name == "exit") {
        if (const auto result = noArguments(); !result) {
            return std::unexpected(result.error());
        }
        return std::optional<Command>{Command{QuitCommand{}}};
    }
    return std::unexpected("unknown command; type 'help' for commands");
}

}  // namespace memseek::cli
