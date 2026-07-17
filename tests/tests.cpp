#include "predictor.h"
#include "trace.h"

#include <cassert>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

const std::string testTraceFile = "tests/test_input.tmp";

void writeTestTrace(const std::string& text) {
    std::ofstream file(testTraceFile);
    file << text;
}

void testAlwaysPredictors() {
    Predictor taken = createPredictor(ALWAYS_TAKEN, 2, 2);
    Predictor notTaken = createPredictor(ALWAYS_NOT_TAKEN, 2, 2);

    assert(makePrediction(taken, 0x10));
    assert(!makePrediction(notTaken, 0x10));

    updatePredictor(taken, 0x10, false);
    updatePredictor(notTaken, 0x10, true);

    assert(makePrediction(taken, 0x10));
    assert(!makePrediction(notTaken, 0x10));
}

void testOneBitPredictor() {
    Predictor predictor = createPredictor(ONE_BIT, 2, 2);

    assert(getIndex(predictor, 0x06) == 2);
    assert(!makePrediction(predictor, 0x06));

    updatePredictor(predictor, 0x06, true);
    assert(predictor.table[2] == 1);
    assert(makePrediction(predictor, 0x06));

    updatePredictor(predictor, 0x06, false);
    assert(predictor.table[2] == 0);
}

void testTwoBitPredictor() {
    Predictor predictor = createPredictor(TWO_BIT, 0, 2);

    assert(predictor.table[0] == 1);
    assert(!makePrediction(predictor, 0x10));

    updatePredictor(predictor, 0x10, true);
    assert(predictor.table[0] == 2);
    assert(makePrediction(predictor, 0x10));

    updatePredictor(predictor, 0x10, true);
    assert(predictor.table[0] == 3);
    updatePredictor(predictor, 0x10, true);
    assert(predictor.table[0] == 3);

    updatePredictor(predictor, 0x10, false);
    assert(predictor.table[0] == 2);
    updatePredictor(predictor, 0x10, false);
    assert(predictor.table[0] == 1);
    updatePredictor(predictor, 0x10, false);
    assert(predictor.table[0] == 0);
    updatePredictor(predictor, 0x10, false);
    assert(predictor.table[0] == 0);
}

void testGSharePredictor() {
    Predictor predictor = createPredictor(GSHARE, 3, 3);

    assert(getIndex(predictor, 0x06) == 6);

    PredictionInfo first = predictAndUpdate(predictor, 0x06, true);
    assert(first.index == 6);
    assert(first.oldState == 1);
    assert(first.newState == 2);
    assert(first.oldHistory == 0);
    assert(first.newHistory == 1);

    // 0x07 XOR history 1 selects entry 6.
    assert(getIndex(predictor, 0x07) == 6);
    PredictionInfo second = predictAndUpdate(predictor, 0x07, false);
    assert(second.index == 6);
    assert(second.prediction);
    assert(second.newState == 1);
    assert(second.newHistory == 2);
}

void testValidTrace() {
    writeTestTrace(
        "# sample trace\n"
        "\n"
        "0x10 T\n"
        "  0X20 N\n");

    std::vector<Branch> branches = readTrace(testTraceFile);

    assert(branches.size() == 2);
    assert(branches[0].pc == 0x10);
    assert(branches[0].taken);
    assert(branches[1].pc == 0x20);
    assert(!branches[1].taken);
}

void expectTraceError(const std::string& text) {
    writeTestTrace(text);

    bool threwError = false;
    try {
        readTrace(testTraceFile);
    } catch (const std::runtime_error&) {
        threwError = true;
    }

    assert(threwError);
}

void testInvalidTraces() {
    expectTraceError("0x10 X\n");
    expectTraceError("not-an-address T\n");
    expectTraceError("0x10 T extra\n");
    expectTraceError("0x10\n");
}

void testCompleteSimulation() {
    std::vector<Branch> branches = {
        {0x10, true},
        {0x10, true},
        {0x10, false},
        {0x10, false}
    };

    Predictor predictor = createPredictor(TWO_BIT, 0, 2);
    int correct = 0;

    for (const Branch& branch : branches) {
        bool prediction = makePrediction(predictor, branch.pc);
        if (prediction == branch.taken) {
            correct++;
        }
        updatePredictor(predictor, branch.pc, branch.taken);
    }

    assert(correct == 1);
    assert(predictor.table[0] == 1);
}

void testInvalidSettings() {
    bool badTableBits = false;
    try {
        createPredictor(TWO_BIT, 25, 2);
    } catch (const std::invalid_argument&) {
        badTableBits = true;
    }
    assert(badTableBits);

    bool badHistoryBits = false;
    try {
        createPredictor(GSHARE, 2, 0);
    } catch (const std::invalid_argument&) {
        badHistoryBits = true;
    }
    assert(badHistoryBits);
}

int main() {
    testAlwaysPredictors();
    testOneBitPredictor();
    testTwoBitPredictor();
    testGSharePredictor();
    testValidTrace();
    testInvalidTraces();
    testCompleteSimulation();
    testInvalidSettings();

    std::remove(testTraceFile.c_str());
    std::cout << "All tests passed\n";
    return 0;
}
