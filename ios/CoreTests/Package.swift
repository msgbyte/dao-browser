// swift-tools-version: 6.0
import PackageDescription

let package = Package(
    name: "DaoCoreChecks",
    platforms: [.macOS(.v15)],
    products: [.library(name: "DaoCore", targets: ["DaoCore"])],
    targets: [
        .target(name: "DaoCore", path: "Sources"),
        .testTarget(name: "DaoCoreTests", dependencies: ["DaoCore"], path: "Tests")
    ]
)
