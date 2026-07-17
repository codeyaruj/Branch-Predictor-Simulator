#include "statistics.hpp"

namespace branchsim {

void Statistics::record(bool actual_taken, bool predicted_taken) noexcept {
    ++total_;
    if (actual_taken) {
        ++taken_;
    } else {
        ++not_taken_;
    }

    if (actual_taken == predicted_taken) {
        ++correct_;
    } else {
        ++incorrect_;
    }
}

std::uint64_t Statistics::total() const noexcept { return total_; }

std::uint64_t Statistics::taken() const noexcept { return taken_; }

std::uint64_t Statistics::not_taken() const noexcept { return not_taken_; }

std::uint64_t Statistics::correct() const noexcept { return correct_; }

std::uint64_t Statistics::incorrect() const noexcept { return incorrect_; }

double Statistics::accuracy_percent() const noexcept {
    if (total_ == 0U) {
        return 0.0;
    }

    const double correct = static_cast<double>(correct_);
    const double total = static_cast<double>(total_);
    return correct * 100.0 / total;
}

double Statistics::misprediction_rate_percent() const noexcept {
    if (total_ == 0U) {
        return 0.0;
    }

    const double incorrect = static_cast<double>(incorrect_);
    const double total = static_cast<double>(total_);
    return incorrect * 100.0 / total;
}

}  // namespace branchsim
