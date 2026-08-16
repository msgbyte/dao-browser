// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_HOME_DAO_HOME_HISTORY_MATERIAL_H_
#define DAO_BROWSER_HOME_DAO_HOME_HISTORY_MATERIAL_H_

#include "base/time/time.h"
#include "base/values.h"
#include "dao/browser/home/dao_home_types.h"

namespace history {
class QueryResults;
}

namespace dao {

// Reduces history into bounded action hints for the explicit history bootstrap.
// History rows and paths never appear in the returned brief.
HomeBootstrapBrief BuildHomeBootstrapBrief(const history::QueryResults& results,
                                           base::Time now,
                                           std::string locale);

base::DictValue HomeBootstrapBriefToValue(const HomeBootstrapBrief& brief);

}  // namespace dao

#endif  // DAO_BROWSER_HOME_DAO_HOME_HISTORY_MATERIAL_H_
