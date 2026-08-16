# Image Context-Menu QR Decode — Design

**Status:** Draft
**Author:** moonrailgun
**Date:** 2026-05-13
**Chromium baseline:** 147.0.7727.135

## 1. Goal

When the user right-clicks an `<img>` (or any image-typed context node) in a web page, surface a "Decode QR Code" item in the existing Chromium context menu. Selecting it decodes the image bitmap, then:

- Always shows a results dialog (so the experience is consistent regardless of what the QR encodes).
- Lists every QR code found in the image; for URL-typed payloads the dialog offers an **Open** action, for any payload the dialog offers a **Copy** action.
- If no code is detected, surfaces a single `DaoToastView` ("未识别到二维码" / no QR code found) instead of an empty dialog — this is the only "failure" path that bypasses the dialog, because an empty modal would be worse UX than a toast.

Non-goals:

- Decoding non-QR symbologies (Data Matrix, PDF417, Aztec, etc.). ZXing-cpp supports them at near-zero extra cost, but UX/menu copy is QR-only for v1; if ZXing returns a non-QR symbology in `BarcodeFormat`, we still display it but label it "条码" generically.
- Decoding videos / animated GIFs frame-by-frame — only the still bitmap delivered by `RequestBitmapForContextNode()` is examined.
- Right-click on plain page area (no image) — menu item only appears when context type is image.

## 2. Architectural Approach

ZXing-cpp is **vendored under `third_party/zxing-cpp/`** with a Dao-authored `BUILD.gn`, mirroring the layout of any other Chromium third-party. The patch system applies the BUILD.gn alongside the new sources, so subsequent ZXing-cpp upgrades become a "drop new source + adjust BUILD.gn" operation, not a rewrite.

Decoding runs on a **`base::ThreadPool` background sequence in the browser process** (`base::TaskPriority::USER_VISIBLE`, `base::MayBlock()` not required since CPU-only). UI work (menu, dialog, toast) stays on the UI thread; only the ZXing call itself is off-thread.

The context-menu integration patches `chrome/browser/renderer_context_menu/render_view_context_menu.{h,cc}` to:

1. Add a new command id `IDC_DAO_DECODE_QR_CODE` (declared in `chrome/app/chrome_command_ids.h`).
2. Append the menu item to the image submenu inside `AppendImageItems()` (around line 2083, next to `IDC_CONTENT_CONTEXT_COPYIMAGE`).
3. Handle execution in `ExecuteCommand()` and enablement in `IsCommandIdEnabled()`.

Bitmap retrieval **reuses Chromium's existing path**: `mojo::AssociatedRemote<chrome::mojom::ChromeRenderFrame>` → `RequestBitmapForContextNode()` (returns `BitmapN32?`). This is the same plumbing Save Image / Copy Image / Lens already use, so all sources (http/https/data/blob/canvas/CSS background image once Chromium captures it) are covered automatically.

## 3. Components

### 3.1 `third_party/zxing-cpp/` (new, vendored)

- ZXing-cpp source at the latest tagged release (v2.x).
- `BUILD.gn` declares a `static_library("zxing")` exposing `core/src/` headers.
- `DEPS` / `OWNERS` / `LICENSE` files per Chromium third_party convention.
- Compiler flags: `-fno-exceptions` is **incompatible** with ZXing-cpp's API (it throws `std::runtime_error` from `ReadBarcode` when input is invalid). We wrap ZXing calls in a thin C++ shim that catches and converts to `std::optional`, so the rest of Chromium can keep `-fno-exceptions`. The third_party target itself enables exceptions only for ZXing translation units (`cflags_cc = [ "-fexceptions" ]`).

### 3.2 `src/dao/browser/qrcode/` (new, Dao-owned)

**`dao_qr_code_types.h`**
```cpp
namespace dao {
struct DecodedQrCode {
  std::string text;            // raw payload
  std::string format;          // "QR_CODE" / "DATA_MATRIX" etc., from ZXing
  bool is_url = false;         // GURL(text).is_valid() && has http/https scheme
  GURL url;                    // populated iff is_url
};
using DecodedQrCodes = std::vector<DecodedQrCode>;
}  // namespace dao
```

