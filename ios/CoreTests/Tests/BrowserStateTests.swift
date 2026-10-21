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

@Test func sessionRestoresThumbnailsWithoutPersistingPrivatePreviews() throws {
    let directory = FileManager.default.temporaryDirectory.appending(path: UUID().uuidString)
    try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
    defer { try? FileManager.default.removeItem(at: directory) }
    let archive = directory.appending(path: "session.json")
    let records: [[String: Any]] = [
        ["id": UUID().uuidString, "url": "https://example.com", "title": "First", "isPrivate": false,
         "thumbnailData": Data("first preview".utf8).base64EncodedString()],
        ["id": UUID().uuidString, "url": "https://example.org", "title": "Second", "isPrivate": false,
         "thumbnailData": Data("second preview".utf8).base64EncodedString()],
        ["id": UUID().uuidString, "url": "https://private.example", "title": "Private", "isPrivate": true,
         "thumbnailData": Data("private preview".utf8).base64EncodedString()],
        // Existing archives and new tabs have no thumbnail field.
        ["id": UUID().uuidString, "url": "", "title": "", "isPrivate": false]
    ]
    let tabs = try JSONDecoder().decode([TabRecord].self, from: JSONSerialization.data(withJSONObject: records))
    let snapshot = SessionSnapshot(tabs: tabs, selectedID: tabs[1].id)
    try JSONEncoder().encode(snapshot).write(to: archive, options: .atomic)

    let restored = try JSONDecoder().decode(SessionSnapshot.self, from: Data(contentsOf: archive))
    #expect(restored.tabs.map(\.id) == [tabs[0].id, tabs[1].id, tabs[3].id])
    #expect(restored.selectedID == tabs[1].id)
    let json = try #require(JSONSerialization.jsonObject(with: JSONEncoder().encode(restored)) as? [String: Any])
    let savedTabs = try #require(json["tabs"] as? [[String: Any]])
    #expect(savedTabs[0]["thumbnailData"] as? String == Data("first preview".utf8).base64EncodedString())
    #expect(savedTabs[1]["thumbnailData"] as? String == Data("second preview".utf8).base64EncodedString())
    #expect(savedTabs[2]["thumbnailData"] == nil)
    #expect(!String(decoding: try Data(contentsOf: archive), as: UTF8.self)
        .contains(Data("private preview".utf8).base64EncodedString()))
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

@Test func transferMeterReportsSpeedOnlyFromFreshSamples() {
    var meter = TransferMeter()
    meter.record(0, total: 4_000, at: 10)
    #expect(meter.speed(at: 10) == nil)
    meter.record(500, total: 4_000, at: 10.5)
    #expect(meter.speed(at: 10.5) == nil)
    meter.record(2_000, total: 4_000, at: 11)
    #expect(meter.speed(at: 11) == 2_000)
    #expect(meter.fraction == 0.5)
    // Without published progress for five seconds the row shows zero.
    #expect(meter.speed(at: 15.9) == 2_000)
    #expect(meter.speed(at: 16) == 0)
    // A longer gap or a counter reset hides speed until a new window completes.
    meter.record(3_000, total: 4_000, at: 22)
    #expect(meter.speed(at: 22) == nil)
    meter.record(3_500, total: 4_000, at: 23)
    #expect(meter.speed(at: 23) == 500)
    meter.record(100, total: nil, at: 23.5)
    #expect(meter.speed(at: 23.5) == nil)
    #expect(meter.total == nil)
    #expect(meter.fraction == 0)
}

@Test func pageImageMenusOnlyActOnImageSources() {
    #expect(PageImageSource.url("https://example.com/a.png")?.host == "example.com")
    #expect(PageImageSource.url("data:image/png;base64,iVBORw0KGgo=") != nil)
    #expect(PageImageSource.url("DATA:IMAGE/GIF;base64,R0lGOD") != nil)
    #expect(PageImageSource.url("blob:https://example.com/0f2c") != nil)
    #expect(PageImageSource.url("data:text/html,<b>") == nil)
    #expect(PageImageSource.url("javascript:alert(1)") == nil)
    #expect(PageImageSource.url("file:///etc/passwd") == nil)
    #expect(PageImageSource.url("") == nil)
    #expect(PageImageSource.canOpenInTab(URL(string: "https://example.com/a.png")!))
    #expect(!PageImageSource.canOpenInTab(URL(string: "blob:https://example.com/0f2c")!))
    #expect(!PageImageSource.canOpenInTab(URL(string: "data:image/png;base64,iVBORw0KGgo=")!))
}
