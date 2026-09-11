import SwiftUI
import WebKit

struct BrowserView: View {
    @Bindable var model: BrowserModel
    @Environment(\.scenePhase) private var scenePhase
    @Environment(\.accessibilityReduceMotion) private var reduceMotion
    @State private var showScanner = false
    @State private var showSecurity = false
    @State private var showQR = false
    @State private var promptText = ""
    @State private var promptShown = false

    var body: some View {
        ZStack(alignment: .trailing) {
            Nova.background.ignoresSafeArea()
            destination
                .accessibilityHidden(model.drawerOpen)
            if model.drawerOpen {
                Color.black.opacity(0.25).ignoresSafeArea().onTapGesture { model.drawerOpen = false }
                    .accessibilityLabel(L("close_menu")).accessibilityAddTraits(.isButton)
                drawer.transition(.move(edge: .trailing))
            }
            if scenePhase != .active && (model.tabs.contains { $0.record.isPrivate } || model.downloads.items.contains { $0.isPrivate }) {
                Nova.background.ignoresSafeArea().overlay(Text(L("private_browsing")))
            }
        }
        .foregroundStyle(Nova.foreground)
        .tint(Nova.foreground)
        .animation(reduceMotion ? nil : Nova.animation, value: model.drawerOpen)
        .sheet(isPresented: $showScanner) { QRScanner { value in showScanner = false; model.navigate(value) } }
        .sheet(isPresented: $showQR) { QRCodeSheet(value: model.selected.record.url) }
        .sheet(isPresented: $showSecurity) { SecuritySheet(tab: model.selected, webView: model.currentSession?.webView) }
        .alert(model.prompt?.title ?? "", isPresented: $promptShown) {
            if model.prompt?.defaultText != nil { TextField(L("response"), text: $promptText) }
            if model.prompt?.allowsCancel == true { Button(L("cancel"), role: .cancel) { model.answer(false, text: nil) } }
            Button(L("ok")) { model.answer(true, text: promptText) }
        } message: { Text(model.prompt?.message ?? "") }
        .onChange(of: model.prompt?.id, initial: true) { _, id in
            promptText = model.prompt?.defaultText ?? ""
            // Re-arm presentation after SwiftUI dismisses the previous alert so queued prompts appear in turn.
            promptShown = false
            if id != nil { Task { @MainActor in promptShown = model.prompt != nil } }
        }
        .onChange(of: scenePhase) { _, phase in if phase != .active { model.saveNow() } }
        .onReceive(NotificationCenter.default.publisher(for: UIApplication.didReceiveMemoryWarningNotification)) { _ in model.reduceMemory() }
        .onChange(of: model.preferences.fontScale) { _, _ in model.applyPreferences() }
        .onChange(of: model.preferences.inspectable) { _, _ in model.applyPreferences() }
    }

    @ViewBuilder private var destination: some View {
        switch model.page {
        case .browser:
            if (model.selected.record.url.isEmpty && !model.selected.isPopup) || model.addressEditing {
                NewTabView(model: model, scan: { showScanner = true }).id(model.addressEditing)
            } else { browsing }
        case .tabs: TabsView(model: model)
        case .history, .bookmarks, .readingList: LibraryView(model: model, kind: model.page.rawValue)
        case .downloads: DownloadsView(model: model)
        case .settings: SettingsView(model: model)
        case .about: AboutView(model: model)
        }
    }

