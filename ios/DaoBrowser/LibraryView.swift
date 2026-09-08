import SwiftData
import SwiftUI

struct LibraryView: View {
    let model: BrowserModel
    let kind: String
    @Environment(\.modelContext) private var context
    @Query(sort: \LibraryEntry.visitedAt, order: .reverse) private var entries: [LibraryEntry]
    @State private var search = ""
    @State private var folder = ""
    @State private var editing: LibraryEntry?
    @State private var clearing = false

    private var folders: [String] { Array(Set(entries.filter { $0.kind == kind }.map(\.folder).filter { !$0.isEmpty })).sorted() }
    private var visible: [LibraryEntry] {
        entries.filter { $0.kind == kind && (folder.isEmpty || $0.folder == folder) &&
            (search.isEmpty || $0.title.localizedCaseInsensitiveContains(search) || $0.url.localizedCaseInsensitiveContains(search)) }
    }

    var body: some View {
        UtilityPage(title: kind, back: { model.page = .browser }) {
            HStack {
                DaoIcon(name: "search").foregroundStyle(Nova.muted)
                TextField(L("search_library"), text: $search).textInputAutocapitalization(.never)
                if kind == "history" {
                    IconButton(icon: "trash-2", label: "clear_history") { clearing = true }
                        .disabled(!entries.contains { $0.kind == "history" })
                }
            }.padding(.horizontal, 16).frame(minHeight: 48).background(Nova.secondary, in: .rect(cornerRadius: 14)).padding(.horizontal, 16)
            if !folders.isEmpty {
                ScrollView(.horizontal, showsIndicators: false) {
                    HStack {
                        folderButton("", title: L("all"))
                        ForEach(folders, id: \.self) { folderButton($0, title: $0) }
                    }.padding(16)
                }
            }
            if visible.isEmpty {
                Spacer()
                Text(L("library_empty")).foregroundStyle(Nova.muted).padding()
                Spacer()
            } else {
                ScrollView {
                    LazyVStack(spacing: 0) {
                        ForEach(visible) { entry in
                            HStack(spacing: 0) {
                                NovaRow(icon: kind == "history" ? "history" : kind == "readingList" ? "book-open" : "bookmark",
                                        title: entry.title.isEmpty ? entry.url : entry.title, subtitle: entry.url) { model.navigate(entry.url) }
                                Menu {
                                    if kind != "history" { Button(L("edit")) { editing = entry } }
                                    Button(L("open_new_tab")) { model.addTab(); model.navigate(entry.url) }
                                    Button(L("delete"), role: .destructive) { delete(entry) }
                                } label: { DaoIcon(name: "ellipsis").frame(width: 44, height: 44) }
                                    .accessibilityLabel(L("more"))
                            }
                            Divider().padding(.leading, 66)
                        }
                    }.padding(.vertical, 8)
                }
            }
        }
        .sheet(item: $editing) { entry in BookmarkEditor(entry: entry, onError: { model.notify($0) }) }
        .alert(L("clear_history"), isPresented: $clearing) {
            Button(L("cancel"), role: .cancel) {}
            Button(L("clear"), role: .destructive) {
                for entry in entries where entry.kind == "history" { context.delete(entry) }
                save()
            }
        } message: { Text(L("clear_history_notice")) }
    }

    private func folderButton(_ value: String, title: String) -> some View {
        Button { folder = value } label: {
            Text(title).font(.subheadline).padding(.horizontal, 14).padding(.vertical, 12)
                .background(folder == value ? Nova.foreground : Nova.secondary, in: .capsule)
                .foregroundStyle(folder == value ? Nova.background : Nova.foreground)
        }.buttonStyle(.plain).accessibilityAddTraits(folder == value ? .isSelected : [])
    }

    private func delete(_ entry: LibraryEntry) { context.delete(entry); save() }
    private func save() {
        do { try context.save() }
        catch { context.rollback(); model.notify(L("save_failed")) }
    }
}

private struct BookmarkEditor: View {
    let entry: LibraryEntry
    let onError: (String) -> Void
    @Environment(\.dismiss) private var dismiss
    @Environment(\.modelContext) private var context
    @State private var title: String
    @State private var url: String
    @State private var folder: String
    @State private var invalid = false

    init(entry: LibraryEntry, onError: @escaping (String) -> Void) {
        self.entry = entry
        self.onError = onError
        _title = State(initialValue: entry.title)
        _url = State(initialValue: entry.url)
        _folder = State(initialValue: entry.folder)
    }

    var body: some View {
        NavigationStack {
            VStack(spacing: 20) {
                TextField(L("title"), text: $title)
                TextField(L("address"), text: $url).keyboardType(.URL).textInputAutocapitalization(.never).autocorrectionDisabled()
                TextField(L("folder"), text: $folder)
                Text(L("folder_hint")).font(.caption).foregroundStyle(Nova.muted)
                if invalid { Text(L("invalid_address")).foregroundStyle(.red) }
                Spacer()
            }.textFieldStyle(.roundedBorder).padding(20).navigationTitle(L("edit_bookmark"))
                .navigationBarTitleDisplayMode(.inline)
                .toolbar {
                    ToolbarItem(placement: .cancellationAction) { Button(L("cancel")) { dismiss() } }
                    ToolbarItem(placement: .confirmationAction) { Button(L("save")) { save() } }
                }
        }
    }

    private func save() {
        guard let resolved = URL(string: url.trimmingCharacters(in: .whitespacesAndNewlines)), Address.isWeb(resolved) else { invalid = true; return }
        entry.title = title.trimmingCharacters(in: .whitespacesAndNewlines)
        entry.url = resolved.absoluteString
        entry.folder = folder.trimmingCharacters(in: .whitespacesAndNewlines)
        do { try context.save(); dismiss() }
        catch { context.rollback(); onError(L("save_failed")); dismiss() }
    }
}
