#include "trace.h"

#include <fstream>
#include <sstream>
#include <stdexcept>

bool isBlankOrComment(const std::string& line) {
    std::size_t first = line.find_first_not_of(" \t\r\n\f\v");

    if (first == std::string::npos) {
        return true;
    }

    return line[first] == '#';
}

Branch readBranch(const std::string& line, int lineNumber) {
    std::stringstream stream(line);
    std::string address;
    std::string outcome;
    std::string extra;

    if (!(stream >> address >> outcome)) {
        throw std::runtime_error("Invalid trace at line " +
                                 std::to_string(lineNumber));
    }

    if (stream >> extra) {
        throw std::runtime_error("Invalid trace at line " +
                                 std::to_string(lineNumber));
    }

    if (address.size() <= 2 || address[0] != '0' ||
        (address[1] != 'x' && address[1] != 'X')) {
        throw std::runtime_error("Invalid address at line " +
                                 std::to_string(lineNumber));
    }

    if (outcome != "T" && outcome != "N") {
        throw std::runtime_error("Invalid outcome at line " +
                                 std::to_string(lineNumber));
    }

    std::size_t parsedCharacters = 0;
    std::uint64_t pc = 0;

    try {
        pc = std::stoull(address, &parsedCharacters, 16);
    } catch (const std::exception&) {
        throw std::runtime_error("Invalid address at line " +
                                 std::to_string(lineNumber));
    }

    if (parsedCharacters != address.size()) {
        throw std::runtime_error("Invalid address at line " +
                                 std::to_string(lineNumber));
    }

    Branch branch;
    branch.pc = pc;
    branch.taken = outcome == "T";
    return branch;
}

std::vector<Branch> readTrace(const std::string& filename) {
    std::ifstream file(filename);

    if (!file.is_open()) {
        throw std::runtime_error("Could not open trace file: " + filename);
    }

    std::vector<Branch> branches;
    std::string line;
    int lineNumber = 0;

    while (std::getline(file, line)) {
        lineNumber++;

        if (isBlankOrComment(line)) {
            continue;
        }

        Branch branch = readBranch(line, lineNumber);
        branches.push_back(branch);
    }

    if (file.bad()) {
        throw std::runtime_error("Could not read trace file: " + filename);
    }

    return branches;
}
