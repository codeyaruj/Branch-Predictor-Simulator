#include "statistics.hpp"
#include "trace_parser.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace {

class TestFailure : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

void require(bool condition, const char* expression, const char* file, int line) {
    if (!condition) {
        throw TestFailure(std::string(file) + ":" + std::to_string(line) +
                          ": requirement failed: " + expression);
    }
}

#define REQUIRE(expression)                                                    \
    require(static_cast<bool>(expression), #expression, __FILE__, __LINE__)

[[nodiscard]] std::filesystem::path unique_temp_path() {
    static std::atomic<std::uint64_t> sequence{0U};
    const auto timestamp =
        std::chrono::high_resolution_clock::now().time_since_epoch().count();
    const std::uint64_t ordinal =
        sequence.fetch_add(1U, std::memory_order_relaxed);
    const auto process_marker = reinterpret_cast<std::uintptr_t>(&sequence);

    const std::string filename =
        "branchsim-test-" + std::to_string(timestamp) + "-" +
        std::to_string(process_marker) + "-" + std::to_string(ordinal) +
        ".trace";
    return std::filesystem::temp_directory_path() / filename;
}

class TemporaryTrace {
public:
    explicit TemporaryTrace(const std::string& contents)
        : path_(unique_temp_path()) {
        std::ofstream output(path_, std::ios::binary);
        if (!output.is_open()) {
            throw TestFailure("could not create temporary trace: " +
                              path_.string());
        }
        output << contents;
        output.close();
        if (!output) {
            std::error_code error;
            static_cast<void>(std::filesystem::remove(path_, error));
            throw TestFailure("could not write temporary trace: " +
                              path_.string());
        }
    }

    TemporaryTrace(const TemporaryTrace&) = delete;
    TemporaryTrace& operator=(const TemporaryTrace&) = delete;

    ~TemporaryTrace() {
        std::error_code error;
        static_cast<void>(std::filesystem::remove(path_, error));
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

private:
    std::filesystem::path path_;
};

[[nodiscard]] std::string parse_error_for(const std::string& contents) {
    const TemporaryTrace trace(contents);
    try {
        branchsim::TraceParser parser(trace.path().string());
        parser.for_each([](const branchsim::TraceRecord&) {});
    } catch (const std::runtime_error& error) {
        return error.what();
    }
    throw TestFailure("expected malformed trace to be rejected");
}

void test_valid_records_blank_comments_and_maximum_pc() {
    const TemporaryTrace trace(
        "\n"
        "  \t  \n"
        "   # comment after leading whitespace\n"
        "0x0040123A T\n"
        "\t0x0   N  \n"
        "0xFFFFFFFFFFFFFFFF T\n");

    std::vector<branchsim::TraceRecord> records;
    const branchsim::TraceParser parser(trace.path().string());
    parser.for_each([&records](const branchsim::TraceRecord& record) {
        records.push_back(record);
    });

    REQUIRE(parser.path() == trace.path().string());
    REQUIRE(records.size() == 3U);
    REQUIRE(records[0].pc == UINT64_C(0x0040123A));
    REQUIRE(records[0].taken);
    REQUIRE(records[1].pc == 0U);
    REQUIRE(!records[1].taken);
    REQUIRE(records[2].pc == UINT64_MAX);
    REQUIRE(records[2].taken);
}

void test_malformed_line_reports_exact_line_number() {
    const std::string message = parse_error_for(
        "# line one is ignored\n"
        "\n"
        "0x10\n");
    REQUIRE(message.find("at line 3:") != std::string::npos);
}

void test_bad_address_is_rejected() {
    const std::array<std::string, 2U> bad_addresses{
        "0xNOTHEX T\n", "0X1234 T\n"};
    for (const std::string& input : bad_addresses) {
        const std::string message = parse_error_for(input);
        REQUIRE(message.find("at line 1:") != std::string::npos);
    }
}

void test_bad_outcome_is_rejected() {
    const std::string message = parse_error_for("0x1234 X\n");
    REQUIRE(message.find("at line 1:") != std::string::npos);
}

void test_extra_token_is_rejected() {
    const std::string message = parse_error_for("0x1234 T extra\n");
    REQUIRE(message.find("at line 1:") != std::string::npos);
}

void test_nonexistent_file_is_rejected() {
    std::filesystem::path missing_path;
    {
        const TemporaryTrace trace("");
        missing_path = trace.path();
    }

    try {
        branchsim::TraceParser parser(missing_path.string());
        parser.for_each([](const branchsim::TraceRecord&) {});
        throw TestFailure("expected nonexistent trace to be rejected");
    } catch (const TestFailure&) {
        throw;
    } catch (const std::runtime_error& error) {
        const std::string message = error.what();
        REQUIRE(message.find(missing_path.string()) != std::string::npos);
    }
}

void test_directory_is_rejected() {
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path();
    try {
        branchsim::TraceParser parser(directory.string());
        parser.for_each([](const branchsim::TraceRecord&) {});
        throw TestFailure("expected directory trace path to be rejected");
    } catch (const TestFailure&) {
        throw;
    } catch (const std::runtime_error& error) {
        const std::string message = error.what();
        REQUIRE(message.find(directory.string()) != std::string::npos);
        REQUIRE(message.find("not a regular file") != std::string::npos);
    }
}

void test_statistics_counts_and_percentages() {
    branchsim::Statistics statistics;
    statistics.record(true, true);
    statistics.record(true, false);
    statistics.record(true, true);
    statistics.record(false, false);
    statistics.record(false, true);

    REQUIRE(statistics.total() == 5U);
    REQUIRE(statistics.taken() == 3U);
    REQUIRE(statistics.not_taken() == 2U);
    REQUIRE(statistics.correct() == 3U);
    REQUIRE(statistics.incorrect() == 2U);
    REQUIRE(std::abs(statistics.accuracy_percent() - 60.0) < 0.0001);
    REQUIRE(std::abs(statistics.misprediction_rate_percent() - 40.0) <
            0.0001);
}

void test_empty_statistics_have_zero_percentages() {
    const branchsim::Statistics statistics;
    REQUIRE(statistics.total() == 0U);
    REQUIRE(statistics.accuracy_percent() == 0.0);
    REQUIRE(statistics.misprediction_rate_percent() == 0.0);
}

struct TestCase {
    const char* name;
    void (*function)();
};

}  // namespace

int main() {
    const std::array<TestCase, 9U> tests{{
        {"valid records, blank lines, comments, and maximum PC",
         test_valid_records_blank_comments_and_maximum_pc},
        {"malformed line reports exact line number",
         test_malformed_line_reports_exact_line_number},
        {"bad address", test_bad_address_is_rejected},
        {"bad outcome", test_bad_outcome_is_rejected},
        {"extra token", test_extra_token_is_rejected},
        {"nonexistent file", test_nonexistent_file_is_rejected},
        {"directory path", test_directory_is_rejected},
        {"statistics counts and percentages",
         test_statistics_counts_and_percentages},
        {"empty statistics percentages",
         test_empty_statistics_have_zero_percentages},
    }};

    std::size_t passed = 0U;
    for (const TestCase& test : tests) {
        try {
            test.function();
            ++passed;
        } catch (const std::exception& error) {
            std::cerr << "[FAIL] " << test.name << ": " << error.what()
                      << '\n';
        } catch (...) {
            std::cerr << "[FAIL] " << test.name << ": unknown exception\n";
        }
    }

    if (passed != tests.size()) {
        std::cerr << passed << '/' << tests.size() << " tests passed\n";
        return 1;
    }

    std::cout << "All " << passed << " parser/statistics tests passed\n";
    return 0;
}
