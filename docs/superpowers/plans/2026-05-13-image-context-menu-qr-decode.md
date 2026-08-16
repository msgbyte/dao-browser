# Image Context-Menu QR Decode Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a "识别二维码" item to the image right-click menu that decodes the bitmap with ZXing-cpp and shows results (URL→Open / text→Copy) in a Dao-styled dialog, with a toast fallback when nothing is found.

**Architecture:** Vendor ZXing-cpp under `engine/src/third_party/zxing-cpp/` with a Dao-authored BUILD.gn. Decoder runs on a `base::ThreadPool` background sequence in the browser process. Bitmap is fetched via the existing Chromium `chrome::mojom::ChromeRenderFrame::RequestBitmapForContextNode()` mojo path (same one Save Image / Copy Image use). Results render through a `views::DialogDelegateView` styled like `dao_control_center_qr_view`.

**Tech Stack:** C++ (Chromium 147.0.7727.135), ZXing-cpp v2.x, Skia, Chromium Views, mojo, `base::ThreadPool`, `gtest` / `browser_tests`.

---

## Source-of-Truth Reminder

**Never edit `engine/` directly as a deliverable.** All changes go through:
- `src/dao/browser/qrcode/*` — new Dao-owned C++ code (copied into `engine/src/dao/browser/qrcode/` by `npm run import`).
- `src/patches/**/*.patch` — modifications to Chromium files (applied by `npm run import`).
- `src/patches/third_party/zxing-cpp/BUILD.gn.patch` plus the **vendored ZXing-cpp source tree** committed under `src/dao/third_party/zxing-cpp/` (Dao's `import` step copies it into `engine/src/third_party/zxing-cpp/`).

The iterative loop is: edit `src/dao/` or `src/patches/` → `npm run rebuild` (which does import + build:debug) → test → `npm run export` to capture any engine-side experimentation back into patches.

**Build rule:** NEVER run `autoninja`/`ninja`/`siso` directly. Only `npm run rebuild` or `npm run build:debug`.

---

## File Structure

### New files (Dao-owned, under `src/dao/`)

| Path | Responsibility |
|------|----------------|
| `src/dao/browser/qrcode/dao_qr_code_types.h` | `DecodedQrCode` struct + `DecodedQrCodes` typedef |
| `src/dao/browser/qrcode/dao_qr_code_decoder.h` | Public API: `DecodeBitmapAsync(SkBitmap, callback)` |
| `src/dao/browser/qrcode/dao_qr_code_decoder.cc` | ThreadPool dispatch + ZXing shim (catches exceptions) |
| `src/dao/browser/qrcode/dao_qr_code_decoder_unittest.cc` | Unit tests for the decoder against synthetic bitmaps |
| `src/dao/browser/ui/views/dao_qr_code_result_dialog_view.h` | `DialogDelegateView` declaration |
| `src/dao/browser/ui/views/dao_qr_code_result_dialog_view.cc` | Dialog implementation |
| `src/dao/browser/ui/views/dao_qr_code_browsertest.cc` | Browser tests for menu + decode + dialog |

### New files (vendored, under `src/dao/third_party/zxing-cpp/`)

The full ZXing-cpp `core/` directory at the chosen tag. Plus a Dao-authored Chromium-style `BUILD.gn` at the package root.

| Path | Responsibility |
|------|----------------|
| `src/dao/third_party/zxing-cpp/core/...` | ZXing-cpp upstream source tree (verbatim from release tarball) |
| `src/dao/third_party/zxing-cpp/BUILD.gn` | `static_library("zxing")` target with all .cpp sources |
| `src/dao/third_party/zxing-cpp/README.chromium` | Provenance metadata (URL, version, license) |
| `src/dao/third_party/zxing-cpp/LICENSE` | ZXing-cpp Apache-2.0 license file |
| `src/dao/third_party/zxing-cpp/OWNERS` | `file://src/dao/OWNERS` |

The Dao import script copies `src/dao/third_party/zxing-cpp/` into `engine/src/third_party/zxing-cpp/` alongside the `BUILD.gn`. We do **not** pull ZXing-cpp via DEPS — it's vendored and version-pinned through the tarball SHA in `README.chromium`.

### Patched Chromium files

| Path | Change |
|------|--------|
| `chrome/app/chrome_command_ids.h` | Add `#define IDC_DAO_DECODE_QR_CODE 34075` after the existing Dao block |
| `chrome/browser/renderer_context_menu/render_view_context_menu.h` | Forward-declare + private method `ExecuteDecodeQrCode()`, callbacks |
| `chrome/browser/renderer_context_menu/render_view_context_menu.cc` | Append menu item, route command, fetch bitmap, async decode, show dialog/toast |
| `chrome/browser/renderer_context_menu/BUILD.gn` | Add `//dao/browser/qrcode` and `//chrome/browser/ui` decoder/dialog deps |
| `chrome/browser/ui/BUILD.gn` | Append the new `dao/browser/qrcode/` and `dao/browser/ui/views/dao_qr_code_result_dialog_view.{h,cc}` sources to the existing `dao_browser` block in `static_library("ui")` |

---

## Task 0: Pre-flight — confirm clean baseline

**Files:**
- Read only: `src/patches/`, `src/dao/`, `engine/src/`

- [ ] **Step 1: Verify clean tree**

Run: `git -C /Users/moonrailgun/Develop/dao-browser status`
Expected: clean (or only the new spec/plan files committed earlier).

- [ ] **Step 2: Verify baseline builds**

Run: `npm run rebuild`
Expected: build succeeds. (Skip if you've built within the last hour and nothing in `engine/src` changed.)

- [ ] **Step 3: Verify baseline tests pass**

Run: `./engine/src/out/dao-debug/browser_tests --gtest_filter='Dao*' --gtest_brief=1`
Expected: all existing `Dao*` tests pass. Capture the pass count for later regression comparison.

---

## Task 1: Vendor ZXing-cpp source

**Files:**
- Create: `src/dao/third_party/zxing-cpp/` (full source tree)
- Create: `src/dao/third_party/zxing-cpp/README.chromium`
- Create: `src/dao/third_party/zxing-cpp/LICENSE`
- Create: `src/dao/third_party/zxing-cpp/OWNERS`

**Goal:** Get the upstream source on disk so subsequent tasks can reference it. Use **zxing-cpp v2.2.1** (latest stable at the time this plan was written; the README.chromium will pin the exact tag).

- [ ] **Step 1: Download the upstream tarball**

Run:
```bash
cd /tmp
curl -fL -o zxing-cpp-2.2.1.tar.gz \
  https://github.com/zxing-cpp/zxing-cpp/archive/refs/tags/v2.2.1.tar.gz
shasum -a 256 zxing-cpp-2.2.1.tar.gz
```

Record the printed SHA-256 — it goes into `README.chromium` in Step 4.

- [ ] **Step 2: Extract only `core/` and `LICENSE` into the vendor location**

Run:
```bash
cd /Users/moonrailgun/Develop/dao-browser
mkdir -p src/dao/third_party/zxing-cpp
cd src/dao/third_party/zxing-cpp
tar -xzf /tmp/zxing-cpp-2.2.1.tar.gz \
  --strip-components=1 \
  zxing-cpp-2.2.1/core \
  zxing-cpp-2.2.1/LICENSE
```

Verify: `ls core/src` should list directories like `aztec/`, `datamatrix/`, `qrcode/`, `BarcodeFormat.cpp`, `ReadBarcode.cpp`.

- [ ] **Step 3: Create `OWNERS`**

Write `src/dao/third_party/zxing-cpp/OWNERS`:
```
file://src/dao/OWNERS
```

(If `src/dao/OWNERS` doesn't exist, replace this with `*` for now — Chromium's OWNERS check is permissive in third_party for builds.)

- [ ] **Step 4: Create `README.chromium`**

Write `src/dao/third_party/zxing-cpp/README.chromium` (replace `<SHA>` with the value captured in Step 1):
```
Name: ZXing-cpp
Short Name: zxing-cpp
URL: https://github.com/zxing-cpp/zxing-cpp
Version: 2.2.1
Date: 2024-09-01
Revision: v2.2.1
SHA-256: <SHA>
License: Apache-2.0
License File: LICENSE
Security Critical: yes
Shipped: yes

Description:
ZXing-cpp is a C++ port of the ZXing barcode scanning library. Used by Dao
Browser to decode QR codes (and incidentally other supported symbologies)
from images selected via the right-click context menu. Only the core/
directory is vendored; tooling, examples, and language bindings are
omitted.

Local Modifications:
None.
```

- [ ] **Step 5: Commit**

```bash
cd /Users/moonrailgun/Develop/dao-browser
git add src/dao/third_party/zxing-cpp
git commit -m "feat(third_party): vendor zxing-cpp v2.2.1 for QR decoding"
```

---

## Task 2: Author the ZXing-cpp BUILD.gn

**Files:**
- Create: `src/dao/third_party/zxing-cpp/BUILD.gn`
- Create: `src/patches/dao_third_party_copy.txt` *(if your import script needs an explicit include list — check `scripts/cli.ts` import logic first; if it copies the whole `src/dao/third_party/` directory recursively this file is not needed)*

- [ ] **Step 1: Inspect upstream CMakeLists.txt to enumerate sources**

Run:
```bash
cat /Users/moonrailgun/Develop/dao-browser/src/dao/third_party/zxing-cpp/core/CMakeLists.txt | grep -E 'add_library|target_sources' | head -50
```

This produces the master list of `.cpp`/`.h` files in the library. Use it as the basis for the `sources = []` list.

- [ ] **Step 2: List all .cpp files in core/src**

Run:
```bash
cd /Users/moonrailgun/Develop/dao-browser/src/dao/third_party/zxing-cpp
find core/src -name '*.cpp' | sort
```

Capture the output — this is the source list for BUILD.gn.

- [ ] **Step 3: Write BUILD.gn**

Create `src/dao/third_party/zxing-cpp/BUILD.gn`:

```gn
# Copyright 2026 Dao Browser. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import("//build/config/c++/c++.gni")

config("zxing_external_config") {
  include_dirs = [ "core/src" ]
}

config("zxing_internal_config") {
  cflags_cc = [
    "-std=c++17",
    "-fexceptions",
    "-Wno-unused-but-set-variable",
    "-Wno-unused-variable",
    "-Wno-shadow",
    "-Wno-implicit-int-conversion",
    "-Wno-shorten-64-to-32",
    "-Wno-conversion",
    "-Wno-deprecated-declarations",
  ]
  defines = [ "ZX_USE_UTF8=1" ]
}

static_library("zxing") {
  sources = [
    # Paste the full sorted file list from Step 2 here, e.g.:
    # "core/src/BarcodeFormat.cpp",
    # "core/src/BinaryBitmap.cpp",
    # ... (~150 files)
  ]
  configs -= [ "//build/config/compiler:no_exceptions" ]
  configs += [ ":zxing_internal_config" ]
  public_configs = [ ":zxing_external_config" ]
  visibility = [
    "//chrome/browser/ui:*",
    "//chrome/browser/renderer_context_menu:*",
    "//dao/browser/qrcode:*",
  ]
}
```

The `configs -=` line is **critical** — Chromium globally enforces `-fno-exceptions`, but ZXing-cpp throws on invalid input. Removing `no_exceptions` and adding `-fexceptions` only on this target keeps the rest of the binary exception-free.

- [ ] **Step 4: Build verify**

Run: `npm run rebuild`
Expected: build proceeds past the third_party step. If you see "unknown source" errors, double-check that the file list from Step 2 is complete and paths are relative to `src/dao/third_party/zxing-cpp/`.

The first run will take a long time because ~150 ZXing files compile fresh. Subsequent rebuilds will be incremental.

- [ ] **Step 5: Commit**

```bash
git add src/dao/third_party/zxing-cpp/BUILD.gn
git commit -m "feat(third_party): add BUILD.gn for vendored zxing-cpp"
```

---

## Task 3: Define `DecodedQrCode` types

**Files:**
- Create: `src/dao/browser/qrcode/dao_qr_code_types.h`

- [ ] **Step 1: Write the header**

Create `src/dao/browser/qrcode/dao_qr_code_types.h`:

```cpp
// Copyright 2026 Dao Browser. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_QRCODE_DAO_QR_CODE_TYPES_H_
#define DAO_BROWSER_QRCODE_DAO_QR_CODE_TYPES_H_

#include <string>
#include <vector>

#include "url/gurl.h"

namespace dao {

// One decoded barcode. ZXing's vocabulary is broader than QR
// (QR_CODE / MICRO_QR_CODE / DATA_MATRIX / AZTEC), so we keep the format
// string verbatim from ZXing rather than enumerating it.
struct DecodedQrCode {
  std::string text;
  std::string format;
  bool is_url = false;
  GURL url;

  DecodedQrCode();
  DecodedQrCode(const DecodedQrCode&);
  DecodedQrCode& operator=(const DecodedQrCode&);
  DecodedQrCode(DecodedQrCode&&) noexcept;
  DecodedQrCode& operator=(DecodedQrCode&&) noexcept;
  ~DecodedQrCode();
};

using DecodedQrCodes = std::vector<DecodedQrCode>;

}  // namespace dao

#endif  // DAO_BROWSER_QRCODE_DAO_QR_CODE_TYPES_H_
```

The constructors / destructor are explicitly declared (not defaulted inline) so `GURL`'s incomplete-type forward-declaration doesn't break translation units that only need `DecodedQrCode` as a value type. Their definitions live in the next file (`dao_qr_code_decoder.cc`).

- [ ] **Step 2: Commit**

```bash
git add src/dao/browser/qrcode/dao_qr_code_types.h
git commit -m "feat(qrcode): add DecodedQrCode type"
```

---

## Task 4: Decoder header + skeleton implementation

**Files:**
- Create: `src/dao/browser/qrcode/dao_qr_code_decoder.h`
- Create: `src/dao/browser/qrcode/dao_qr_code_decoder.cc`

- [ ] **Step 1: Write the header**

Create `src/dao/browser/qrcode/dao_qr_code_decoder.h`:

```cpp
// Copyright 2026 Dao Browser. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_QRCODE_DAO_QR_CODE_DECODER_H_
#define DAO_BROWSER_QRCODE_DAO_QR_CODE_DECODER_H_

#include "base/functional/callback_forward.h"
#include "dao/browser/qrcode/dao_qr_code_types.h"

class SkBitmap;

namespace dao {

// Off-thread QR decoder. The blocking ZXing call runs on the ThreadPool;
// the callback is invoked on the calling sequence. Empty result means
// "no codes found" — failures (corrupt input, ZXing exception) are also
// surfaced as an empty vector so callers don't need to distinguish.
class DaoQrCodeDecoder {
 public:
  using Callback = base::OnceCallback<void(DecodedQrCodes)>;

  static void DecodeBitmapAsync(SkBitmap bitmap, Callback callback);

  // Public for unit tests. Blocks the calling thread.
  static DecodedQrCodes DecodeBitmapBlocking(const SkBitmap& bitmap);

 private:
  DaoQrCodeDecoder() = delete;
};

}  // namespace dao

#endif  // DAO_BROWSER_QRCODE_DAO_QR_CODE_DECODER_H_
```

- [ ] **Step 2: Write the implementation skeleton (no ZXing yet)**

Create `src/dao/browser/qrcode/dao_qr_code_decoder.cc`:

```cpp
// Copyright 2026 Dao Browser. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/qrcode/dao_qr_code_decoder.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/task/thread_pool.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "url/gurl.h"

namespace dao {

DecodedQrCode::DecodedQrCode() = default;
DecodedQrCode::DecodedQrCode(const DecodedQrCode&) = default;
DecodedQrCode& DecodedQrCode::operator=(const DecodedQrCode&) = default;
DecodedQrCode::DecodedQrCode(DecodedQrCode&&) noexcept = default;
DecodedQrCode& DecodedQrCode::operator=(DecodedQrCode&&) noexcept = default;
DecodedQrCode::~DecodedQrCode() = default;

// static
DecodedQrCodes DaoQrCodeDecoder::DecodeBitmapBlocking(const SkBitmap& bitmap) {
  // Filled in by Task 5.
  (void)bitmap;
  return {};
}

// static
void DaoQrCodeDecoder::DecodeBitmapAsync(SkBitmap bitmap, Callback callback) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&DaoQrCodeDecoder::DecodeBitmapBlocking,
                     std::move(bitmap)),
      std::move(callback));
}

}  // namespace dao
```

- [ ] **Step 3: Wire into `chrome/browser/ui/BUILD.gn` patch**

Open `src/patches/chrome/browser/ui/BUILD.gn.patch`. Inside the existing Dao block (the `+` lines starting at the `static_library("ui")` sources entry), add the four new source/header lines in alphabetical order with the surrounding entries:

```
+    "//dao/browser/qrcode/dao_qr_code_decoder.cc",
+    "//dao/browser/qrcode/dao_qr_code_decoder.h",
+    "//dao/browser/qrcode/dao_qr_code_types.h",
```

Also add `//dao/browser/ui/views/dao_qr_code_result_dialog_view.{cc,h}` in alphabetical order in the same block (you'll create these files in Task 7, but adding them to BUILD.gn now means we patch the file once instead of twice):

```
+    "//dao/browser/ui/views/dao_qr_code_result_dialog_view.cc",
+    "//dao/browser/ui/views/dao_qr_code_result_dialog_view.h",
```

Add `//third_party/zxing-cpp:zxing` to the `deps = [ ... ]` of the same `static_library("ui")` block. If you can't see a Dao-block edit in `deps` already, add the line with a simple anchor — e.g., right after `"//chrome/browser/ui/views",` if that's present.

- [ ] **Step 4: Build verify**

Run: `npm run rebuild`
Expected: builds clean. If linker errors mention `DaoQrCodeDecoder::DecodeBitmapBlocking`, the BUILD.gn patch in Step 3 did not pick up `dao_qr_code_decoder.cc` — re-check the patch file alphabetical ordering and re-run.

- [ ] **Step 5: Commit**

```bash
git add src/dao/browser/qrcode src/patches/chrome/browser/ui/BUILD.gn.patch
git commit -m "feat(qrcode): scaffold DaoQrCodeDecoder (no decode yet)"
```

---

## Task 5: Implement ZXing-backed `DecodeBitmapBlocking` (TDD)

**Files:**
- Modify: `src/dao/browser/qrcode/dao_qr_code_decoder.cc`
- Create: `src/dao/browser/qrcode/dao_qr_code_decoder_unittest.cc`
- Modify: `src/patches/chrome/test/BUILD.gn.patch` (or wherever Dao unit tests are wired — see investigation in Step 1)

- [ ] **Step 1: Find where Dao unit tests get linked**

Run:
```bash
grep -rn "dao_browser_tests\|dao_unittests\|dao.*unittest\.cc" /Users/moonrailgun/Develop/dao-browser/src/patches | head -20
grep -rn "dao_browser_tests" /Users/moonrailgun/Develop/dao-browser/src/patches | head
```

If a `dao_unittests` target already exists, append to it. If only `browser_tests` integration exists, plan to write the decoder check as a small `IN_PROC_BROWSER_TEST_F` instead — but try `unit_tests` first because it's much faster to iterate.

If neither exists, create a new `source_set("dao_qrcode_unittests")` in the patch for `chrome/test/BUILD.gn` and add it to the `unit_tests` deps:

```
+    "//dao/browser/qrcode:dao_qrcode_unittests",
```

For this plan we'll assume you'll add a new source_set; adapt if a target already exists.

- [ ] **Step 2: Write the failing test**

Create `src/dao/browser/qrcode/dao_qr_code_decoder_unittest.cc`:

```cpp
// Copyright 2026 Dao Browser. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/qrcode/dao_qr_code_decoder.h"

#include "base/test/task_environment.h"
#include "base/test/test_future.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkColor.h"

// Generate a minimal QR code bitmap inline using the existing Chromium
// generator so we don't need a binary fixture.
#include "components/qr_code_generator/bitmap_generator.h"

namespace dao {

namespace {

SkBitmap GenerateTestQrBitmap(const std::string& payload) {
  auto generated = qr_code_generator::GenerateBitmap(
      base::as_bytes(base::make_span(payload)),
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
```

Notes:
- `qr_code_generator::GenerateBitmap` already exists in Chromium (it's what the address-bar QR sharing uses). Confirm the actual function signature — it may differ slightly; if so, adapt the call.

Confirm: `grep -n "GenerateBitmap" /Users/moonrailgun/Develop/dao-browser/engine/src/components/qr_code_generator/bitmap_generator.h`

- [ ] **Step 3: Add a BUILD.gn entry for the unit test**

If you went the source_set route in Step 1, add a sibling BUILD.gn under `src/dao/browser/qrcode/` is **not** needed — the convention so far has been to wire Dao tests into `chrome/browser/ui/BUILD.gn`. Append the unittest source to the same Dao block:

```
+    "//dao/browser/qrcode/dao_qr_code_decoder_unittest.cc",
```

into the existing `chrome_unit_tests_sources_list` (or whichever sources list `chrome/test/BUILD.gn` puts unit tests into; verify by running:

```bash
grep -n 'dao_.*unittest\|unit_tests' /Users/moonrailgun/Develop/dao-browser/src/patches/chrome/test/BUILD.gn.patch 2>/dev/null
```

If no `chrome/test/BUILD.gn.patch` exists yet, create one as a thin patch that adds the unittest source to the appropriate `unit_tests` source_set — model it on the existing `chrome/test/BUILD.gn.patch` if there is one, or on Chromium's pattern (`unit_tests` target's `sources +=`).

- [ ] **Step 4: Run test and verify it fails for the right reason**

Run: `npm run build:debug` (build only — `npm run rebuild` re-imports patches)

Then: `./engine/src/out/dao-debug/unit_tests --gtest_filter='DaoQrCodeDecoderTest.*' --gtest_brief=1`

Expected: tests fail because `DecodeBitmapBlocking` returns empty (we haven't implemented it). Confirm specifically `DecodesUrlPayload` and `DecodesPlainTextPayload` fail with `Expected: 1u, actual: 0`.

- [ ] **Step 5: Implement the real decoder**

Replace the `DecodeBitmapBlocking` body in `src/dao/browser/qrcode/dao_qr_code_decoder.cc`:

```cpp
#include "ReadBarcode.h"  // ZXing-cpp public header
#include "Result.h"
```

(at the top, with the other includes) and:

```cpp
namespace {

// Convert a SkBitmap (N32 BGRA/RGBA depending on platform) to an 8-bit
// luminance buffer in row-major order.
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
      uint8_t l = static_cast<uint8_t>(
          0.299 * SkColorGetR(c) +
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

  std::vector<uint8_t> luma = SkBitmapToLuminance(bitmap);
  if (luma.empty()) {
    return {};
  }

  DecodedQrCodes out;
  try {
    ZXing::ImageView image_view(
        luma.data(),
        bitmap.width(),
        bitmap.height(),
        ZXing::ImageFormat::Lum);

    ZXing::DecodeHints hints;
    hints.setFormats(
        ZXing::BarcodeFormat::QRCode |
        ZXing::BarcodeFormat::MicroQRCode |
        ZXing::BarcodeFormat::DataMatrix |
        ZXing::BarcodeFormat::Aztec);
    hints.setTryRotate(true);
    hints.setTryHarder(true);

    ZXing::Results results = ZXing::ReadBarcodes(image_view, hints);
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
```

**Confirm the actual ZXing-cpp 2.2.1 API** before committing — the API has shifted between minor versions. Inspect:

```bash
cat src/dao/third_party/zxing-cpp/core/src/ReadBarcode.h | head -80
```

If the function is `ZXing::ReadBarcodes(image_view, ZXing::DecodeHints{...})` with a different shape (e.g., builder pattern, or `ReaderOptions`), adapt the snippet above to match.

- [ ] **Step 6: Run tests, verify they pass**

Run: `npm run build:debug`
Run: `./engine/src/out/dao-debug/unit_tests --gtest_filter='DaoQrCodeDecoderTest.*' -v`

Expected: all 5 tests pass.

If `DecodesUrlPayload` passes but `JavascriptUrlIsNotMarkedAsUrl` fails because `is_url` came back true, the `IsHttpUrl` guard isn't being hit — re-check that the function uses `SchemeIs("http") || SchemeIs("https")` exactly.

- [ ] **Step 7: Commit**

```bash
git add src/dao/browser/qrcode/dao_qr_code_decoder.cc \
        src/dao/browser/qrcode/dao_qr_code_decoder_unittest.cc \
        src/patches/chrome/test/BUILD.gn.patch \
        src/patches/chrome/browser/ui/BUILD.gn.patch
git commit -m "feat(qrcode): implement DecodeBitmapBlocking via zxing-cpp"
```

---

## Task 6: Add the `IDC_DAO_DECODE_QR_CODE` command id

**Files:**
- Modify: `src/patches/chrome/app/chrome_command_ids.h.patch`

- [ ] **Step 1: Edit the patch**

Open `src/patches/chrome/app/chrome_command_ids.h.patch`. After the existing line `+#define IDC_DAO_CHECK_FOR_UPDATES       34074`, add:

```
+#define IDC_DAO_DECODE_QR_CODE          34075
```

Match the column alignment of the other Dao defines.

- [ ] **Step 2: Verify the patch still applies cleanly**

Run: `npm run import`
Expected: no "patch did not apply" errors.

- [ ] **Step 3: Quick sanity build**

Run: `npm run build:debug`
Expected: compiles. (No code references `IDC_DAO_DECODE_QR_CODE` yet so there's nothing to verify functionally.)

- [ ] **Step 4: Commit**

```bash
git add src/patches/chrome/app/chrome_command_ids.h.patch
git commit -m "feat(commands): reserve IDC_DAO_DECODE_QR_CODE (34075)"
```

---

## Task 7: Result dialog view

**Files:**
- Create: `src/dao/browser/ui/views/dao_qr_code_result_dialog_view.h`
- Create: `src/dao/browser/ui/views/dao_qr_code_result_dialog_view.cc`

(BUILD.gn was already updated in Task 4 Step 3.)

- [ ] **Step 1: Write the header**

Create `src/dao/browser/ui/views/dao_qr_code_result_dialog_view.h`:

```cpp
// Copyright 2026 Dao Browser. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_VIEWS_DAO_QR_CODE_RESULT_DIALOG_VIEW_H_
#define DAO_BROWSER_UI_VIEWS_DAO_QR_CODE_RESULT_DIALOG_VIEW_H_

#include "dao/browser/qrcode/dao_qr_code_types.h"
#include "ui/views/window/dialog_delegate.h"

namespace content {
class WebContents;
}  // namespace content

namespace dao {

class DaoQrCodeResultDialogView : public views::DialogDelegateView {
  METADATA_HEADER(DaoQrCodeResultDialogView, views::DialogDelegateView)

 public:
  // Convenience: shows a constrained dialog anchored to `web_contents`.
  static void Show(content::WebContents* web_contents,
                   DecodedQrCodes results);

  explicit DaoQrCodeResultDialogView(DecodedQrCodes results);
  DaoQrCodeResultDialogView(const DaoQrCodeResultDialogView&) = delete;
  DaoQrCodeResultDialogView& operator=(const DaoQrCodeResultDialogView&) =
      delete;
  ~DaoQrCodeResultDialogView() override;

 private:
  void BuildContents();

  DecodedQrCodes results_;
};

}  // namespace dao

#endif  // DAO_BROWSER_UI_VIEWS_DAO_QR_CODE_RESULT_DIALOG_VIEW_H_
```

- [ ] **Step 2: Write the implementation**

Create `src/dao/browser/ui/views/dao_qr_code_result_dialog_view.cc`:

```cpp
// Copyright 2026 Dao Browser. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/dao_qr_code_result_dialog_view.h"

#include <utility>

#include "base/strings/utf_string_conversions.h"
#include "components/constrained_window/constrained_window_views.h"
#include "content/public/browser/web_contents.h"
#include "dao/browser/ui/views/dao_colors.h"
#include "ui/base/clipboard/clipboard.h"
#include "ui/base/clipboard/clipboard_buffer.h"
#include "ui/base/clipboard/scoped_clipboard_writer.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/page_transition_types.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/widget/widget.h"

namespace dao {

namespace {

constexpr int kDialogWidth = 420;
constexpr int kDialogMaxHeight = 480;

class ResultRow : public views::View {
  METADATA_HEADER(ResultRow, views::View)

 public:
  explicit ResultRow(DecodedQrCode entry) : entry_(std::move(entry)) {
    SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets(12), 8));

    auto* text = AddChildView(std::make_unique<views::Label>(
        base::UTF8ToUTF16(entry_.text)));
    text->SetMultiLine(true);
    text->SetMaxLines(3);
    text->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    text->SetSelectable(true);

    auto* actions = AddChildView(std::make_unique<views::View>());
    actions->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 8));

    actions->AddChildView(std::make_unique<views::MdTextButton>(
        base::BindRepeating(&ResultRow::OnCopy,
                            base::Unretained(this)),
        u"复制"));

    if (entry_.is_url) {
      actions->AddChildView(std::make_unique<views::MdTextButton>(
          base::BindRepeating(&ResultRow::OnOpen,
                              base::Unretained(this)),
          u"打开"));
    }
  }

 private:
  void OnCopy() {
    ui::ScopedClipboardWriter writer(ui::ClipboardBuffer::kCopyPaste);
    writer.WriteText(base::UTF8ToUTF16(entry_.text));
  }

  void OnOpen() {
    if (!entry_.is_url) {
      return;
    }
    auto* widget = GetWidget();
    if (!widget) {
      return;
    }
    auto* wc = views::Widget::GetTopLevelWidgetForNativeView(
        widget->GetNativeView());
    (void)wc;
    // Open in a new tab via the browser; the dialog closes itself
    // afterwards. Find the active Browser via the widget's parent
    // WebContents (passed when the dialog was created).
    content::OpenURLParams params(
        entry_.url,
        content::Referrer(),
        WindowOpenDisposition::NEW_FOREGROUND_TAB,
        ui::PAGE_TRANSITION_LINK,
        /*is_renderer_initiated=*/false);
    if (auto* host_wc = static_cast<content::WebContents*>(
            widget->GetNativeWindowProperty("dao_qr_dialog_host_wc"))) {
      host_wc->OpenURL(std::move(params), {});
    }
    GetWidget()->CloseWithReason(views::Widget::ClosedReason::kAcceptButtonClicked);
  }

  DecodedQrCode entry_;
};

BEGIN_METADATA(ResultRow)
END_METADATA

}  // namespace

// static
void DaoQrCodeResultDialogView::Show(content::WebContents* web_contents,
                                     DecodedQrCodes results) {
  auto dialog = std::make_unique<DaoQrCodeResultDialogView>(std::move(results));
  auto* dialog_raw = dialog.get();
  views::Widget* widget = constrained_window::ShowWebModalDialogViews(
      dialog.release(), web_contents);
  // Stash the host WebContents so ResultRow's "Open" action can navigate.
  widget->SetNativeWindowProperty("dao_qr_dialog_host_wc", web_contents);
  (void)dialog_raw;
}

DaoQrCodeResultDialogView::DaoQrCodeResultDialogView(DecodedQrCodes results)
    : results_(std::move(results)) {
  SetButtons(static_cast<int>(ui::mojom::DialogButton::kCancel));
  SetButtonLabel(ui::mojom::DialogButton::kCancel, u"关闭");
  SetTitle(u"二维码识别结果");
  SetModalType(ui::mojom::ModalType::kChild);
  SetShowCloseButton(true);
  set_fixed_width(kDialogWidth);
  BuildContents();
}

DaoQrCodeResultDialogView::~DaoQrCodeResultDialogView() = default;

void DaoQrCodeResultDialogView::BuildContents() {
  auto scroll = std::make_unique<views::ScrollView>();
  scroll->ClipHeightTo(0, kDialogMaxHeight);
  auto inner = std::make_unique<views::View>();
  inner->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(0), 1));
  for (const auto& entry : results_) {
    inner->AddChildView(std::make_unique<ResultRow>(entry));
  }
  scroll->SetContents(std::move(inner));

  SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(16), 0));
  AddChildView(std::move(scroll));
}

BEGIN_METADATA(DaoQrCodeResultDialogView)
END_METADATA

}  // namespace dao
```

Several stylistic shortcuts here (no fancy icons, no shadow card) — first cut focuses on correctness; design polish can come once we can see it on screen.

- [ ] **Step 3: Build verify**

Run: `npm run build:debug`
Expected: compiles. If it complains about `OpenURL` signature mismatches (Chromium has migrated this API multiple times), adapt by inspecting `engine/src/content/public/browser/web_contents.h` for the current `OpenURL` overload and matching it.

- [ ] **Step 4: Commit**

```bash
git add src/dao/browser/ui/views/dao_qr_code_result_dialog_view.h \
        src/dao/browser/ui/views/dao_qr_code_result_dialog_view.cc
git commit -m "feat(qrcode): add DaoQrCodeResultDialogView"
```

---

## Task 8: Wire menu item, command dispatch, bitmap fetch, async decode

**Files:**
- Modify: `src/patches/chrome/browser/renderer_context_menu/render_view_context_menu.cc.patch` (create if it doesn't exist)
- Modify: `src/patches/chrome/browser/renderer_context_menu/render_view_context_menu.h.patch` (create if it doesn't exist)
- Modify: `src/patches/chrome/browser/renderer_context_menu/BUILD.gn.patch` (create if it doesn't exist)

This task does **not** use full TDD — the integration points are tested in Task 9 (browser tests) because spinning up a renderer + context menu is heavyweight. Treat Task 9 as the "test" for this task.

- [ ] **Step 1: Append menu item in `AppendImageItems()`**

Edit `engine/src/chrome/browser/renderer_context_menu/render_view_context_menu.cc` directly first (we'll export to a patch in Step 5). Locate `void RenderViewContextMenu::AppendImageItems()` (around line 2071). After the existing `IDC_CONTENT_CONTEXT_COPYIMAGELOCATION` block, before the `if (params_.link_url.is_empty())` block, insert:

```cpp
  // Dao: decode QR codes embedded in the image.
  menu_model_.AddItem(IDC_DAO_DECODE_QR_CODE, u"识别二维码");
```

- [ ] **Step 2: Add the include**

At the top of the same file, add (alphabetised among Dao includes; if there are no Dao includes yet, group with `dao/`):

```cpp
#include "dao/browser/qrcode/dao_qr_code_decoder.h"
#include "dao/browser/qrcode/dao_qr_code_types.h"
#include "dao/browser/ui/views/dao_qr_code_result_dialog_view.h"
#include "dao/browser/ui/views/dao_toast_view.h"
```

- [ ] **Step 3: Handle `IsCommandIdEnabled` and `ExecuteCommand`**

Locate `RenderViewContextMenu::IsCommandIdEnabled` (line 2857). Inside the switch, add:

```cpp
    case IDC_DAO_DECODE_QR_CODE:
      return params_.has_image_contents;
```

Locate `RenderViewContextMenu::ExecuteCommand` (line 3241). Inside the switch, add:

```cpp
    case IDC_DAO_DECODE_QR_CODE:
      ExecuteDecodeQrCode();
      break;
```

- [ ] **Step 4: Add the implementation**

At the bottom of `render_view_context_menu.cc` (just before the closing namespace if there is one, or at end of file):

```cpp
void RenderViewContextMenu::ExecuteDecodeQrCode() {
  content::RenderFrameHost* rfh =
      content::RenderFrameHost::FromID(render_process_id_, render_frame_id_);
  if (!rfh) {
    return;
  }
  mojo::AssociatedRemote<chrome::mojom::ChromeRenderFrame> chrome_render_frame;
  rfh->GetRemoteAssociatedInterfaces()->GetInterface(&chrome_render_frame);
  auto* frame_raw = chrome_render_frame.get();
  frame_raw->RequestBitmapForContextNode(base::BindOnce(
      &RenderViewContextMenu::OnQrBitmapReceived,
      weak_pointer_factory_.GetWeakPtr(),
      std::move(chrome_render_frame)));
}

void RenderViewContextMenu::OnQrBitmapReceived(
    mojo::AssociatedRemote<chrome::mojom::ChromeRenderFrame>
        /*kept_alive*/,
    const std::optional<SkBitmap>& bitmap) {
  if (!bitmap.has_value() || bitmap->drawsNothing()) {
    ShowQrToast(u"未识别到二维码");
    return;
  }
  dao::DaoQrCodeDecoder::DecodeBitmapAsync(
      *bitmap,
      base::BindOnce(&RenderViewContextMenu::OnQrDecoded,
                     weak_pointer_factory_.GetWeakPtr()));
}

void RenderViewContextMenu::OnQrDecoded(dao::DecodedQrCodes results) {
  if (results.empty()) {
    ShowQrToast(u"未识别到二维码");
    return;
  }
  dao::DaoQrCodeResultDialogView::Show(source_web_contents_,
                                       std::move(results));
}

void RenderViewContextMenu::ShowQrToast(const std::u16string& message) {
  // Reuse existing DaoToastView; if the API needs a parent View, anchor to
  // the top-level browser widget for source_web_contents_.
  if (!source_web_contents_) {
    return;
  }
  dao::DaoToastView::ShowGlobal(source_web_contents_, message);
}
```

If `DaoToastView` doesn't already have a `ShowGlobal(WebContents*, ...)` overload (check `src/dao/browser/ui/views/dao_toast_view.h`), prefer the existing overload — likely `Show(View* anchor, std::u16string text)` — and resolve `anchor` from `source_web_contents_->GetTopLevelNativeWindow()` or the appropriate views helper.

- [ ] **Step 5: Header changes**

In `engine/src/chrome/browser/renderer_context_menu/render_view_context_menu.h`, declare the new methods in the private section (search for `weak_pointer_factory_` to find roughly the right spot):

```cpp
  // Dao QR decode pipeline.
  void ExecuteDecodeQrCode();
  void OnQrBitmapReceived(
      mojo::AssociatedRemote<chrome::mojom::ChromeRenderFrame>
          chrome_render_frame,
      const std::optional<SkBitmap>& bitmap);
  void OnQrDecoded(dao::DecodedQrCodes results);
  void ShowQrToast(const std::u16string& message);
```

And the forward declarations at the top of the file:

```cpp
namespace dao {
struct DecodedQrCode;
using DecodedQrCodes = std::vector<DecodedQrCode>;
}  // namespace dao
```

- [ ] **Step 6: BUILD.gn dependency**

Edit `engine/src/chrome/browser/renderer_context_menu/BUILD.gn`. Find the source_set / static_library that contains `render_view_context_menu.cc` and add to its `deps`:

```
"//chrome/browser/ui",
```

(if not already present — `chrome/browser/ui` re-exports the dao_browser sources, so this single dep brings in the decoder, dialog, and toast view).

- [ ] **Step 7: Build verify**

Run: `npm run build:debug`
Expected: builds clean.

If you see "use of undefined `dao::DecodedQrCodes`" in the header, the forward decl is wrong (you can't `using` to a `std::vector<incomplete>` reliably in a forward decl). Fall back to including `dao/browser/qrcode/dao_qr_code_types.h` directly in the .h.

- [ ] **Step 8: Manual smoke test**

Run: `npm run start:debug`

In the browser:
1. Visit a page containing a QR code image — e.g., the Wikipedia "QR code" article has several.
2. Right-click the image. Confirm "识别二维码" appears between "Copy image address" and the existing QR-share entry.
3. Click it. A dialog should appear with the decoded URL and an "打开" / "复制" button.
4. Right-click a non-QR image (page logo). Click "识别二维码". A toast should appear with "未识别到二维码".

If something is broken, fix it before committing.

- [ ] **Step 9: Export the patches**

Run:
```bash
npm run export -- chrome/app/chrome_command_ids.h
npm run export -- chrome/browser/renderer_context_menu/render_view_context_menu.cc
npm run export -- chrome/browser/renderer_context_menu/render_view_context_menu.h
npm run export -- chrome/browser/renderer_context_menu/BUILD.gn
```

If `npm run export` corrupts patches with stripped trailing whitespace (a known issue per `MEMORY.md`), fall back to:
```bash
cd /Users/moonrailgun/Develop/dao-browser/engine/src
git diff chrome/browser/renderer_context_menu/render_view_context_menu.cc \
  > /Users/moonrailgun/Develop/dao-browser/src/patches/chrome/browser/renderer_context_menu/render_view_context_menu.cc.patch
# repeat for the other three files
```

- [ ] **Step 10: Commit**

```bash
cd /Users/moonrailgun/Develop/dao-browser
git add src/patches/chrome/app/chrome_command_ids.h.patch \
        src/patches/chrome/browser/renderer_context_menu/
git commit -m "feat(context-menu): wire IDC_DAO_DECODE_QR_CODE pipeline"
```

---

## Task 9: Browser tests

**Files:**
- Create: `src/dao/browser/ui/views/dao_qr_code_browsertest.cc`
- Modify: `src/patches/chrome/browser/ui/BUILD.gn.patch` (add the browsertest source to the existing `dao_browser_tests` source_set — check it exists per `MEMORY.md`, which says `dao_browser_browsertest.cc` is already wired)

- [ ] **Step 1: Inspect how existing dao_browser tests are wired**

Run:
```bash
grep -n "dao_browser_browsertest\|dao_browser_tests" /Users/moonrailgun/Develop/dao-browser/src/patches/chrome/browser/ui/BUILD.gn.patch
```

Identify the exact Dao test source_set block. The new test joins the same set.

- [ ] **Step 2: Write the browser test**

Create `src/dao/browser/ui/views/dao_qr_code_browsertest.cc`:

```cpp
// Copyright 2026 Dao Browser. All rights reserved.

#include "base/test/bind.h"
#include "base/test/test_future.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/renderer_context_menu/render_view_context_menu_test_util.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/qr_code_generator/bitmap_generator.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/common/context_menu_params.h"
#include "third_party/blink/public/mojom/context_menu/context_menu.mojom.h"
#include "ui/base/clipboard/clipboard.h"
#include "ui/base/clipboard/test/test_clipboard.h"
#include "url/gurl.h"

#include "chrome/app/chrome_command_ids.h"

namespace dao {
namespace {

// Generate a data: URL containing a PNG of the given QR payload, so the
// test page can <img> it without network IO.
std::string MakeQrDataUrl(const std::string& payload) {
  auto bm = qr_code_generator::GenerateBitmap(
      base::as_bytes(base::make_span(payload)),
      qr_code_generator::ModuleStyle::kSquares,
      qr_code_generator::LocatorStyle::kSquare,
      qr_code_generator::CenterImage::kNoCenterImage,
      qr_code_generator::QuietZone::kIncluded);
  // Encode bm as PNG and base64-wrap into a data: URL.
  // (Helper omitted; see dao_qr_test_util.h if you factor it out.)
  return EncodeBitmapAsDataUrl(bm.value());  // implement inline in the cc file
}

class DaoQrCodeBrowserTest : public InProcessBrowserTest {
 protected:
  content::WebContents* web_contents() {
    return browser()->tab_strip_model()->GetActiveWebContents();
  }
};

IN_PROC_BROWSER_TEST_F(DaoQrCodeBrowserTest, MenuItemAppearsOnImage) {
  GURL url("data:text/html,<img id='img' src='" + MakeQrDataUrl("hello") + "'>");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), url));
  ASSERT_TRUE(content::WaitForLoadStop(web_contents()));

  TestRenderViewContextMenu menu(web_contents()->GetPrimaryMainFrame(),
                                 BuildContextMenuParamsForImage(url));
  menu.Init();
  EXPECT_TRUE(menu.IsItemPresent(IDC_DAO_DECODE_QR_CODE));
}

IN_PROC_BROWSER_TEST_F(DaoQrCodeBrowserTest, MenuItemAbsentOnPlainPage) {
  GURL url("data:text/html,<p>no image here</p>");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), url));

  TestRenderViewContextMenu menu(web_contents()->GetPrimaryMainFrame(),
                                 BuildContextMenuParamsForPage());
  menu.Init();
  EXPECT_FALSE(menu.IsItemPresent(IDC_DAO_DECODE_QR_CODE));
}

// Decoding integration: invoke the command and assert the dialog text.
// The dialog is constrained-modal, so we look it up by widget name.
IN_PROC_BROWSER_TEST_F(DaoQrCodeBrowserTest, DecodesUrlPayloadAndShowsDialog) {
  GURL src("data:image/png;base64," + MakeQrDataUrl("https://dao.example/x"));
  GURL url("data:text/html,<img id='img' src='" + src.spec() + "'>");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), url));

  TestRenderViewContextMenu menu(web_contents()->GetPrimaryMainFrame(),
                                 BuildContextMenuParamsForImage(src));
  menu.Init();
  ASSERT_TRUE(menu.IsItemPresent(IDC_DAO_DECODE_QR_CODE));
  menu.ExecuteCommand(IDC_DAO_DECODE_QR_CODE, /*event_flags=*/0);

  // The decode is async. Wait for the dialog widget to appear.
  base::test::TestFuture<views::Widget*> dialog_future;
  // ... wait via a Widget observer registered against the Browser's
  // anchor widget. Adapt to the project's existing pattern (e.g.,
  // `views::test::AnyWidgetObserver`).
  views::Widget* dialog = dialog_future.Get();
  ASSERT_NE(nullptr, dialog);
  // Verify dialog has at least one row containing the URL.
  EXPECT_NE(std::u16string::npos,
            dialog->GetContentsView()->GetAccessibleName().find(
                u"https://dao.example/x"));
}

}  // namespace
}  // namespace dao
```

This test sketch leaves two helper functions for you to implement inline:

- `EncodeBitmapAsDataUrl(SkBitmap)` — use `gfx::PNGCodec::Encode` then base64.
- `BuildContextMenuParamsForImage(const GURL& src)` and `BuildContextMenuParamsForPage()` — see existing `chrome/test/base/...context_menu_test_util.h` for templates; if not, construct `content::ContextMenuParams` directly with `media_type = blink::mojom::ContextMenuDataMediaType::kImage` and `has_image_contents = true`.

The third test (dialog observer) is the tricky one — if `views::test::AnyWidgetObserver` doesn't fit, an alternative is to add a test-only signal on `DaoQrCodeResultDialogView::Show()` that fires a `base::OnceClosure` once the widget exists; gate it on `#if defined(UNIT_TEST)` or a static `base::OnceCallback*` setter.

- [ ] **Step 3: Add to BUILD.gn patch**

In `src/patches/chrome/browser/ui/BUILD.gn.patch`, find the existing `dao_browser_tests` source_set and append:

```
+    "//dao/browser/ui/views/dao_qr_code_browsertest.cc",
```

(Same alphabetical-ordering rule.)

- [ ] **Step 4: Build and run**

Run: `npm run test`
Expected: all `Dao*` browser tests pass, including the three new `DaoQrCodeBrowserTest.*`.

If `DecodesUrlPayloadAndShowsDialog` flakes or the future never resolves, your widget observer pattern isn't catching the dialog — try `BrowserView::GetBrowserViewForBrowser(browser())->GetWidget()->GetAllChildWidgets()` polling on `WebContentsModalDialogManager::IsDialogActive`.

- [ ] **Step 5: Commit**

```bash
git add src/dao/browser/ui/views/dao_qr_code_browsertest.cc \
        src/patches/chrome/browser/ui/BUILD.gn.patch
git commit -m "test(qrcode): browser tests for image-context QR decode"
```

---

## Task 10: Final regression sweep + smoke test

**Files:** none (verification only).

- [ ] **Step 1: Re-run the full Dao test suite**

Run: `./engine/src/out/dao-debug/browser_tests --gtest_filter='Dao*' --gtest_brief=1`
Run: `./engine/src/out/dao-debug/unit_tests --gtest_filter='Dao*' --gtest_brief=1`

Expected: pass count is **(previous baseline from Task 0 Step 3) + (3 new browser tests) + (5 new unit tests)**. Any unexpected failures must be investigated before declaring done.

- [ ] **Step 2: Manual smoke matrix**

Run: `npm run start:debug`

Test each row:

| Image source | QR? | Expected |
|--------------|-----|----------|
| `https://en.wikipedia.org/wiki/QR_code` page screenshot images | yes | Dialog with URL row + Open button |
| Any non-QR `<img>` (page logo) | no | Toast "未识别到二维码" |
| `data:image/png;base64,...` containing a QR | yes | Dialog appears |
| `<canvas>` rendering a QR (right-click) | yes | Dialog appears |
| Cross-origin `<img>` from a CDN | yes | Dialog appears (no CORS issue — bitmap is fetched via mojo, not fetch()) |
| `javascript:` payload QR (you'll need to generate this externally) | n/a | Dialog shows the text **without** an Open button |

- [ ] **Step 3: Final build**

Run: `npm run rebuild`
Expected: clean build, no warnings about Dao code.

- [ ] **Step 4: Final commit (if any housekeeping)**

If there were any tweaks during the smoke matrix:

```bash
git add -A
git commit -m "fix(qrcode): smoke-test follow-ups"
```

If not, leave the tree clean — the user will commit the final feature when they're satisfied.

---

## Self-Review

**Spec coverage:**

| Spec section | Task |
|--------------|------|
| 1. Goal: menu item + decode + dialog + toast fallback | Tasks 6, 7, 8 |
| 2. Vendor ZXing-cpp, ThreadPool, mojo bitmap path | Tasks 1, 2, 4, 5, 8 |
| 3.1 third_party/zxing-cpp + BUILD.gn + exception flag | Tasks 1, 2 |
| 3.2 dao_qr_code_types.h | Task 3 |
| 3.2 dao_qr_code_decoder.{h,cc} | Tasks 4, 5 |
| 3.2 dao_qr_code_result_dialog_view.{h,cc} | Task 7 |
| 3.3 IDC + render_view_context_menu patches + BUILD.gn deps | Tasks 6, 8 |
| 4. Data flow (right-click → mojo → ThreadPool → dialog) | Task 8 |
| 5. Edge cases (null bitmap, huge image, animated frame, javascript: URL, tab close, multiple QR) | Task 5 (javascript: + multiple), Task 8 (null bitmap, tab close); huge-image cap deferred — see note below |
| 6. Tests | Tasks 5 (unit), 9 (browser) |

**Gap found (huge-image cap):** Spec §5 says we cap input at 4096×4096 by downscaling; this is **not** in any task. Add a sub-step to Task 5 Step 5: before constructing `ImageView`, if `bitmap.width() > 4096 || bitmap.height() > 4096`, downscale via `skia::ImageOperations::Resize` and convert the smaller bitmap. Keeping it as a tiny inline addition rather than a separate task because it's three lines.

Adjusted snippet for Task 5 Step 5 (replace the start of `DecodeBitmapBlocking`):

```cpp
DecodedQrCodes DaoQrCodeDecoder::DecodeBitmapBlocking(const SkBitmap& bitmap) {
  if (bitmap.drawsNothing()) {
    return {};
  }
  constexpr int kMaxDim = 4096;
  SkBitmap effective = bitmap;
  if (bitmap.width() > kMaxDim || bitmap.height() > kMaxDim) {
    float ratio = std::min(static_cast<float>(kMaxDim) / bitmap.width(),
                           static_cast<float>(kMaxDim) / bitmap.height());
    effective = skia::ImageOperations::Resize(
        bitmap, skia::ImageOperations::RESIZE_BEST,
        static_cast<int>(bitmap.width() * ratio),
        static_cast<int>(bitmap.height() * ratio));
  }
  std::vector<uint8_t> luma = SkBitmapToLuminance(effective);
  // ... rest of function uses `effective` instead of `bitmap`
```

And add `#include "skia/ext/image_operations.h"` to the same .cc.

**Placeholder scan:**

- One soft TODO: Task 9 Step 2 leaves `EncodeBitmapAsDataUrl` and the widget observer to be implemented inline by the engineer. This is intentional — the helper is small and Chromium-version-dependent — but flagging here so the engineer knows to write them rather than skip. Marked as `// implement inline in the cc file`.
- No "TBD"/"add appropriate"/"similar to Task N" usages elsewhere.

**Type consistency:**

- `DecodedQrCode` / `DecodedQrCodes` named consistently across Tasks 3, 4, 5, 7, 8, 9. ✓
- `DecodeBitmapAsync` / `DecodeBitmapBlocking` named consistently. ✓
- `IDC_DAO_DECODE_QR_CODE` (34075) used identically in Tasks 6 and 8 (and tests). ✓
- `dao::DaoQrCodeResultDialogView::Show(WebContents*, DecodedQrCodes)` signature is used identically by Task 7's static method and Task 8's caller. ✓
- `RequestBitmapForContextNode` (no bounds-hint variant) — Task 8's wiring matches the mojom signature `RequestBitmapForContextNode() => (skia.mojom.BitmapN32? bitmap)` confirmed during research. ✓

No further fixes needed.
