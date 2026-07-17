#include "predictors.hpp"
#include "simulator.hpp"
#include "trace_parser.hpp"

#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
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
    std::string csv_path;
    bool compare{false};
    bool verbose{false};
    bool csv_requested{false};
    bool predictor_explicit{false};
    bool table_bits_explicit{false};
    bool history_bits_explicit{false};
    bool initial_state_explicit{false};
};

void print_help(std::ostream& output) {
    output << "Branch Predictor Simulator\n\n";
    output << "Usage:\n";
    output << "  branchsim <trace-file> [options]\n\n";
    output << "Options:\n";
    output << "  --predictor <name>       always-taken, always-not-taken, one-bit,\n";
    output << "                           two-bit (default), or gshare\n";
    output << "  --table-bits <N>         Table index bits, 0-24 (default: 10)\n";
    output << "  --history-bits <N>       GShare history bits, 1-63 (default: 8)\n";
    output << "  --initial-state <state>  taken, not-taken, strongly-taken,\n";
    output << "                           weakly-taken, weakly-not-taken, or\n";
    output << "                           strongly-not-taken\n";
    output << "  --compare                Run all five predictors\n";
    output << "  --csv <path>             Write result rows to a CSV file\n";
    output << "  --verbose                Print one line for every branch\n";
    output << "  --help                   Show this help text\n\n";
    output << "Examples:\n";
    output << "  branchsim trace.txt --predictor gshare --history-bits 8 --table-bits 10\n";
    output << "  branchsim trace.txt --compare --csv results.csv\n";
}

PredictorKind parse_predictor(const std::string& value) {
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
    throw std::invalid_argument("invalid predictor '" + value + "'");
}

InitialState parse_initial_state(const std::string& value) {
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
    throw std::invalid_argument("invalid initial state '" + value + "'");
}

unsigned parse_unsigned(const std::string& text, const std::string& option) {
    unsigned value = 0U;
    const auto result =
        std::from_chars(text.data(), text.data() + text.size(), value, 10);
    if (text.empty() || result.ec != std::errc{} ||
        result.ptr != text.data() + text.size()) {
        throw std::invalid_argument(option +
                                    " requires a non-negative integer");
    }
    return value;
}

std::string require_value(int& index, int argc, char* argv[],
                          const std::string& option) {
    if (index + 1 >= argc) {
        throw std::invalid_argument(option + " requires a value");
    }

    const std::string value = argv[index + 1];
    if (value.rfind("--", 0U) == 0U) {
        throw std::invalid_argument(option + " requires a value");
    }

    ++index;
    return value;
}

Options parse_options(int argc, char* argv[]) {
    Options options;

    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--predictor") {
            if (options.predictor_explicit) {
                throw std::invalid_argument(
                    "--predictor may only be specified once");
            }
            options.predictor = parse_predictor(
                require_value(index, argc, argv, argument));
            options.predictor_explicit = true;
        } else if (argument == "--table-bits") {
            if (options.table_bits_explicit) {
                throw std::invalid_argument(
                    "--table-bits may only be specified once");
            }
            options.table_bits = parse_unsigned(
                require_value(index, argc, argv, argument), argument);
            options.table_bits_explicit = true;
        } else if (argument == "--history-bits") {
            if (options.history_bits_explicit) {
                throw std::invalid_argument(
                    "--history-bits may only be specified once");
            }
            options.history_bits = parse_unsigned(
                require_value(index, argc, argv, argument), argument);
            options.history_bits_explicit = true;
        } else if (argument == "--initial-state") {
            if (options.initial_state_explicit) {
                throw std::invalid_argument(
                    "--initial-state may only be specified once");
            }
            options.initial_state = parse_initial_state(
                require_value(index, argc, argv, argument));
            options.initial_state_explicit = true;
        } else if (argument == "--csv") {
            if (options.csv_requested) {
                throw std::invalid_argument(
                    "--csv may only be specified once");
            }
            options.csv_path = require_value(index, argc, argv, argument);
            if (options.csv_path.empty()) {
                throw std::invalid_argument(
                    "--csv requires a non-empty path");
            }
            options.csv_requested = true;
        } else if (argument == "--compare") {
            if (options.compare) {
                throw std::invalid_argument(
                    "--compare may only be specified once");
            }
            options.compare = true;
        } else if (argument == "--verbose") {
            if (options.verbose) {
                throw std::invalid_argument(
                    "--verbose may only be specified once");
            }
            options.verbose = true;
        } else if (!argument.empty() && argument.front() == '-') {
            throw std::invalid_argument("unknown option '" + argument + "'");
        } else if (!options.trace_path.empty()) {
            throw std::invalid_argument(
                "multiple trace files were provided");
        } else {
            options.trace_path = argument;
        }
    }

    if (options.trace_path.empty()) {
        throw std::invalid_argument("a trace file is required");
    }
    if (options.compare && options.verbose) {
        throw std::invalid_argument(
            "--compare cannot be combined with --verbose");
    }
    if (options.compare && options.predictor_explicit) {
        throw std::invalid_argument(
            "--compare cannot be combined with --predictor");
    }
    if (options.table_bits > kMaximumTableBits) {
        throw std::invalid_argument(
            "--table-bits must be between 0 and 24");
    }
    if (options.history_bits == 0U ||
        options.history_bits > kMaximumHistoryBits) {
        throw std::invalid_argument(
            "--history-bits must be between 1 and 63");
    }

    bool selected_predictor_has_table = false;
    if (options.predictor == PredictorKind::one_bit ||
        options.predictor == PredictorKind::two_bit ||
        options.predictor == PredictorKind::gshare) {
        selected_predictor_has_table = true;
    }

    if (!options.compare && options.table_bits_explicit &&
        !selected_predictor_has_table) {
        throw std::invalid_argument(
            "--table-bits is only valid for table-based predictors or "
            "--compare");
    }
    if (!options.compare && options.history_bits_explicit &&
        options.predictor != PredictorKind::gshare) {
        throw std::invalid_argument(
            "--history-bits is only valid with gshare or --compare");
    }
    if (!options.compare && options.initial_state_explicit &&
        !selected_predictor_has_table) {
        throw std::invalid_argument(
            "--initial-state is only valid for table-based predictors or "
            "--compare");
    }

    return options;
}

