#include "predictors.hpp"

#include <stdexcept>

namespace branchsim {
namespace {

constexpr unsigned kMaximumTableBits = 24U;

std::size_t table_entries_for(unsigned table_bits) {
    if (table_bits > kMaximumTableBits) {
        throw std::invalid_argument("table bits must be in the range 0..24");
    }

    return std::size_t{1U} << table_bits;
}

std::uint8_t validated_two_bit_state(std::uint8_t state) {
    if (state > 3U) {
        throw std::invalid_argument("two-bit initial state must be in the range 0..3");
    }
    return state;
}

std::uint8_t validated_gshare_state(unsigned history_bits,
                                    std::uint8_t state) {
    if (history_bits < 1U || history_bits > 63U) {
        throw std::invalid_argument("history bits must be in the range 1..63");
    }
    return validated_two_bit_state(state);
}

std::uint8_t updated_counter(std::uint8_t state, bool taken) {
    if (taken) {
        return state < 3U ? static_cast<std::uint8_t>(state + 1U) : state;
    }
    return state > 0U ? static_cast<std::uint8_t>(state - 1U) : state;
}

}  // namespace

Prediction AlwaysTakenPredictor::predict(std::uint64_t pc) const {
    static_cast<void>(pc);
    return Prediction{true, std::nullopt, std::nullopt, std::nullopt};
}

UpdateResult AlwaysTakenPredictor::update(std::uint64_t pc, bool taken) {
    static_cast<void>(pc);
    static_cast<void>(taken);
    return UpdateResult{};
}

std::string AlwaysTakenPredictor::name() const { return "Always Taken"; }

Prediction AlwaysNotTakenPredictor::predict(std::uint64_t pc) const {
    static_cast<void>(pc);
    return Prediction{false, std::nullopt, std::nullopt, std::nullopt};
}

UpdateResult AlwaysNotTakenPredictor::update(std::uint64_t pc, bool taken) {
    static_cast<void>(pc);
    static_cast<void>(taken);
    return UpdateResult{};
}

std::string AlwaysNotTakenPredictor::name() const {
    return "Always Not Taken";
}

OneBitPredictor::OneBitPredictor(unsigned table_bits, bool initially_taken)
    : table_(table_entries_for(table_bits),
             static_cast<std::uint8_t>(initially_taken)),
      index_mask_(static_cast<std::uint64_t>(table_.size() - 1U)) {}

Prediction OneBitPredictor::predict(std::uint64_t pc) const {
    const std::size_t index = index_for(pc);
    const std::uint8_t state = table_[index];
    return Prediction{state != 0U, index, state, std::nullopt};
}

UpdateResult OneBitPredictor::update(std::uint64_t pc, bool taken) {
    const std::size_t index = index_for(pc);
    table_[index] = static_cast<std::uint8_t>(taken);
    return UpdateResult{table_[index], std::nullopt};
}

std::string OneBitPredictor::name() const { return "One-Bit"; }

std::optional<std::size_t> OneBitPredictor::table_entries() const {
    return table_.size();
}

std::size_t OneBitPredictor::bits_per_entry() const { return 1U; }

std::size_t OneBitPredictor::index_for(std::uint64_t pc) const {
    return static_cast<std::size_t>(pc & index_mask_);
}

TwoBitPredictor::TwoBitPredictor(unsigned table_bits,
                                 std::uint8_t initial_state)
    : table_(table_entries_for(table_bits),
             validated_two_bit_state(initial_state)),
      index_mask_(static_cast<std::uint64_t>(table_.size() - 1U)) {}

Prediction TwoBitPredictor::predict(std::uint64_t pc) const {
    const std::size_t index = index_for(pc);
    const std::uint8_t state = table_[index];
    return Prediction{state >= 2U, index, state, std::nullopt};
}

UpdateResult TwoBitPredictor::update(std::uint64_t pc, bool taken) {
    const std::size_t index = index_for(pc);
    table_[index] = updated_counter(table_[index], taken);
    return UpdateResult{table_[index], std::nullopt};
}

std::string TwoBitPredictor::name() const { return "Two-Bit"; }

std::optional<std::size_t> TwoBitPredictor::table_entries() const {
    return table_.size();
}

std::size_t TwoBitPredictor::bits_per_entry() const { return 2U; }

std::size_t TwoBitPredictor::index_for(std::uint64_t pc) const {
    return static_cast<std::size_t>(pc & index_mask_);
}

GSharePredictor::GSharePredictor(unsigned table_bits, unsigned history_bits,
                                 std::uint8_t initial_state)
    : table_(table_entries_for(table_bits),
             validated_gshare_state(history_bits, initial_state)),
      index_mask_(static_cast<std::uint64_t>(table_.size() - 1U)),
      history_mask_((std::uint64_t{1U} << history_bits) - 1U),
      history_bits_(history_bits) {}

Prediction GSharePredictor::predict(std::uint64_t pc) const {
    const std::size_t index = index_for(pc);
    const std::uint8_t state = table_[index];
    return Prediction{state >= 2U, index, state, global_history_};
}

UpdateResult GSharePredictor::update(std::uint64_t pc, bool taken) {
    // The current history selects the counter; the resolved outcome is shifted
    // into history only after that counter has been updated.
    const std::size_t index = index_for(pc);
    table_[index] = updated_counter(table_[index], taken);
    global_history_ = ((global_history_ << 1U) |
                       static_cast<std::uint64_t>(taken)) &
                      history_mask_;
    return UpdateResult{table_[index], global_history_};
}

std::string GSharePredictor::name() const { return "GShare"; }

std::optional<std::size_t> GSharePredictor::table_entries() const {
    return table_.size();
}

std::size_t GSharePredictor::bits_per_entry() const { return 2U; }

std::optional<unsigned> GSharePredictor::history_bits() const {
    return history_bits_;
}

std::optional<std::uint64_t> GSharePredictor::global_history() const {
    return global_history_;
}

std::size_t GSharePredictor::index_for(std::uint64_t pc) const {
    return static_cast<std::size_t>((pc ^ global_history_) & index_mask_);
}

}  // namespace branchsim
