import Foundation
import Observation
import WebKit

struct DownloadRecord: Identifiable, Codable {
    var id = UUID()
    var name = ""
    /// The committed page that started the download. Original requests can be authenticated POSTs
    /// or blobs, so a transfer that cannot resume sends the user back here instead of replaying them.
    var page: String?
    var state = "downloading"
    var fraction = 0.0
    var isPrivate = false
    var date = Date.now
    var fileURL: URL {
        URL.documentsDirectory.appending(path: "Downloads").appending(path: id.uuidString)
            .appending(path: DownloadName.sanitize(name))
    }
}

/// What the user approves before a page download writes anything.
struct DownloadRequest {
    let name: String
    /// The response's expected length, or nil when the server does not report one.
    let size: Int64?
}

typealias DownloadConfirmation = (DownloadRequest, @escaping (Bool) -> Void) -> Void

@MainActor @Observable final class DownloadStore: NSObject, WKDownloadDelegate {
    var items: [DownloadRecord] = []
    /// Byte counts of running transfers, published a few times a second and never persisted.
    private(set) var transfers: [UUID: TransferMeter] = [:]
    @ObservationIgnored var onError: ((String) -> Void)?
    @ObservationIgnored var onToast: ((String) -> Void)?
    @ObservationIgnored private var active: [ObjectIdentifier: (WKDownload, UUID)] = [:]
    @ObservationIgnored private var observations: [UUID: NSKeyValueObservation] = [:]
    // A new download has no visible record until it is approved and given a destination.
    @ObservationIgnored private var pending: [UUID: DownloadRecord] = [:]
    @ObservationIgnored private var confirmations: [UUID: DownloadConfirmation] = [:]
    @ObservationIgnored private var images: Set<UUID> = []
    @ObservationIgnored private var meters: [UUID: TransferMeter] = [:]
    @ObservationIgnored private var published: [UUID: TimeInterval] = [:]
    private var resumable: [UUID: Data] = [:]
    @ObservationIgnored private var canSave = true

    override init() {
        super.init()
        do {
            items = try BrowserFiles.read([DownloadRecord].self, name: "downloads.json") ?? []
            for index in items.indices where items[index].state == "downloading" { items[index].state = "interrupted" }
            removeOrphanedFiles()
        } catch { canSave = BrowserFiles.quarantine("downloads.json") }
    }

    /// Private downloads are never persisted, so their files have no record after a relaunch.
    private func removeOrphanedFiles() {
        let root = URL.documentsDirectory.appending(path: "Downloads")
        let known = Set(items.map { $0.id.uuidString })
        let folders = (try? FileManager.default.contentsOfDirectory(at: root, includingPropertiesForKeys: nil)) ?? []
        for folder in folders where !known.contains(folder.lastPathComponent) {
            try? FileManager.default.removeItem(at: folder)
        }
    }

    /// Tracks a new download. Without `confirm` (an explicit image download) it starts once WebKit names it.
    func attach(_ download: WKDownload, isPrivate: Bool, page: URL?, image: Bool = false,
                confirm: DownloadConfirmation?) {
        var item = DownloadRecord()
        if let page, Address.isWeb(page) { item.page = page.absoluteString }
        item.isPrivate = isPrivate
        pending[item.id] = item
        confirmations[item.id] = confirm
        if image { images.insert(item.id) }
        track(download, id: item.id)
    }

    private func track(_ download: WKDownload, id: UUID) {
        active[ObjectIdentifier(download)] = (download, id)
        download.delegate = self
        // A resumed transfer starts a new speed baseline.
        meters[id] = nil
        observations[id] = download.progress.observe(\.completedUnitCount, options: [.new]) { [weak self] progress, _ in
            let completed = progress.completedUnitCount
            let total = progress.totalUnitCount
            Task { @MainActor in self?.progressed(id, completed: completed, total: total) }
        }
    }

    private func progressed(_ id: UUID, completed: Int64, total: Int64) {
        guard observations[id] != nil else { return }
        let now = ProcessInfo.processInfo.systemUptime
        var meter = meters[id] ?? TransferMeter()
        meter.record(completed, total: total > 0 ? total : nil, at: now)
        meters[id] = meter
        // Progress can arrive for every network chunk; refresh the visible row a few times a second.
        guard now - (published[id] ?? 0) >= 0.25 else { return }
        published[id] = now
        transfers[id] = meter
        update(id) { $0.fraction = meter.fraction }
    }

    private func update(_ id: UUID, _ body: (inout DownloadRecord) -> Void) {
        guard let index = items.firstIndex(where: { $0.id == id }) else { return }
        body(&items[index])
    }

    private func persist() {
        guard canSave else { onError?(L("save_failed")); return }
        do { try BrowserFiles.save(items.filter { !$0.isPrivate }, name: "downloads.json") }
        catch { onError?(L("save_failed")) }
    }

