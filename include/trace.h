#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct Branch {
    std::uint64_t pc = 0;
    bool taken = false;
};

std::vector<Branch> readTrace(const std::string& filename);
