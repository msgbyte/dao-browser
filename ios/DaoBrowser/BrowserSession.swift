import SwiftUI
import WebKit

@MainActor final class BrowserSession: NSObject, WKNavigationDelegate, WKUIDelegate {
    let webView: WKWebView
    private weak var model: BrowserModel?
    private let tab: BrowserTab
    private var observations: [NSKeyValueObservation] = []

    init(tab: BrowserTab, model: BrowserModel, dataStore: WKWebsiteDataStore,
         configuration supplied: WKWebViewConfiguration? = nil) {
        self.tab = tab
        self.model = model
        let configuration = supplied ?? WKWebViewConfiguration()
        configuration.websiteDataStore = dataStore
        configuration.allowsInlineMediaPlayback = true
        configuration.preferences.javaScriptCanOpenWindowsAutomatically = false
        webView = WKWebView(frame: .zero, configuration: configuration)
        super.init()
        webView.navigationDelegate = self
        webView.uiDelegate = self
        webView.allowsBackForwardNavigationGestures = true
        webView.isFindInteractionEnabled = true
        webView.scrollView.keyboardDismissMode = .onDrag
        let refresh = UIRefreshControl()
        refresh.addTarget(self, action: #selector(reload), for: .valueChanged)
        webView.scrollView.refreshControl = refresh
        observations = [
            webView.observe(\.url, options: [.new]) { [weak self] _, _ in self?.updateLater() },
            webView.observe(\.title, options: [.new]) { [weak self] _, _ in self?.updateLater() },
            webView.observe(\.estimatedProgress, options: [.new]) { [weak self] _, _ in self?.updateLater() },
            webView.observe(\.isLoading, options: [.new]) { [weak self] _, _ in self?.updateLater() },
            webView.observe(\.canGoBack, options: [.new]) { [weak self] _, _ in self?.updateLater() },
            webView.observe(\.canGoForward, options: [.new]) { [weak self] _, _ in self?.updateLater() }
        ]
        applyPreferences()
    }

    nonisolated private func updateLater() {
        Task { @MainActor [weak self] in self?.update() }
    }

    var isPrivate: Bool { tab.record.isPrivate }

    private func update() {
        guard model != nil else { return }
        let previous = (tab.record.url, tab.record.title)
        if let url = webView.url, Address.isWeb(url) { tab.record.url = url.absoluteString }
        tab.record.title = webView.title ?? ""
        tab.progress = webView.estimatedProgress
        tab.isLoading = webView.isLoading
        tab.canGoBack = webView.canGoBack
        tab.canGoForward = webView.canGoForward
        tab.secure = webView.hasOnlySecureContent && webView.url?.scheme == "https"
        if !webView.isLoading { webView.scrollView.refreshControl?.endRefreshing() }
        if previous != (tab.record.url, tab.record.title) { model?.scheduleSave() }
    }

    func restore() {
        guard let url = URL(string: tab.record.url), Address.isWeb(url) else { return }
        if let data = tab.record.interactionState {
            webView.interactionState = data
            if webView.url != nil { return }
        }
        webView.load(URLRequest(url: url))
    }

    func applyPreferences() {
        guard let preferences = model?.preferences else { return }
        webView.pageZoom = preferences.fontScale
        webView.isInspectable = preferences.inspectable && !tab.record.isPrivate
    }

    func dispose() {
        model = nil
        observations.removeAll()
        webView.navigationDelegate = nil
        webView.uiDelegate = nil
        webView.stopLoading()
        webView.removeFromSuperview()
    }

    @objc private func reload() { webView.reload() }

    func webView(_ webView: WKWebView, decidePolicyFor action: WKNavigationAction,
                 decisionHandler: @escaping (WKNavigationActionPolicy) -> Void) {
        guard let url = action.request.url else { decisionHandler(.cancel); return }
        let scheme = url.scheme?.lowercased() ?? ""
        let isSubframe = action.targetFrame?.isMainFrame == false
        // Subframes may render inline documents (srcdoc, data:) that WebKit handles itself.
        if Address.isWeb(url) || scheme == "about" || scheme == "blob" || (isSubframe && scheme == "data") {
            decisionHandler(action.shouldPerformDownload ? .download : .allow)
        } else {
            decisionHandler(.cancel)
            model?.openExternal(url, userInitiated: action.navigationType == .linkActivated)
        }
    }

    func webView(_ webView: WKWebView, decidePolicyFor response: WKNavigationResponse,
                 decisionHandler: @escaping (WKNavigationResponsePolicy) -> Void) {
        decisionHandler(response.canShowMIMEType ? .allow : .download)
    }

    func webView(_ webView: WKWebView, didFinish navigation: WKNavigation!) {
        tab.error = nil
        update()
        model?.visited(tab)
        model?.scheduleSave()
    }

    func webView(_ webView: WKWebView, didStartProvisionalNavigation navigation: WKNavigation!) {
        tab.error = nil
        update()
    }

    func webView(_ webView: WKWebView, didFailProvisionalNavigation navigation: WKNavigation!, withError error: Error) {
        failed(error)
    }

    func webView(_ webView: WKWebView, didFail navigation: WKNavigation!, withError error: Error) { failed(error) }

    private func failed(_ error: Error) {
        let error = error as NSError
        if error.code == NSURLErrorCancelled { return }
        // WebKitErrorFrameLoadInterruptedByPolicyChange: the navigation became a download, not a failure.
        if error.domain == "WebKitErrorDomain", error.code == 102 { return }
        tab.error = error.localizedDescription
        webView.scrollView.refreshControl?.endRefreshing()
        update()
    }

    func webViewWebContentProcessDidTerminate(_ webView: WKWebView) {
        tab.error = L("page_terminated")
        update()
    }

    func webView(_ webView: WKWebView, navigationAction: WKNavigationAction, didBecome download: WKDownload) {
        accept(download)
    }

    func webView(_ webView: WKWebView, navigationResponse: WKNavigationResponse, didBecome download: WKDownload) {
        accept(download)
    }

    private func accept(_ download: WKDownload) {
        guard let model else { download.cancel(nil); return }
        // Every download asks before writing to Files, including private and script-triggered downloads.
        model.downloads.attach(download, isPrivate: tab.record.isPrivate) { [weak model] filename, completion in
            guard let model else { completion(false); return }
            model.ask(WebPrompt(title: L("download_confirm"),
                message: filename + "\n" + L("download_disk_notice")) { accepted, _ in completion(accepted) })
        }
    }

    func webView(_ webView: WKWebView, createWebViewWith configuration: WKWebViewConfiguration,
                 for navigationAction: WKNavigationAction, windowFeatures: WKWindowFeatures) -> WKWebView? {
        guard let model, let url = navigationAction.request.url,
              Address.isWeb(url) || url.absoluteString == "about:blank" else { return nil }
        let newTab = model.addTab(isPrivate: tab.record.isPrivate, configuration: configuration)
        return model.session(for: newTab).webView
    }

    func webViewDidClose(_ webView: WKWebView) { model?.close(tab) }

    func webView(_ webView: WKWebView, runJavaScriptAlertPanelWithMessage message: String,
                 initiatedByFrame frame: WKFrameInfo, completionHandler: @escaping () -> Void) {
        guard let model else { completionHandler(); return }
        model.ask(WebPrompt(title: frame.securityOrigin.host, message: message, allowsCancel: false) { _, _ in completionHandler() })
    }

    func webView(_ webView: WKWebView, runJavaScriptConfirmPanelWithMessage message: String,
                 initiatedByFrame frame: WKFrameInfo, completionHandler: @escaping (Bool) -> Void) {
        guard let model else { completionHandler(false); return }
        model.ask(WebPrompt(title: frame.securityOrigin.host, message: message) { accepted, _ in completionHandler(accepted) })
    }

    func webView(_ webView: WKWebView, runJavaScriptTextInputPanelWithPrompt prompt: String, defaultText: String?,
                 initiatedByFrame frame: WKFrameInfo, completionHandler: @escaping (String?) -> Void) {
        guard let model else { completionHandler(nil); return }
        model.ask(WebPrompt(title: frame.securityOrigin.host, message: prompt, defaultText: defaultText ?? "") { accepted, value in
            completionHandler(accepted ? value ?? "" : nil)
        })
    }

    func webView(_ webView: WKWebView, requestMediaCapturePermissionFor origin: WKSecurityOrigin,
                 initiatedByFrame frame: WKFrameInfo, type: WKMediaCaptureType,
                 decisionHandler: @escaping (WKPermissionDecision) -> Void) {
        decisionHandler(.prompt)
    }
}

struct BrowserSurface: UIViewRepresentable {
    let session: BrowserSession
    func makeUIView(context: Context) -> WKWebView { session.webView }
    func updateUIView(_ uiView: WKWebView, context: Context) {}
}
