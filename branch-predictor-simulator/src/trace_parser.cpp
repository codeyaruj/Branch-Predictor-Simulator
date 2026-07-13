#include "trace_parser.hpp"

#include <charconv>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace branchsim {
namespace {

[[nodiscard]] bool is_whitespace(char character) noexcept {
    return std::isspace(static_cast<unsigned char>(character)) != 0;
}

[[nodiscard]] std::string_view trim(std::string_view text) noexcept {
    while (!text.empty() && is_whitespace(text.front())) {
        text.remove_prefix(1U);
    }
    while (!text.empty() && is_whitespace(text.back())) {
        text.remove_suffix(1U);
    }
    return text;
}

[[noreturn]] void throw_malformed(const std::string& path,
                                  std::uint64_t line_number) {
    throw std::runtime_error("Malformed trace line in '" + path +
                             "' at line " + std::to_string(line_number) +
                             ": expected '0x<hex-address> T' or "
                             "'0x<hex-address> N'");
}

[[nodiscard]] TraceRecord parse_record(std::string_view line,
                                       const std::string& path,
                                       std::uint64_t line_number) {
    std::size_t position = 0U;
    while (position < line.size() && !is_whitespace(line[position])) {
        ++position;
    }

    const std::string_view address_token = line.substr(0U, position);
    while (position < line.size() && is_whitespace(line[position])) {
        ++position;
    }

    const std::size_t outcome_begin = position;
    while (position < line.size() && !is_whitespace(line[position])) {
        ++position;
    }
    const std::string_view outcome_token =
        line.substr(outcome_begin, position - outcome_begin);

    while (position < line.size() && is_whitespace(line[position])) {
        ++position;
    }

    if (position != line.size() || address_token.size() <= 2U ||
        address_token[0] != '0' || address_token[1] != 'x' ||
        outcome_token.size() != 1U ||
        (outcome_token[0] != 'T' && outcome_token[0] != 'N')) {
        throw_malformed(path, line_number);
    }

    std::uint64_t pc = 0U;
    const char* const digits_begin = address_token.data() + 2;
    const char* const digits_end = address_token.data() + address_token.size();
    const auto conversion = std::from_chars(digits_begin, digits_end, pc, 16);
    if (conversion.ec != std::errc{} || conversion.ptr != digits_end) {
        throw_malformed(path, line_number);
    }

    return TraceRecord{pc, outcome_token[0] == 'T'};
}

}  // namespace

TraceParser::TraceParser(std::string path) : path_(std::move(path)) {}

void TraceParser::for_each(
    const std::function<void(const TraceRecord&)>& callback) const {
    if (!callback) {
        throw std::invalid_argument("Trace parser callback must not be empty");
    }

    std::error_code status_error;
    const std::filesystem::file_status status =
        std::filesystem::status(path_, status_error);
    if (status_error || !std::filesystem::exists(status)) {
        throw std::runtime_error("Unable to open trace file '" + path_ + "'");
    }
    if (!std::filesystem::is_regular_file(status)) {
        throw std::runtime_error("Trace path '" + path_ +
                                 "' is not a regular file");
    }

    std::ifstream input(path_);
    if (!input.is_open()) {
        throw std::runtime_error("Unable to open trace file '" + path_ + "'");
    }

    std::string line;
    std::uint64_t line_number = 0U;
    while (std::getline(input, line)) {
        ++line_number;
        const std::string_view content = trim(line);
        if (content.empty() || content.front() == '#') {
            continue;
        }

        const TraceRecord record = parse_record(content, path_, line_number);
        callback(record);
    }

    if (!input.eof()) {
        throw std::runtime_error("Failed while reading trace file '" + path_ +
                                 "' after line " +
                                 std::to_string(line_number));
    }
}

const std::string& TraceParser::path() const noexcept { return path_; }

}  // namespace branchsim
