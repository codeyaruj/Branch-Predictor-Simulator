#include "predictor.h"

#include <stdexcept>

Predictor createPredictor(PredictorType type, unsigned int tableBits,
                          unsigned int historyBits) {
    if (tableBits > 24) {
        throw std::invalid_argument("table bits must be between 0 and 24");
    }

    if (historyBits == 0 || historyBits > 63) {
        throw std::invalid_argument("history bits must be between 1 and 63");
    }

    Predictor predictor;
    predictor.type = type;
    predictor.tableBits = tableBits;
    predictor.historyBits = historyBits;
    predictor.tableMask = (std::uint64_t{1} << tableBits) - 1;
    predictor.historyMask = (std::uint64_t{1} << historyBits) - 1;
    predictor.history = 0;

    if (type == ONE_BIT || type == TWO_BIT || type == GSHARE) {
        std::size_t tableSize = std::size_t{1} << tableBits;
        unsigned char initialState = 0;

        if (type == TWO_BIT || type == GSHARE) {
            initialState = 1;
        }

        predictor.table.assign(tableSize, initialState);
    }

    return predictor;
}

int getIndex(const Predictor& predictor, std::uint64_t pc) {
    if (predictor.type == ALWAYS_TAKEN ||
        predictor.type == ALWAYS_NOT_TAKEN) {
        return -1;
    }

    std::uint64_t pcIndex = pc & predictor.tableMask;

    if (predictor.type == GSHARE) {
        std::uint64_t historyPart = predictor.history & predictor.tableMask;
        pcIndex = (pcIndex ^ historyPart) & predictor.tableMask;
    }

    return static_cast<int>(pcIndex);
}

bool makePrediction(const Predictor& predictor, std::uint64_t pc) {
    if (predictor.type == ALWAYS_TAKEN) {
        return true;
    }

    if (predictor.type == ALWAYS_NOT_TAKEN) {
        return false;
    }

    int index = getIndex(predictor, pc);
    unsigned char state = predictor.table[static_cast<std::size_t>(index)];

    if (predictor.type == ONE_BIT) {
        return state == 1;
    }

    return state >= 2;
}

void updatePredictor(Predictor& predictor, std::uint64_t pc, bool taken) {
    if (predictor.type == ALWAYS_TAKEN ||
        predictor.type == ALWAYS_NOT_TAKEN) {
        return;
    }

    int index = getIndex(predictor, pc);
    std::size_t tableIndex = static_cast<std::size_t>(index);

    if (predictor.type == ONE_BIT) {
        if (taken) {
            predictor.table[tableIndex] = 1;
        } else {
            predictor.table[tableIndex] = 0;
        }
        return;
    }

    unsigned char state = predictor.table[tableIndex];

    if (taken) {
        if (state < 3) {
            state++;
        }
    } else {
        if (state > 0) {
            state--;
        }
    }

    predictor.table[tableIndex] = state;

    if (predictor.type == GSHARE) {
        // The old history chooses the table entry. Update history afterwards.
        predictor.history = predictor.history << 1;
        if (taken) {
            predictor.history = predictor.history | 1;
        }
        predictor.history = predictor.history & predictor.historyMask;
    }
}

PredictionInfo predictAndUpdate(Predictor& predictor, std::uint64_t pc,
                                bool taken) {
    PredictionInfo info;
    info.prediction = makePrediction(predictor, pc);
    info.index = getIndex(predictor, pc);
    info.oldHistory = predictor.history;

    if (info.index >= 0) {
        std::size_t index = static_cast<std::size_t>(info.index);
        info.oldState = predictor.table[index];
    }

    updatePredictor(predictor, pc, taken);

    if (info.index >= 0) {
        std::size_t index = static_cast<std::size_t>(info.index);
        info.newState = predictor.table[index];
    }

    info.newHistory = predictor.history;
    return info;
}

std::string predictorName(PredictorType type) {
    if (type == ALWAYS_TAKEN) {
        return "Always Taken";
    }
    if (type == ALWAYS_NOT_TAKEN) {
        return "Always Not Taken";
    }
    if (type == ONE_BIT) {
        return "One-Bit";
    }
    if (type == TWO_BIT) {
        return "Two-Bit";
    }
    return "GShare";
}
