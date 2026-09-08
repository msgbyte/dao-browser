import SwiftUI
import WebKit

struct SettingsView: View {
    let model: BrowserModel
    @State private var clearing = false
    @State private var busy = false

    var body: some View {
        @Bindable var preferences = model.preferences
        UtilityPage(title: "settings", back: { model.page = .browser }) {
            ScrollView {
                VStack(spacing: 20) {
                    VStack(spacing: 18) {
                        LabeledContent(L("theme")) {
                            Picker(selection: $preferences.theme) {
                                Text(L("system")).tag("system")
                                Text(L("light")).tag("light")
                                Text(L("dark")).tag("dark")
                            } label: { EmptyView() }
                        }
                        LabeledContent(L("search_engine")) {
                            Picker(selection: $preferences.searchEngine) {
                                ForEach(SearchEngine.allCases, id: \.self) { Text(L("search_" + $0.rawValue)).tag($0) }
                            } label: { EmptyView() }
                        }
                        VStack(alignment: .leading) {
                            HStack { Text(L("page_scale")); Spacer(); Text(preferences.fontScale, format: .percent.precision(.fractionLength(0))) }
                            Slider(value: $preferences.fontScale, in: 0.75...1.5, step: 0.05).accessibilityLabel(L("page_scale"))
                        }
                    }.padding(16).background(Nova.surface, in: .rect(cornerRadius: 16))
                    VStack(alignment: .leading, spacing: 16) {
                        Toggle(L("default_private"), isOn: $preferences.defaultPrivate)
                        Text(L("private_notice")).font(.caption).foregroundStyle(Nova.muted)
                        Toggle(L("web_inspector"), isOn: $preferences.inspectable)
                        Text(L("inspector_notice")).font(.caption).foregroundStyle(Nova.muted)
                    }.padding(16).background(Nova.surface, in: .rect(cornerRadius: 16))
                    VStack(spacing: 0) {
                        NovaRow(icon: "shield-check", title: L("clear_site_data"), subtitle: L("clear_site_data_hint")) { clearing = true }.disabled(busy)
                        NovaRow(icon: "info", title: L("about")) { model.open(.about) }
                    }.background(Nova.surface, in: .rect(cornerRadius: 16))
                }.padding(16)
            }
        }
        .disabled(busy)
        .alert(L("clear_site_data"), isPresented: $clearing) {
            Button(L("cancel"), role: .cancel) {}
            Button(L("clear"), role: .destructive) {
                busy = true
                model.clearSiteData { busy = false }
            }
        } message: { Text(L("clear_site_data_notice")) }
    }
}

struct AboutView: View {
    let model: BrowserModel
    @State private var licenses = false
    var body: some View {
        UtilityPage(title: "about", back: { model.page = .settings }) {
            ScrollView {
                VStack(spacing: 16) {
                    Image("DaoLogo").resizable().scaledToFit().frame(width: 80, height: 80).padding(.top, 30)
                    Text(L("dao")).font(.title.bold())
                    Text(Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? "").foregroundStyle(Nova.muted)
                    Text(L("about_description")).multilineTextAlignment(.center).padding(.horizontal, 24)
                    NovaRow(icon: "globe", title: L("website")) { model.navigate("https://dao.msgbyte.com/") }
                    NovaRow(icon: "code", title: L("source_code")) { model.navigate("https://github.com/msgbyte/dao-browser") }
                    NovaRow(icon: "file-text", title: L("open_source_licenses")) { licenses = true }
                }
            }
        }.sheet(isPresented: $licenses) {
            NavigationStack {
                ScrollView { Text(licenseText).font(.caption.monospaced()).textSelection(.enabled).padding(20) }
                    .navigationTitle(L("open_source_licenses"))
                    .toolbar { ToolbarItem(placement: .confirmationAction) { Button(L("done")) { licenses = false } } }
            }
        }
    }
    private var licenseText: String {
        guard let url = Bundle.main.url(forResource: "Licenses", withExtension: "txt"),
              let text = try? String(contentsOf: url, encoding: .utf8) else { return L("file_missing") }
        return text
    }
}
