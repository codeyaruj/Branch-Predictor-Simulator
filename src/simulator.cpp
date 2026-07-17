#include "simulator.hpp"

#include <iomanip>
#include <ostream>
#include <sstream>
#include <stdexcept>

namespace branchsim {
namespace {

std::string format_pc(std::uint64_t pc) {
    std::ostringstream output;
    output << "0x" << std::uppercase << std::hex << std::setfill('0')
           << std::setw(8) << pc;
    return output.str();
}

std::string format_history(std::uint64_t history, unsigned bit_count) {
    std::string bits(bit_count, '0');
    for (unsigned bit = 0U; bit < bit_count; ++bit) {
        const unsigned shift = bit_count - bit - 1U;
        if (((history >> shift) & 1U) != 0U) {
            bits[bit] = '1';
        }
    }
    return bits;
}

void write_verbose_line(std::ostream& output, const TraceRecord& branch,
                        const Prediction& prediction,
                        const UpdateResult& update,
                        std::optional<unsigned> history_bits) {
    char actual = 'N';
    if (branch.taken) {
        actual = 'T';
    }

    char predicted = 'N';
    if (prediction.taken) {
        predicted = 'T';
    }

    const char* result = "MISS";
    if (prediction.taken == branch.taken) {
        result = "HIT";
    }

    output << "PC=" << format_pc(branch.pc)
           << " actual=" << actual
           << " predicted=" << predicted
           << " result=" << result;

    if (prediction.index.has_value()) {
        output << " index=" << *prediction.index;
    }
    if (prediction.state.has_value() && update.state.has_value()) {
        output << " state=" << static_cast<unsigned>(*prediction.state) << "->"
               << static_cast<unsigned>(*update.state);
    }
    if (prediction.history.has_value() && update.history.has_value() &&
        history_bits.has_value()) {
        output << " history="
               << format_history(*prediction.history, *history_bits) << "->"
               << format_history(*update.history, *history_bits);
    }
    output << '\n';
}

}  // namespace

Simulator::Simulator(std::unique_ptr<BranchPredictor> predictor)
    : predictor_(std::move(predictor)) {
    if (!predictor_) {
        throw std::invalid_argument("simulator requires a predictor");
    }
}

SimulationResult Simulator::run(const TraceParser& trace,
                                std::ostream* verbose_output) {
    Statistics statistics;
    const std::optional<unsigned> configured_history_bits =
        predictor_->history_bits();

    trace.for_each([&](const TraceRecord& branch) {
        const Prediction prediction = predictor_->predict(branch.pc);
        const UpdateResult update = predictor_->update(branch.pc, branch.taken);
        statistics.record(branch.taken, prediction.taken);

        if (verbose_output != nullptr) {
            write_verbose_line(*verbose_output, branch, prediction, update,
                               configured_history_bits);
        }
    });

    const std::optional<std::size_t> entries = predictor_->table_entries();
    std::size_t memory_bits = 0U;
    if (entries.has_value()) {
        memory_bits = *entries * predictor_->bits_per_entry();
    }

    SimulationResult result;
    result.predictor = predictor_->name();
    result.statistics = statistics;
    result.table_entries = entries;
    result.table_memory_bits = memory_bits;
    result.history_bits = configured_history_bits;
    result.final_history = predictor_->global_history();
    return result;
}

}  // namespace branchsim
