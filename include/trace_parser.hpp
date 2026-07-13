#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace branchsim {

struct TraceRecord {
    std::uint64_t pc{0U};
    bool taken{false};
};

class TraceParser {
public:
    explicit TraceParser(std::string path);

    void for_each(
        const std::function<void(const TraceRecord&)>& callback) const;

    [[nodiscard]] const std::string& path() const noexcept;

private:
    std::string path_;
};

}  // namespace branchsim
