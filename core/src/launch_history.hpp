// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "solar/core.hpp"
#include <algorithm>
#include <limits>

namespace solar::launch_history {
struct Snapshot {
    Json history = Json::object();
    Json document = Json::object();
    Json warning;
    bool writable = true;
};
Snapshot load(const fs::path &directory, const Json &settings);
// Every mutation is an incremental transaction under an OS-held file lock.
// Busy locks fail immediately; the API keeps pending work for a later checkpoint.
Json accumulate(const fs::path &directory, bool count_launch,
                std::int64_t timestamp, std::int64_t seconds);
std::int64_t number(const Json &history, const std::string &key);
Json started(const Json &history, std::int64_t timestamp);
Json elapsed(const Json &history, std::int64_t seconds);

// Receives a monotonic time only while this launcher owns the process handle.
// The uncertain interval before an observed exit is deliberately not included.
class ObservedRuntime {
    bool active_ = false;
    std::int64_t sampled_ms_ = 0, observed_ms_ = 0, saved_seconds_ = 0;
public:
    void start(std::int64_t monotonic_ms) {
        active_ = true;
        sampled_ms_ = monotonic_ms;
        observed_ms_ = 0;
        saved_seconds_ = 0;
    }
    void observe(std::int64_t monotonic_ms, bool running) {
        if (!active_) return;
        if (running && monotonic_ms > sampled_ms_) {
            const auto delta = monotonic_ms - sampled_ms_;
            observed_ms_ = std::min(std::numeric_limits<std::int64_t>::max() - delta,
                                    observed_ms_) + delta;
        }
        sampled_ms_ = std::max(sampled_ms_, monotonic_ms);
        if (!running) active_ = false;
    }
    std::int64_t pending_seconds() const {
        return std::max(std::int64_t(0), observed_ms_ / 1000 - saved_seconds_);
    }
    void saved(std::int64_t seconds) {
        saved_seconds_ += std::clamp(seconds, std::int64_t(0), pending_seconds());
    }
};
Json flush(const fs::path &directory, bool &launch_pending,
           std::int64_t timestamp, ObservedRuntime &runtime);
} // namespace solar::launch_history
