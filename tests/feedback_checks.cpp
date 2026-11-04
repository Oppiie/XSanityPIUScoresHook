#include "support.h"
#include "engine.h"

namespace piu::test {
namespace {
struct FeedbackApi : Api {
    int error = 0;
    bool offline = false;
    std::function<void()> on_read;
    int posts = 0;
    Score pb{"chart", 981836, false, 33902};

    std::vector<Chart> catalog(const std::string&, const std::string&) override {
        return {{"chart", "Test Song!", "Single", 10}};
    }

    std::vector<Score> scores(const std::string&, const std::string&) override {
        if (on_read) {
            on_read();
        }
        if (offline) {
            throw Error("offline");
        }
        return {pb};
    }

    void upload(const std::string&, const std::string&, const Play& play) override {
        ++posts;
        if (error) {
            throw HttpError(error);
        }
        pb = {play.chart_id, play.score, play.broken, 34408};
    }
};
} // namespace

void feedback_checks() {
    std::vector<Chart> charts;
    std::map<std::string, Score> pool;
    for (int i = 0; i < 50; ++i) {
        auto id = std::to_string(i);
        charts.push_back({id, "Chart", "Single", 19});
        pool[id] = {id, 980000, false, 33210};
    }
    charts.push_back({"new", "New clear", "Double", 18});
    charts.push_back({"coop", "Two players", "CoOp", 2});
    pool["coop"] = {"coop", 999999, false, 99999};
    Score improved{"new", 990000, false, 33200};
    auto before = pool_total(charts, pool);
    check(before == 50 * 33210 && pool_total(charts, pool, &improved) == before,
        "a PB below the merged pool floor gains zero and co-op is excluded");
    improved.pumbility_cents = 34408;
    check(pool_total(charts, pool, &improved) == *before + 1198,
        "a new entry gains only the amount above the displaced clear");
    improved.broken = true;
    check(pool_total(charts, pool, &improved) == before, "a failed PB cannot enter the pool");

    ResultFeedback notices;
    auto stamp = now();
    notices.observe(result("older"), "checking", stamp);
    notices.observe(result("newer"), "checking", stamp);
    notices.update("older", "accepted");
    check(notices.snapshot().find("accepted") == std::string::npos,
        "an older upload cannot overwrite a newer attempt on the same side");
    check(notices.snapshot(stamp + 1200000000) == "PIUCOMPANION 1\n",
        "feedback expires and retains no account or score payload");
    notices.observe(result("injected\nline"), "accepted", stamp);
    check(notices.snapshot().find("injected") == std::string::npos,
        "event IDs cannot introduce records into the feedback format");

    Fixture fixture("feedback");
    Preferences config;
    config.game_root = fixture.game();
    config.accounts[0] = {"Test Player", protect("test-key")};
    auto state = fixture.root / L"state";
    save_preferences(state, config);
    FeedbackApi api;
    Engine engine(state, api, [](const fs::path&) -> std::optional<ExportFrame> {
        return {};
    });
    engine.load();
    auto status_path = GameHook::exports(config.game_root) / L"status.txt";
    auto status = [&] {
        engine.poll();
        return read(status_path);
    };
    engine.capture(result("first", 992575), now());
    check(status().find("first\tchecking") != std::string::npos,
        "capture alone never announces a successful upload");
    api.error = 401;
    engine.sync();
    check(status().find("first\tauth") != std::string::npos && engine.pending_count(0) == 1,
        "invalid credentials retain the score and request account settings");
    api.error = 0;
    engine.sync();
    check(status().find("first\taccepted\t34408\t506") != std::string::npos && engine.pending_count(0) == 0,
        "only confirmed upload acceptance displays submitted");
    auto modified = fs::last_write_time(status_path);
    engine.poll();
    check(
        fs::last_write_time(status_path) == modified, "unchanged outcomes do not rewrite the feedback file");

    engine.capture(result("lost-response", 995000), now());
    api.error = 500;
    engine.sync();
    auto posts = api.posts;
    check(status().find("lost-response\tuncertain") != std::string::npos,
        "an ambiguous response says unconfirmed, never submitted or safely queued for retry");
    engine.sync();
    check(api.posts == posts, "notifications do not alter ambiguous-upload retry safety");
    engine.discard_pending(0);
    check(status().find("lost-response\tdiscarded") != std::string::npos,
        "explicit discard reaches the current result card");

    auto unknown = result("unknown");
    unknown.profile = "Unconfigured";
    engine.capture(unknown, now());
    check(status().find("unknown\tunlinked") != std::string::npos && engine.pending_count(0) == 0,
        "unmatched profiles get a useful explanation without uploading");
    for (const auto& reason : {"rate",
             "judgement",
             "notes",
             "mode",
             "autoplay",
             "chart",
             "profile_changed",
             "disqualified",
             "capture_error"}) {
        auto skipped = result(std::string("skip-") + reason);
        skipped.eligible = false;
        skipped.skip_reason = reason;
        engine.capture(result_from_json(result_json(skipped)), now());
        auto expected = std::string("skipped_") + reason;
        check(status().find(skipped.id + "\t" + expected) != std::string::npos &&
                  load_store(state).receipts.at(skipped.id) == expected && engine.pending_count(0) == 0,
            "specific rejection survives export parsing, persistence and feedback without queuing");
    }

    auto unknown_reason = result("unknown-reason");
    unknown_reason.skip_reason = "bad\nrecord";
    engine.capture(unknown_reason, now());
    check(status().find("unknown-reason\tskipped\t34408\t0\n") != std::string::npos &&
              engine.pending_count(0) == 0,
        "unknown reasons stay ineligible and cannot inject feedback records");
    engine.capture(result("offline", 995000), now());
    api.offline = true;
    rejects(
        [&] {
            engine.sync();
        },
        "offline read remains retryable");
    check(status().find("offline\tretry") != std::string::npos,
        "saved offline scores are distinguished from uncertain POST responses");

    fs::remove(status_path);
    fs::create_directory(status_path);
    api.pb.pumbility_cents.reset();
    engine.capture(result("write-failure", 998000), now());
    engine.poll();
    api.offline = false;
    api.error = 0;
    engine.sync();
    check(engine.pending_count(0) == 0, "feedback write failure cannot stop uploads");

    fs::remove(status_path);
    api.on_read = [&] {
        if (api.pb.score == 1000000) {
            check(status().find("refresh-error\taccepted\tfetching\n") != std::string::npos,
                "accepted result advertises the actual outstanding website read");
            throw Error("refresh failed");
        }
    };
    engine.capture(result("refresh-error", 1000000), now());
    rejects(
        [&] {
            engine.sync();
        },
        "post-upload read failed");
    auto failed_refresh = status();
    check(failed_refresh.find("refresh-error\taccepted") != std::string::npos &&
              failed_refresh.find("fetching") == std::string::npos && engine.pending_count(0) == 0,
        "failed refresh clears the loading state and preserves upload acceptance");

    auto overlay_root = fixture.game(L"overlay-only");
    config.game_root = overlay_root;
    config.sync = false;
    config.overlay = true;
    save_preferences(state, config);
    Engine overlay(state, api, [](const fs::path&) -> std::optional<ExportFrame> {
        return {};
    });
    overlay.load();
    overlay.poll();
    check(!fs::exists(GameHook::exports(overlay_root)), "overlay-only mode writes no feedback files");
}
} // namespace piu::test
