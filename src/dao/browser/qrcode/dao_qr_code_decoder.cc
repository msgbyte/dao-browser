// Copyright 2026 Dao Browser. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/qrcode/dao_qr_code_decoder.h"

#include <algorithm>
#include <exception>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/task/thread_pool.h"
#include "skia/ext/image_operations.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkColor.h"
#include "url/gurl.h"

// ZXing-cpp 2.2.1 public headers.
#include "BarcodeFormat.h"
#include "ImageView.h"
#include "ReadBarcode.h"
#include "ReaderOptions.h"
#include "Result.h"

namespace dao {

DecodedQrCode::DecodedQrCode() = default;
DecodedQrCode::DecodedQrCode(const DecodedQrCode&) = default;
DecodedQrCode& DecodedQrCode::operator=(const DecodedQrCode&) = default;
DecodedQrCode::DecodedQrCode(DecodedQrCode&&) noexcept = default;
DecodedQrCode& DecodedQrCode::operator=(DecodedQrCode&&) noexcept = default;
DecodedQrCode::~DecodedQrCode() = default;

namespace {

// Convert a SkBitmap (N32: BGRA on macOS/Windows, RGBA on Linux) to an 8-bit
// luminance buffer in row-major order. Uses SkBitmap::getColor so we don't
// have to branch on the underlying pixel order.
std::vector<uint8_t> SkBitmapToLuminance(const SkBitmap& bm) {
  std::vector<uint8_t> luma;
  if (bm.drawsNothing() || bm.colorType() != kN32_SkColorType) {
    return luma;
  }
  const int w = bm.width();
  const int h = bm.height();
  luma.resize(static_cast<size_t>(w) * static_cast<size_t>(h));
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      SkColor c = bm.getColor(x, y);
      uint8_t l = static_cast<uint8_t>(0.299 * SkColorGetR(c) +
                                       0.587 * SkColorGetG(c) +
                                       0.114 * SkColorGetB(c));
      luma[static_cast<size_t>(y) * w + x] = l;
    }
  }
  return luma;
}

bool IsHttpUrl(const GURL& g) {
  return g.is_valid() && (g.SchemeIs("http") || g.SchemeIs("https"));
}

}  // namespace

// static
DecodedQrCodes DaoQrCodeDecoder::DecodeBitmapBlocking(const SkBitmap& bitmap) {
  if (bitmap.drawsNothing()) {
    return {};
  }

  // Cap input dimensions per spec §5: limits decode CPU cost.
  constexpr int kMaxDim = 4096;
  SkBitmap effective = bitmap;
  if (bitmap.width() > kMaxDim || bitmap.height() > kMaxDim) {
    float ratio =
        std::min(static_cast<float>(kMaxDim) / bitmap.width(),
                 static_cast<float>(kMaxDim) / bitmap.height());
    effective = skia::ImageOperations::Resize(
        bitmap, skia::ImageOperations::RESIZE_BEST,
        static_cast<int>(bitmap.width() * ratio),
        static_cast<int>(bitmap.height() * ratio));
  }

  std::vector<uint8_t> luma = SkBitmapToLuminance(effective);
  if (luma.empty()) {
    return {};
  }

  DecodedQrCodes out;
  try {
    ZXing::ImageView image_view(luma.data(), effective.width(),
                                effective.height(), ZXing::ImageFormat::Lum);

    ZXing::ReaderOptions opts;
    opts.setFormats(ZXing::BarcodeFormat::QRCode |
                    ZXing::BarcodeFormat::MicroQRCode |
                    ZXing::BarcodeFormat::DataMatrix |
                    ZXing::BarcodeFormat::Aztec);
    opts.setTryRotate(true);
    opts.setTryHarder(true);

    ZXing::Results results = ZXing::ReadBarcodes(image_view, opts);
    out.reserve(results.size());
    for (const auto& r : results) {
      if (!r.isValid()) {
        continue;
      }
      DecodedQrCode entry;
      entry.text = r.text();
      entry.format = ZXing::ToString(r.format());
      GURL maybe_url(entry.text);
      if (IsHttpUrl(maybe_url)) {
        entry.is_url = true;
        entry.url = maybe_url;
      }
      out.push_back(std::move(entry));
    }
  } catch (const std::exception& e) {
    LOG(WARNING) << "ZXing decode threw: " << e.what();
    return {};
  } catch (...) {
    LOG(WARNING) << "ZXing decode threw an unknown exception";
    return {};
  }
  return out;
}

// static
void DaoQrCodeDecoder::DecodeBitmapAsync(SkBitmap bitmap, Callback callback) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&DaoQrCodeDecoder::DecodeBitmapBlocking,
                     std::move(bitmap)),
      std::move(callback));
}

}  // namespace dao
