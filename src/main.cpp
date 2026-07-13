#include "predictors.hpp"
#include "simulator.hpp"
#include "trace_parser.hpp"

#include <array>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace branchsim {
namespace {

constexpr unsigned kDefaultTableBits = 10U;
constexpr unsigned kDefaultHistoryBits = 8U;
constexpr unsigned kMaximumTableBits = 24U;
constexpr unsigned kMaximumHistoryBits = 63U;

enum class PredictorKind {
    always_taken,
    always_not_taken,
    one_bit,
    two_bit,
    gshare,
};

enum class InitialState {
    default_state,
    taken,
    not_taken,
    strongly_taken,
    weakly_taken,
    weakly_not_taken,
    strongly_not_taken,
};

struct Options {
    std::string trace_path;
    PredictorKind predictor{PredictorKind::two_bit};
    unsigned table_bits{kDefaultTableBits};
    unsigned history_bits{kDefaultHistoryBits};
    InitialState initial_state{InitialState::default_state};
    std::optional<std::string> csv_path;
    bool compare{false};
    bool verbose{false};
    bool predictor_explicit{false};
    bool table_bits_explicit{false};
    bool history_bits_explicit{false};
    bool initial_state_explicit{false};
};

[[noreturn]] void option_error(const std::string& message) {
    throw std::invalid_argument(message);
}

void print_help(std::ostream& output) {
    output
        << "Branch Predictor Simulator\n\n"
        << "Usage:\n"
        << "  branchsim <trace-file> [options]\n\n"
        << "Options:\n"
        << "  --predictor <name>       always-taken, always-not-taken, one-bit,\n"
        << "                           two-bit (default), or gshare\n"
        << "  --table-bits <N>         Table index bits, 0-24 (default: 10)\n"
        << "  --history-bits <N>       GShare history bits, 1-63 (default: 8)\n"
        << "  --initial-state <state>  taken, not-taken, strongly-taken,\n"
        << "                           weakly-taken, weakly-not-taken, or\n"
        << "                           strongly-not-taken\n"
        << "  --compare                Run all five predictors\n"
        << "  --csv <path>             Write result rows to a CSV file\n"
        << "  --verbose                Print one line for every branch\n"
        << "  --help                   Show this help text\n\n"
        << "Examples:\n"
        << "  branchsim trace.txt --predictor gshare --history-bits 8 --table-bits 10\n"
        << "  branchsim trace.txt --compare --csv results.csv\n";
}

PredictorKind parse_predictor(std::string_view value) {
    if (value == "always-taken") {
        return PredictorKind::always_taken;
    }
    if (value == "always-not-taken") {
        return PredictorKind::always_not_taken;
    }
    if (value == "one-bit") {
        return PredictorKind::one_bit;
    }
    if (value == "two-bit") {
        return PredictorKind::two_bit;
    }
    if (value == "gshare") {
        return PredictorKind::gshare;
    }
    option_error("invalid predictor '" + std::string(value) + "'");
}

InitialState parse_initial_state(std::string_view value) {
    if (value == "taken") {
        return InitialState::taken;
    }
    if (value == "not-taken") {
        return InitialState::not_taken;
    }
    if (value == "strongly-taken") {
        return InitialState::strongly_taken;
    }
    if (value == "weakly-taken") {
        return InitialState::weakly_taken;
    }
    if (value == "weakly-not-taken") {
        return InitialState::weakly_not_taken;
    }
    if (value == "strongly-not-taken") {
        return InitialState::strongly_not_taken;
    }
    option_error("invalid initial state '" + std::string(value) + "'");
}

unsigned parse_unsigned(std::string_view text, std::string_view option) {
    unsigned value = 0U;
    const auto result =
        std::from_chars(text.data(), text.data() + text.size(), value, 10);
    if (text.empty() || result.ec != std::errc{} ||
        result.ptr != text.data() + text.size()) {
        option_error(std::string(option) + " requires a non-negative integer");
    }
    return value;
}

std::string_view require_value(int& index, int argc, char* argv[],
                               std::string_view option) {
    if (index + 1 >= argc) {
        option_error(std::string(option) + " requires a value");
    }
    const std::string_view value(argv[index + 1]);
    if (value.rfind("--", 0U) == 0U) {
        option_error(std::string(option) + " requires a value");
    }
    ++index;
    return value;
}

Options parse_options(int argc, char* argv[]) {
    Options options;
    bool compare_seen = false;
    bool verbose_seen = false;
    bool csv_seen = false;

    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "--predictor") {
            if (options.predictor_explicit) {
                option_error("--predictor may only be specified once");
            }
            options.predictor = parse_predictor(
                require_value(index, argc, argv, argument));
            options.predictor_explicit = true;
        } else if (argument == "--table-bits") {
            if (options.table_bits_explicit) {
                option_error("--table-bits may only be specified once");
            }
            options.table_bits = parse_unsigned(
                require_value(index, argc, argv, argument), argument);
            options.table_bits_explicit = true;
        } else if (argument == "--history-bits") {
            if (options.history_bits_explicit) {
                option_error("--history-bits may only be specified once");
            }
            options.history_bits = parse_unsigned(
                require_value(index, argc, argv, argument), argument);
            options.history_bits_explicit = true;
        } else if (argument == "--initial-state") {
            if (options.initial_state_explicit) {
                option_error("--initial-state may only be specified once");
            }
            options.initial_state = parse_initial_state(
                require_value(index, argc, argv, argument));
            options.initial_state_explicit = true;
        } else if (argument == "--csv") {
            if (csv_seen) {
                option_error("--csv may only be specified once");
            }
            options.csv_path = std::string(
                require_value(index, argc, argv, argument));
            if (options.csv_path->empty()) {
                option_error("--csv requires a non-empty path");
            }
            csv_seen = true;
        } else if (argument == "--compare") {
            if (compare_seen) {
                option_error("--compare may only be specified once");
            }
            options.compare = true;
            compare_seen = true;
        } else if (argument == "--verbose") {
            if (verbose_seen) {
                option_error("--verbose may only be specified once");
            }
            options.verbose = true;
            verbose_seen = true;
        } else if (!argument.empty() && argument.front() == '-') {
            option_error("unknown option '" + std::string(argument) + "'");
        } else if (!options.trace_path.empty()) {
            option_error("multiple trace files were provided");
        } else {
            options.trace_path = std::string(argument);
        }
    }

    if (options.trace_path.empty()) {
        option_error("a trace file is required");
    }
    if (options.compare && options.verbose) {
        option_error("--compare cannot be combined with --verbose");
    }
    if (options.compare && options.predictor_explicit) {
        option_error("--compare cannot be combined with --predictor");
    }
    if (options.table_bits > kMaximumTableBits) {
        option_error("--table-bits must be between 0 and 24");
    }
    if (options.history_bits == 0U ||
        options.history_bits > kMaximumHistoryBits) {
        option_error("--history-bits must be between 1 and 63");
    }

    const bool selected_predictor_has_table =
        options.predictor == PredictorKind::one_bit ||
        options.predictor == PredictorKind::two_bit ||
        options.predictor == PredictorKind::gshare;
    if (!options.compare && options.table_bits_explicit &&
        !selected_predictor_has_table) {
        option_error("--table-bits is only valid for table-based predictors or --compare");
    }
    if (!options.compare && options.history_bits_explicit &&
        options.predictor != PredictorKind::gshare) {
        option_error("--history-bits is only valid with gshare or --compare");
    }
    if (!options.compare && options.initial_state_explicit &&
        !selected_predictor_has_table) {
        option_error("--initial-state is only valid for table-based predictors or --compare");
    }

    return options;
}

