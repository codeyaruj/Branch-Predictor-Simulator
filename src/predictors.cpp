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

void validate_two_bit_state(std::uint8_t state) {
    if (state > 3U) {
        throw std::invalid_argument("two-bit initial state must be in the range 0..3");
    }
}

void validate_history_bits(unsigned history_bits) {
    if (history_bits < 1U || history_bits > 63U) {
        throw std::invalid_argument("history bits must be in the range 1..63");
    }
}

std::uint8_t update_counter(std::uint8_t state, bool taken) {
    if (taken) {
        if (state < 3U) {
            ++state;
        }
    } else {
        if (state > 0U) {
            --state;
        }
    }
    return state;
}

}  // namespace

Prediction AlwaysTakenPredictor::predict(std::uint64_t) const {
    Prediction prediction;
    prediction.taken = true;
    return prediction;
}

UpdateResult AlwaysTakenPredictor::update(std::uint64_t, bool) {
    UpdateResult result;
    return result;
}

std::string AlwaysTakenPredictor::name() const {
    return "Always Taken";
}

Prediction AlwaysNotTakenPredictor::predict(std::uint64_t) const {
    Prediction prediction;
    prediction.taken = false;
    return prediction;
}

UpdateResult AlwaysNotTakenPredictor::update(std::uint64_t, bool) {
    UpdateResult result;
    return result;
}

std::string AlwaysNotTakenPredictor::name() const {
    return "Always Not Taken";
}

OneBitPredictor::OneBitPredictor(unsigned table_bits, bool initially_taken) {
    const std::size_t entry_count = table_entries_for(table_bits);
    std::uint8_t initial_state = 0U;
    if (initially_taken) {
        initial_state = 1U;
    }

    table_.assign(entry_count, initial_state);
    index_mask_ = static_cast<std::uint64_t>(entry_count - 1U);
}

Prediction OneBitPredictor::predict(std::uint64_t pc) const {
    const std::size_t index = index_for(pc);
    const std::uint8_t state = table_[index];

    Prediction prediction;
    prediction.taken = state != 0U;
    prediction.index = index;
    prediction.state = state;
    return prediction;
}

UpdateResult OneBitPredictor::update(std::uint64_t pc, bool taken) {
    const std::size_t index = index_for(pc);
    if (taken) {
        table_[index] = 1U;
    } else {
        table_[index] = 0U;
    }

    UpdateResult result;
    result.state = table_[index];
    return result;
}

std::string OneBitPredictor::name() const {
    return "One-Bit";
}

std::optional<std::size_t> OneBitPredictor::table_entries() const {
    return table_.size();
}

std::size_t OneBitPredictor::bits_per_entry() const { return 1U; }

std::size_t OneBitPredictor::index_for(std::uint64_t pc) const {
    return static_cast<std::size_t>(pc & index_mask_);
}

TwoBitPredictor::TwoBitPredictor(unsigned table_bits,
                                 std::uint8_t initial_state) {
    validate_two_bit_state(initial_state);
    const std::size_t entry_count = table_entries_for(table_bits);

    table_.assign(entry_count, initial_state);
    index_mask_ = static_cast<std::uint64_t>(entry_count - 1U);
}

Prediction TwoBitPredictor::predict(std::uint64_t pc) const {
    const std::size_t index = index_for(pc);
    const std::uint8_t state = table_[index];

    Prediction prediction;
    prediction.taken = state >= 2U;
    prediction.index = index;
    prediction.state = state;
    return prediction;
}

UpdateResult TwoBitPredictor::update(std::uint64_t pc, bool taken) {
    const std::size_t index = index_for(pc);
    table_[index] = update_counter(table_[index], taken);

    UpdateResult result;
    result.state = table_[index];
    return result;
}

std::string TwoBitPredictor::name() const {
    return "Two-Bit";
}

std::optional<std::size_t> TwoBitPredictor::table_entries() const {
    return table_.size();
}

std::size_t TwoBitPredictor::bits_per_entry() const { return 2U; }

std::size_t TwoBitPredictor::index_for(std::uint64_t pc) const {
    return static_cast<std::size_t>(pc & index_mask_);
}

GSharePredictor::GSharePredictor(unsigned table_bits, unsigned history_bits,
                                 std::uint8_t initial_state) {
    validate_history_bits(history_bits);
    validate_two_bit_state(initial_state);
    const std::size_t entry_count = table_entries_for(table_bits);

    table_.assign(entry_count, initial_state);
    index_mask_ = static_cast<std::uint64_t>(entry_count - 1U);
    history_mask_ = (std::uint64_t{1U} << history_bits) - 1U;
    history_bits_ = history_bits;
}

Prediction GSharePredictor::predict(std::uint64_t pc) const {
    const std::size_t index = index_for(pc);
    const std::uint8_t state = table_[index];

    Prediction prediction;
    prediction.taken = state >= 2U;
    prediction.index = index;
    prediction.state = state;
    prediction.history = global_history_;
    return prediction;
}

UpdateResult GSharePredictor::update(std::uint64_t pc, bool taken) {
    // The current history selects the counter; the resolved outcome is shifted
    // into history only after that counter has been updated.
    const std::size_t index = index_for(pc);
    table_[index] = update_counter(table_[index], taken);

    global_history_ <<= 1U;
    if (taken) {
        global_history_ |= 1U;
    }
    global_history_ &= history_mask_;

    UpdateResult result;
    result.state = table_[index];
    result.history = global_history_;
    return result;
}

std::string GSharePredictor::name() const {
    return "GShare";
}

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
    const std::uint64_t mixed_bits = pc ^ global_history_;
    const std::uint64_t masked_index = mixed_bits & index_mask_;
    return static_cast<std::size_t>(masked_index);
}

}  // namespace branchsim
