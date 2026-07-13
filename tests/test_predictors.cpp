#include "predictors.hpp"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

void require(bool condition, const char* expression, int line) {
    if (condition) {
        return;
    }

    std::ostringstream message;
    message << "line " << line << ": " << expression;
    throw std::runtime_error(message.str());
}

#define REQUIRE(expression) require((expression), #expression, __LINE__)

template <typename Function>
void require_invalid_argument(Function&& function) {
    try {
        std::forward<Function>(function)();
    } catch (const std::invalid_argument&) {
        return;
    } catch (const std::exception& error) {
        throw std::runtime_error(std::string{"expected std::invalid_argument, got: "} +
                                 error.what());
    }
    throw std::runtime_error("expected std::invalid_argument");
}

class TestRunner {
public:
    using TestFunction = void (*)();

    void run(const char* name, TestFunction test) {
        ++total_;
        try {
            test();
            ++passed_;
        } catch (const std::exception& error) {
            std::cerr << "FAIL " << name << ": " << error.what() << '\n';
        } catch (...) {
            std::cerr << "FAIL " << name << ": unknown exception\n";
        }
    }

    [[nodiscard]] int finish() const {
        std::cout << passed_ << '/' << total_ << " predictor tests passed\n";
        return passed_ == total_ ? 0 : 1;
    }

private:
    std::size_t passed_{0U};
    std::size_t total_{0U};
};

void test_static_predictors() {
    branchsim::AlwaysTakenPredictor always_taken;
    const branchsim::Prediction taken = always_taken.predict(0x12345678U);
    REQUIRE(taken.taken);
    REQUIRE(!taken.index.has_value());
    REQUIRE(!taken.state.has_value());
    REQUIRE(!taken.history.has_value());
    const branchsim::UpdateResult taken_update =
        always_taken.update(0x12345678U, false);
    REQUIRE(!taken_update.state.has_value());
    REQUIRE(!taken_update.history.has_value());
    REQUIRE(always_taken.name() == "Always Taken");
    REQUIRE(!always_taken.table_entries().has_value());
    REQUIRE(always_taken.bits_per_entry() == 0U);
    REQUIRE(!always_taken.history_bits().has_value());
    REQUIRE(!always_taken.global_history().has_value());

    branchsim::AlwaysNotTakenPredictor always_not_taken;
    const branchsim::Prediction not_taken =
        always_not_taken.predict(0xFFFFFFFFFFFFFFFFULL);
    REQUIRE(!not_taken.taken);
    REQUIRE(!not_taken.index.has_value());
    REQUIRE(!not_taken.state.has_value());
    REQUIRE(!not_taken.history.has_value());
    const branchsim::UpdateResult not_taken_update =
        always_not_taken.update(0U, true);
    REQUIRE(!not_taken_update.state.has_value());
    REQUIRE(!not_taken_update.history.has_value());
    REQUIRE(always_not_taken.name() == "Always Not Taken");
    REQUIRE(!always_not_taken.table_entries().has_value());
    REQUIRE(always_not_taken.bits_per_entry() == 0U);
}

void test_one_bit_transitions_and_initial_states() {
    branchsim::OneBitPredictor predictor(2U);
    branchsim::Prediction prediction = predictor.predict(0x101U);
    REQUIRE(!prediction.taken);
    REQUIRE(prediction.index.has_value() && prediction.index.value() == 1U);
    REQUIRE(prediction.state.has_value() && prediction.state.value() == 0U);
    REQUIRE(!prediction.history.has_value());

    branchsim::UpdateResult update = predictor.update(0x101U, true);
    REQUIRE(update.state.has_value() && update.state.value() == 1U);
    REQUIRE(!update.history.has_value());
    prediction = predictor.predict(0x105U);
    REQUIRE(prediction.taken);
    REQUIRE(prediction.index.value() == 1U);
    REQUIRE(prediction.state.value() == 1U);

    update = predictor.update(0x109U, false);
    REQUIRE(update.state.value() == 0U);
    REQUIRE(!predictor.predict(0x101U).taken);

    branchsim::OneBitPredictor initially_taken(1U, true);
    prediction = initially_taken.predict(0U);
    REQUIRE(prediction.taken);
    REQUIRE(prediction.state.value() == 1U);
    initially_taken.update(0U, false);
    REQUIRE(!initially_taken.predict(0U).taken);
}

