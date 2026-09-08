import SwiftData
import SwiftUI

struct NewTabView: View {
    @Bindable var model: BrowserModel
    let scan: () -> Void
    @Query(sort: \LibraryEntry.visitedAt, order: .reverse) private var entries: [LibraryEntry]
    @Environment(\.accessibilityReduceMotion) private var reduceMotion
    @State private var query = ""
    @State private var expanded = false
    @FocusState private var focused: Bool

    private var results: [LibraryEntry] {
        var seen = Set<String>()
        return entries.filter {
            (query.isEmpty || $0.title.localizedCaseInsensitiveContains(query) || $0.url.localizedCaseInsensitiveContains(query))
                && seen.insert($0.url).inserted
        }.prefix(12).map { $0 }
    }

    var body: some View {
        GeometryReader { geometry in
            VStack(spacing: 0) {
                VStack(spacing: 0) {
                    HStack {
                        if model.selected.record.isPrivate {
                            DaoIcon(name: "venetian-mask").padding(.leading, 20).accessibilityLabel(L("private_browsing"))
                        }
                        Spacer()
                        IconButton(icon: "settings", label: "settings") { model.open(.settings) }
                    }.padding(12)
                    VStack(spacing: 9) {
                        Image("DaoLogo").resizable().scaledToFit().frame(width: 64, height: 64).padding(.bottom, 13)
                            .accessibilityLabel(L("dao"))
                        Text(Date.now, format: .dateTime.year().month().day()).font(.system(size: 11, design: .monospaced))
                            .tracking(1.5).foregroundStyle(Nova.muted)
                        Text(L(greeting)).font(.title2.weight(.semibold))
                        Text(L("where_next")).font(.title2.weight(.semibold)).foregroundStyle(Nova.muted)
                    }.padding(.top, max(12, geometry.size.height * 0.15))
                }
                .fixedSize(horizontal: false, vertical: true)
                .frame(height: expanded ? 0 : nil, alignment: .top).clipped()
                .opacity(expanded ? 0 : 1).allowsHitTesting(!expanded).accessibilityHidden(expanded)
                HStack(spacing: 4) {
                    DaoIcon(name: "search").foregroundStyle(Nova.muted).padding(.leading, 18)
                    TextField(L("search_placeholder"), text: $query,
                              prompt: Text(L("search_placeholder")).foregroundStyle(Nova.muted))
                        .focused($focused).submitLabel(.go)
                        .keyboardType(.webSearch).textInputAutocapitalization(.never).autocorrectionDisabled()
                        .onSubmit { model.navigate(query) }.accessibilityIdentifier("address-input")
                        .allowsHitTesting(expanded).accessibilityHidden(!expanded)
                        .frame(minHeight: 56)
                        .overlay {
                            if !expanded {
                                Button(action: activate) { Color.clear.contentShape(Rectangle()) }
                                    .buttonStyle(.plain).accessibilityLabel(L("search_placeholder"))
                                    .accessibilityIdentifier("search-open")
                            }
                        }
                    ZStack(alignment: .trailing) {
                        IconButton(icon: "x", label: query.isEmpty ? "cancel" : "clear") {
                            if !query.isEmpty { query = "" }
                            else {
                                focused = false
                                withAnimation(reduceMotion ? nil : Nova.animation) { expanded = false }
                                model.addressEditing = false
                            }
                        }
                        .opacity(expanded ? 1 : 0).allowsHitTesting(expanded).accessibilityHidden(!expanded)
                        HStack(spacing: 4) {
                            IconButton(icon: "scan-line", label: "scan_qr", action: scan)
                            Button { model.open(.tabs) } label: {
                                Text(model.tabs.count, format: .number).font(.caption.weight(.semibold))
                                    .frame(width: 22, height: 22).overlay(RoundedRectangle(cornerRadius: 6).stroke(lineWidth: 1.5))
                                    .frame(width: 44, height: 44)
                            }.buttonStyle(.plain).accessibilityLabel(L("tabs"))
                        }.opacity(expanded ? 0 : 1).allowsHitTesting(!expanded).accessibilityHidden(expanded)
                    }.frame(width: expanded ? 44 : 92, alignment: .trailing)
                }.frame(minHeight: 56).background(Nova.surface, in: .rect(cornerRadius: 28))
                    .overlay(RoundedRectangle(cornerRadius: 28).stroke(Nova.border, lineWidth: 1))
                    .padding(.horizontal, 20).padding(.top, expanded ? 16 : 32)
                ScrollView {
                    VStack(spacing: 0) {
                        if !query.isEmpty {
                            NovaRow(icon: "search", title: query, subtitle: L("search_" + model.preferences.searchEngine.rawValue)) { model.navigate(query) }
                        }
                        ForEach(results) { entry in
                            NovaRow(icon: entry.kind == "history" ? "history" : "bookmark", title: entry.title, subtitle: entry.url) {
                                model.navigate(entry.url)
                            }
                        }
                    }.padding(.top, 16)
                }.scrollDismissesKeyboard(.interactively)
                    .opacity(expanded ? 1 : 0).allowsHitTesting(expanded).accessibilityHidden(!expanded)
            }
        }.onAppear {
            if model.addressEditing {
                query = model.selected.record.url
                expanded = true
                focused = true
            }
        }
    }

