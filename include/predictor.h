#pragma once

#include <cstdint>
#include <string>
#include <vector>

enum PredictorType {
    ALWAYS_TAKEN,
    ALWAYS_NOT_TAKEN,
    ONE_BIT,
    TWO_BIT,
    GSHARE
};

struct Predictor {
    PredictorType type = TWO_BIT;
    std::vector<unsigned char> table;

    unsigned int tableBits = 10;
    unsigned int historyBits = 8;

    std::uint64_t tableMask = 0;
    std::uint64_t historyMask = 0;
    std::uint64_t history = 0;
};

struct PredictionInfo {
    bool prediction = false;
    int index = -1;
    int oldState = -1;
    int newState = -1;
    std::uint64_t oldHistory = 0;
    std::uint64_t newHistory = 0;
};

struct Statistics {
    std::uint64_t total = 0;
    std::uint64_t correct = 0;
    std::uint64_t incorrect = 0;
    std::uint64_t taken = 0;
    std::uint64_t notTaken = 0;
};

Predictor createPredictor(PredictorType type, unsigned int tableBits,
                          unsigned int historyBits);

int getIndex(const Predictor& predictor, std::uint64_t pc);
bool makePrediction(const Predictor& predictor, std::uint64_t pc);
void updatePredictor(Predictor& predictor, std::uint64_t pc, bool taken);

PredictionInfo predictAndUpdate(Predictor& predictor, std::uint64_t pc,
                                bool taken);

std::string predictorName(PredictorType type);