    private var browsing: some View {
        VStack(spacing: 0) {
            HStack(spacing: 4) {
                HStack(spacing: 0) {
                    IconButton(icon: model.selected.secure ? "lock-keyhole" : "info", label: "site_information") { showSecurity = true }
                    Button { model.addressEditing = true } label: {
                        Text(URL(string: model.selected.record.url)?.host ?? model.selected.record.url)
                            .font(.subheadline).lineLimit(1).frame(maxWidth: .infinity, alignment: .leading).frame(minHeight: 44)
                    }.buttonStyle(.plain).accessibilityLabel(L("edit_address"))
                    if model.selected.record.isPrivate { DaoIcon(name: "venetian-mask", size: 16).padding(.trailing, 8) }
                }
                .background(Nova.secondary, in: .rect(cornerRadius: 12))
                .overlay(alignment: .bottomLeading) {
                    if model.selected.isLoading {
                        GeometryReader { geometry in
                            Rectangle().fill(Nova.muted.opacity(0.4)).frame(width: geometry.size.width * model.selected.progress, height: 2)
                        }.frame(height: 2).padding(.horizontal, 10).accessibilityHidden(true)
                    }
                }
                Button { model.open(.tabs) } label: {
                    Text(model.tabs.count, format: .number).font(.caption.weight(.semibold))
                        .frame(width: 23, height: 23).overlay(RoundedRectangle(cornerRadius: 6).stroke(lineWidth: 1.5))
                        .frame(width: 44, height: 44)
                }.buttonStyle(.plain).accessibilityLabel(L("tabs"))
                IconButton(icon: "menu", label: "menu") { model.drawerOpen = true }
            }.padding(.horizontal, 12).padding(.vertical, 10)
            ZStack {
                let session = model.session(for: model.selected)
                BrowserSurface(session: session).id(ObjectIdentifier(session.webView))
                if let error = model.selected.error {
                    Nova.background.overlay {
                        VStack(spacing: 16) {
                            DaoIcon(name: "globe", size: 36)
                            Text(L("page_failed")).font(.headline)
                            Text(error).font(.subheadline).multilineTextAlignment(.center).foregroundStyle(Nova.muted)
                            Button(L("retry")) { model.navigate(model.selected.record.url) }.buttonStyle(.bordered)
                        }.padding(24)
                    }
                }
            }
        }
        .ignoresSafeArea(.container, edges: .bottom)
    }

    private var drawer: some View {
        VStack(spacing: 8) {
            HStack {
                IconButton(icon: "arrow-left", label: "back") { model.currentSession?.webView.goBack(); model.drawerOpen = false }
                    .disabled(!model.selected.canGoBack)
                IconButton(icon: "arrow-right", label: "forward") { model.currentSession?.webView.goForward(); model.drawerOpen = false }
                    .disabled(!model.selected.canGoForward)
                IconButton(icon: model.selected.isLoading ? "x" : "rotate-cw", label: model.selected.isLoading ? "stop" : "reload") {
                    if model.selected.isLoading { model.currentSession?.webView.stopLoading() } else { model.currentSession?.webView.reload() }
                    model.drawerOpen = false
                }
                Spacer()
                IconButton(icon: "x", label: "close_menu") { model.drawerOpen = false }
            }.padding(.horizontal, 8)
            ScrollView {
                VStack(spacing: 0) {
                    LazyVGrid(columns: Array(repeating: GridItem(.flexible(), spacing: 10), count: 3), spacing: 10) {
                        Button(action: model.home) { drawerTile("house", "home") }
                        Button { model.bookmark() } label: { drawerTile("bookmark-plus", "bookmark_page") }
                        Button { model.bookmark(kind: "readingList") } label: { drawerTile("book-open", "read_later") }
                        if let url = URL(string: model.selected.record.url) {
                            ShareLink(item: url) { drawerTile("share-2", "share") }
                        }
                        Button { model.drawerOpen = false; showQR = true } label: { drawerTile("qr-code", "qr_code") }
                        Button {
                            model.drawerOpen = false
                            model.currentSession?.webView.findInteraction?.presentFindNavigator(showingReplace: false)
                        } label: { drawerTile("search", "find_in_page") }
                    }.buttonStyle(.plain).padding(.horizontal, 16).padding(.bottom, 14)
                    Divider().padding(.horizontal, 16)
                    NovaRow(icon: "history", title: L("history")) { model.open(.history) }
                    NovaRow(icon: "bookmark", title: L("bookmarks")) { model.open(.bookmarks) }
                    NovaRow(icon: "book-open", title: L("readingList")) { model.open(.readingList) }
                    NovaRow(icon: "download", title: L("downloads")) { model.open(.downloads) }
                    NovaRow(icon: "settings", title: L("settings")) { model.open(.settings) }
                }
            }
        }.frame(width: 300).frame(maxHeight: .infinity).background(Nova.surface)
    }

    private func drawerTile(_ icon: String, _ label: String) -> some View {
        VStack(spacing: 10) {
            DaoIcon(name: icon, size: 22)
            Text(L(label)).font(.caption.weight(.medium)).multilineTextAlignment(.center).lineLimit(2)
        }
        .padding(6).frame(maxWidth: .infinity, minHeight: 88)
        .background(Nova.secondary, in: .rect(cornerRadius: 13))
        .overlay(RoundedRectangle(cornerRadius: 13).stroke(Nova.border, lineWidth: 1))
        .contentShape(.rect(cornerRadius: 13))
    }
}
