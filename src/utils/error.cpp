#include "memseek/error.hpp"

#include <iomanip>
#include <sstream>

namespace memseek {

auto makeError(const ErrorCode code, std::string message) -> MemSeekError {
    MemSeekError error;
    error.code = code;
    error.message = std::move(message);
    return error;
}

auto errorCodeName(const ErrorCode code) noexcept -> const char* {
    switch (code) {
        case ErrorCode::INVALID_ARGUMENT:
            return "invalid_argument";
        case ErrorCode::INVALID_VALUE:
            return "invalid_value";
        case ErrorCode::UNSUPPORTED_OPERATION:
            return "unsupported_operation";
        case ErrorCode::UNSUPPORTED_PLATFORM:
            return "unsupported_platform";
        case ErrorCode::MAPPING_QUERY_FAILED:
            return "mapping_query_failed";
        case ErrorCode::PROCESS_ACCESS_FAILED:
            return "process_access_failed";
        case ErrorCode::ADDRESS_OUT_OF_RANGE:
            return "address_out_of_range";
        case ErrorCode::MEMORY_READ_FAILED:
            return "memory_read_failed";
        case ErrorCode::MEMORY_WRITE_FAILED:
            return "memory_write_failed";
        case ErrorCode::MAPPING_NOT_WRITABLE:
            return "mapping_not_writable";
        case ErrorCode::REGION_MISMATCH:
            return "region_mismatch";
        case ErrorCode::SHORT_TRANSFER:
            return "short_transfer";
        case ErrorCode::NO_BASELINE:
            return "no_baseline";
    }
    return "unknown_error";
}

auto formatError(const MemSeekError& error) -> std::string {
    std::ostringstream output;
    output << '[' << errorCodeName(error.code) << "] " << error.message;
    const bool hasContext = error.pid || error.address || error.size ||
                            error.bytesTransferred || error.systemError;
    if (hasContext) {
        output << " (";
        bool needsSpace = false;
        if (error.pid) {
            output << "pid=" << *error.pid;
            needsSpace = true;
        }
        if (error.address) {
            output << (needsSpace ? " " : "") << "address=0x" << std::hex
                   << *error.address << std::dec;
            needsSpace = true;
        }
        if (error.size) {
            output << (needsSpace ? " " : "") << "size=" << *error.size;
            needsSpace = true;
        }
        if (error.bytesTransferred) {
            output << (needsSpace ? " " : "")
                   << "bytes_transferred=" << *error.bytesTransferred;
            needsSpace = true;
        }
        if (error.systemError) {
            output << (needsSpace ? " " : "")
                   << "system_error=" << error.systemError.message();
        }
        output << ')';
    }
    return output.str();
}

}  // namespace memseek
