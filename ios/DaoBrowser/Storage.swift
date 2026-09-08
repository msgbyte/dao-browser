import Foundation
import Observation
import SwiftData
import SwiftUI

func L(_ key: String) -> String { NSLocalizedString(key, comment: "") }

@Model final class LibraryEntry {
    @Attribute(.unique) var id: UUID
    var url: String
    var title: String
    var kind: String
    var folder: String
    var visitedAt: Date

    init(url: String, title: String, kind: String, folder: String = "") {
        id = UUID()
        self.url = url
        self.title = title
        self.kind = kind
        self.folder = folder
        visitedAt = .now
    }
}

@MainActor @Observable final class Preferences {
    var theme: String { didSet { defaults.set(theme, forKey: "theme") } }
    var searchEngine: SearchEngine { didSet { defaults.set(searchEngine.rawValue, forKey: "searchEngine") } }
    var fontScale: Double { didSet { defaults.set(fontScale, forKey: "fontScale") } }
    var defaultPrivate: Bool { didSet { defaults.set(defaultPrivate, forKey: "defaultPrivate") } }
    var inspectable: Bool { didSet { defaults.set(inspectable, forKey: "inspectable") } }
    @ObservationIgnored private let defaults: UserDefaults

    init(defaults: UserDefaults = .standard) {
        self.defaults = defaults
        theme = defaults.string(forKey: "theme") ?? "system"
        searchEngine = SearchEngine(rawValue: defaults.string(forKey: "searchEngine") ?? "") ?? .google
        let scale = defaults.double(forKey: "fontScale")
        fontScale = scale == 0 ? 1 : min(1.5, max(0.75, scale))
        defaultPrivate = defaults.bool(forKey: "defaultPrivate")
        inspectable = defaults.bool(forKey: "inspectable")
    }

    var colorScheme: ColorScheme? { theme == "dark" ? .dark : theme == "light" ? .light : nil }
}

enum BrowserFiles {
    static var directory: URL {
        URL.applicationSupportDirectory.appending(path: "Dao", directoryHint: .isDirectory)
    }

    static func save<T: Encodable>(_ value: T, name: String) throws {
        try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
        try JSONEncoder().encode(value).write(to: directory.appending(path: name), options: [.atomic, .completeFileProtectionUnlessOpen])
    }

    static func read<T: Decodable>(_ type: T.Type, name: String) throws -> T? {
        let url = directory.appending(path: name)
        guard FileManager.default.fileExists(atPath: url.path) else { return nil }
        return try JSONDecoder().decode(type, from: Data(contentsOf: url))
    }

    /// Moves an unreadable file aside so saving can continue; returns false if it could not be moved.
    static func quarantine(_ name: String) -> Bool {
        let url = directory.appending(path: name)
        let target = directory.appending(path: name + ".corrupt")
        try? FileManager.default.removeItem(at: target)
        return (try? FileManager.default.moveItem(at: url, to: target)) != nil
    }
}
