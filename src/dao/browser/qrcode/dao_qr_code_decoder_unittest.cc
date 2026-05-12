// Copyright 2026 Dao Browser. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/qrcode/dao_qr_code_decoder.h"

#include <string>
#include <string_view>

#include "base/containers/span.h"
#include "base/test/task_environment.h"
#include "base/test/test_future.h"
#include "components/qr_code_generator/bitmap_generator.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "url/gurl.h"

namespace dao {
namespace {

SkBitmap GenerateTestQrBitmap(std::string_view payload) {
  auto bytes = base::as_byte_span(payload);
  auto generated = qr_code_generator::GenerateBitmap(
      bytes,
      qr_code_generator::ModuleStyle::kSquares,
      qr_code_generator::LocatorStyle::kSquare,
      qr_code_generator::CenterImage::kNoCenterImage,
      qr_code_generator::QuietZone::kIncluded);
  EXPECT_TRUE(generated.has_value());
  return generated.value();
}

class DaoQrCodeDecoderTest : public ::testing::Test {
 protected:
  base::test::TaskEnvironment task_environment_;
};

}  // namespace

TEST_F(DaoQrCodeDecoderTest, DecodesUrlPayload) {
  SkBitmap bm = GenerateTestQrBitmap("https://dao.example/test");
  DecodedQrCodes results = DaoQrCodeDecoder::DecodeBitmapBlocking(bm);
  ASSERT_EQ(1u, results.size());
  EXPECT_EQ("https://dao.example/test", results[0].text);
  EXPECT_TRUE(results[0].is_url);
  EXPECT_EQ(GURL("https://dao.example/test"), results[0].url);
}

TEST_F(DaoQrCodeDecoderTest, DecodesPlainTextPayload) {
  SkBitmap bm = GenerateTestQrBitmap("hello world");
  DecodedQrCodes results = DaoQrCodeDecoder::DecodeBitmapBlocking(bm);
  ASSERT_EQ(1u, results.size());
  EXPECT_EQ("hello world", results[0].text);
  EXPECT_FALSE(results[0].is_url);
}

TEST_F(DaoQrCodeDecoderTest, EmptyBitmapReturnsEmpty) {
  SkBitmap empty;
  EXPECT_TRUE(DaoQrCodeDecoder::DecodeBitmapBlocking(empty).empty());
}

TEST_F(DaoQrCodeDecoderTest, JavascriptUrlIsNotMarkedAsUrl) {
  SkBitmap bm = GenerateTestQrBitmap("javascript:alert(1)");
  DecodedQrCodes results = DaoQrCodeDecoder::DecodeBitmapBlocking(bm);
  ASSERT_EQ(1u, results.size());
  EXPECT_EQ("javascript:alert(1)", results[0].text);
  EXPECT_FALSE(results[0].is_url) << "javascript: must never be is_url=true";
}

TEST_F(DaoQrCodeDecoderTest, AsyncCallbackOnSequence) {
  SkBitmap bm = GenerateTestQrBitmap("async-test");
  base::test::TestFuture<DecodedQrCodes> future;
  DaoQrCodeDecoder::DecodeBitmapAsync(bm, future.GetCallback());
  DecodedQrCodes results = future.Take();
  ASSERT_EQ(1u, results.size());
  EXPECT_EQ("async-test", results[0].text);
}

}  // namespace dao
