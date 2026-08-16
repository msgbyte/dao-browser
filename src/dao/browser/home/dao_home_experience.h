// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_HOME_DAO_HOME_EXPERIENCE_H_
#define DAO_BROWSER_HOME_DAO_HOME_EXPERIENCE_H_

#include <string_view>

#include "base/types/expected.h"
#include "dao/browser/home/dao_home_types.h"

namespace dao {

base::expected<HomeExperience, HomeError> ParseHomeExperience(
    std::string_view json);

}  // namespace dao

#endif  // DAO_BROWSER_HOME_DAO_HOME_EXPERIENCE_H_
