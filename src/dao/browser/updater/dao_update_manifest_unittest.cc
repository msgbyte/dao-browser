// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/updater/dao_update_manifest.h"

#include <cstring>
#include <utility>

#include "base/base64.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/json/json_writer.h"
#include "base/values.h"
#include "crypto/keypair.h"
#include "crypto/sign.h"
#include "dao/browser/updater/dao_update_manifest_test_data.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace dao {
namespace {
const char kPayload[] =
    R"({"schema":1,"platform":"windows","arch":"x64","version":"1.2.3","chromiumVersion":"149.0.7827.201","url":"https://dao-release.msgbyte.com/dao-browser-1.2.3-windows-x64.exe","size":14,"sha256":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"})";

TEST(DaoUpdateManifestTest, CacheCleanupFileNames) {
  EXPECT_TRUE(
      IsWindowsUpdateCacheFileName("dao-browser-1.2.3-windows-x64.exe"));
  EXPECT_TRUE(IsWindowsUpdateCacheFileName(
      "6788c784-4f73-4a7b-9c25-54b929c4a519.download"));
  for (const auto* name :
       {"../dao-browser-1.2.3-windows-x64.exe", "User Data",
        "dao-browser-1.2.3-mac-arm64.dmg", "dao-browser-01.2.3-windows-x64.exe",
        "personal.download", "ready.json"}) {
    EXPECT_FALSE(IsWindowsUpdateCacheFileName(name)) << name;
  }
}

TEST(DaoUpdateManifestTest, SharedSignedFixtureAndArtifactIntegrity) {
  auto parsed = ParseSignedWindowsUpdateWithKeyForTesting(
      kTestUpdateEnvelope, base::Version("1.2.2"),
      base::Version("149.0.7827.201"), kTestUpdateKey);
  ASSERT_TRUE(parsed.has_value());
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  auto file = directory.GetPath().AppendASCII("installer.exe");
  ASSERT_TRUE(base::WriteFile(file, "test installer"));
  EXPECT_TRUE(VerifyWindowsUpdateFile(file, *parsed));
  ASSERT_TRUE(base::WriteFile(file, "evil installer"));
  EXPECT_FALSE(VerifyWindowsUpdateFile(file, *parsed));
  ASSERT_TRUE(base::WriteFile(file, "truncated"));
  EXPECT_FALSE(VerifyWindowsUpdateFile(file, *parsed));
  ASSERT_TRUE(base::DeleteFile(file));
  EXPECT_FALSE(VerifyWindowsUpdateFile(file, *parsed));
  EXPECT_FALSE(ParseSignedWindowsUpdate(kTestUpdateEnvelope,
                                        base::Version("1.2.2"),
                                        base::Version("149.0.7827.201"))
                   .has_value());
}

TEST(DaoUpdateManifestTest, SignedMetadataAndTampering) {
  auto key = crypto::keypair::PrivateKey::GenerateEd25519();
  auto public_key = crypto::keypair::PublicKey::FromPrivateKey(key);
  // Sign the JSON bytes without the C string terminator.
  auto sign_text = [&](std::string_view payload) {
    base::DictValue envelope;
    envelope.Set("payload", std::string(payload));
    envelope.Set("signature",
                 base::Base64Encode(crypto::sign::Sign(
                     crypto::sign::ED25519, key, base::as_byte_span(payload))));
    return *base::WriteJson(envelope);
  };
  const std::string encoded_key =
      base::Base64Encode(public_key.ToEd25519PublicKey());
  auto parse = [&](std::string_view text, std::string current = "1.2.2") {
    return ParseSignedWindowsUpdateWithKeyForTesting(
        text, base::Version(current), base::Version("149.0.7827.201"),
        encoded_key);
  };
  const std::string envelope = sign_text(kPayload);
  ASSERT_TRUE(parse(envelope).has_value());
  EXPECT_EQ(14, parse(envelope)->size);
  EXPECT_EQ(WindowsUpdateManifestError::kNoUpdate,
            parse(envelope, "1.2.3").error());
  EXPECT_EQ(WindowsUpdateManifestError::kNoUpdate,
            parse(envelope, "1.2.4").error());
  std::string tampered = envelope;
  tampered.replace(tampered.find("1.2.3"), 5, "9.2.3");
  EXPECT_FALSE(parse(tampered).has_value());
  for (auto [old_value, new_value] :
       {std::pair{"\"size\":14", "\"size\":0"},
        {"\"x64\"", "\"arm64\""},
        {"\"windows\"", "\"mac\""},
        {"dao-release.msgbyte.com", "evil.example"},
        {"149.0.7827.201", "148.0.7827.201"}}) {
    std::string payload = kPayload;
    payload.replace(payload.find(old_value), strlen(old_value), new_value);
    EXPECT_FALSE(parse(sign_text(payload)).has_value()) << payload;
  }
  EXPECT_FALSE(parse(std::string(65537, ' ')).has_value());
}
}  // namespace
}  // namespace dao