void test_one_bit_metadata_and_raw_indexing() {
    branchsim::OneBitPredictor predictor(3U, false);
    REQUIRE(predictor.name() == "One-Bit");
    REQUIRE(predictor.table_entries().has_value());
    REQUIRE(predictor.table_entries().value() == 8U);
    REQUIRE(predictor.bits_per_entry() == 1U);
    REQUIRE(!predictor.history_bits().has_value());
    REQUIRE(!predictor.global_history().has_value());

    const branchsim::Prediction prediction = predictor.predict(0xABCDU);
    REQUIRE(prediction.index.value() == 5U);
}

void check_two_bit_transition(std::uint8_t initial_state, bool taken,
                              std::uint8_t expected_state) {
    branchsim::TwoBitPredictor predictor(0U, initial_state);
    const branchsim::Prediction before = predictor.predict(0xFFFFU);
    REQUIRE(before.state.value() == initial_state);
    REQUIRE(before.taken == (initial_state >= 2U));

    const branchsim::UpdateResult update = predictor.update(0x1234U, taken);
    REQUIRE(update.state.has_value() && update.state.value() == expected_state);
    REQUIRE(!update.history.has_value());

    const branchsim::Prediction after = predictor.predict(0U);
    REQUIRE(after.state.value() == expected_state);
    REQUIRE(after.taken == (expected_state >= 2U));
}

void test_two_bit_all_transitions() {
    check_two_bit_transition(0U, false, 0U);
    check_two_bit_transition(0U, true, 1U);
    check_two_bit_transition(1U, false, 0U);
    check_two_bit_transition(1U, true, 2U);
    check_two_bit_transition(2U, false, 1U);
    check_two_bit_transition(2U, true, 3U);
    check_two_bit_transition(3U, false, 2U);
    check_two_bit_transition(3U, true, 3U);
}

void test_two_bit_saturation_indexing_and_metadata() {
    branchsim::TwoBitPredictor predictor(3U, 1U);
    REQUIRE(predictor.name() == "Two-Bit");
    REQUIRE(predictor.table_entries().value() == 8U);
    REQUIRE(predictor.bits_per_entry() == 2U);
    REQUIRE(!predictor.history_bits().has_value());
    REQUIRE(!predictor.global_history().has_value());

    branchsim::Prediction prediction = predictor.predict(0xABCDU);
    REQUIRE(prediction.index.value() == 5U);
    REQUIRE(prediction.state.value() == 1U);
    REQUIRE(!prediction.history.has_value());

    REQUIRE(predictor.update(0xABCDU, false).state.value() == 0U);
    REQUIRE(predictor.update(0xABCDU, false).state.value() == 0U);
    REQUIRE(predictor.update(0xABCDU, false).state.value() == 0U);
    prediction = predictor.predict(0x5U);
    REQUIRE(!prediction.taken && prediction.state.value() == 0U);

    REQUIRE(predictor.update(0x5U, true).state.value() == 1U);
    REQUIRE(predictor.update(0x5U, true).state.value() == 2U);
    REQUIRE(predictor.update(0x5U, true).state.value() == 3U);
    REQUIRE(predictor.update(0x5U, true).state.value() == 3U);
    REQUIRE(predictor.update(0x5U, true).state.value() == 3U);
    prediction = predictor.predict(0x15U);
    REQUIRE(prediction.taken && prediction.state.value() == 3U);
}

