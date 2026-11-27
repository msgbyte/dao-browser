import Foundation

@MainActor final class SessionWriter {
    private let queue = OperationQueue()
    private let write: @Sendable (SessionSnapshot) throws -> Void

    init(write: @escaping @Sendable (SessionSnapshot) throws -> Void) {
        self.write = write
        queue.maxConcurrentOperationCount = 1
    }

    func save(_ snapshot: SessionSnapshot, synchronously: Bool, onError: @escaping @MainActor @Sendable () -> Void) {
        // A write already in progress finishes before the newest snapshot can replace it.
        queue.cancelAllOperations()
        queue.addOperation { [write] in
            do { try write(snapshot) }
            catch { Task { @MainActor in onError() } }
        }
        if synchronously { queue.waitUntilAllOperationsAreFinished() }
    }
}
