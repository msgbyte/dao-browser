import SwiftUI
import WebKit

@MainActor final class BrowserSession: NSObject, WKNavigationDelegate, WKUIDelegate, UIContextMenuInteractionDelegate {
    let webView: WKWebView
    private weak var model: BrowserModel?
    private let tab: BrowserTab
    private var observations: [NSKeyValueObservation] = []
    /// The image under the latest touch, reported by the page script before any long press is recognized.
    private var touchedImage: TouchedImage?
    private var menuLocation = CGPoint.zero
    private var needsRecovery = false
    private var recoveredAt: TimeInterval?
    /// Set until the main-frame decision of a load the user did not ask for: crash recovery or session restore.
    private var automaticLoad = false

    // An isolated world keeps page scripts away from the handler; they share only the DOM.
    private static let imageWorld = WKContentWorld.world(name: "DaoPageImage")
    private static let imageScript = """
    (() => {
      const post = body => webkit.messageHandlers.daoPageImage.postMessage(body);
      const properties = ['-webkit-touch-callout', '-webkit-user-select', '-webkit-user-drag'];
      let held = null;
      const release = () => {
        if (!held) return;
        for (const [name, value, priority] of held.saved) {
          if (value) held.image.style.setProperty(name, value, priority); else held.image.style.removeProperty(name);
        }
        held = null;
      };
      addEventListener('touchstart', event => {
        release();
        if (event.touches.length !== 1) return;
        const image = event.target instanceof HTMLImageElement ? event.target : null;
        const source = image ? image.currentSrc || image.src : '';
        // Pages that turn off the callout keep their own long press; huge inline images would cross on every touch.
        if (!source || source.length > 1048576 || getComputedStyle(image).getPropertyValue('-webkit-touch-callout') === 'none') {
          return post({});
        }
        // While the finger is down, WebKit's own selection, drag and link menu would race Dao's image menu.
        held = { image, saved: properties.map(name => [name, image.style.getPropertyValue(name), image.style.getPropertyPriority(name)]) };
        for (const name of properties) image.style.setProperty(name, 'none', 'important');
        const box = image.getBoundingClientRect(), view = visualViewport;
        // Frame-relative boxes cannot be placed in the web view, so only the top document reports one.
        const rect = window === top ? [(box.left - view.offsetLeft) * view.scale, (box.top - view.offsetTop) * view.scale,
          box.width * view.scale, box.height * view.scale] : null;
        post({ source, rect });
      }, { capture: true, passive: true });
      addEventListener('touchend', release, { capture: true, passive: true });
      addEventListener('touchcancel', release, { capture: true, passive: true });
    })();
    """

    init(tab: BrowserTab, model: BrowserModel, dataStore: WKWebsiteDataStore,
         configuration supplied: WKWebViewConfiguration? = nil) {
        self.tab = tab
        self.model = model
        let configuration = supplied ?? WKWebViewConfiguration()
        // Popup configurations carry the opener's controller, which already has this session's handler name.
        configuration.userContentController = WKUserContentController()
        configuration.userContentController.addUserScript(WKUserScript(source: Self.imageScript,
            injectionTime: .atDocumentStart, forMainFrameOnly: false, in: Self.imageWorld))
        configuration.websiteDataStore = dataStore
        configuration.allowsInlineMediaPlayback = true
        configuration.preferences.javaScriptCanOpenWindowsAutomatically = false
        webView = WKWebView(frame: .zero, configuration: configuration)
        super.init()
        webView.configuration.userContentController.add(ImageTouchHandler(session: self), contentWorld: Self.imageWorld, name: "daoPageImage")
        webView.navigationDelegate = self
        webView.uiDelegate = self
        webView.allowsBackForwardNavigationGestures = true
        webView.isFindInteractionEnabled = true
        // WebKit asks its UI delegate only about links, so page images get their menu from this interaction.
        webView.addInteraction(UIContextMenuInteraction(delegate: self))
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
            automaticLoad = true
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
        webView.configuration.userContentController.removeAllScriptMessageHandlers()
        webView.navigationDelegate = nil
        webView.uiDelegate = nil
        webView.stopLoading()
        webView.removeFromSuperview()
    }

    @objc private func reload() { webView.reload() }

