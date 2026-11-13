// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/local_ipc/dao_local_ipc_protocol.h"

#include <string>

#include "base/json/json_reader.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace dao {
namespace {

std::string SizedRequest(size_t bytes) {
  const std::string prefix =
      R"({"version":1,"id":"stage","method":"draft.stage","params":{"data":")";
  const std::string suffix = R"("}})";
  return prefix + std::string(bytes - prefix.size() - suffix.size(), 'a') +
         suffix;
}

TEST(DaoLocalIpcProtocolTest, HomeAcceptsSnapshotBeyondDefaultFrameLimit) {
  const std::string request = SizedRequest(8 * 1024 * 1024 + 1);
  EXPECT_FALSE(ParseDaoLocalIpcRequestLine(request).has_value());
  auto home = ParseDaoLocalIpcRequestLine(request, DaoLocalIpcConfig::Home());
  ASSERT_TRUE(home.has_value());
  EXPECT_EQ("stage", home->id);
  EXPECT_EQ("draft.stage", home->method);
}

TEST(DaoLocalIpcProtocolTest, HomeEnforcesThirtyTwoMiBFrameBoundary) {
  std::string request = SizedRequest(32 * 1024 * 1024);
  EXPECT_TRUE(ParseDaoLocalIpcRequestLine(request, DaoLocalIpcConfig::Home())
                  .has_value());
  request.push_back(' ');
  EXPECT_FALSE(ParseDaoLocalIpcRequestLine(request, DaoLocalIpcConfig::Home())
                   .has_value());
}

TEST(DaoLocalIpcProtocolTest, RejectsMalformedEnvelopesBeforeDispatch) {
  for (const std::string request :
       {"", "{", "[]",
        R"({"version":1,"id":3,"method":"session.open","params":{}})",
        R"({"version":1,"id":"","method":"session.open","params":{}})",
        R"({"version":1,"id":"1","method":[],"params":{}})",
        R"({"version":1,"id":"1","method":"session.open","params":[]})",
        "{\"version\":1,\"method\":\"session.open\",\"params\":{}}\n"}) {
    EXPECT_FALSE(ParseDaoLocalIpcRequestLine(request, DaoLocalIpcConfig::Home())
                     .has_value())
        << request;
  }
}

TEST(DaoLocalIpcProtocolTest, UnsupportedVersionPreservesCorrelationId) {
  auto request = ParseDaoLocalIpcRequestLine(
      R"({"version":2,"id":"request-1","method":"session.open","params":{}})",
      DaoLocalIpcConfig::Home());
  ASSERT_FALSE(request.has_value());
  EXPECT_EQ("request-1", request.error().id);
  EXPECT_EQ("IPC_VERSION_UNSUPPORTED", request.error().error.code);
}

TEST(DaoLocalIpcProtocolTest, SuccessKeepsNativeResponseEnvelope) {
  const std::string response = SerializeDaoLocalIpcSuccessResponse(
      "request-1", base::DictValue().Set("ok", true),
      DaoLocalIpcConfig::Home());
  ASSERT_EQ('\n', response.back());
  auto parsed = base::JSONReader::ReadDict(response, base::JSON_PARSE_RFC);
  ASSERT_TRUE(parsed);
  EXPECT_EQ(1, parsed->FindInt("version"));
  EXPECT_EQ("request-1", *parsed->FindString("id"));
  EXPECT_EQ(true, parsed->FindBoolByDottedPath("result.ok"));
}

}  // namespace
}  // namespace dao
