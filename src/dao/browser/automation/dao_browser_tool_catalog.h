// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_AUTOMATION_DAO_BROWSER_TOOL_CATALOG_H_
#define DAO_BROWSER_AUTOMATION_DAO_BROWSER_TOOL_CATALOG_H_

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/functional/function_ref.h"
#include "base/synchronization/lock.h"
#include "base/time/time.h"
#include "base/types/expected.h"
#include "base/values.h"
#include "dao/browser/automation/dao_browser_tool_types.h"

namespace dao {

enum class DaoBrowserToolGroup {
  kPage,
  kTabs,
  kDevTools,
};

enum class DaoBrowserToolSideEffect {
  kRead,
  kInteraction,
  kDestructive,
};

struct DaoBrowserToolDefinition {
  DaoBrowserToolDefinition();
  ~DaoBrowserToolDefinition();
  DaoBrowserToolDefinition(const DaoBrowserToolDefinition&) = delete;
  DaoBrowserToolDefinition& operator=(const DaoBrowserToolDefinition&) = delete;
  DaoBrowserToolDefinition(DaoBrowserToolDefinition&&) noexcept;
  DaoBrowserToolDefinition& operator=(DaoBrowserToolDefinition&&) noexcept;

  std::string name;
  std::string description;
  base::DictValue input_schema;
  std::optional<base::DictValue> output_schema;
  DaoBrowserToolGroup group = DaoBrowserToolGroup::kPage;
  std::vector<DaoToolClient> clients;
  DaoBrowserToolSideEffect side_effect = DaoBrowserToolSideEffect::kRead;
  base::TimeDelta timeout;
};

class DaoBrowserToolCatalog {
 public:
  using LoadResult =
      base::expected<std::unique_ptr<DaoBrowserToolCatalog>, DaoToolError>;

  class Cache {
   public:
    Cache();
    ~Cache();

    Cache(const Cache&) = delete;
    Cache& operator=(const Cache&) = delete;

    base::expected<const DaoBrowserToolCatalog*, DaoToolError> GetOrLoad(
        base::FunctionRef<LoadResult()> loader);

   private:
    base::Lock lock_;
    std::unique_ptr<DaoBrowserToolCatalog> catalog_;
  };

  static const DaoBrowserToolCatalog* Get();
  static base::expected<const DaoBrowserToolCatalog*, DaoToolError> TryGet();
  static LoadResult CreateFromJson(std::string_view json);
  static LoadResult LoadFromResourceBundle();

  DaoBrowserToolCatalog(const DaoBrowserToolCatalog&) = delete;
  DaoBrowserToolCatalog& operator=(const DaoBrowserToolCatalog&) = delete;
  ~DaoBrowserToolCatalog();

  const DaoBrowserToolDefinition* Find(std::string_view name,
                                       DaoToolClient client) const;
  std::vector<const DaoBrowserToolDefinition*> List(DaoToolClient client) const;

 private:
  explicit DaoBrowserToolCatalog(std::vector<DaoBrowserToolDefinition> tools);

  std::vector<DaoBrowserToolDefinition> tools_;
};

}  // namespace dao

#endif  // DAO_BROWSER_AUTOMATION_DAO_BROWSER_TOOL_CATALOG_H_
