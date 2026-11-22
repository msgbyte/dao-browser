// Copyright 2026 Dao Browser Authors. All rights reserved.
#ifndef DAO_BROWSER_PERFORMANCE_DAO_RESOURCE_USAGE_STATE_H_
#define DAO_BROWSER_PERFORMANCE_DAO_RESOURCE_USAGE_STATE_H_

#include "base/byte_size.h"
#include "base/time/time.h"

namespace dao {

// Sampling state only; never owns a timer or requests a measurement.
class DaoResourceUsageState {
 public:
  DaoResourceUsageState();
  enum Alert { kNone = 0, kCpu = 1, kMemory = 2 };
  void UpdateCpu(base::TimeTicks time, base::TimeDelta cumulative);
  void UpdateMemory(base::TimeTicks time, base::ByteSize bytes,
                    base::ByteSize threshold);
  void Expire(base::TimeTicks now);
  int alerts() const { return (cpu_high_ ? kCpu : 0) | (memory_high_ ? kMemory : 0); }
  bool dismissed() const { return dismissed_; }
  void Dismiss() { dismissed_ = true; }

 private:
  void UpdateDismissal();
  base::TimeTicks cpu_time_;
  base::TimeDelta cpu_total_;
  base::TimeDelta cpu_high_duration_;
  base::TimeDelta cpu_low_duration_;
  base::TimeTicks memory_time_;
  int memory_high_count_ = 0;
  int memory_low_count_ = 0;
  bool cpu_high_ = false;
  bool memory_high_ = false;
  bool dismissed_ = false;
};

}  // namespace dao
#endif  // DAO_BROWSER_PERFORMANCE_DAO_RESOURCE_USAGE_STATE_H_
