/*
 *  Copyright 2019 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "rtc_base/experiments/balanced_degradation_settings.h"

#include <limits>

#include "rtc_base/experiments/field_trial_list.h"
#include "rtc_base/experiments/field_trial_parser.h"
#include "rtc_base/logging.h"
#include "system_wrappers/include/field_trial.h"

namespace webrtc {
namespace {
constexpr char kFieldTrial[] = "WebRTC-Video-BalancedDegradationSettings";
constexpr int kMinFps = 1;
constexpr int kMaxFps = 100;  // 100 means unlimited fps.

std::vector<BalancedDegradationSettings::Config> DefaultConfigs() {
  return {{320 * 240,
           7,
           0,
           BalancedDegradationSettings::kNoFpsDiff,
           {0, 0, 0},
           {0, 0, 0},
           {0, 0, 0},
           {0, 0, 0}},
          {480 * 270,
           10,
           0,
           BalancedDegradationSettings::kNoFpsDiff,
           {0, 0, 0},
           {0, 0, 0},
           {0, 0, 0},
           {0, 0, 0}},
          {640 * 480,
           15,
           0,
           BalancedDegradationSettings::kNoFpsDiff,
           {0, 0, 0},
           {0, 0, 0},
           {0, 0, 0},
           {0, 0, 0}}};
}

bool IsValidConfig(
    const BalancedDegradationSettings::CodecTypeSpecific& config) {
  if (config.GetQpLow().has_value() != config.GetQpHigh().has_value()) {
    RTC_LOG(LS_WARNING) << "Neither or both thresholds should be set.";
    return false;
  }
  if (config.GetQpLow().has_value() && config.GetQpHigh().has_value() &&
      config.GetQpLow().value() >= config.GetQpHigh().value()) {
    RTC_LOG(LS_WARNING) << "Invalid threshold value, low >= high threshold.";
    return false;
  }
  if (config.GetFps().has_value() && (config.GetFps().value() < kMinFps ||
                                      config.GetFps().value() > kMaxFps)) {
    RTC_LOG(LS_WARNING) << "Unsupported fps setting, value ignored.";
    return false;
  }
  return true;
}

bool IsValid(const BalancedDegradationSettings::CodecTypeSpecific& config1,
             const BalancedDegradationSettings::CodecTypeSpecific& config2) {
  bool both_or_none_set = ((config1.qp_low > 0) == (config2.qp_low > 0) &&
                           (config1.qp_high > 0) == (config2.qp_high > 0) &&
                           (config1.fps > 0) == (config2.fps > 0));
  if (!both_or_none_set) {
    RTC_LOG(LS_WARNING) << "Invalid value, all/none should be set.";
    return false;
  }
  if (config1.fps > 0 && config1.fps < config2.fps) {
    RTC_LOG(LS_WARNING) << "Invalid fps/pixel value provided.";
    return false;
  }
  return true;
}

bool IsValid(const std::vector<BalancedDegradationSettings::Config>& configs) {
  if (configs.size() <= 1) {
    RTC_LOG(LS_WARNING) << "Unsupported size, value ignored.";
    return false;
  }
  for (const auto& config : configs) {
    if (config.fps < kMinFps || config.fps > kMaxFps) {
      RTC_LOG(LS_WARNING) << "Unsupported fps setting, value ignored.";
      return false;
    }
  }
  int last_kbps = configs[0].kbps;
  for (size_t i = 1; i < configs.size(); ++i) {
    if (configs[i].kbps > 0) {
      if (configs[i].kbps < last_kbps) {
        RTC_LOG(LS_WARNING) << "Invalid bitrate value provided.";
        return false;
      }
      last_kbps = configs[i].kbps;
    }
  }
  for (size_t i = 1; i < configs.size(); ++i) {
    if (configs[i].pixels < configs[i - 1].pixels ||
        configs[i].fps < configs[i - 1].fps) {
      RTC_LOG(LS_WARNING) << "Invalid fps/pixel value provided.";
      return false;
    }
    if (!IsValid(configs[i].vp8, configs[i - 1].vp8) ||
        !IsValid(configs[i].vp9, configs[i - 1].vp9) ||
        !IsValid(configs[i].h264, configs[i - 1].h264) ||
        !IsValid(configs[i].generic, configs[i - 1].generic)) {
      return false;
    }
  }
  for (const auto& config : configs) {
    if (!IsValidConfig(config.vp8) || !IsValidConfig(config.vp9) ||
        !IsValidConfig(config.h264) || !IsValidConfig(config.generic)) {
      return false;
    }
  }
  return true;
}

std::vector<BalancedDegradationSettings::Config> GetValidOrDefault(
    const std::vector<BalancedDegradationSettings::Config>& configs) {
  if (IsValid(configs)) {
    return configs;
  }
  return DefaultConfigs();
}

absl::optional<VideoEncoder::QpThresholds> GetThresholds(
    VideoCodecType type,
    const BalancedDegradationSettings::Config& config) {
  absl::optional<int> low;
  absl::optional<int> high;

  switch (type) {
    case kVideoCodecVP8:
      low = config.vp8.GetQpLow();
      high = config.vp8.GetQpHigh();
      break;
    case kVideoCodecVP9:
      low = config.vp9.GetQpLow();
      high = config.vp9.GetQpHigh();
      break;
    case kVideoCodecH264:
      low = config.h264.GetQpLow();
      high = config.h264.GetQpHigh();
      break;
    case kVideoCodecGeneric:
      low = config.generic.GetQpLow();
      high = config.generic.GetQpHigh();
      break;
    default:
      break;
  }

  if (low && high) {
    RTC_LOG(LS_INFO) << "QP thresholds: low: " << *low << ", high: " << *high;
    return absl::optional<VideoEncoder::QpThresholds>(
        VideoEncoder::QpThresholds(*low, *high));
  }
  return absl::nullopt;
}

int GetFps(VideoCodecType type,
           const absl::optional<BalancedDegradationSettings::Config>& config) {
  if (!config.has_value()) {
    return std::numeric_limits<int>::max();
  }

  absl::optional<int> fps;
  switch (type) {
    case kVideoCodecVP8:
      fps = config->vp8.GetFps();
      break;
    case kVideoCodecVP9:
      fps = config->vp9.GetFps();
      break;
    case kVideoCodecH264:
      fps = config->h264.GetFps();
      break;
    case kVideoCodecGeneric:
      fps = config->generic.GetFps();
      break;
    default:
      break;
  }

  const int framerate = fps.value_or(config->fps);

  return (framerate == kMaxFps) ? std::numeric_limits<int>::max() : framerate;
}
}  // namespace

absl::optional<int> BalancedDegradationSettings::CodecTypeSpecific::GetQpLow()
    const {
  return (qp_low > 0) ? absl::optional<int>(qp_low) : absl::nullopt;
}

absl::optional<int> BalancedDegradationSettings::CodecTypeSpecific::GetQpHigh()
    const {
  return (qp_high > 0) ? absl::optional<int>(qp_high) : absl::nullopt;
}

absl::optional<int> BalancedDegradationSettings::CodecTypeSpecific::GetFps()
    const {
  return (fps > 0) ? absl::optional<int>(fps) : absl::nullopt;
}

BalancedDegradationSettings::Config::Config() = default;

BalancedDegradationSettings::Config::Config(int pixels,
                                            int fps,
                                            int kbps,
                                            int fps_diff,
                                            CodecTypeSpecific vp8,
                                            CodecTypeSpecific vp9,
                                            CodecTypeSpecific h264,
                                            CodecTypeSpecific generic)
    : pixels(pixels),
      fps(fps),
      kbps(kbps),
      fps_diff(fps_diff),
      vp8(vp8),
      vp9(vp9),
      h264(h264),
      generic(generic) {}

BalancedDegradationSettings::BalancedDegradationSettings() {
  FieldTrialStructList<Config> configs(
      {FieldTrialStructMember("pixels", [](Config* c) { return &c->pixels; }),
       FieldTrialStructMember("fps", [](Config* c) { return &c->fps; }),
       FieldTrialStructMember("kbps", [](Config* c) { return &c->kbps; }),
       FieldTrialStructMember("fps_diff",
                              [](Config* c) { return &c->fps_diff; }),
       FieldTrialStructMember("vp8_qp_low",
                              [](Config* c) { return &c->vp8.qp_low; }),
       FieldTrialStructMember("vp8_qp_high",
                              [](Config* c) { return &c->vp8.qp_high; }),
       FieldTrialStructMember("vp8_fps", [](Config* c) { return &c->vp8.fps; }),
       FieldTrialStructMember("vp9_qp_low",
                              [](Config* c) { return &c->vp9.qp_low; }),
       FieldTrialStructMember("vp9_qp_high",
                              [](Config* c) { return &c->vp9.qp_high; }),
       FieldTrialStructMember("vp9_fps", [](Config* c) { return &c->vp9.fps; }),
       FieldTrialStructMember("h264_qp_low",
                              [](Config* c) { return &c->h264.qp_low; }),
       FieldTrialStructMember("h264_qp_high",
                              [](Config* c) { return &c->h264.qp_high; }),
       FieldTrialStructMember("h264_fps",
                              [](Config* c) { return &c->h264.fps; }),
       FieldTrialStructMember("generic_qp_low",
                              [](Config* c) { return &c->generic.qp_low; }),
       FieldTrialStructMember("generic_qp_high",
                              [](Config* c) { return &c->generic.qp_high; }),
       FieldTrialStructMember("generic_fps",
                              [](Config* c) { return &c->generic.fps; })},
      {});

  ParseFieldTrial({&configs}, field_trial::FindFullName(kFieldTrial));

  configs_ = GetValidOrDefault(configs.Get());
  RTC_DCHECK_GT(configs_.size(), 1);
}

BalancedDegradationSettings::~BalancedDegradationSettings() {}

std::vector<BalancedDegradationSettings::Config>
BalancedDegradationSettings::GetConfigs() const {
  return configs_;
}

int BalancedDegradationSettings::MinFps(VideoCodecType type, int pixels) const {
  return GetFps(type, GetMinFpsConfig(pixels));
}

absl::optional<BalancedDegradationSettings::Config>
BalancedDegradationSettings::GetMinFpsConfig(int pixels) const {
  for (const auto& config : configs_) {
    if (pixels <= config.pixels)
      return config;
  }
  return absl::nullopt;
}

int BalancedDegradationSettings::MaxFps(VideoCodecType type, int pixels) const {
  return GetFps(type, GetMaxFpsConfig(pixels));
}

absl::optional<BalancedDegradationSettings::Config>
BalancedDegradationSettings::GetMaxFpsConfig(int pixels) const {
  for (size_t i = 0; i < configs_.size() - 1; ++i) {
    if (pixels <= configs_[i].pixels)
      return configs_[i + 1];
  }
  return absl::nullopt;
}

absl::optional<int> BalancedDegradationSettings::NextHigherBitrateKbps(
    int pixels) const {
  for (size_t i = 0; i < configs_.size() - 1; ++i) {
    if (pixels <= configs_[i].pixels) {
      return (configs_[i + 1].kbps > 0)
                 ? absl::optional<int>(configs_[i + 1].kbps)
                 : absl::nullopt;
    }
  }
  return absl::nullopt;
}

absl::optional<int> BalancedDegradationSettings::MinFpsDiff(int pixels) const {
  for (const auto& config : configs_) {
    if (pixels <= config.pixels) {
      return (config.fps_diff > kNoFpsDiff)
                 ? absl::optional<int>(config.fps_diff)
                 : absl::nullopt;
    }
  }
  return absl::nullopt;
}

absl::optional<VideoEncoder::QpThresholds>
BalancedDegradationSettings::GetQpThresholds(VideoCodecType type,
                                             int pixels) const {
  return GetThresholds(type, GetConfig(pixels));
}

BalancedDegradationSettings::Config BalancedDegradationSettings::GetConfig(
    int pixels) const {
  for (const auto& config : configs_) {
    if (pixels <= config.pixels)
      return config;
  }
  return configs_.back();  // Use last above highest pixels.
}

}  // namespace webrtc
