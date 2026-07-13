#include "predictors.hpp"
#include "simulator.hpp"
#include "trace_parser.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace {

class TestFailure final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

#define CHECK(condition)                                                      \
    do {                                                                      \
        if (!(condition)) {                                                   \
            throw TestFailure(std::string("CHECK failed: ") + #condition +   \
                              " at line " + std::to_string(__LINE__));        \
        }                                                                     \
    } while (false)

class TemporaryTrace {
public:
    explicit TemporaryTrace(const std::string& contents) {
        static std::atomic<std::uint64_t> sequence{0U};
        const std::uint64_t ordinal =
            sequence.fetch_add(1U, std::memory_order_relaxed);
        const auto timestamp =
            std::chrono::high_resolution_clock::now().time_since_epoch().count();
        const auto process_marker =
            reinterpret_cast<std::uintptr_t>(&sequence);
        path_ = std::filesystem::temp_directory_path() /
                ("branchsim-simulator-test-" + std::to_string(timestamp) + "-" +
                 std::to_string(process_marker) + "-" +
                 std::to_string(ordinal) + ".trace");
        std::ofstream output(path_);
        if (!output) {
            throw TestFailure("unable to create temporary trace");
        }
        output << contents;
        if (!output) {
            throw TestFailure("unable to write temporary trace");
        }
    }

    TemporaryTrace(const TemporaryTrace&) = delete;
    TemporaryTrace& operator=(const TemporaryTrace&) = delete;

    ~TemporaryTrace() {
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
    }

    [[nodiscard]] std::string string() const { return path_.string(); }

private:
    std::filesystem::path path_;
};

void deterministic_simulation() {
    const TemporaryTrace trace("# one shared counter\n"
                               "0x10 T\n"
                               "0x20 T\n"
                               "0x30 N\n"
                               "0x40 N\n");
    branchsim::Simulator simulator(
        std::make_unique<branchsim::TwoBitPredictor>(0U, 1U));

    const branchsim::SimulationResult result =
        simulator.run(branchsim::TraceParser(trace.string()));

    CHECK(result.predictor == "Two-Bit");
    CHECK(result.statistics.total() == 4U);
    CHECK(result.statistics.taken() == 2U);
    CHECK(result.statistics.not_taken() == 2U);
    CHECK(result.statistics.correct() == 1U);
    CHECK(result.statistics.incorrect() == 3U);
    CHECK(result.statistics.accuracy_percent() == 25.0);
    CHECK(result.table_entries == 1U);
    CHECK(result.table_memory_bits == 2U);
    CHECK(!result.history_bits.has_value());
    CHECK(!result.final_history.has_value());
}

void verbose_output_contains_exact_transitions() {
    const TemporaryTrace trace("0xA T\n0xA N\n");
    branchsim::Simulator simulator(
        std::make_unique<branchsim::GSharePredictor>(0U, 3U, 1U));
    std::ostringstream verbose;

    const auto result =
        simulator.run(branchsim::TraceParser(trace.string()), &verbose);

    CHECK(verbose.str() ==
          "PC=0x0000000A actual=T predicted=N result=MISS index=0 "
          "state=1->2 history=000->001\n"
          "PC=0x0000000A actual=N predicted=T result=MISS index=0 "
          "state=2->1 history=001->010\n");
    CHECK(result.history_bits == 3U);
    CHECK(result.final_history == 2U);
    CHECK(result.statistics.correct() == 0U);
}

void empty_trace_is_valid() {
    const TemporaryTrace trace("  \n# no branches\n");
    branchsim::Simulator simulator(
        std::make_unique<branchsim::AlwaysTakenPredictor>());

    const auto result =
        simulator.run(branchsim::TraceParser(trace.string()));

    CHECK(result.statistics.total() == 0U);
    CHECK(result.statistics.accuracy_percent() == 0.0);
    CHECK(result.statistics.misprediction_rate_percent() == 0.0);
    CHECK(!result.table_entries.has_value());
    CHECK(result.table_memory_bits == 0U);
}

void null_predictor_is_rejected() {
    bool threw = false;
    try {
        branchsim::Simulator simulator(
            std::unique_ptr<branchsim::BranchPredictor>{});
        static_cast<void>(simulator);
    } catch (const std::invalid_argument& error) {
        threw = std::string(error.what()).find("predictor") != std::string::npos;
    }
    CHECK(threw);
}

}  // namespace

int main() {
    const std::vector<std::pair<std::string, std::function<void()>>> tests{
        {"deterministic simulation", deterministic_simulation},
        {"verbose transitions", verbose_output_contains_exact_transitions},
        {"empty trace", empty_trace_is_valid},
        {"null predictor", null_predictor_is_rejected},
    };

    std::size_t passed = 0U;
    for (const auto& test : tests) {
        try {
            test.second();
            ++passed;
        } catch (const std::exception& error) {
            std::cerr << "FAIL: " << test.first << ": " << error.what() << '\n';
        }
    }

    if (passed != tests.size()) {
        std::cerr << passed << '/' << tests.size() << " simulator tests passed\n";
        return 1;
    }

    std::cout << "All " << passed << " simulator tests passed\n";
    return 0;
}