    func download(_ download: WKDownload, decideDestinationUsing response: URLResponse,
                  suggestedFilename: String, completionHandler: @escaping (URL?) -> Void) {
        guard let (_, id) = active[ObjectIdentifier(download)] else { completionHandler(nil); return }
        guard pending[id] != nil else {
            // A resumed transfer can request its previously approved destination again.
            completionHandler(items.first { $0.id == id && !$0.name.isEmpty }?.fileURL)
            return
        }
        if images.contains(id), !PageImage.accepts(response) {
            completionHandler(nil)
            forget(id)
            onToast?(L("image_download_failed"))
            return
        }
        let name = DownloadName.sanitize(suggestedFilename)
        let begin: (Bool) -> Void = { [weak self] accepted in
            guard let self else { completionHandler(nil); return }
            let approved = self.pending.removeValue(forKey: id)
            guard accepted else { completionHandler(nil); self.forget(id); return }
            // The transfer may have failed while the prompt was open.
            guard var item = approved, self.active[ObjectIdentifier(download)] != nil else {
                completionHandler(nil)
                self.forget(id)
                self.onError?(L("download_failed"))
                return
            }
            item.name = name
            do {
                try FileManager.default.createDirectory(at: item.fileURL.deletingLastPathComponent(), withIntermediateDirectories: true)
            } catch {
                completionHandler(nil)
                self.forget(id)
                self.onError?(L("download_failed"))
                return
            }
            item.fraction = self.meters[id]?.fraction ?? 0
            self.items.insert(item, at: 0)
            self.persist()
            completionHandler(item.fileURL)
        }
        if let confirm = confirmations.removeValue(forKey: id) {
            let size = response.expectedContentLength
            confirm(DownloadRequest(name: name, size: size >= 0 ? size : nil), begin)
        } else { begin(true) }
    }

    func downloadDidFinish(_ download: WKDownload) {
        guard let (_, id) = active.removeValue(forKey: ObjectIdentifier(download)) else { return }
        update(id) { $0.state = "complete"; $0.fraction = 1 }
        forget(id)
        resumable.removeValue(forKey: id)
        persist()
    }

    func download(_ download: WKDownload, didFailWithError error: Error, resumeData: Data?) {
        guard let (_, id) = active.removeValue(forKey: ObjectIdentifier(download)) else { return }
        // Unapproved downloads have no record. A prompt still open reports the failure if accepted;
        // one that failed before it could ask, or an image download, says so now.
        guard pending[id] == nil else {
            let message = images.contains(id) ? L("image_download_failed")
                : confirmations[id] != nil ? L("download_failed") : nil
            forget(id)
            if let message { onToast?(message) }
            return
        }
        update(id) { $0.state = "failed" }
        forget(id)
        resumable[id] = resumeData
        persist()
    }

    /// Drops live tracking for a download without touching its record or file.
    private func forget(_ id: UUID) {
        observations.removeValue(forKey: id)
        confirmations.removeValue(forKey: id)
        pending.removeValue(forKey: id)
        images.remove(id)
        meters.removeValue(forKey: id)
        published.removeValue(forKey: id)
        transfers.removeValue(forKey: id)
        if let (key, value) = active.first(where: { $0.value.1 == id }) {
            active.removeValue(forKey: key)
            value.0.cancel(nil)
        }
    }

    func cancel(_ id: UUID) {
        observations.removeValue(forKey: id)
        confirmations.removeValue(forKey: id)
        transfers.removeValue(forKey: id)
        if let (key, value) = active.first(where: { $0.value.1 == id }) {
            active.removeValue(forKey: key)
            value.0.cancel { [weak self] data in
                guard let self, self.items.contains(where: { $0.id == id }) else { return }
                self.resumable[id] = data
            }
        }
        update(id) { if $0.state == "downloading" { $0.state = "cancelled" } }
        persist()
    }

    func canResume(_ id: UUID) -> Bool {
        // Without an approved destination the resumed transfer has nowhere to write.
        resumable[id] != nil && items.contains { $0.id == id && !$0.name.isEmpty && $0.state != "downloading" && $0.state != "complete" }
    }

    func retry(_ item: DownloadRecord, webView: WKWebView) {
        guard canResume(item.id), let data = resumable[item.id] else { return }
        update(item.id) { $0.state = "downloading" }
        webView.resumeDownload(fromResumeData: data) { [weak self] download in
            guard let self, self.items.contains(where: { $0.id == item.id && $0.state == "downloading" }) else {
                download.cancel(nil)
                return
            }
            self.track(download, id: item.id)
        }
        persist()
    }

    /// Deletes a record and its file. Returns whether a file was removed, or nil if deletion failed.
    @discardableResult func remove(_ id: UUID) -> Bool? {
        cancel(id)
        var fileDeleted = false
        if let item = items.first(where: { $0.id == id }), !item.name.isEmpty {
            fileDeleted = FileManager.default.fileExists(atPath: item.fileURL.path)
            do { try FileManager.default.removeItem(at: item.fileURL.deletingLastPathComponent()) }
            catch {
                if (error as NSError).code != NSFileNoSuchFileError { onError?(L("delete_failed")); return nil }
                fileDeleted = false
            }
        }
        items.removeAll { $0.id == id }
        resumable.removeValue(forKey: id)
        forget(id)
        persist()
        return fileDeleted
    }
}
