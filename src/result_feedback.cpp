#include "result_feedback.h"

namespace piu {
void ResultFeedback::observe(
    const Result& result, std::string code, uint64_t written, int account, std::optional<int> total) {
    if (result.side < 1 || result.side > 2 || result.id.empty() || result.id.size() > 128 ||
        result.id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_") !=
            std::string::npos) {
        return;
    }

    auto& notice = notices_[result.side - 1];
    if (notice.id != result.id) {
        notice = {result.id, std::move(code), written + 1200000000, account, total, {}};
    }
}

void ResultFeedback::update(const std::string& id, const std::string& code) {
    for (auto& notice : notices_) {
        if (notice.id == id) {
            notice.code = code;
        }
    }
}

void ResultFeedback::rating(int account, std::optional<int> total) {
    for (auto& notice : notices_) {
        if (notice.account == account) {
            notice.total = total;
        }
    }
}

void ResultFeedback::gain(const std::string& id, std::optional<int> before, std::optional<int> after) {
    for (auto& notice : notices_) {
        if (notice.id == id && before && after && *after >= *before) {
            notice.gain = *after - *before;
        }
    }
}

std::string ResultFeedback::snapshot(uint64_t clock) const {
    std::string text = "PIUCOMPANION 1\n";
    for (const auto& notice : notices_) {
        if (!notice.id.empty() && clock < notice.expires) {
            text += notice.id + "\t" + notice.code;
            if (notice.total) {
                const bool skipped = notice.code == "covered" || notice.code == "failed_disabled" ||
                                     notice.code == "unmatched" || notice.code.starts_with("skipped");
                text += "\t" + std::to_string(*notice.total) + "\t" +
                        (notice.gain  ? std::to_string(*notice.gain)
                            : skipped ? "0"
                                      : "-");
            }
            text += "\n";
        }
    }

    return text;
}
} // namespace piu
