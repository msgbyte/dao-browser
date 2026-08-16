// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/home/dao_home_sample_shape.h"

#include <string>

#include "base/json/json_writer.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace dao {
namespace {

std::string SerializeShape(const base::Value& sample) {
  std::string serialized;
  EXPECT_TRUE(base::JSONWriter::Write(
      base::Value(BuildHomeConnectorSampleShape(sample)), &serialized));
  return serialized;
}

TEST(DaoHomeSampleShapeTest, RemovesEveryScalarAndDynamicObjectKey) {
  base::DictValue private_object;
  private_object.Set("secret_dynamic_key", "private-token-7fc298e1");
  private_object.Set("account_balance", 917263);
  private_object.Set("authenticated", true);
  private_object.Set("origin", "https://private.example/account");

  const base::DictValue shape =
      BuildHomeConnectorSampleShape(base::Value(std::move(private_object)));
  EXPECT_EQ("object", *shape.FindString("kind"));
  EXPECT_EQ("non_empty", *shape.FindString("state"));
  const base::ListValue* value_kinds = shape.FindList("value_kinds");
  ASSERT_TRUE(value_kinds);
  EXPECT_EQ(
      base::ListValue().Append("boolean").Append("number").Append("string"),
      *value_kinds);

  std::string serialized;
  ASSERT_TRUE(base::JSONWriter::Write(base::Value(shape.Clone()), &serialized));
  for (const char* forbidden :
       {"secret_dynamic_key", "private-token", "account_balance", "917263",
        "authenticated", "private.example"}) {
    EXPECT_EQ(std::string::npos, serialized.find(forbidden)) << serialized;
  }
}

TEST(DaoHomeSampleShapeTest, PreservesOnlyBoundedCollectionStateAndKinds) {
  base::ListValue one_item;
  one_item.Append(base::DictValue().Set("first-secret", "alpha"));
  const base::DictValue one_shape =
      BuildHomeConnectorSampleShape(base::Value(std::move(one_item)));
  EXPECT_EQ("array", *one_shape.FindString("kind"));
  EXPECT_EQ("non_empty", *one_shape.FindString("state"));
  const base::ListValue* item_kinds = one_shape.FindList("item_kinds");
  ASSERT_TRUE(item_kinds);
  EXPECT_EQ(base::ListValue().Append("object"), *item_kinds);

  base::ListValue many_items;
  for (int index = 0; index < 1000; ++index) {
    many_items.Append(
        base::DictValue().Set("different-secret-key-" + std::to_string(index),
                              std::string(1024, 'x')));
  }
  const base::DictValue many_shape =
      BuildHomeConnectorSampleShape(base::Value(std::move(many_items)));
  EXPECT_EQ(one_shape, many_shape);

  const std::string serialized =
      SerializeShape(base::Value(std::string(1024 * 1024, 's')));
  EXPECT_LT(serialized.size(), 128u);
  EXPECT_EQ(std::string::npos, serialized.find(std::string(64, 's')));
}

TEST(DaoHomeSampleShapeTest, DistinguishesEmptyStateWithoutRevealingValues) {
  const base::DictValue empty_string =
      BuildHomeConnectorSampleShape(base::Value(""));
  const base::DictValue non_empty_string =
      BuildHomeConnectorSampleShape(base::Value("not-agent-visible"));
  EXPECT_EQ("empty", *empty_string.FindString("state"));
  EXPECT_EQ("non_empty", *non_empty_string.FindString("state"));

  const base::DictValue true_shape =
      BuildHomeConnectorSampleShape(base::Value(true));
  const base::DictValue false_shape =
      BuildHomeConnectorSampleShape(base::Value(false));
  EXPECT_EQ(true_shape, false_shape);

  const base::DictValue integer_shape =
      BuildHomeConnectorSampleShape(base::Value(917263));
  const base::DictValue double_shape =
      BuildHomeConnectorSampleShape(base::Value(3.1415926));
  EXPECT_EQ(integer_shape, double_shape);
}

}  // namespace
}  // namespace dao
