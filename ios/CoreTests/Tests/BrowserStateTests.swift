import Foundation
import Testing
@testable import DaoCore

@Test func addressResolutionKeepsNavigationSafe() throws {
    #expect(Address.resolve("example.com/path", engine: .google)?.absoluteString == "https://example.com/path")
    #expect(Address.resolve("localhost:8080", engine: .google)?.absoluteString == "http://localhost:8080")
    #expect(Address.resolve("[::1]:8080", engine: .google)?.absoluteString == "http://[::1]:8080")
    #expect(Address.resolve("   ", engine: .google) == nil)
    #expect(Address.resolve("javascript:alert(1)", engine: .google) == nil)
    #expect(Address.resolve("file:///etc/passwd", engine: .google) == nil)
    #expect(Address.resolve("data:text/html,<script>alert(1)</script>", engine: .google) == nil)
    #expect(Address.resolve("itms-apps://apps.apple.com", engine: .google) == nil)
    #expect(Address.resolve("re:invent", engine: .google)?.host == "www.google.com")
    #expect(Address.resolve("python:tutorial", engine: .bing)?.host == "www.bing.com")
    let search = try #require(Address.resolve("a & b?#中文", engine: .duckDuckGo))
    #expect(URLComponents(url: search, resolvingAgainstBaseURL: false)?.queryItems?.first?.value == "a & b?#中文")
    #expect(Address.incoming(URL(string: "dao://open?url=https%3A%2F%2Fexample.com")!)?.host == "example.com")
    #expect(Address.incoming(URL(string: "dao://open?url=javascript%3Aalert(1)")!) == nil)
}

@Test func snapshotNeverPersistsPrivateTabsOrAnInvalidSelection() throws {
    let normal = TabRecord(url: "https://example.com", title: "Example")
    let secret = TabRecord(url: "https://private.example", title: "Secret", isPrivate: true)
    let snapshot = SessionSnapshot(tabs: [normal, secret], selectedID: secret.id)
    let data = try JSONEncoder().encode(snapshot)
    #expect(!String(decoding: data, as: UTF8.self).contains("private.example"))
    #expect(snapshot.tabs == [normal])
    #expect(snapshot.selectedID == normal.id)
    #expect(try JSONDecoder().decode(SessionSnapshot.self, from: data) == snapshot)
    #expect(SessionSnapshot(tabs: [secret], selectedID: secret.id).tabs.isEmpty)
}

@Test func closingLastTabPreservesItsPrivacy() {
    var state = TabCollection(tabs: [TabRecord(isPrivate: true)], selectedID: nil)
    state.close(state.selectedID)
    #expect(state.tabs.count == 1)
    #expect(state.tabs.first?.isPrivate == true)
    let next = TabRecord(url: "https://example.com")
    state.add(next)
    state.close(next.id)
    #expect(state.tabs.contains { $0.id == state.selectedID })
    state.close(UUID())
    #expect(state.tabs.count == 1)
}

@Test func downloadNamesCannotEscapeTheirDirectory() {
    #expect(DownloadName.sanitize("../../secret.txt") == "secret.txt")
    #expect(DownloadName.sanitize("..") == "download")
    #expect(!DownloadName.sanitize("a\\b\u{0}.txt").contains("\u{0}"))
}
