import AVFoundation
import CoreImage.CIFilterBuiltins
import CryptoKit
import Security
import SwiftUI
import VisionKit
import WebKit

struct QRScanner: View {
    let onScan: (String) -> Void
    @Environment(\.dismiss) private var dismiss
    @State private var ready = false
    @State private var unavailable = false

    var body: some View {
        NavigationStack {
            Group {
                if ready { ScannerSurface(onScan: onScan, onFailure: { unavailable = true; ready = false }) }
                else if unavailable { Text(L("scanner_unavailable")).multilineTextAlignment(.center).padding(24) }
                else { ProgressView().accessibilityLabel(L("camera_permission")) }
            }.navigationTitle(L("scan_qr"))
                .toolbar { ToolbarItem(placement: .cancellationAction) { Button(L("cancel")) { dismiss() } } }
                .task {
                    guard DataScannerViewController.isSupported else { unavailable = true; return }
                    let granted = await AVCaptureDevice.requestAccess(for: .video)
                    ready = granted && DataScannerViewController.isAvailable
                    unavailable = !ready
                }
        }
    }
}

private struct ScannerSurface: UIViewControllerRepresentable {
    let onScan: (String) -> Void
    let onFailure: () -> Void
    func makeCoordinator() -> Coordinator { Coordinator(onScan: onScan) }
    func makeUIViewController(context: Context) -> DataScannerViewController {
        let scanner = DataScannerViewController(recognizedDataTypes: [.barcode(symbologies: [.qr])],
            qualityLevel: .balanced, recognizesMultipleItems: false, isHighlightingEnabled: true)
        scanner.delegate = context.coordinator
        do { try scanner.startScanning() }
        catch { Task { @MainActor in onFailure() } }
        return scanner
    }
    func updateUIViewController(_ uiViewController: DataScannerViewController, context: Context) {}
    static func dismantleUIViewController(_ controller: DataScannerViewController, coordinator: Coordinator) { controller.stopScanning() }

    final class Coordinator: NSObject, DataScannerViewControllerDelegate {
        let onScan: (String) -> Void
        private var delivered = false
        init(onScan: @escaping (String) -> Void) { self.onScan = onScan }
        func dataScanner(_ dataScanner: DataScannerViewController, didAdd addedItems: [RecognizedItem], allItems: [RecognizedItem]) {
            guard !delivered else { return }
            for item in addedItems {
                if case .barcode(let barcode) = item, let value = barcode.payloadStringValue {
                    delivered = true
                    dataScanner.stopScanning()
                    onScan(value)
                    return
                }
            }
        }
    }
}

struct QRCodeSheet: View {
    let value: String
    @Environment(\.dismiss) private var dismiss
    var body: some View {
        NavigationStack {
            VStack(spacing: 24) {
                if let image {
                    Image(uiImage: image).interpolation(.none).resizable().scaledToFit().frame(maxWidth: 280, maxHeight: 280)
                        .padding(20).background(.white).accessibilityLabel(L("qr_code"))
                } else { Text(L("qr_failed")) }
                Text(value).font(.subheadline).lineLimit(4).textSelection(.enabled)
                if let url = URL(string: value) { ShareLink(item: url) { Text(L("share")) } }
            }.padding(24).navigationTitle(L("qr_code"))
                .toolbar { ToolbarItem(placement: .confirmationAction) { Button(L("done")) { dismiss() } } }
        }
    }
    private var image: UIImage? {
        let filter = CIFilter.qrCodeGenerator()
        filter.message = Data(value.utf8)
        guard let output = filter.outputImage?.transformed(by: CGAffineTransform(scaleX: 8, y: 8)),
              let bitmap = CIContext().createCGImage(output, from: output.extent) else { return nil }
        return UIImage(cgImage: bitmap)
    }
}

struct SecuritySheet: View {
    let tab: BrowserTab
    let webView: WKWebView?
    @Environment(\.dismiss) private var dismiss
    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(alignment: .leading, spacing: 20) {
                    DaoIcon(name: tab.secure ? "lock-keyhole" : "info", size: 32)
                    Text(L(tab.secure ? "connection_secure" : "connection_not_secure")).font(.headline)
                    Text(tab.record.url).font(.subheadline).textSelection(.enabled)
                    if let trust = webView?.serverTrust, let chain = SecTrustCopyCertificateChain(trust) as? [SecCertificate] {
                        Text(L("certificate_chain")).font(.headline)
                        ForEach(Array(chain.enumerated()), id: \.offset) { _, certificate in
                            VStack(alignment: .leading, spacing: 8) {
                                Text(SecCertificateCopySubjectSummary(certificate) as String? ?? L("certificate"))
                                Text(L("certificate_sha256")).font(.caption)
                                Text(SHA256.hash(data: SecCertificateCopyData(certificate) as Data).map { String(format: "%02X", $0) }.joined(separator: ":"))
                                    .font(.caption2.monospaced()).textSelection(.enabled)
                            }.padding(16).background(Nova.secondary, in: .rect(cornerRadius: 14))
                        }
                    }
                }.padding(24)
            }.navigationTitle(L("site_information"))
                .toolbar { ToolbarItem(placement: .confirmationAction) { Button(L("done")) { dismiss() } } }
        }
    }
}
