// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_TELEMETRY_DAO_TELEMETRY_SERVICE_H_
#define DAO_BROWSER_TELEMETRY_DAO_TELEMETRY_SERVICE_H_

#include <memory>
#include <string>

#include "base/no_destructor.h"
#include "base/values.h"

class Profile;

namespace network {
class SimpleURLLoader;
}

namespace dao {

// Process-wide telemetry facade. Reports anonymous usage events to a Tianji
// application endpoint over HTTPS.
//
// Threading: all methods must be called on the UI thread.
//
// Lifetime: a single instance lives for the duration of the process. Pending
// SimpleURLLoaders are kept alive in an in-flight map and removed on
// completion (results are discarded — telemetry must never affect user-facing
// behavior).
class DaoTelemetryService {
 public:
  static DaoTelemetryService* GetInstance();

  DaoTelemetryService(const DaoTelemetryService&) = delete;
  DaoTelemetryService& operator=(const DaoTelemetryService&) = delete;

  // Records the URLLoaderFactory source. Safe to call multiple times; the most
  // recent profile wins. Without this, ReportEvent silently drops the event.
  void SetProfile(Profile* profile);

  // Fires the canonical "browser opened" event once per process. Subsequent
  // calls are no-ops.
  void ReportBrowserOpenedOnce();

  // Fires an arbitrary application event. `event_data` is forwarded as the
  // payload.data dict. No-op if SetProfile has not been called.
  void ReportEvent(const std::string& event_name, base::DictValue event_data);

 private:
  friend class base::NoDestructor<DaoTelemetryService>;

  DaoTelemetryService();
  ~DaoTelemetryService();

  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace dao

#endif  // DAO_BROWSER_TELEMETRY_DAO_TELEMETRY_SERVICE_H_