bool initial_one_bit_state(InitialState state) {
    if (state == InitialState::taken) {
        return true;
    }
    if (state == InitialState::weakly_taken) {
        return true;
    }
    if (state == InitialState::strongly_taken) {
        return true;
    }
    return false;
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
    output << "Predictor: " << result.predictor << '\n';
    output << "Total branches: " << result.statistics.total() << '\n';
    output << "Taken branches: " << result.statistics.taken() << '\n';
    output << "Not-taken branches: " << result.statistics.not_taken() << '\n';
    output << "Correct predictions: " << result.statistics.correct() << '\n';
    output << "Incorrect predictions: " << result.statistics.incorrect() << '\n';
    output << std::fixed << std::setprecision(2);
    output << "Prediction accuracy: "
           << result.statistics.accuracy_percent() << "%\n";
    output << "Misprediction rate: "
           << result.statistics.misprediction_rate_percent() << "%\n";

    if (result.table_entries.has_value()) {
        const std::size_t bytes = (result.table_memory_bits + 7U) / 8U;

        const char* bit_label = "bits";
        if (result.table_memory_bits == 1U) {
            bit_label = "bit";
        }

        const char* byte_label = "bytes";
        if (bytes == 1U) {
            byte_label = "byte";
        }

        output << "Table entries: " << *result.table_entries << '\n';
        output << "Table memory usage: " << result.table_memory_bits << ' '
               << bit_label << " (" << bytes << ' ' << byte_label << ")\n";
    }

    if (result.history_bits.has_value() && result.final_history.has_value()) {
        const std::string final_history =
            history_string(*result.final_history, *result.history_bits);
        output << "History bits: " << *result.history_bits << '\n';
        output << "Final global history: " << final_history << '\n';
    }
}

void print_comparison(const std::vector<SimulationResult>& results,
                      std::ostream& output) {
    output << std::left << std::setw(22) << "Predictor";
    output << std::right << std::setw(12) << "Correct";
    output << std::setw(14) << "Incorrect";
    output << std::setw(14) << "Accuracy" << '\n';

    for (const SimulationResult& result : results) {
        output << std::left << std::setw(22) << result.predictor;
        output << std::right << std::setw(12)
               << result.statistics.correct();
        output << std::setw(14) << result.statistics.incorrect();
        output << std::setw(13) << std::fixed << std::setprecision(2)
               << result.statistics.accuracy_percent();
        output << "%\n";
    }
}

void validate_csv_destination(const std::string& trace_path,
                              const std::string& csv_path) {
    std::error_code trace_canonical_error;
    std::error_code csv_canonical_error;
    const std::filesystem::path canonical_trace =
        std::filesystem::weakly_canonical(trace_path, trace_canonical_error);
    const std::filesystem::path canonical_csv =
        std::filesystem::weakly_canonical(csv_path, csv_canonical_error);

    if (!trace_canonical_error && !csv_canonical_error &&
        canonical_trace == canonical_csv) {
        throw std::invalid_argument(
            "CSV output must not overwrite the input trace");
    }

    std::error_code equivalent_error;
    const bool same_file =
        std::filesystem::equivalent(trace_path, csv_path, equivalent_error);
    if (!equivalent_error && same_file) {
        throw std::invalid_argument(
            "CSV output must not overwrite the input trace");
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
    for (const SimulationResult& result : results) {
        output << result.predictor << ',';
        output << result.statistics.total() << ',';
        output << result.statistics.correct() << ',';
        output << result.statistics.incorrect() << ',';
        output << result.statistics.accuracy_percent() << ',';
        output << result.statistics.misprediction_rate_percent() << '\n';
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
        const PredictorKind kinds[] = {
            PredictorKind::always_taken, PredictorKind::always_not_taken,
            PredictorKind::one_bit, PredictorKind::two_bit,
            PredictorKind::gshare
        };

        for (PredictorKind kind : kinds) {
            Simulator simulator(make_predictor(kind, options));
            const SimulationResult result = simulator.run(trace);
            results.push_back(result);
        }
    } else {
        Simulator simulator(make_predictor(options.predictor, options));
        std::ostream* verbose_output = nullptr;
        if (options.verbose) {
            verbose_output = &std::cout;
        }

        const SimulationResult result =
            simulator.run(trace, verbose_output);
        results.push_back(result);
    }
    return results;
}

}  // namespace
}  // namespace branchsim

int main(int argc, char* argv[]) {
    using namespace branchsim;

    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--help") {
            print_help(std::cout);
            return 0;
        }
    }

    try {
        const Options options = parse_options(argc, argv);
        if (options.csv_requested) {
            validate_csv_destination(options.trace_path, options.csv_path);
        }

        const std::vector<SimulationResult> results = run_simulations(options);

        if (options.csv_requested) {
            write_csv(options.csv_path, results);
        }

        if (options.compare) {
            print_comparison(results, std::cout);
        } else {
            print_summary(results.front(), std::cout);
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        std::cerr << "Try 'branchsim --help' for usage.\n";
        return 1;
    }
}