bool initial_one_bit_state(InitialState state) {
    return state == InitialState::taken || state == InitialState::weakly_taken ||
           state == InitialState::strongly_taken;
}

std::uint8_t initial_counter_state(InitialState state) {
    switch (state) {
        case InitialState::strongly_not_taken:
            return 0U;
        case InitialState::default_state:
        case InitialState::not_taken:
        case InitialState::weakly_not_taken:
            return 1U;
        case InitialState::taken:
        case InitialState::weakly_taken:
            return 2U;
        case InitialState::strongly_taken:
            return 3U;
    }
    throw std::logic_error("unhandled initial state");
}

std::unique_ptr<BranchPredictor> make_predictor(PredictorKind kind,
                                                const Options& options) {
    switch (kind) {
        case PredictorKind::always_taken:
            return std::make_unique<AlwaysTakenPredictor>();
        case PredictorKind::always_not_taken:
            return std::make_unique<AlwaysNotTakenPredictor>();
        case PredictorKind::one_bit:
            return std::make_unique<OneBitPredictor>(
                options.table_bits, initial_one_bit_state(options.initial_state));
        case PredictorKind::two_bit:
            return std::make_unique<TwoBitPredictor>(
                options.table_bits,
                initial_counter_state(options.initial_state));
        case PredictorKind::gshare:
            return std::make_unique<GSharePredictor>(
                options.table_bits, options.history_bits,
                initial_counter_state(options.initial_state));
    }
    throw std::logic_error("unhandled predictor kind");
}

std::string history_string(std::uint64_t history, unsigned bits) {
    std::string result(bits, '0');
    for (unsigned index = 0U; index < bits; ++index) {
        const unsigned shift = bits - index - 1U;
        if (((history >> shift) & 1U) != 0U) {
            result[index] = '1';
        }
    }
    return result;
}

