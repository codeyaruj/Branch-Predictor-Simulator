#pragma once

#include <cstdint>

namespace branchsim {

class Statistics {
public:
    void record(bool actual_taken, bool predicted_taken) noexcept;

    [[nodiscard]] std::uint64_t total() const noexcept;
    [[nodiscard]] std::uint64_t taken() const noexcept;
    [[nodiscard]] std::uint64_t not_taken() const noexcept;
    [[nodiscard]] std::uint64_t correct() const noexcept;
    [[nodiscard]] std::uint64_t incorrect() const noexcept;

    [[nodiscard]] double accuracy_percent() const noexcept;
    [[nodiscard]] double misprediction_rate_percent() const noexcept;

private:
    std::uint64_t total_{0U};
    std::uint64_t taken_{0U};
    std::uint64_t not_taken_{0U};
    std::uint64_t correct_{0U};
    std::uint64_t incorrect_{0U};
};

}  // namespace branchsim