void test_gshare_index_history_and_update_order() {
    branchsim::GSharePredictor predictor(3U, 3U, 1U);
    REQUIRE(predictor.name() == "GShare");
    REQUIRE(predictor.table_entries().value() == 8U);
    REQUIRE(predictor.bits_per_entry() == 2U);
    REQUIRE(predictor.history_bits().value() == 3U);
    REQUIRE(predictor.global_history().value() == 0U);

    branchsim::Prediction prediction = predictor.predict(0x6U);
    REQUIRE(!prediction.taken);
    REQUIRE(prediction.index.value() == 6U);
    REQUIRE(prediction.state.value() == 1U);
    REQUIRE(prediction.history.value() == 0U);

    branchsim::UpdateResult update = predictor.update(0x6U, true);
    REQUIRE(update.state.value() == 2U);
    REQUIRE(update.history.value() == 1U);
    REQUIRE(predictor.global_history().value() == 1U);

    // With history now equal to one, PC 7 selects the old index 6. Seeing
    // state 2 proves update() changed the counter selected by pre-update history.
    prediction = predictor.predict(0x7U);
    REQUIRE(prediction.index.value() == 6U);
    REQUIRE(prediction.state.value() == 2U);
    REQUIRE(prediction.taken);
    REQUIRE(prediction.history.value() == 1U);

    prediction = predictor.predict(0U);
    REQUIRE(prediction.index.value() == 1U);
    update = predictor.update(0U, true);
    REQUIRE(update.state.value() == 2U);
    REQUIRE(update.history.value() == 3U);

    prediction = predictor.predict(0U);
    REQUIRE(prediction.index.value() == 3U);
    update = predictor.update(0U, false);
    REQUIRE(update.state.value() == 0U);
    REQUIRE(update.history.value() == 6U);

    prediction = predictor.predict(0U);
    REQUIRE(prediction.index.value() == 6U);
    REQUIRE(prediction.state.value() == 2U);
    update = predictor.update(0U, true);
    REQUIRE(update.state.value() == 3U);
    REQUIRE(update.history.value() == 5U);
    REQUIRE(predictor.global_history().value() == 5U);
}

void test_constructor_boundaries() {
    branchsim::OneBitPredictor one_entry(0U);
    REQUIRE(one_entry.table_entries().value() == 1U);
    branchsim::TwoBitPredictor two_bit_one_entry(0U, 3U);
    REQUIRE(two_bit_one_entry.table_entries().value() == 1U);
    branchsim::GSharePredictor widest_history(0U, 63U, 0U);
    REQUIRE(widest_history.table_entries().value() == 1U);
    REQUIRE(widest_history.history_bits().value() == 63U);

    {
        branchsim::OneBitPredictor largest_table(24U);
        REQUIRE(largest_table.table_entries().value() == 16'777'216U);
    }

    require_invalid_argument([] { branchsim::OneBitPredictor invalid(25U); });
    require_invalid_argument(
        [] { branchsim::TwoBitPredictor invalid(25U, 1U); });
    require_invalid_argument(
        [] { branchsim::TwoBitPredictor invalid(1U, 4U); });
    require_invalid_argument(
        [] { branchsim::GSharePredictor invalid(25U, 1U, 1U); });
    require_invalid_argument(
        [] { branchsim::GSharePredictor invalid(1U, 0U, 1U); });
    require_invalid_argument(
        [] { branchsim::GSharePredictor invalid(1U, 64U, 1U); });
    require_invalid_argument(
        [] { branchsim::GSharePredictor invalid(1U, 1U, 4U); });
}

}  // namespace

int main() {
    TestRunner runner;
    runner.run("static predictors", test_static_predictors);
    runner.run("one-bit transitions and initial states",
               test_one_bit_transitions_and_initial_states);
    runner.run("one-bit metadata and raw indexing",
               test_one_bit_metadata_and_raw_indexing);
    runner.run("two-bit transitions", test_two_bit_all_transitions);
    runner.run("two-bit saturation, indexing, and metadata",
               test_two_bit_saturation_indexing_and_metadata);
    runner.run("GShare indexing, history, and update order",
               test_gshare_index_history_and_update_order);
    runner.run("constructor boundaries", test_constructor_boundaries);
    return runner.finish();
}
