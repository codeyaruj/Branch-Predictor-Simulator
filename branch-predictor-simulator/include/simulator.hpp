#pragma once

#include "branch_predictor.hpp"
#include "statistics.hpp"
#include "trace_parser.hpp"

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <memory>
#include <optional>
#include <string>

namespace branchsim {

struct SimulationResult {
    std::string predictor;
    Statistics statistics;
    std::optional<std::size_t> table_entries;
    std::size_t table_memory_bits{0U};
    std::optional<unsigned> history_bits;
    std::optional<std::uint64_t> final_history;
};

class Simulator {
public:
    explicit Simulator(std::unique_ptr<BranchPredictor> predictor);

    [[nodiscard]] SimulationResult run(const TraceParser& trace,
                                       std::ostream* verbose_output = nullptr);

private:
    std::unique_ptr<BranchPredictor> predictor_;
};

}  // namespace branchsim
