#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace branchsim {

struct Prediction {
    bool taken{false};
    std::optional<std::size_t> index;
    std::optional<std::uint8_t> state;
    std::optional<std::uint64_t> history;
};

struct UpdateResult {
    std::optional<std::uint8_t> state;
    std::optional<std::uint64_t> history;
};

class BranchPredictor {
public:
    virtual ~BranchPredictor() = default;

    [[nodiscard]] virtual Prediction predict(std::uint64_t pc) const = 0;
    virtual UpdateResult update(std::uint64_t pc, bool taken) = 0;
    [[nodiscard]] virtual std::string name() const = 0;

    [[nodiscard]] virtual std::optional<std::size_t> table_entries() const {
        return std::nullopt;
    }

    [[nodiscard]] virtual std::size_t bits_per_entry() const { return 0U; }

    [[nodiscard]] virtual std::optional<unsigned> history_bits() const {
        return std::nullopt;
    }

    [[nodiscard]] virtual std::optional<std::uint64_t> global_history() const {
        return std::nullopt;
    }
};

}  // namespace branchsim