    private var greeting: String {
        let hour = Calendar.current.component(.hour, from: .now)
        return hour < 12 ? "good_morning" : hour < 18 ? "good_afternoon" : "good_evening"
    }

    private func activate() {
        withAnimation(reduceMotion ? nil : Nova.animation) { expanded = true } completion: {
            if expanded { focused = true }
        }
    }
}

struct TabsView: View {
    @Bindable var model: BrowserModel
    var body: some View {
        UtilityPage(title: "tabs", back: { model.page = .browser }) {
            ScrollView {
                LazyVGrid(columns: [GridItem(.flexible()), GridItem(.flexible())], spacing: 10) {
                    ForEach(model.tabs) { tab in
                        VStack(spacing: 0) {
                            HStack(spacing: 4) {
                                if tab.record.isPrivate { DaoIcon(name: "venetian-mask", size: 16) }
                                Text(tab.title).font(.caption.weight(.medium)).lineLimit(1)
                                Spacer(minLength: 0)
                                IconButton(icon: "x", label: "close_tab") { model.close(tab) }
                            }.padding(.leading, 10)
                            Button { model.select(tab) } label: {
                                ZStack {
                                    Nova.secondary
                                    if let image = tab.thumbnail { Image(uiImage: image).resizable().scaledToFill() }
                                    else { Image("DaoLogo").resizable().scaledToFit().frame(width: 40, height: 40) }
                                }.frame(height: 190).clipped().contentShape(Rectangle())
                            }.buttonStyle(.plain).accessibilityLabel(tab.title)
                        }.background(Nova.surface).clipShape(.rect(cornerRadius: 16))
                            .overlay(RoundedRectangle(cornerRadius: 16).stroke(tab.id == model.selectedID ? Nova.foreground : Nova.border, lineWidth: tab.id == model.selectedID ? 2 : 1))
                            .simultaneousGesture(DragGesture(minimumDistance: 30).onEnded { value in
                                if value.translation.width > 85 && abs(value.translation.height) < 60 { model.close(tab) }
                            })
                            .accessibilityAction(named: L("close_tab")) { model.close(tab) }
                    }
                }.padding(12)
            }
            HStack {
                Button { model.addTab(isPrivate: false) } label: { HStack { DaoIcon(name: "plus"); Text(L("new_tab")) } }
                Spacer()
                Button { model.addTab(isPrivate: true) } label: { HStack { DaoIcon(name: "venetian-mask"); Text(L("private_tab")) } }
            }.font(.subheadline).padding(20).background(Nova.surface)
        }
    }
}
