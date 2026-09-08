import Observation
import SwiftData
import SwiftUI
import WebKit

enum BrowserPage: String { case browser, tabs, history, bookmarks, readingList, downloads, settings, about }

@MainActor @Observable final class BrowserTab: Identifiable {
    var record: TabRecord
    var progress = 0.0
    var isLoading = false
    var canGoBack = false
    var canGoForward = false
    var secure = false
    var error: String?
    var thumbnail: UIImage?
    // Popups own a live web view before they have a web URL, so they must not show the new-tab page.
    var isPopup = false
    nonisolated let id: UUID
    var title: String { record.title.isEmpty ? (record.url.isEmpty ? L("new_tab") : record.url) : record.title }
    init(_ record: TabRecord) { id = record.id; self.record = record }
}

struct WebPrompt: Identifiable {
    let id = UUID()
    var title: String
    var message: String
    var defaultText: String?
    var allowsCancel = true
    var completion: (Bool, String?) -> Void
}

@MainActor @Observable final class BrowserModel {
    var tabs: [BrowserTab]
    var selectedID: UUID
    var page = BrowserPage.browser
    var addressEditing = false
    var drawerOpen = false
    private(set) var prompts: [WebPrompt] = []
    var preferences: Preferences
    let downloads = DownloadStore()
    let container: ModelContainer
    @ObservationIgnored private var sessions: [UUID: BrowserSession] = [:]
    @ObservationIgnored private var privateData = WKWebsiteDataStore.nonPersistent()
    @ObservationIgnored private var saveTask: Task<Void, Never>?
    @ObservationIgnored private var canSaveSession = true

    var selected: BrowserTab { tabs.first { $0.id == selectedID } ?? tabs[0] }
    var currentSession: BrowserSession? { sessions[selectedID] }
    var prompt: WebPrompt? { prompts.first }

    init(container: ModelContainer) {
        self.container = container
        preferences = Preferences()
        var snapshot: SessionSnapshot?
        var restoreError: Error?
        do { snapshot = try BrowserFiles.read(SessionSnapshot.self, name: "session.json") }
        catch { restoreError = error }
        let restored = snapshot?.tabs.filter { !$0.isPrivate && ($0.url.isEmpty || URL(string: $0.url).map(Address.isWeb) == true) } ?? []
        let state = TabCollection(tabs: restored, selectedID: snapshot?.selectedID)
        tabs = state.tabs.map(BrowserTab.init)
        selectedID = state.selectedID
        if preferences.defaultPrivate {
            let tab = BrowserTab(TabRecord(isPrivate: true))
            tabs.append(tab)
            selectedID = tab.id
        }
        if restoreError != nil {
            // Preserve the unreadable archive for recovery instead of overwriting it.
            canSaveSession = BrowserFiles.quarantine("session.json")
            notify(L(canSaveSession ? "restore_failed" : "restore_failed_readonly"))
        }
        downloads.onError = { [weak self] in self?.notify($0) }
    }

    func session(for tab: BrowserTab, restore: Bool = true) -> BrowserSession {
        if let session = sessions[tab.id] { return session }
        let session = BrowserSession(tab: tab, model: self, dataStore: tab.record.isPrivate ? privateData : .default())
        sessions[tab.id] = session
        if restore { session.restore() }
        return session
    }

    func navigate(_ input: String) {
        guard let url = Address.resolve(input, engine: preferences.searchEngine) else {
            notify(L("invalid_address"))
            return
        }
        addressEditing = false
        drawerOpen = false
        page = .browser
        selected.error = nil
        let session = session(for: selected, restore: false)
        selected.record.url = url.absoluteString
        session.webView.load(URLRequest(url: url))
        scheduleSave()
    }

    func incoming(_ url: URL) {
        guard let target = Address.incoming(url) else { notify(L("invalid_address")); return }
        addTab(isPrivate: preferences.defaultPrivate)
        navigate(target.absoluteString)
    }

    @discardableResult func addTab(isPrivate: Bool? = nil, configuration: WKWebViewConfiguration? = nil) -> BrowserTab {
        captureSelected()
        let tab = BrowserTab(TabRecord(isPrivate: isPrivate ?? selected.record.isPrivate))
        tabs.append(tab)
        selectedID = tab.id
        if let configuration {
            tab.isPopup = true
            sessions[tab.id] = BrowserSession(tab: tab, model: self,
                dataStore: tab.record.isPrivate ? privateData : .default(), configuration: configuration)
        }
        page = .browser
        drawerOpen = false
        scheduleSave()
        return tab
    }

    func select(_ tab: BrowserTab) {
        captureSelected()
        selectedID = tab.id
        page = .browser
        addressEditing = false
        scheduleSave()
    }

    func close(_ tab: BrowserTab) {
        var state = TabCollection(tabs: tabs.map(\.record), selectedID: selectedID)
        state.close(tab.id)
        sessions.removeValue(forKey: tab.id)?.dispose()
        let existing = Dictionary(uniqueKeysWithValues: tabs.map { ($0.id, $0) })
        tabs = state.tabs.map { existing[$0.id] ?? BrowserTab($0) }
        selectedID = state.selectedID
        // Closing the last private tab leaves a fresh blank one behind, so check for live sessions instead.
        if tab.record.isPrivate, !sessions.values.contains(where: \.isPrivate) { privateData = .nonPersistent() }
        scheduleSave()
    }

