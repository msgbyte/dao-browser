import Foundation
import Observation
import WebKit

struct DownloadRecord: Identifiable, Codable {
    var id = UUID()
    var name = ""
    var source = ""
    var state = "downloading"
    var fraction = 0.0
    var isPrivate = false
    var date = Date.now
    var fileURL: URL {
        URL.documentsDirectory.appending(path: "Downloads").appending(path: id.uuidString)
            .appending(path: DownloadName.sanitize(name))
    }
}

@MainActor @Observable final class DownloadStore: NSObject, WKDownloadDelegate {
    var items: [DownloadRecord] = []
    @ObservationIgnored var onError: ((String) -> Void)?
    @ObservationIgnored private var active: [ObjectIdentifier: (WKDownload, UUID)] = [:]
    @ObservationIgnored private var observations: [UUID: NSKeyValueObservation] = [:]
    @ObservationIgnored private var confirmations: [UUID: (String, @escaping (Bool) -> Void) -> Void] = [:]
    @ObservationIgnored private var resumable: [UUID: Data] = [:]
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

    func attach(_ download: WKDownload, isPrivate: Bool,
                confirm: @escaping (String, @escaping (Bool) -> Void) -> Void) {
        var item = DownloadRecord()
        item.source = download.originalRequest?.url?.absoluteString ?? ""
        item.isPrivate = isPrivate
        items.insert(item, at: 0)
        confirmations[item.id] = confirm
        track(download, id: item.id)
    }

    private func track(_ download: WKDownload, id: UUID) {
        active[ObjectIdentifier(download)] = (download, id)
        download.delegate = self
        observations[id] = download.progress.observe(\.fractionCompleted, options: [.new]) { [weak self] progress, _ in
            let fraction = progress.fractionCompleted
            Task { @MainActor in self?.update(id) { $0.fraction = fraction } }
        }
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
        guard let confirm = confirmations.removeValue(forKey: id) else {
            // A resumed transfer can request its previously approved destination again.
            completionHandler(items.first { $0.id == id && !$0.name.isEmpty }?.fileURL)
            return
        }
        let name = DownloadName.sanitize(suggestedFilename)
        confirm(name) { [weak self] accepted in
            guard let self else { completionHandler(nil); return }
            guard accepted else { completionHandler(nil); self.remove(id); return }
            // The transfer may have failed or been cancelled while the prompt was open; keep that record.
            guard self.active[ObjectIdentifier(download)] != nil else { completionHandler(nil); return }
            self.update(id) { $0.name = name }
            guard let destination = self.items.first(where: { $0.id == id })?.fileURL else { completionHandler(nil); return }
            do {
                try FileManager.default.createDirectory(at: destination.deletingLastPathComponent(), withIntermediateDirectories: true)
                self.persist()
                completionHandler(destination)
            } catch { completionHandler(nil); self.onError?(L("download_failed")) }
        }
    }

    func downloadDidFinish(_ download: WKDownload) {
        guard let (_, id) = active.removeValue(forKey: ObjectIdentifier(download)) else { return }
        update(id) { $0.state = "complete"; $0.fraction = 1 }
        observations.removeValue(forKey: id)
        confirmations.removeValue(forKey: id)
        resumable.removeValue(forKey: id)
        persist()
    }

    func download(_ download: WKDownload, didFailWithError error: Error, resumeData: Data?) {
        guard let (_, id) = active.removeValue(forKey: ObjectIdentifier(download)) else { return }
        update(id) { $0.state = "failed" }
        observations.removeValue(forKey: id)
        confirmations.removeValue(forKey: id)
        resumable[id] = resumeData
        persist()
    }

    func cancel(_ id: UUID) {
        observations.removeValue(forKey: id)
        confirmations.removeValue(forKey: id)
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

    func retry(_ item: DownloadRecord, webView: WKWebView) {
        guard items.contains(where: { $0.id == item.id && $0.state != "downloading" && $0.state != "complete" }) else { return }
        // Without an approved destination the resumed transfer has nowhere to write.
        guard let data = resumable[item.id], !item.name.isEmpty else {
            // Original requests can be authenticated POSTs or blobs; do not replay them as anonymous GETs.
            onError?(L("download_retry_page"))
            return
        }
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

    func remove(_ id: UUID) {
        cancel(id)
        if let item = items.first(where: { $0.id == id }), !item.name.isEmpty {
            do { try FileManager.default.removeItem(at: item.fileURL.deletingLastPathComponent()) }
            catch {
                if (error as NSError).code != NSFileNoSuchFileError { onError?(L("delete_failed")); return }
            }
        }
        items.removeAll { $0.id == id }
        resumable.removeValue(forKey: id)
        confirmations.removeValue(forKey: id)
        persist()
    }
}
