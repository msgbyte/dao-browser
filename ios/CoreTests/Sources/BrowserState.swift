import Foundation

public enum SearchEngine: String, CaseIterable, Codable, Sendable {
    case google, baidu, bing, duckDuckGo

    func search(_ query: String) -> URL? {
        let base: String
        let key: String
        switch self {
        case .google: (base, key) = ("https://www.google.com/search", "q")
        case .baidu: (base, key) = ("https://www.baidu.com/s", "wd")
        case .bing: (base, key) = ("https://www.bing.com/search", "q")
        case .duckDuckGo: (base, key) = ("https://duckduckgo.com/", "q")
        }
        var components = URLComponents(string: base)!
        components.queryItems = [URLQueryItem(name: key, value: query)]
        return components.url
    }
}

public enum Address {
    public static func isWeb(_ url: URL) -> Bool {
        ["http", "https"].contains(url.scheme?.lowercased() ?? "") && !(url.host ?? "").isEmpty
    }

    public static func resolve(_ input: String, engine: SearchEngine) -> URL? {
        let text = input.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !text.isEmpty else { return nil }
        if let url = URL(string: text), isWeb(url) { return url }
        let host = text.split(separator: "/", maxSplits: 1).first.map(String.init) ?? text
        let local = host == "localhost" || host.hasPrefix("localhost:") || host.hasPrefix("[::1]")
        let hasWhitespace = text.rangeOfCharacter(from: .whitespacesAndNewlines) != nil
        if !hasWhitespace, local || (host.contains(".") && !host.contains("@")) {
            if let url = URL(string: "\(local ? "http" : "https")://\(text)"), isWeb(url), url.user == nil {
                return url
            }
        }
        // Schemes supplied through the address field must never execute scripts or open files,
        // and a fully formed non-web URL is not a search query either.
        if let match = text.range(of: #"^[a-zA-Z][a-zA-Z0-9+.-]*:"#, options: .regularExpression) {
            let scheme = text[match].dropLast().lowercased()
            if blockedSchemes.contains(scheme) || text[match.upperBound...].hasPrefix("//") { return nil }
        }
        return engine.search(text)
    }

    private static let blockedSchemes: Set<String> = ["javascript", "data", "file", "blob", "about", "vbscript", "dao"]

    public static func incoming(_ url: URL) -> URL? {
        if isWeb(url) { return url }
        guard url.scheme == "dao", url.host == "open",
              let value = URLComponents(url: url, resolvingAgainstBaseURL: false)?.queryItems?
                .first(where: { $0.name == "url" })?.value,
              let target = URL(string: value), isWeb(target) else { return nil }
        return target
    }
}

public struct TabRecord: Identifiable, Codable, Equatable, Sendable {
    public var id = UUID()
    public var url = ""
    public var title = ""
    public var isPrivate = false
    public var interactionState: Data?

    public init(id: UUID = UUID(), url: String = "", title: String = "", isPrivate: Bool = false, interactionState: Data? = nil) {
        self.id = id
        self.url = url
        self.title = title
        self.isPrivate = isPrivate
        self.interactionState = interactionState
    }
}

public struct SessionSnapshot: Codable, Equatable, Sendable {
    public let tabs: [TabRecord]
    public let selectedID: UUID?

    public init(tabs: [TabRecord], selectedID: UUID?) {
        self.tabs = tabs.filter { !$0.isPrivate }
        self.selectedID = self.tabs.contains { $0.id == selectedID } ? selectedID : self.tabs.first?.id
    }
}

public struct TabCollection: Sendable {
    public private(set) var tabs: [TabRecord]
    public var selectedID: UUID

    public init(tabs: [TabRecord], selectedID: UUID?) {
        self.tabs = tabs.isEmpty ? [TabRecord()] : tabs
        self.selectedID = self.tabs.first(where: { $0.id == selectedID })?.id ?? self.tabs[0].id
    }

    public mutating func add(_ tab: TabRecord) {
        tabs.append(tab)
        selectedID = tab.id
    }

    public mutating func close(_ id: UUID) {
        guard let index = tabs.firstIndex(where: { $0.id == id }) else { return }
        let wasPrivate = tabs[index].isPrivate
        tabs.remove(at: index)
        if tabs.isEmpty { tabs = [TabRecord(isPrivate: wasPrivate)] }
        if selectedID == id { selectedID = tabs[min(index, tabs.count - 1)].id }
    }
}

public enum DownloadName {
    public static func sanitize(_ input: String) -> String {
        let name = (input.replacingOccurrences(of: "\\", with: "/") as NSString).lastPathComponent
            .components(separatedBy: .controlCharacters).joined()
            .trimmingCharacters(in: .whitespacesAndNewlines)
        return name.isEmpty || name == "." || name == ".." ? "download" : String(name.prefix(180))
    }
}