    func webView(_ webView: WKWebView, decidePolicyFor action: WKNavigationAction,
                 decisionHandler: @escaping (WKNavigationActionPolicy) -> Void) {
        guard let url = action.request.url else { decisionHandler(.cancel); return }
        if action.targetFrame?.isMainFrame == true, automaticLoad {
            automaticLoad = false
            // Reloading a page that came from a form would send the form again without the user asking.
            // WebKit reports these loads as "other" rather than form resubmissions, so check the method.
            if action.request.httpMethod?.uppercased() == "POST" {
                decisionHandler(.cancel)
                showFailure(L("form_not_resent"))
                return
            }
        }
        let scheme = url.scheme?.lowercased() ?? ""
        let isSubframe = action.targetFrame?.isMainFrame == false
        // Subframes may render inline documents (srcdoc, data:) that WebKit handles itself.
        if Address.isWeb(url) || scheme == "about" || scheme == "blob" || (isSubframe && scheme == "data") {
            decisionHandler(action.shouldPerformDownload ? .download : .allow)
        } else {
            decisionHandler(.cancel)
            model?.openExternal(url, userInitiated: action.navigationType == .linkActivated, tabID: tab.id)
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
        if model?.selectedID == tab.id { model?.captureSelected() }
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
        // A failed provisional load leaves WebKit on the previous page (or none in a popup), so remember what failed.
        tab.failedURL = (error.userInfo[NSURLErrorFailingURLErrorKey] as? URL).flatMap { Address.isWeb($0) ? $0.absoluteString : nil }
        webView.scrollView.refreshControl?.endRefreshing()
        update()
    }

    func webViewWebContentProcessDidTerminate(_ webView: WKWebView) {
        // A page that keeps ending its process gets the error page instead of a reload loop.
        if let recoveredAt, ProcessInfo.processInfo.systemUptime - recoveredAt < 30 {
            needsRecovery = false
            showFailure(L("page_terminated"))
            return
        }
        // Background tabs reload when selected, so a memory warning does not reload every tab at once.
        needsRecovery = true
        if model?.selectedID == tab.id { recoverIfNeeded() }
    }

    /// Reloads the current page after iOS ended its content process; the scroll position is not kept.
    func recoverIfNeeded() {
        guard needsRecovery else { return }
        needsRecovery = false
        recoveredAt = ProcessInfo.processInfo.systemUptime
        automaticLoad = true
        tab.error = nil
        if webView.url != nil { webView.reload() } else { restore() }
    }

    /// Shows the failure view for the current page; Retry loads its address afresh.
    private func showFailure(_ message: String) {
        tab.error = message
        tab.failedURL = nil
        update()
    }

    fileprivate func touched(_ body: Any) {
        let info = body as? [String: Any]
        // The page reports CSS pixels; the page-scale preference enlarges each of them on screen.
        let zoom = CGAffineTransform(scaleX: webView.pageZoom, y: webView.pageZoom)
        touchedImage = (info?["source"] as? String).flatMap(PageImageSource.url).map { url in
            let rect = (info?["rect"] as? [Double]).flatMap { $0.count == 4 ? CGRect(x: $0[0], y: $0[1], width: $0[2], height: $0[3]) : nil }
            return TouchedImage(url: url, rect: rect?.applying(zoom))
        }
    }

    func webView(_ webView: WKWebView, contextMenuConfigurationForElement elementInfo: WKContextMenuElementInfo,
                 completionHandler: @escaping (UIContextMenuConfiguration?) -> Void) {
        // The page script normally keeps WebKit from asking about images; nil keeps its own link menu and preview.
        guard let image = touchedImage?.url else { completionHandler(nil); return }
        completionHandler(imageMenuConfiguration(image))
    }

    func contextMenuInteraction(_ interaction: UIContextMenuInteraction,
                                configurationForMenuAtLocation location: CGPoint) -> UIContextMenuConfiguration? {
        guard let touched = touchedImage else { return nil }
        menuLocation = location
        return imageMenuConfiguration(touched.url)
    }

    func contextMenuInteraction(_ interaction: UIContextMenuInteraction,
                                previewForHighlightingMenuWithConfiguration configuration: UIContextMenuConfiguration) -> UITargetedPreview? {
        // Lift only the image instead of a snapshot of the whole page; an unplaceable box lifts the touch area.
        let fallback = CGRect(x: menuLocation.x - 22, y: menuLocation.y - 22, width: 44, height: 44)
        let rect = touchedImage?.rect.flatMap { $0.contains(menuLocation) ? $0.intersection(webView.bounds) : nil } ?? fallback
        let parameters = UIPreviewParameters()
        parameters.visiblePath = UIBezierPath(roundedRect: rect, cornerRadius: 8)
        return UITargetedPreview(view: webView, parameters: parameters)
    }

    func contextMenuInteraction(_ interaction: UIContextMenuInteraction,
                                previewForDismissingMenuWithConfiguration configuration: UIContextMenuConfiguration) -> UITargetedPreview? {
        self.contextMenuInteraction(interaction, previewForHighlightingMenuWithConfiguration: configuration)
    }

    private func imageMenuConfiguration(_ image: URL) -> UIContextMenuConfiguration {
        UIContextMenuConfiguration(identifier: nil, previewProvider: nil) { [weak self] _ in
            guard let self, let model = self.model else { return nil }
            var actions = [
                UIAction(title: L("image_download"), image: UIImage(named: "lucide-download")) { [weak self] _ in
                    if let self { model.downloadImage(image, from: self) }
                },
                UIAction(title: L("image_copy"), image: UIImage(named: "lucide-copy")) { [weak self] _ in
                    if let self { model.copyImage(image, from: self) }
                },
                UIAction(title: L("image_preview"), image: UIImage(named: "lucide-eye")) { [weak self] _ in
                    if let self { model.previewImage(image, from: self) }
                }
            ]
            if PageImageSource.canOpenInTab(image) {
                let isPrivate = self.isPrivate
                actions.append(UIAction(title: L("image_open_in_new_tab"), image: UIImage(named: "lucide-square-plus")) { _ in
                    model.openInBackground(image, isPrivate: isPrivate)
                    model.showToast(L("image_opened_in_new_tab"))
                })
            }
            // data: URLs can hold a whole image; the header only needs their start.
            return UIMenu(title: String(image.absoluteString.prefix(300)), children: actions)
        }
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
        model.downloads.attach(download, isPrivate: tab.record.isPrivate,
                               page: webView.backForwardList.currentItem?.url) { [weak model, tab] request, completion in
            // A response can arrive after the user closed the tab; closing it is a decline.
            guard let model, !tab.closedByUser else { completion(false); return }
            // The file name is the alert title so it stands out; the question and size stay secondary.
            let size = request.size.map { ByteCountFormatter.string(fromByteCount: $0, countStyle: .file) }
            let message = [L("download_confirm"), size, L("download_disk_notice")].compactMap { $0 }.joined(separator: "\n")
            model.ask(WebPrompt(title: request.name, message: message, confirmTitle: L("download_action"),
                                tabID: tab.id) { accepted, _ in completion(accepted) })
        }
    }

    func webView(_ webView: WKWebView, createWebViewWith configuration: WKWebViewConfiguration,
                 for navigationAction: WKNavigationAction, windowFeatures: WKWindowFeatures) -> WKWebView? {
        guard let model, let url = navigationAction.request.url,
              Address.isWeb(url) || url.absoluteString == "about:blank" else { return nil }
        let newTab = model.addTab(isPrivate: tab.record.isPrivate, configuration: configuration)
        return model.session(for: newTab).webView
    }

    func webViewDidClose(_ webView: WKWebView) { model?.close(tab, byPage: true) }

    func webView(_ webView: WKWebView, runJavaScriptAlertPanelWithMessage message: String,
                 initiatedByFrame frame: WKFrameInfo, completionHandler: @escaping () -> Void) {
        guard let model else { completionHandler(); return }
        model.ask(WebPrompt(title: frame.securityOrigin.host, message: message, allowsCancel: false, tabID: tab.id) { _, _ in completionHandler() })
    }

    func webView(_ webView: WKWebView, runJavaScriptConfirmPanelWithMessage message: String,
                 initiatedByFrame frame: WKFrameInfo, completionHandler: @escaping (Bool) -> Void) {
        guard let model else { completionHandler(false); return }
        model.ask(WebPrompt(title: frame.securityOrigin.host, message: message, tabID: tab.id) { accepted, _ in completionHandler(accepted) })
    }

    func webView(_ webView: WKWebView, runJavaScriptTextInputPanelWithPrompt prompt: String, defaultText: String?,
                 initiatedByFrame frame: WKFrameInfo, completionHandler: @escaping (String?) -> Void) {
        guard let model else { completionHandler(nil); return }
        model.ask(WebPrompt(title: frame.securityOrigin.host, message: prompt, defaultText: defaultText ?? "", tabID: tab.id) { accepted, value in
            completionHandler(accepted ? value ?? "" : nil)
        })
    }

    func webView(_ webView: WKWebView, requestMediaCapturePermissionFor origin: WKSecurityOrigin,
                 initiatedByFrame frame: WKFrameInfo, type: WKMediaCaptureType,
                 decisionHandler: @escaping (WKPermissionDecision) -> Void) {
        decisionHandler(.prompt)
    }
}

private struct TouchedImage {
    let url: URL
    /// The image's frame in web view points, when it is in the top document.
    let rect: CGRect?
}

/// Forwards page-script messages without the controller retaining the session.
@MainActor private final class ImageTouchHandler: NSObject, WKScriptMessageHandler {
    private weak var session: BrowserSession?
    init(session: BrowserSession) { self.session = session }
    func userContentController(_ userContentController: WKUserContentController, didReceive message: WKScriptMessage) {
        session?.touched(message.body)
    }
}

struct BrowserSurface: UIViewRepresentable {
    let session: BrowserSession
    func makeUIView(context: Context) -> WKWebView { session.webView }
    func updateUIView(_ uiView: WKWebView, context: Context) {}
}
