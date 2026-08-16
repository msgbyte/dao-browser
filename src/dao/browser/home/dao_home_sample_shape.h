// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_HOME_DAO_HOME_SAMPLE_SHAPE_H_
#define DAO_BROWSER_HOME_DAO_HOME_SAMPLE_SHAPE_H_

#include "base/values.h"

namespace dao {

// Returns bounded, non-content metadata derived from a connector result.
// Dynamic object keys, scalar values, collection lengths, and nested content
// never cross the native-to-Agent boundary.
base::DictValue BuildHomeConnectorSampleShape(const base::Value& sample);

}  // namespace dao

#endif  // DAO_BROWSER_HOME_DAO_HOME_SAMPLE_SHAPE_H_
