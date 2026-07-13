#pragma once

#include "branch_predictor.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace branchsim {

class AlwaysTakenPredictor final : public BranchPredictor {
public:
    [[nodiscard]] Prediction predict(std::uint64_t pc) const override;
    UpdateResult update(std::uint64_t pc, bool taken) override;
    [[nodiscard]] std::string name() const override;
};

class AlwaysNotTakenPredictor final : public BranchPredictor {
public:
    [[nodiscard]] Prediction predict(std::uint64_t pc) const override;
    UpdateResult update(std::uint64_t pc, bool taken) override;
    [[nodiscard]] std::string name() const override;
};

class OneBitPredictor final : public BranchPredictor {
public:
    explicit OneBitPredictor(unsigned table_bits,
                             bool initially_taken = false);

    [[nodiscard]] Prediction predict(std::uint64_t pc) const override;
    UpdateResult update(std::uint64_t pc, bool taken) override;
    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::optional<std::size_t> table_entries() const override;
    [[nodiscard]] std::size_t bits_per_entry() const override;

private:
    [[nodiscard]] std::size_t index_for(std::uint64_t pc) const;

    std::vector<std::uint8_t> table_;
    std::uint64_t index_mask_;
};

class TwoBitPredictor final : public BranchPredictor {
public:
    explicit TwoBitPredictor(unsigned table_bits,
                             std::uint8_t initial_state = 1U);

    [[nodiscard]] Prediction predict(std::uint64_t pc) const override;
    UpdateResult update(std::uint64_t pc, bool taken) override;
    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::optional<std::size_t> table_entries() const override;
    [[nodiscard]] std::size_t bits_per_entry() const override;

private:
    [[nodiscard]] std::size_t index_for(std::uint64_t pc) const;

    std::vector<std::uint8_t> table_;
    std::uint64_t index_mask_;
};

class GSharePredictor final : public BranchPredictor {
public:
    GSharePredictor(unsigned table_bits, unsigned history_bits,
                    std::uint8_t initial_state = 1U);

    [[nodiscard]] Prediction predict(std::uint64_t pc) const override;
    UpdateResult update(std::uint64_t pc, bool taken) override;
    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::optional<std::size_t> table_entries() const override;
    [[nodiscard]] std::size_t bits_per_entry() const override;
    [[nodiscard]] std::optional<unsigned> history_bits() const override;
    [[nodiscard]] std::optional<std::uint64_t> global_history() const override;

private:
    [[nodiscard]] std::size_t index_for(std::uint64_t pc) const;

    std::vector<std::uint8_t> table_;
    std::uint64_t index_mask_;
    std::uint64_t history_mask_;
    std::uint64_t global_history_{0U};
    unsigned history_bits_;
};

}  // namespace branchsim
