// Build this entry point only for the update window integration test.
// Run the shipped entry point with per-process registry roots and a mutex
// private to the test run, so a concurrent real update remains independent.
#include <windows.h>

#include <string>

namespace {
std::wstring fixture_id;
HANDLE WINAPI CreateTestMutex(LPSECURITY_ATTRIBUTES security, BOOL owner,
                             LPCWSTR name) {
  return CreateMutexW(security, owner,
                      name ? (std::wstring(name) + L"-Test-" + fixture_id).c_str()
                           : nullptr);
}
}  // namespace

#define CreateMutexW CreateTestMutex
#define wWinMain DaoInstallerMainForTest
#include "../../windows-installer/native/host.cc"
#undef wWinMain
#undef CreateMutexW

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR args,
                    int show) {
  wchar_t token[33]{};
  if (GetEnvironmentVariableW(L"DAO_UPDATE_WINDOW_TEST_ID", token, 33) != 32 ||
      std::wstring(token).find_first_not_of(L"0123456789abcdef") !=
          std::wstring::npos) {
    return 91;
  }
  fixture_id = token;
  const std::wstring root = L"Software\\DaoInstallerTests\\" + fixture_id;
  HKEY user = nullptr, machine = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, (root + L"\\User").c_str(), 0,
                    KEY_READ, &user) != ERROR_SUCCESS ||
      RegOpenKeyExW(HKEY_CURRENT_USER, (root + L"\\Machine").c_str(), 0,
                    KEY_READ, &machine) != ERROR_SUCCESS ||
      RegOverridePredefKey(HKEY_CURRENT_USER, user) != ERROR_SUCCESS ||
      RegOverridePredefKey(HKEY_LOCAL_MACHINE, machine) != ERROR_SUCCESS) {
    return 91;
  }
  return DaoInstallerMainForTest(instance, previous, args, show);
}