**`dao_qr_code_decoder.{h,cc}`**
- Static API: `void DecodeBitmapAsync(SkBitmap bitmap, base::OnceCallback<void(DecodedQrCodes)> on_done)`.
- Internally: `base::ThreadPool::PostTaskAndReplyWithResult` → blocking decode helper → reply to UI thread.
- The blocking helper converts `SkBitmap` to ZXing's `ImageView` (8-bit luminance buffer derived from N32 pixels), calls `ZXing::ReadBarcodes` with `DecodeHints` configured to `BarcodeFormat::QRCode | BarcodeFormat::MicroQRCode | BarcodeFormat::Aztec | BarcodeFormat::DataMatrix` (broad to maximize useful hits).
- On std::exception → log `LOG(WARNING)` and return empty vector (caller treats as "not found").

**`dao_qr_code_result_dialog_view.{h,cc}`**
- A `views::DialogDelegateView` shown via `constrained_window::ShowWebModalDialogViews(...)` anchored to the source `WebContents`.
- Visual style consistent with `dao_control_center_qr_view`: dark header, rounded corners, Lucide icons, Dao palette tokens from `dao_colors.h`.
- Layout: title "二维码识别结果" + scrollable list of result rows.
- Each row: `[format icon][monospace payload, max 3 lines, ellipsised][Copy button][Open button — only when `is_url`]`.
- Close button bottom-right.
- Single-result and multi-result use the same template; the list just has 1 child.

### 3.3 Modified Chromium files (via patch)

| File | Change |
|------|--------|
| `chrome/app/chrome_command_ids.h` | `+#define IDC_DAO_DECODE_QR_CODE 34075` (next id after the existing Dao block 34071–34074: COPY_URL / DUPLICATE_TAB / WELCOME / CHECK_FOR_UPDATES). |
| `chrome/browser/renderer_context_menu/render_view_context_menu.h` | `+void ExecuteDecodeQrCode();` private method declaration. |
| `chrome/browser/renderer_context_menu/render_view_context_menu.cc` | (a) Insert menu item in `AppendImageItems()`. (b) Handle in `ExecuteCommand()` and `IsCommandIdEnabled()` (always enabled when image context). (c) Implement `ExecuteDecodeQrCode()`: get `chrome_render_frame` mojo, call `RequestBitmapForContextNode()`, on reply hand `SkBitmap` to `DaoQrCodeDecoder::DecodeBitmapAsync`, on result either show toast (empty) or dialog. |
| `chrome/browser/ui/BUILD.gn` | Already patched for Dao; add the new `qrcode/*.cc` files to the existing `dao_browser_sources` source_set, and add `//third_party/zxing-cpp:zxing` to `deps`. |
| `chrome/browser/renderer_context_menu/BUILD.gn` | Add `//src/dao/browser/qrcode` (or appropriate target alias) to `deps` so the menu code can call into the decoder. |

Localization: menu text "识别二维码" lives in a Dao-only string resource (or, simpler for v1, a hardcoded `u"识别二维码"` literal inside the Dao patch block, matching how other Dao-only menu items are handled today). Toast and dialog strings are likewise hardcoded Chinese for v1; English fallback is a future task.

## 4. Data Flow (concrete)

```
[user] right-click <img>
  ↓
RenderViewContextMenu::AppendImageItems()
  └─ menu_model_.AddItem(IDC_DAO_DECODE_QR_CODE, u"识别二维码")
  ↓ user selects
RenderViewContextMenu::ExecuteCommand(IDC_DAO_DECODE_QR_CODE)
  ↓
RenderViewContextMenu::ExecuteDecodeQrCode()
  ├─ get RenderFrameHost from params_.render_frame_id
  ├─ bind ChromeRenderFrame mojo
  └─ chrome_render_frame->RequestBitmapForContextNode(callback)
     ↓ (mojo IPC, async, renderer process)
RenderViewContextMenu::OnQrBitmapReceived(SkBitmap? bitmap)
  ├─ if !bitmap: ShowToast("未识别到二维码"); return
  └─ DaoQrCodeDecoder::DecodeBitmapAsync(*bitmap, BindOnce(OnQrDecoded))
     ↓ (PostTaskAndReplyWithResult, browser process ThreadPool)
[ThreadPool] DaoQrCodeDecoder::DecodeOnBackground(SkBitmap)
  ├─ SkBitmap → 8-bit luminance buffer
  ├─ ZXing::ReadBarcodes(image_view, hints)
  └─ return std::vector<DecodedQrCode>
     ↓ (reply to UI thread)
RenderViewContextMenu::OnQrDecoded(DecodedQrCodes results)
  ├─ if results.empty(): ShowToast("未识别到二维码")
  └─ else: DaoQrCodeResultDialogView::Show(web_contents_, std::move(results))
```

