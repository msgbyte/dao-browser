// Test-only Ed25519 fixture. Seed bytes are 0..31; never use this key for releases.
#ifndef DAO_BROWSER_UPDATER_DAO_UPDATE_MANIFEST_TEST_DATA_H_
#define DAO_BROWSER_UPDATER_DAO_UPDATE_MANIFEST_TEST_DATA_H_
namespace dao {
inline constexpr char kTestUpdateKey[] = "A6EHv/POEL4dcN0Y50vAmWfk1jCbpQ1fHdyGZBJVMbg=";
inline constexpr char kTestUpdateEnvelope[] = R"fixture({"payload":"{\"schema\":1,\"platform\":\"windows\",\"arch\":\"x64\",\"version\":\"1.2.3\",\"chromiumVersion\":\"149.0.7827.201\",\"url\":\"https://dao-release.msgbyte.com/dao-browser-1.2.3-windows-x64.exe\",\"size\":14,\"sha256\":\"6b9fdebf5b00e422a7d3cc227559c4682b8ef3a23ec2b1a65d0dc90860131f87\"}","signature":"2Vvv8tTqz2NXaA2qz10slobMjjCuc7gWnSp4JdBVtfO+VQPvU9jCqeZwD6WElw6uKMFuUxn7xUb30I2YZ+qwBg=="})fixture";
}  // namespace dao
#endif
