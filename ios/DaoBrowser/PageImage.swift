import ImageIO
import SwiftUI
import UniformTypeIdentifiers
import WebKit

/// A page image fetched through its tab's web view, so cookies and private browsing match the page.
struct PageImage {
    static let byteLimit = 32 << 20
    let data: Data

    struct Unavailable: Error {}

    /// A web page or an error status served in place of the image is a failure, not something to keep.
    static func accepts(_ response: URLResponse) -> Bool {
        let status = (response as? HTTPURLResponse)?.statusCode ?? 200
        return (200..<300).contains(status) && response.mimeType?.lowercased() != "text/html"
    }

    @MainActor static func load(_ url: URL, in webView: WKWebView) async throws -> PageImage {
        let transfer = ImageTransfer()
        let data = try await transfer.run(URLRequest(url: url), in: webView)
        return PageImage(data: data)
    }

    /// Copies the original encoding; formats other apps may not paste also get a PNG representation.
    /// Decoding and re-encoding run off the main thread so a large image cannot stall the interface.
    @MainActor func copy() async throws {
        let item = try await Task.detached(priority: .userInitiated) { [data] in
            guard let source = CGImageSourceCreateWithData(data as CFData, nil),
                  CGImageSourceGetStatus(source) == .statusComplete,
                  let type = CGImageSourceGetType(source) as String? else { throw Unavailable() }
            var item = [type: data]
            if ![UTType.png, .jpeg, .gif].map(\.identifier).contains(type) {
                guard let png = UIImage(data: data)?.pngData() else { throw Unavailable() }
                item[UTType.png.identifier] = png
            }
            return item
        }.value
        UIPasteboard.general.setItems([item])
    }

    /// Decodes at most 2048 pixels on the long edge, off the main thread, so large images cannot exhaust memory.
    func preview() async throws -> UIImage {
        try await Task.detached(priority: .userInitiated) { [data] in
            let options = [kCGImageSourceCreateThumbnailFromImageAlways: true,
                           kCGImageSourceCreateThumbnailWithTransform: true,
                           kCGImageSourceShouldCacheImmediately: true,
                           kCGImageSourceThumbnailMaxPixelSize: 2048] as CFDictionary
            guard let source = CGImageSourceCreateWithData(data as CFData, nil),
                  let image = CGImageSourceCreateThumbnailAtIndex(source, 0, options) else { throw Unavailable() }
            return UIImage(cgImage: image)
        }.value
    }
}

/// Downloads one image into a temporary file and reads it back.
@MainActor private final class ImageTransfer: NSObject, WKDownloadDelegate {
    private var continuation: CheckedContinuation<Data, Error>?
    private var file: URL?

    func run(_ request: URLRequest, in webView: WKWebView) async throws -> Data {
        try await withCheckedThrowingContinuation { continuation in
            self.continuation = continuation
            webView.startDownload(using: request) { download in download.delegate = self }
        }
    }

    func download(_ download: WKDownload, decideDestinationUsing response: URLResponse,
                  suggestedFilename: String, completionHandler: @escaping (URL?) -> Void) {
        guard PageImage.accepts(response), response.expectedContentLength <= Int64(PageImage.byteLimit) else {
            completionHandler(nil)
            finish(.failure(PageImage.Unavailable()))
            return
        }
        let file = FileManager.default.temporaryDirectory.appending(path: "PageImages").appending(path: UUID().uuidString)
        do { try FileManager.default.createDirectory(at: file.deletingLastPathComponent(), withIntermediateDirectories: true) }
        catch {
            completionHandler(nil)
            finish(.failure(error))
            return
        }
        self.file = file
        completionHandler(file)
    }

    func downloadDidFinish(_ download: WKDownload) {
        guard let file else { finish(.failure(PageImage.Unavailable())); return }
        // Reading up to the byte limit on the main thread would stall the interface.
        Task {
            finish(await Task.detached(priority: .userInitiated) {
                Result {
                    guard let size = try file.resourceValues(forKeys: [.fileSizeKey]).fileSize,
                          size <= PageImage.byteLimit else { throw PageImage.Unavailable() }
                    return try Data(contentsOf: file)
                }
            }.value)
        }
    }

    func download(_ download: WKDownload, didFailWithError error: Error, resumeData: Data?) {
        finish(.failure(error))
    }

    private func finish(_ result: Result<Data, Error>) {
        if let file { try? FileManager.default.removeItem(at: file) }
        file = nil
        continuation?.resume(with: result)
        continuation = nil
    }
}

struct ImagePreviewView: View {
    let image: UIImage?
    let close: () -> Void

    var body: some View {
        ZStack(alignment: .topTrailing) {
            Color.black.ignoresSafeArea()
            if let image {
                ZoomableImage(image: image).ignoresSafeArea()
                    .accessibilityElement().accessibilityLabel(L("image_preview")).accessibilityAddTraits(.isImage)
            } else {
                ProgressView().tint(.white).frame(maxWidth: .infinity, maxHeight: .infinity)
            }
            Button(action: close) {
                DaoIcon(name: "x").foregroundStyle(.white).frame(width: 44, height: 44)
                    .overlay(Circle().stroke(.white.opacity(0.2))).contentShape(Circle())
            }.buttonStyle(.plain).accessibilityLabel(L("close_image_preview")).padding(8)
        }
        .accessibilityAction(.escape, close)
    }
}

/// Pinch to zoom up to five times and pan while zoomed.
private struct ZoomableImage: UIViewRepresentable {
    let image: UIImage

    func makeUIView(context: Context) -> ZoomView { ZoomView(image: image) }
    func updateUIView(_ view: ZoomView, context: Context) {}

    final class ZoomView: UIScrollView, UIScrollViewDelegate {
        private let imageView: UIImageView

        init(image: UIImage) {
            imageView = UIImageView(image: image)
            super.init(frame: .zero)
            imageView.contentMode = .scaleAspectFit
            addSubview(imageView)
            minimumZoomScale = 1
            maximumZoomScale = 5
            showsVerticalScrollIndicator = false
            showsHorizontalScrollIndicator = false
            contentInsetAdjustmentBehavior = .never
            delegate = self
        }

        required init?(coder: NSCoder) { nil }

        override func layoutSubviews() {
            super.layoutSubviews()
            // Fit the image to the screen until the user zooms; zooming scales this frame.
            guard zoomScale == 1 else { return }
            imageView.frame = bounds
            contentSize = bounds.size
        }

        func viewForZooming(in scrollView: UIScrollView) -> UIView? { imageView }
    }
}
