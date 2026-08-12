// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_IMPORT_DAO_SOURCE_DETECTOR_H_
#define DAO_BROWSER_IMPORT_DAO_SOURCE_DETECTOR_H_

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/functional/callback_forward.h"
#include "base/memory/weak_ptr.h"
#include "dao/browser/import/dao_migration_types.h"

namespace dao::import {

class DaoSourceDetector {
 public:
  struct BrowserRoot {
    SourceKind kind;
    std::string browser_name;
    base::FilePath path;
  };

  struct DetectionResult {
    DetectionResult();
    DetectionResult(const DetectionResult&);
    DetectionResult& operator=(const DetectionResult&);
    DetectionResult(DetectionResult&&);
    DetectionResult& operator=(DetectionResult&&);
    ~DetectionResult();

    std::vector<SourceProfile> profiles;
    std::map<std::string, base::FilePath> profile_paths;
  };

  using DetectCallback =
      base::OnceCallback<void(std::vector<SourceProfile> profiles)>;

  DaoSourceDetector();
  DaoSourceDetector(const DaoSourceDetector&) = delete;
  DaoSourceDetector& operator=(const DaoSourceDetector&) = delete;
  ~DaoSourceDetector();

  void Detect(DetectCallback callback);
  std::optional<base::FilePath> ResolveProfilePath(
      const std::string& profile_id) const;

  static DetectionResult DetectFromRootsForTesting(
      std::vector<BrowserRoot> roots);
  static std::string BuildProfileIdForTesting(
      SourceKind kind,
      const base::FilePath& profile_path);

 private:
  static std::vector<BrowserRoot> GetDefaultBrowserRoots();
  static DetectionResult DetectFromRoots(std::vector<BrowserRoot> roots);
  void OnDetectionComplete(uint64_t generation,
                           DetectCallback callback,
                           DetectionResult result);

  std::map<std::string, base::FilePath> profile_paths_;
  uint64_t detection_generation_ = 0;
  base::WeakPtrFactory<DaoSourceDetector> weak_ptr_factory_{this};
};

}  // namespace dao::import

#endif  // DAO_BROWSER_IMPORT_DAO_SOURCE_DETECTOR_H_
