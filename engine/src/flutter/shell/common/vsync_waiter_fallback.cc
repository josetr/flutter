// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/common/vsync_waiter_fallback.h"

#include <cstdlib>
#include <cstdio>
#include <memory>

#include "flutter/fml/logging.h"
#include "flutter/fml/message_loop.h"
#include "flutter/fml/trace_event.h"

namespace flutter {
namespace {

static fml::TimeDelta GetFrameInterval() {
  static const fml::TimeDelta frame_interval = [] {
  constexpr double kDefaultRefreshRate = 60.0;
  constexpr double kMinRefreshRate = 1.0;
  constexpr double kMaxRefreshRate = 1000.0;

  double refresh_rate = kDefaultRefreshRate;
  const char* source = "default";
  const char* refresh_rate_value =
      std::getenv("FLUTTER_ENGINE_FALLBACK_VSYNC_HZ");
  if (refresh_rate_value != nullptr && refresh_rate_value[0] != '\0') {
    char* end = nullptr;
    double parsed_refresh_rate = std::strtod(refresh_rate_value, &end);
    if (end != refresh_rate_value && parsed_refresh_rate >= kMinRefreshRate &&
        parsed_refresh_rate <= kMaxRefreshRate) {
      refresh_rate = parsed_refresh_rate;
      source = "FLUTTER_ENGINE_FALLBACK_VSYNC_HZ";
    } else {
      FML_LOG(WARNING) << "Ignoring invalid FLUTTER_ENGINE_FALLBACK_VSYNC_HZ="
                       << refresh_rate_value;
    }
  }

  fml::TimeDelta interval = fml::TimeDelta::FromSecondsF(1.0 / refresh_rate);
  FML_LOG(WARNING) << "VsyncWaiterFallback using timer-based vsync at "
                   << refresh_rate << " Hz from " << source << " ("
                   << interval.ToMicroseconds() << " us frame interval).";
  if (FILE* log_file = std::fopen("/home/jose/flutter-pr/vsync_fallback.log",
                                  "a")) {
    std::fprintf(log_file,
                 "VsyncWaiterFallback using timer-based vsync at %.3f Hz from "
                 "%s (%lld us frame interval)\n",
                 refresh_rate, source,
                 static_cast<long long>(interval.ToMicroseconds()));
    std::fclose(log_file);
  }
  return interval;
  }();
  return frame_interval;
}

static fml::TimePoint SnapToNextTick(fml::TimePoint value,
                                     fml::TimePoint tick_phase,
                                     fml::TimeDelta tick_interval) {
  fml::TimeDelta offset = (tick_phase - value) % tick_interval;
  if (offset != fml::TimeDelta::Zero()) {
    offset = offset + tick_interval;
  }
  return value + offset;
}

}  // namespace

VsyncWaiterFallback::VsyncWaiterFallback(const TaskRunners& task_runners,
                                         bool for_testing)
    : VsyncWaiter(task_runners),
      phase_(fml::TimePoint::Now()),
      for_testing_(for_testing) {}

VsyncWaiterFallback::~VsyncWaiterFallback() = default;

// |VsyncWaiter|
void VsyncWaiterFallback::AwaitVSync() {
  const fml::TimeDelta kSingleFrameInterval = GetFrameInterval();
  auto frame_start_time =
      SnapToNextTick(fml::TimePoint::Now(), phase_, kSingleFrameInterval);
  auto frame_target_time = frame_start_time + kSingleFrameInterval;

  TRACE_EVENT2_INT("flutter", "PlatformVsync", "frame_start_time",
                   frame_start_time.ToEpochDelta().ToMicroseconds(),
                   "frame_target_time",
                   frame_target_time.ToEpochDelta().ToMicroseconds());

  std::weak_ptr<VsyncWaiterFallback> weak_this =
      std::static_pointer_cast<VsyncWaiterFallback>(shared_from_this());

  task_runners_.GetUITaskRunner()->PostTaskForTime(
      [frame_start_time, frame_target_time, weak_this]() {
        if (auto vsync_waiter = weak_this.lock()) {
          vsync_waiter->FireCallback(frame_start_time, frame_target_time,
                                     !vsync_waiter->for_testing_);
        }
      },
      frame_start_time);
}

}  // namespace flutter