The `OnQrBitmapReceived` and `OnQrDecoded` callbacks are bound through `weak_pointer_factory_` already living on `RenderViewContextMenu`, so a closed menu / closed tab cleanly drops the result.

## 5. Error Handling & Edge Cases

| Case | Behavior |
|------|----------|
| `RequestBitmapForContextNode()` returns null bitmap (image cross-origin and not yet decoded) | Toast "未识别到二维码" — same as decode-empty. We do **not** distinguish "no bitmap" from "bitmap had no QR" in the UI; the user can't act on the difference. |
| Image is huge (e.g. 10000×10000 screenshot) | ZXing scales internally; we additionally cap the input at 4096×4096 by downscaling with `skia::ImageOperations::RESIZE_BEST` before handing to ZXing, to keep decode under ~200ms. |
| Image is an animated GIF / WebP | `RequestBitmapForContextNode` returns the currently-displayed frame — we decode that one frame. Documented limitation. |
| Multiple QR codes overlap | ZXing returns them all; dialog lists each in scan order. |
| Payload is a `javascript:` or `data:text/html` URL | `is_url` stays false (we only set `is_url` for http/https). Open button hidden. Copy still works. This is a **security choice** — never offer one-click open of script URLs. |
| Tab closes mid-decode | `weak_pointer_factory_` invalidates; result is dropped. |
| Render frame died before bitmap reply | mojo connection error → callback runs with null `BitmapN32?` → toast path. |
| User triggers decode twice rapidly | Each invocation is independent; if the first dialog is open, the second simply opens another (matches Chrome's "Copy Image" behavior — Chrome doesn't dedupe either). |

## 6. Testing

`browser_tests` additions in `src/dao/browser/ui/views/dao_browser_browsertest.cc` (or a new `qrcode/dao_qr_code_browsertest.cc` under the same `dao_browser_tests` source_set):

| Test | Coverage |
|------|----------|
| `DaoQrCode.MenuItemAppearsOnImage` | Right-click on `<img>` → `IDC_DAO_DECODE_QR_CODE` is in the menu model. |
| `DaoQrCode.MenuItemAbsentOnPlainPage` | Right-click on `<body>` background → command id absent. |
| `DaoQrCode.DecodesUrlPayload` | Embed a known QR (URL) in a test page, simulate menu execute, assert the dialog shows with `is_url == true` and the expected URL. |
| `DaoQrCode.DecodesTextPayload` | Same but for plain-text payload; assert no Open button. |
| `DaoQrCode.NoCodeShowsToast` | Right-click on a plain non-QR image, assert toast appears and dialog does not. |
| `DaoQrCode.MultipleCodesAllShown` | Image containing 2 QR codes, assert dialog has 2 rows. |

Unit test for `DaoQrCodeDecoder::DecodeBitmapAsync` happens in `dao_qr_code_decoder_unittest.cc` against `base::test::TaskEnvironment` — feeds in synthetic `SkBitmap`s built from raw ZXing test fixtures.

## 7. Performance Budget

| Stage | Target |
|-------|--------|
| Mojo round-trip for `RequestBitmapForContextNode` | ~5–20 ms (already used by Save Image, no regression) |
| Bitmap → luminance conversion | <5 ms for ≤4096² images |
| ZXing decode | <100 ms for ≤4096² images, single QR; <300 ms for cluttered images |
| UI thread blocking | 0 ms for decode (off-thread); dialog construction <16 ms |

If the entire round-trip exceeds ~500 ms (e.g. on a giant cross-origin image awaiting reload), we still complete asynchronously without UI blocking; user perceives a small delay but no jank.

## 8. Future Work (out of v1 scope)

- English / multi-locale strings (currently zh-CN only).
- "Re-scan with different decoder" button when `ZXing::ReadBarcodes` returns empty but bitmap was non-trivial.
- Menu item also on right-click of a **selected screen region** (would require Lens-style region-capture path).
- Decode `<video>` current frame (separate code path; can be slotted in later behind a feature flag).
