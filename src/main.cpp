#include "predictor.h"
#include "trace.h"

#include <cstdint>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

struct Options {
    std::string traceFile;
    PredictorType predictorType = TWO_BIT;
    unsigned int tableBits = 10;
    unsigned int historyBits = 8;
    bool compare = false;
    bool verbose = false;
    bool help = false;
};

void printHelp() {
    std::cout << "Branch Predictor Simulator\n\n";
    std::cout << "Usage:\n";
    std::cout << "  ./branchsim <trace-file> [options]\n\n";
    std::cout << "Options:\n";
    std::cout << "  --predictor <name>   always-taken, always-not-taken, one-bit,\n";
    std::cout << "                       two-bit, or gshare\n";
    std::cout << "  --table-bits <N>     Table size as 2^N entries\n";
    std::cout << "  --history-bits <N>   Number of GShare history bits\n";
    std::cout << "  --compare            Run every predictor\n";
    std::cout << "  --verbose            Print each prediction\n";
    std::cout << "  --help               Show this message\n";
}

PredictorType parsePredictorType(const std::string& name) {
    if (name == "always-taken") {
        return ALWAYS_TAKEN;
    }
    if (name == "always-not-taken") {
        return ALWAYS_NOT_TAKEN;
    }
    if (name == "one-bit") {
        return ONE_BIT;
    }
    if (name == "two-bit") {
        return TWO_BIT;
    }
    if (name == "gshare") {
        return GSHARE;
    }

    throw std::invalid_argument("Unknown predictor: " + name);
}

int parseNumber(const std::string& text, const std::string& option) {
    std::size_t parsedCharacters = 0;
    int value = 0;

    try {
        value = std::stoi(text, &parsedCharacters);
    } catch (const std::exception&) {
        throw std::invalid_argument(option + " needs a number");
    }

    if (parsedCharacters != text.size() || value < 0) {
        throw std::invalid_argument(option + " needs a non-negative number");
    }

    return value;
}

std::string nextValue(int& index, int argc, char* argv[],
                      const std::string& option) {
    if (index + 1 >= argc) {
        throw std::invalid_argument(option + " needs a value");
    }

    index++;
    return argv[index];
}

Options readOptions(int argc, char* argv[]) {
    Options options;

    for (int index = 1; index < argc; index++) {
        std::string argument = argv[index];

        if (argument == "--help") {
            options.help = true;
            return options;
        } else if (argument == "--predictor") {
            std::string name = nextValue(index, argc, argv, argument);
            options.predictorType = parsePredictorType(name);
        } else if (argument == "--table-bits") {
            std::string value = nextValue(index, argc, argv, argument);
            options.tableBits =
                static_cast<unsigned int>(parseNumber(value, argument));
        } else if (argument == "--history-bits") {
            std::string value = nextValue(index, argc, argv, argument);
            options.historyBits =
                static_cast<unsigned int>(parseNumber(value, argument));
        } else if (argument == "--compare") {
            options.compare = true;
        } else if (argument == "--verbose") {
            options.verbose = true;
        } else if (!argument.empty() && argument[0] == '-') {
            throw std::invalid_argument("Unknown option: " + argument);
        } else if (options.traceFile.empty()) {
            options.traceFile = argument;
        } else {
            throw std::invalid_argument("Only one trace file can be used");
        }
    }

    if (options.traceFile.empty()) {
        throw std::invalid_argument("A trace file is required");
    }

    if (options.tableBits > 24) {
        throw std::invalid_argument("table bits must be between 0 and 24");
    }

    if (options.historyBits == 0 || options.historyBits > 63) {
        throw std::invalid_argument("history bits must be between 1 and 63");
    }

    if (options.compare && options.verbose) {
        throw std::invalid_argument("--compare cannot be used with --verbose");
    }

    return options;
}

std::string formatPc(std::uint64_t pc) {
    std::ostringstream output;
    output << "0x" << std::uppercase << std::hex << std::setfill('0')
           << std::setw(8) << pc;
    return output.str();
}

