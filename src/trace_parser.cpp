#include "trace_parser.hpp"

#include <charconv>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

namespace branchsim {
namespace {

bool is_blank_or_comment(const std::string& line) {
    for (char character : line) {
        const unsigned char value = static_cast<unsigned char>(character);
        if (std::isspace(value) == 0) {
            return character == '#';
        }
    }
    return true;
}

[[noreturn]] void throw_malformed(const std::string& path,
                                  std::uint64_t line_number) {
    throw std::runtime_error("Malformed trace line in '" + path +
                             "' at line " + std::to_string(line_number) +
                             ": expected '0x<hex-address> T' or "
                             "'0x<hex-address> N'");
}

TraceRecord parse_record(const std::string& line, const std::string& path,
                         std::uint64_t line_number) {
    std::istringstream input(line);
    std::string address;
    std::string outcome;
    std::string extra;

    if (!(input >> address >> outcome)) {
        throw_malformed(path, line_number);
    }
    if (input >> extra) {
        throw_malformed(path, line_number);
    }

    if (address.size() <= 2U || address[0] != '0' || address[1] != 'x') {
        throw_malformed(path, line_number);
    }
    if (outcome != "T" && outcome != "N") {
        throw_malformed(path, line_number);
    }

    std::uint64_t pc = 0U;
    const char* digits_begin = address.data() + 2;
    const char* digits_end = address.data() + address.size();
    const auto conversion = std::from_chars(digits_begin, digits_end, pc, 16);
    if (conversion.ec != std::errc{} || conversion.ptr != digits_end) {
        throw_malformed(path, line_number);
    }

    TraceRecord record;
    record.pc = pc;
    record.taken = outcome == "T";
    return record;
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
        if (is_blank_or_comment(line)) {
            continue;
        }

        const TraceRecord record = parse_record(line, path_, line_number);
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