void print_summary(const SimulationResult& result, std::ostream& output) {
    output << "Predictor: " << result.predictor << '\n'
           << "Total branches: " << result.statistics.total() << '\n'
           << "Taken branches: " << result.statistics.taken() << '\n'
           << "Not-taken branches: " << result.statistics.not_taken() << '\n'
           << "Correct predictions: " << result.statistics.correct() << '\n'
           << "Incorrect predictions: " << result.statistics.incorrect() << '\n'
           << std::fixed << std::setprecision(2)
           << "Prediction accuracy: " << result.statistics.accuracy_percent()
           << "%\n"
           << "Misprediction rate: "
           << result.statistics.misprediction_rate_percent() << "%\n";

    if (result.table_entries.has_value()) {
        const std::size_t bytes = (result.table_memory_bits + 7U) / 8U;
        output << "Table entries: " << *result.table_entries << '\n'
               << "Table memory usage: " << result.table_memory_bits
               << (result.table_memory_bits == 1U ? " bit (" : " bits (")
               << bytes << (bytes == 1U ? " byte)\n" : " bytes)\n");
    }
    if (result.history_bits.has_value() && result.final_history.has_value()) {
        output << "History bits: " << *result.history_bits << '\n'
               << "Final global history: "
               << history_string(*result.final_history, *result.history_bits)
               << '\n';
    }
}

void print_comparison(const std::vector<SimulationResult>& results,
                      std::ostream& output) {
    output << std::left << std::setw(22) << "Predictor" << std::right
           << std::setw(12) << "Correct" << std::setw(14) << "Incorrect"
           << std::setw(14) << "Accuracy" << '\n';
    for (const auto& result : results) {
        output << std::left << std::setw(22) << result.predictor << std::right
               << std::setw(12) << result.statistics.correct()
               << std::setw(14) << result.statistics.incorrect() << std::setw(13)
               << std::fixed << std::setprecision(2)
               << result.statistics.accuracy_percent() << "%\n";
    }
}

void validate_csv_destination(const std::string& trace_path,
                              const std::string& csv_path) {
    namespace fs = std::filesystem;

    std::error_code trace_canonical_error;
    std::error_code csv_canonical_error;
    const fs::path canonical_trace =
        fs::weakly_canonical(trace_path, trace_canonical_error);
    const fs::path canonical_csv =
        fs::weakly_canonical(csv_path, csv_canonical_error);
    if (!trace_canonical_error && !csv_canonical_error &&
        canonical_trace == canonical_csv) {
        option_error("CSV output must not overwrite the input trace");
    }

    std::error_code equivalent_error;
    if (fs::equivalent(trace_path, csv_path, equivalent_error) &&
        !equivalent_error) {
        option_error("CSV output must not overwrite the input trace");
    }
}

void write_csv(const std::string& path,
               const std::vector<SimulationResult>& results) {
    std::ofstream output(path);
    if (!output) {
        throw std::runtime_error("cannot open CSV output '" + path + "'");
    }

    output << "predictor,total,correct,incorrect,accuracy,misprediction_rate\n";
    output << std::fixed << std::setprecision(2);
    for (const auto& result : results) {
        output << result.predictor << ',' << result.statistics.total() << ','
               << result.statistics.correct() << ','
               << result.statistics.incorrect() << ','
               << result.statistics.accuracy_percent() << ','
               << result.statistics.misprediction_rate_percent() << '\n';
    }
    output.flush();
    if (!output) {
        throw std::runtime_error("failed while writing CSV output '" + path + "'");
    }
}

std::vector<SimulationResult> run_simulations(const Options& options) {
    const TraceParser trace(options.trace_path);
    std::vector<SimulationResult> results;

    if (options.compare) {
        constexpr std::array<PredictorKind, 5> kinds{
            PredictorKind::always_taken, PredictorKind::always_not_taken,
            PredictorKind::one_bit, PredictorKind::two_bit,
            PredictorKind::gshare};
        results.reserve(kinds.size());
        for (const PredictorKind kind : kinds) {
            Simulator simulator(make_predictor(kind, options));
            results.push_back(simulator.run(trace));
        }
    } else {
        Simulator simulator(make_predictor(options.predictor, options));
        std::ostream* verbose_output = options.verbose ? &std::cout : nullptr;
        results.push_back(simulator.run(trace, verbose_output));
    }
    return results;
}

}  // namespace
}  // namespace branchsim

int main(int argc, char* argv[]) {
    using namespace branchsim;

    for (int index = 1; index < argc; ++index) {
        if (std::string_view(argv[index]) == "--help") {
            print_help(std::cout);
            return 0;
        }
    }

    try {
        const Options options = parse_options(argc, argv);
        if (options.csv_path.has_value()) {
            validate_csv_destination(options.trace_path, *options.csv_path);
        }
        const std::vector<SimulationResult> results = run_simulations(options);

        if (options.csv_path.has_value()) {
            write_csv(*options.csv_path, results);
        }
        if (options.compare) {
            print_comparison(results, std::cout);
        } else {
            print_summary(results.front(), std::cout);
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n'
                  << "Try 'branchsim --help' for usage.\n";
        return 1;
    }
}
