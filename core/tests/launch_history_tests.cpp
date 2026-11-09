// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
#include "solar/core.hpp"
#include "solar/engine.hpp"
#include "../src/launch_history.hpp"
#include <fstream>
#include <iostream>
#include <limits>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif
using namespace solar;
namespace history = solar::launch_history;
static int checks = 0;
static void check(bool okay, const char *label) {
    ++checks;
    if (!okay) throw std::runtime_error(label);
}
// Exercise the same OS lock from a separate process. Ownership disappears on
// process death, but the inode stays in place to avoid split-lock races.
class ExternalLock {
#ifdef _WIN32
    HANDLE handle_ = INVALID_HANDLE_VALUE;
    OVERLAPPED range_{};
#else
    int handle_ = -1;
#endif
public:
    explicit ExternalLock(const fs::path &directory) {
        const auto file = directory / ".solar-history.lock";
#ifdef _WIN32
        handle_ = CreateFileW(file.c_str(), GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle_ == INVALID_HANDLE_VALUE ||
            !LockFileEx(handle_, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY,
                        0, 1, 0, &range_)) throw std::runtime_error("test lock failed");
#else
        handle_ = ::open(file.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
        if (handle_ < 0 || ::flock(handle_, LOCK_EX | LOCK_NB) != 0)
            throw std::runtime_error("test lock failed");
#endif
    }
    ~ExternalLock() {
#ifdef _WIN32
        if (handle_ != INVALID_HANDLE_VALUE) {
            UnlockFileEx(handle_, 0, 1, 0, &range_); CloseHandle(handle_);
        }
#else
        if (handle_ >= 0) { ::flock(handle_, LOCK_UN); ::close(handle_); }
#endif
    }
};
struct Children {
    std::vector<std::int64_t> handles;
    ~Children() { for (auto handle : handles) if (handle) terminate_process(handle); }
};
int main(int argc, char **argv) {
    if (argc == 4 && std::string(argv[1]) == "--increment") {
        try {
            const auto directory = fs::u8path(argv[2]);
            for (int n = 0; n < std::stoi(argv[3]); ++n) {
                bool committed = false;
                // Bounded contention retry belongs to the test harness only.
                // Production checkpoints do one nonblocking attempt per 30 s.
                for (int attempt = 0; attempt < 5000 && !committed; ++attempt) {
                    try { history::accumulate(directory, true, 300 + n, 1); committed = true; }
                    catch (const Error &error) {
                        if (error.code != "history_locked") throw;
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    }
                }
                if (!committed) return 2;
            }
            return 0;
        } catch (...) { return 3; }
    }
    if (argc == 4 && std::string(argv[1]) == "--hold-lock") {
        try {
            const ExternalLock held(fs::u8path(argv[2]));
            engine::atomic_write(fs::u8path(argv[3]), "ready");
            std::this_thread::sleep_for(std::chrono::seconds(30));
            return 0;
        } catch (...) { return 4; }
    }
    const auto root = fs::temp_directory_path() / ("secl-history-tests-" + random_url_token());
    try {
        fs::create_directories(root);
        const Json settings{{"display", {{"name", "Preserved name"}}},
            {"history", {{"last_launch", 100}, {"custom_history", "keep"}}}};
        engine::save_instance_settings(root, settings);
        const auto settings_before = read_file(root / ".solar-instance.json");
        auto original = history::load(root, settings);
        check(original.writable && original.warning.is_null(), "legacy statistics are readable");
        check(history::number(original.history, "last_launch") == 100, "legacy last launch remains");
        history::accumulate(root, true, 200, 35);
        auto loaded = history::load(root, settings);
        check(history::number(loaded.history, "last_launch") == 200, "successful launch timestamp saved");
        check(history::number(loaded.history, "launch_count") == 1, "one launch saved");
        check(history::number(loaded.history, "runtime_seconds") == 35, "observed seconds saved");
        check(loaded.history.at("custom_history") == "keep", "unrelated history field retained");
        check(read_file(root / ".solar-instance.json") == settings_before, "history never replaces instance settings");

        // Saving an old settings snapshot cannot lower the independent totals.
        engine::save_instance_settings(root, settings);
        const auto before = read_file(root / ".solar-instance.json");
        history::accumulate(root, true, 150, 5);
        auto stale = history::load(root, engine::load_instance_settings(root));
        check(history::number(stale.history, "last_launch") == 200, "clock rollback does not lower last launch");
        check(history::number(stale.history, "launch_count") == 2, "another created process counted once");
        check(history::number(stale.history, "runtime_seconds") == 40, "new seconds accumulate");
        check(read_file(root / ".solar-instance.json") == before, "history leaves all settings unchanged");

        history::ObservedRuntime timer;
        timer.start(1000);
        timer.observe(1600, true);
        timer.observe(2300, true);
        check(timer.pending_seconds() == 1, "sub-second samples accumulate conservatively");
        timer.saved(timer.pending_seconds());
        check(timer.pending_seconds() == 0, "checkpoint is acknowledged once");
        timer.observe(4000, true);
        check(timer.pending_seconds() == 2, "idle time while the process remains live is included");
        timer.observe(9000, false);
        check(timer.pending_seconds() == 2, "uncertain time after last live observation is excluded");
        timer.saved(timer.pending_seconds());
        timer.observe(12000, false);
        check(timer.pending_seconds() == 0, "repeat exit observations cannot double settle");
        timer.start(20000);
        timer.observe(21000, true);
        timer.observe(20500, true);
        check(timer.pending_seconds() == 1, "backward sample never manufactures elapsed time");
        history::ObservedRuntime restarted;
        check(restarted.pending_seconds() == 0, "a launcher restart never reconstructs orphan duration");

        const auto file = root / ".solar-history.json";
        engine::atomic_write(file, "{broken history");
        const auto damaged = history::load(root, settings);
        check(!damaged.writable && !damaged.warning.is_null(), "damaged history exposes a warning");
        bool rejected = false;
        try { history::accumulate(root, false, 0, 1); } catch (const Error &) { rejected = true; }
        check(rejected && read_file(file) == "{broken history", "damaged history is never reset");
        engine::atomic_write(file, Json{{"schema", 99}, {"history", Json::object()}}.dump());
        check(!history::load(root, settings).writable, "unknown future schema is retained");
        engine::atomic_write(file, Json{{"schema", 1}, {"history", {{"launch_count", -1}}}}.dump());
        check(!history::load(root, settings).writable, "negative counters are not reset to a fresh total");
        engine::atomic_write(file, Json{{"schema", 1}, {"history", {{"runtime_seconds", "many"}}}}.dump());
        check(!history::load(root, settings).writable, "wrong counter types warn without overwrite");

        Json maximum{{"launch_count", std::numeric_limits<std::int64_t>::max()},
                     {"runtime_seconds", std::numeric_limits<std::int64_t>::max()}};
        check(history::number(history::started(maximum, 1), "launch_count") ==
            std::numeric_limits<std::int64_t>::max(), "launch counter saturates instead of wrapping");
        check(history::number(history::elapsed(maximum, 10), "runtime_seconds") ==
            std::numeric_limits<std::int64_t>::max(), "duration saturates instead of wrapping");

        const auto game = root / "game", directory = game / "versions/fixture";
        fs::create_directories(directory);
        engine::atomic_write(directory / "fixture.json", Json{{"id", "fixture"},
            {"type", "release"}, {"mainClass", "example.Main"}, {"libraries", Json::array()}}.dump());
        history::accumulate(directory, true, 500, 90);
        const auto versions = scan_versions(game);
        check(versions.size() == 1 && versions[0].at("launch_count") == 1 &&
            versions[0].at("runtime_seconds") == 90, "local version scan exposes stored totals");
        const auto instances = engine::scan_instances({game}, Json::object()).at("instances");
        check(instances.size() == 1 && instances[0].at("last_launch") == 500 &&
            instances[0].at("launch_count") == 1 && instances[0].at("runtime_seconds") == 90,
            "game-library scan propagates all three history values");
        engine::atomic_write(directory / ".solar-history.json", "not json");
        const auto with_warning = scan_versions(game);
        check(with_warning[0].at("available") == true &&
            with_warning[0].at("history_warning").is_object(), "history damage does not make a game unlaunchable");

        const auto executable = fs::absolute(fs::u8path(argv[0]));
        const auto concurrent = root / "concurrent";
        fs::create_directories(concurrent);
        engine::save_instance_settings(concurrent, Json{{"history", {{"custom", "retained"}}}});
        Children children;
        for (int n = 0; n < 4; ++n) {
            Command command; command.executable = executable; command.directory = root;
            command.arguments = {"--increment", concurrent.u8string(), "32"};
            children.handles.push_back(spawn_game(command));
        }
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        bool done = false;
        while (!done && std::chrono::steady_clock::now() < deadline) {
            done = true;
            for (auto &handle : children.handles) if (handle) {
                const auto exit = game_exit(handle);
                if (exit == -1) done = false;
                else { handle = 0; check(exit == 0, "concurrent writer exits successfully"); }
            }
            if (!done) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        check(done, "cross-process updates finish within bounded deadline");
        const auto together = history::load(concurrent, engine::load_instance_settings(concurrent));
        check(history::number(together.history, "launch_count") == 128,
              "four processes preserve every recorded launch without lost updates");
        check(history::number(together.history, "runtime_seconds") == 128,
              "four processes preserve every observed runtime delta");
        check(together.history.at("custom") == "retained", "transaction preserves unknown legacy fields");

        const auto blocked = root / "blocked", ready = root / "lock-ready";
        fs::create_directories(blocked);
        Command holder; holder.executable = executable; holder.directory = root;
        holder.arguments = {"--hold-lock", blocked.u8string(), ready.u8string()};
        children.handles.push_back(spawn_game(holder));
        const auto ready_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (!fs::exists(ready) && std::chrono::steady_clock::now() < ready_deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        check(fs::exists(ready), "another process owns the history OS lock");
        history::ObservedRuntime pending_runtime;
        pending_runtime.start(1000); pending_runtime.observe(11000, true);
        bool launch_pending = true, locked = false;
        const auto busy_start = std::chrono::steady_clock::now();
        try { history::flush(blocked, launch_pending, 600, pending_runtime); }
        catch (const Error &error) { locked = error.code == "history_locked"; }
        check(locked && std::chrono::steady_clock::now() - busy_start < std::chrono::milliseconds(500),
              "busy lock fails immediately instead of blocking or spinning");
        check(launch_pending && pending_runtime.pending_seconds() == 10,
              "lock failure preserves both launch count and pending runtime");
        check(!fs::exists(blocked / ".solar-history.json"), "lock failure does not write a partial record");
        terminate_process(children.handles.back()); children.handles.back() = 0;
        check(fs::is_regular_file(blocked / ".solar-history.lock"),
              "crashed owner leaves harmless lock inode without retaining OS ownership");
        const auto recovered = history::flush(blocked, launch_pending, 600, pending_runtime);
        check(!launch_pending && pending_runtime.pending_seconds() == 0,
              "successful retry acknowledges pending work only after commit");
        check(history::number(recovered, "launch_count") == 1 &&
              history::number(recovered, "runtime_seconds") == 10,
              "owner death releases lock and pending changes settle exactly once");
        const auto repeated = history::flush(blocked, launch_pending, 600, pending_runtime);
        check(history::number(repeated, "launch_count") == 1 &&
              history::number(repeated, "runtime_seconds") == 10, "repeat flush never doubles settled statistics");

        engine::atomic_write(blocked / ".solar-history.json", "corrupt");
        pending_runtime.start(12000); pending_runtime.observe(15000, true); launch_pending = true;
        bool invalid = false;
        try { history::flush(blocked, launch_pending, 700, pending_runtime); }
        catch (const Error &error) { invalid = error.code == "invalid_history"; }
        check(invalid && launch_pending && pending_runtime.pending_seconds() == 3 &&
              read_file(blocked / ".solar-history.json") == "corrupt",
              "corrupt-file failure preserves pending work and original bytes");
        // Force an actual atomic commit failure after acquiring the lock and
        // reading a valid snapshot; no pending values may be acknowledged.
        engine::atomic_write(blocked / ".solar-history.json",
            Json{{"schema", 1}, {"history", Json::object()}}.dump());
#ifdef _WIN32
        check(SetFileAttributesW((blocked / ".solar-history.json").c_str(), FILE_ATTRIBUTE_READONLY),
              "prepare read-only history target");
#else
        fs::permissions(blocked, fs::perms::owner_read | fs::perms::owner_exec);
#endif
        bool failed_commit = false;
        try { history::flush(blocked, launch_pending, 700, pending_runtime); }
        catch (const Error &error) { failed_commit = error.stage == "files"; }
#ifdef _WIN32
        SetFileAttributesW((blocked / ".solar-history.json").c_str(), FILE_ATTRIBUTE_NORMAL);
#else
        fs::permissions(blocked, fs::perms::owner_all);
#endif
        check(failed_commit && launch_pending && pending_runtime.pending_seconds() == 3,
              "atomic write failure never acknowledges uncommitted count or seconds");
        check(history::number(history::load(blocked, Json::object()).history, "launch_count") == 0,
              "failed atomic write leaves original valid totals intact");
        const auto final_retry = history::flush(blocked, launch_pending, 700, pending_runtime);
        check(history::number(final_retry, "launch_count") == 1 &&
              history::number(final_retry, "runtime_seconds") == 3,
              "later successful retry commits previously failed pending changes");

        fs::remove_all(root);
        std::cout << checks << " launch-history checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        std::error_code ignored; fs::remove_all(root, ignored);
        return 1;
    }
}
