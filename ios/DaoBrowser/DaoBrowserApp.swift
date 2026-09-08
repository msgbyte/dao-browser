import SwiftData
import SwiftUI

@main struct DaoBrowserApp: App {
    @State private var model: BrowserModel?

    init() {
        do { _model = State(initialValue: BrowserModel(container: try ModelContainer(for: LibraryEntry.self))) }
        catch { _model = State(initialValue: nil) }
    }

    var body: some Scene {
        WindowGroup {
            if let model {
                BrowserView(model: model).modelContainer(model.container)
                    .preferredColorScheme(model.preferences.colorScheme)
                    .onOpenURL { model.incoming($0) }
            } else {
                Text(L("storage_unavailable")).padding().accessibilityAddTraits(.isHeader)
            }
        }
    }
}