    func home() {
        // Retire this navigation delegate before showing home; late callbacks cannot revive the URL.
        sessions.removeValue(forKey: selectedID)?.dispose()
        selected.isPopup = false
        selected.record.url = ""
        selected.record.title = ""
        selected.record.interactionState = nil
        selected.thumbnail = nil
        selected.error = nil
        selected.canGoBack = false
        selected.canGoForward = false
        drawerOpen = false
        page = .browser
        scheduleSave()
    }

    func open(_ destination: BrowserPage) {
        captureSelected()
        drawerOpen = false
        addressEditing = false
        page = destination
    }

    func captureSelected() {
        guard let session = currentSession, !selected.record.url.isEmpty else { return }
        let tab = selected
        let configuration = WKSnapshotConfiguration()
        configuration.snapshotWidth = 400
        session.webView.takeSnapshot(with: configuration) { [weak tab] image, _ in
            tab?.thumbnail = image
        }
    }

    func saveNow() {
        saveTask?.cancel()
        guard canSaveSession else { return }
        do {
            for tab in tabs where !tab.record.isPrivate {
                if let session = sessions[tab.id] {
                    // WebKit currently returns NSData; fall back to the URL if that representation changes.
                    tab.record.interactionState = session.webView.interactionState as? Data
                }
            }
            try BrowserFiles.save(SessionSnapshot(tabs: tabs.map(\.record), selectedID: selectedID), name: "session.json")
        } catch { notify(L("save_failed")) }
    }

    func scheduleSave() {
        saveTask?.cancel()
        saveTask = Task { [weak self] in
            try? await Task.sleep(for: .milliseconds(500))
            guard !Task.isCancelled else { return }
            self?.saveNow()
        }
    }

    func reduceMemory() {
        saveNow()
        for id in Array(sessions.keys) where id != selectedID {
            // Private sessions stay in memory so their back-forward state is never archived.
            guard let tab = tabs.first(where: { $0.id == id }), !tab.record.isPrivate else { continue }
            sessions.removeValue(forKey: id)?.dispose()
            tab.thumbnail = nil
            tab.isPopup = false
        }
    }

    func visited(_ tab: BrowserTab) {
        guard !tab.record.isPrivate, !tab.record.url.isEmpty else { return }
        saveEntry(url: tab.record.url, title: tab.title, kind: "history")
    }

    @discardableResult func saveEntry(url: String, title: String, kind: String, folder: String = "") -> Bool {
        let context = container.mainContext
        do {
            let query = FetchDescriptor<LibraryEntry>(predicate: #Predicate { $0.url == url && $0.kind == kind })
            if let entry = try context.fetch(query).first {
                entry.title = title
                entry.visitedAt = .now
                if kind != "history" { entry.folder = folder }
            } else { context.insert(LibraryEntry(url: url, title: title, kind: kind, folder: folder)) }
            try context.save()
            return true
        } catch { context.rollback(); notify(L("save_failed")); return false }
    }

    func bookmark(kind: String = "bookmarks") {
        guard !selected.record.url.isEmpty else { return }
        let saved = saveEntry(url: selected.record.url, title: selected.title, kind: kind)
        drawerOpen = false
        if saved { notify(L("saved")) }
    }

    func clearSiteData(completion: @escaping () -> Void) {
        for session in sessions.values { session.dispose() }
        sessions.removeAll()
        privateData = .nonPersistent()
        for tab in tabs { tab.record.interactionState = nil; tab.thumbnail = nil }
        WKWebsiteDataStore.default().removeData(ofTypes: WKWebsiteDataStore.allWebsiteDataTypes(), modifiedSince: .distantPast) { [weak self] in
            self?.notify(L("site_data_cleared"))
            self?.scheduleSave()
            completion()
        }
    }

    func applyPreferences() {
        for session in sessions.values { session.applyPreferences() }
    }

    func retryDownload(_ item: DownloadRecord) {
        // Resuming only needs a web view on the matching data store; never leave the Downloads page for it.
        let webView: WKWebView
        if let session = sessions.values.first(where: { $0.isPrivate == item.isPrivate }) {
            webView = session.webView
        } else if let tab = tabs.first(where: { $0.record.isPrivate == item.isPrivate }) {
            webView = session(for: tab).webView
        } else {
            let tab = BrowserTab(TabRecord(isPrivate: item.isPrivate))
            tabs.append(tab)
            webView = session(for: tab).webView
        }
        downloads.retry(item, webView: webView)
    }

    func ask(_ request: WebPrompt) {
        prompts.append(request)
    }

    func notify(_ text: String) {
        ask(WebPrompt(title: L("dao"), message: text, allowsCancel: false) { _, _ in })
    }

    func answer(_ accepted: Bool, text: String?) {
        guard !prompts.isEmpty else { return }
        prompts.removeFirst().completion(accepted, text)
    }

    func openExternal(_ url: URL, userInitiated: Bool) {
        let scheme = url.scheme?.lowercased() ?? ""
        guard userInitiated, !scheme.isEmpty,
              !["javascript", "data", "file", "about", "blob", "dao"].contains(scheme) else { return }
        ask(WebPrompt(title: L("open_external"), message: url.absoluteString) { [weak self] accepted, _ in
            guard accepted else { return }
            UIApplication.shared.open(url, options: [:]) { success in
                if !success { Task { @MainActor in self?.notify(L("external_failed")) } }
            }
        })
    }
}
