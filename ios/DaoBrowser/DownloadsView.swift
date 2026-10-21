import QuickLook
import SwiftUI

struct DownloadsView: View {
    let model: BrowserModel
    @State private var preview: URL?
    @State private var deleting: DownloadRecord?

    var body: some View {
        UtilityPage(title: "downloads", back: { model.page = .browser }) {
            if model.downloads.items.isEmpty {
                Spacer()
                Text(L("downloads_empty")).foregroundStyle(Nova.muted).padding()
                Spacer()
            } else {
                ScrollView {
                    LazyVStack(spacing: 12) {
                        ForEach(model.downloads.items) { item in
                            VStack(alignment: .leading, spacing: 10) {
                                HStack {
                                    DaoIcon(name: "file")
                                    Text(item.name.isEmpty ? L("download_preparing") : item.name).font(.subheadline.weight(.medium)).lineLimit(2)
                                    Spacer()
                                    if item.isPrivate { DaoIcon(name: "venetian-mask", size: 16) }
                                }
                                Text(L("download_" + item.state)).font(.caption).foregroundStyle(Nova.muted)
                                if item.state == "downloading" {
                                    if let transfer = model.downloads.transfers[item.id] {
                                        if transfer.total != nil { ProgressView(value: transfer.fraction) }
                                        TransferLine(transfer: transfer)
                                    } else { ProgressView(value: item.fraction) }
                                    Button(L("cancel")) { model.downloads.cancel(item.id) }.frame(minHeight: 44)
                                } else {
                                    HStack {
                                        if item.state == "complete" {
                                            Button(L("open_file")) {
                                                if FileManager.default.fileExists(atPath: item.fileURL.path) { preview = item.fileURL }
                                                else { model.notify(L("file_missing")) }
                                            }.frame(minHeight: 44)
                                            ShareLink(item: item.fileURL) { Text(L("share")) }.frame(minHeight: 44)
                                        } else if model.downloads.canResume(item.id) {
                                            Button(L("retry")) { model.retryDownload(item) }.frame(minHeight: 44)
                                        } else if let page = item.page {
                                            Button(L("open_download_page")) {
                                                model.addTab(isPrivate: item.isPrivate)
                                                model.navigate(page)
                                            }.frame(minHeight: 44)
                                        }
                                        Spacer()
                                        IconButton(icon: "trash-2", label: "delete") { deleting = item }
                                    }.font(.subheadline)
                                }
                            }.padding(16).background(Nova.surface, in: .rect(cornerRadius: 16))
                                .overlay(RoundedRectangle(cornerRadius: 16).stroke(Nova.border))
                        }
                    }.padding(16)
                }
            }
        }
        .quickLookPreview($preview)
        .alert(L("delete_download"), isPresented: Binding(get: { deleting != nil }, set: { if !$0 { deleting = nil } })) {
            Button(L("cancel"), role: .cancel) { deleting = nil }
            Button(L("delete"), role: .destructive) {
                if let deleting, let fileDeleted = model.downloads.remove(deleting.id) {
                    model.showToast(L(fileDeleted ? "download_deleted_with_file" : "download_deleted_record_only"))
                }
                deleting = nil
            }
        } message: { Text(L("delete_download_notice")) }
    }
}

/// Transferred bytes, plus the speed once a fresh sample exists; it falls to zero when progress stalls.
private struct TransferLine: View {
    let transfer: TransferMeter

    var body: some View {
        TimelineView(.periodic(from: .now, by: 1)) { _ in
            HStack {
                Text(transfer.total.map { String(format: L("download_progress"), size(transfer.received), size($0)) }
                    ?? size(transfer.received))
                Spacer()
                if let speed = transfer.speed(at: ProcessInfo.processInfo.systemUptime) {
                    Text(String(format: L("download_speed"), size(Int64(speed))))
                }
            }.font(.caption.monospacedDigit()).foregroundStyle(Nova.muted)
        }
    }

    private func size(_ bytes: Int64) -> String { ByteCountFormatter.string(fromByteCount: bytes, countStyle: .file) }
}