std::string historyText(std::uint64_t history, unsigned int bits) {
    std::string text(bits, '0');

    for (unsigned int i = 0; i < bits; i++) {
        unsigned int shift = bits - i - 1;
        if (((history >> shift) & 1) != 0) {
            text[i] = '1';
        }
    }

    return text;
}

void printVerbose(const Predictor& predictor, const Branch& branch,
                  const PredictionInfo& info) {
    char actual = 'N';
    if (branch.taken) {
        actual = 'T';
    }

    char predicted = 'N';
    if (info.prediction) {
        predicted = 'T';
    }

    std::string result = "MISS";
    if (info.prediction == branch.taken) {
        result = "HIT";
    }

    std::cout << "PC=" << formatPc(branch.pc);
    std::cout << " actual=" << actual;
    std::cout << " predicted=" << predicted;
    std::cout << ' ' << result;

    if (info.index >= 0) {
        std::cout << " index=" << info.index;
        std::cout << " state=" << info.oldState << "->" << info.newState;
    }

    if (predictor.type == GSHARE) {
        std::cout << " history="
                  << historyText(info.oldHistory, predictor.historyBits)
                  << "->"
                  << historyText(info.newHistory, predictor.historyBits);
    }

    std::cout << '\n';
}

Statistics runSimulation(Predictor& predictor,
                         const std::vector<Branch>& branches,
                         bool verbose) {
    Statistics stats;

    for (const Branch& branch : branches) {
        PredictionInfo info =
            predictAndUpdate(predictor, branch.pc, branch.taken);

        stats.total++;

        if (branch.taken) {
            stats.taken++;
        } else {
            stats.notTaken++;
        }

        if (info.prediction == branch.taken) {
            stats.correct++;
        } else {
            stats.incorrect++;
        }

        if (verbose) {
            printVerbose(predictor, branch, info);
        }
    }

    return stats;
}

double accuracy(const Statistics& stats) {
    if (stats.total == 0) {
        return 0.0;
    }

    double correct = static_cast<double>(stats.correct);
    double total = static_cast<double>(stats.total);
    return correct * 100.0 / total;
}

void printResult(PredictorType type, const Statistics& stats) {
    std::cout << "Predictor: " << predictorName(type) << '\n';
    std::cout << "Branches: " << stats.total << '\n';
    std::cout << "Correct: " << stats.correct << '\n';
    std::cout << "Wrong: " << stats.incorrect << '\n';
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Accuracy: " << accuracy(stats) << "%\n";
}

void runComparison(const std::vector<Branch>& branches,
                   unsigned int tableBits,
                   unsigned int historyBits) {
    PredictorType types[] = {
        ALWAYS_TAKEN,
        ALWAYS_NOT_TAKEN,
        ONE_BIT,
        TWO_BIT,
        GSHARE
    };

    std::cout << std::left << std::setw(20) << "Predictor";
    std::cout << std::right << std::setw(10) << "Correct";
    std::cout << std::setw(10) << "Wrong";
    std::cout << std::setw(12) << "Accuracy" << '\n';

    for (PredictorType type : types) {
        Predictor predictor = createPredictor(type, tableBits, historyBits);
        Statistics stats = runSimulation(predictor, branches, false);

        std::cout << std::left << std::setw(20) << predictorName(type);
        std::cout << std::right << std::setw(10) << stats.correct;
        std::cout << std::setw(10) << stats.incorrect;
        std::cout << std::setw(11) << std::fixed << std::setprecision(2)
                  << accuracy(stats);
        std::cout << "%\n";
    }
}

int main(int argc, char* argv[]) {
    try {
        Options options = readOptions(argc, argv);

        if (options.help) {
            printHelp();
            return 0;
        }

        std::vector<Branch> branches = readTrace(options.traceFile);

        if (options.compare) {
            runComparison(branches, options.tableBits, options.historyBits);
        } else {
            Predictor predictor =
                createPredictor(options.predictorType,
                                options.tableBits,
                                options.historyBits);
            Statistics stats =
                runSimulation(predictor, branches, options.verbose);
            printResult(options.predictorType, stats);
        }
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
