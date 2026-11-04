#pragma once
#include "model.h"

namespace piu {
// Result details are transient and keyed by attempt, even when players swap sides.
class ResultFeedback {
    struct Notice {
        std::string id;
        std::string code;
        uint64_t expires = 0;
        int account = -1;
        std::optional<int> total;
        std::optional<int> gain;
    };

    std::array<Notice, 2> notices_;

public:
    void observe(const Result& result,
        std::string code,
        uint64_t written,
        int account = -1,
        std::optional<int> total = {});
    void rating(int account, std::optional<int> total);
    void gain(const std::string& id, std::optional<int> before, std::optional<int> after);
    void update(const std::string& id, const std::string& code);
    std::string snapshot(uint64_t clock = now()) const;
};
} // namespace piu
