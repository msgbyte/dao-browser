// Copyright 2026 Dao Browser Authors. All rights reserved.
#include "dao/browser/performance/dao_resource_usage_state.h"

namespace dao {

DaoResourceUsageState::DaoResourceUsageState() = default;

void DaoResourceUsageState::UpdateCpu(base::TimeTicks time,
                                     base::TimeDelta cumulative) {
  // Shared asynchronous samples need bounded jitter tolerance.
  if (!cpu_time_.is_null() && time - cpu_time_ < base::Seconds(25)) {
    return;
  }
  const auto elapsed = time - cpu_time_;
  if (cpu_time_.is_null() || elapsed > base::Seconds(75) ||
      cumulative < cpu_total_) {
    cpu_high_ = false;
    cpu_high_duration_ = cpu_low_duration_ = base::TimeDelta();
  } else {
    const double usage = (cumulative - cpu_total_) / elapsed;
    cpu_high_duration_ = usage >= 0.8 ? cpu_high_duration_ + elapsed
                                     : base::TimeDelta();
    cpu_low_duration_ = usage < 0.4 ? cpu_low_duration_ + elapsed
                                   : base::TimeDelta();
    if (cpu_high_duration_ >= base::Seconds(90)) {
      cpu_high_ = true;
    } else if (cpu_low_duration_ >= base::Seconds(60)) {
      cpu_high_ = false;
    }
  }
  cpu_time_ = time;
  cpu_total_ = cumulative;
  UpdateDismissal();
}

void DaoResourceUsageState::UpdateMemory(base::TimeTicks time,
                                        base::ByteSize bytes,
                                        base::ByteSize threshold) {
  if (!memory_time_.is_null() && time - memory_time_ < base::Seconds(120)) {
    return;
  }
  if (!memory_time_.is_null() && time - memory_time_ > base::Minutes(5)) {
    memory_high_count_ = memory_low_count_ = 0;
    memory_high_ = false;
  }
  memory_time_ = time;
  memory_high_count_ = bytes >= threshold ? memory_high_count_ + 1 : 0;
  memory_low_count_ = bytes.InBytesF() < threshold.InBytesF() * 0.8
                          ? memory_low_count_ + 1 : 0;
  if (memory_high_count_ >= 2) {
    memory_high_ = true;
    memory_high_count_ = 2;
  } else if (memory_low_count_ >= 2) {
    memory_high_ = false;
    memory_low_count_ = 2;
  }
  UpdateDismissal();
}

void DaoResourceUsageState::Expire(base::TimeTicks now) {
  if (!cpu_time_.is_null() && now - cpu_time_ > base::Seconds(75)) {
    cpu_time_ = {};
    cpu_high_ = false;
    cpu_high_duration_ = cpu_low_duration_ = {};
  }
  if (!memory_time_.is_null() && now - memory_time_ > base::Minutes(5)) {
    memory_time_ = {};
    memory_high_ = false;
    memory_high_count_ = memory_low_count_ = 0;
  }
  UpdateDismissal();
}

void DaoResourceUsageState::UpdateDismissal() {
  if (alerts() == kNone) {
    dismissed_ = false;
  }
}

}  // namespace dao
