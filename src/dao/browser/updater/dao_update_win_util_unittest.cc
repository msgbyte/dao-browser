// Copyright 2026 Dao Browser Authors. All rights reserved.
#include "dao/browser/updater/dao_update_win_util.h"

#include "base/command_line.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/process/launch.h"
#include "base/strings/utf_string_conversions.h"
#include "base/uuid.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace dao::update_win {
TEST(DaoUpdateWinUtilTest, PipeTokensAndLocalPaths) {
  EXPECT_TRUE(PipeName(L"../../other").empty());
  EXPECT_TRUE(PipeName(L"0123456789abcdef0123456789abcdeF").empty());
  EXPECT_FALSE(PipeName(L"0123456789abcdef0123456789abcdef").empty());
  EXPECT_FALSE(SafeLocalPath(L"\\\\server\\share\\Dao"));
  EXPECT_FALSE(SafeLocalPath(L"C:\\Dao\" /D=other"));
  EXPECT_FALSE(SafeLocalPath(L"C:\\Dao\\file:stream"));
  EXPECT_TRUE(SafeLocalPath(L"C:\\Users\\Test\\Dao"));
}

TEST(DaoUpdateWinUtilTest, LegacyBrowserMigration) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  const auto application = directory.GetPath().AppendASCII("Application");
  ASSERT_TRUE(base::CreateDirectory(application));
  const auto legacy = application.AppendASCII("chrome.exe");
  const auto browser = application.AppendASCII("dao.exe");
  EXPECT_TRUE(BrowserInDirectory(application.value()).empty());
  ASSERT_TRUE(base::WriteFile(legacy, "legacy"));
  EXPECT_EQ(legacy.value(), BrowserInDirectory(application.value()));
  ASSERT_TRUE(base::WriteFile(browser, "current"));
  EXPECT_EQ(browser.value(), BrowserInDirectory(application.value()));
  ASSERT_TRUE(base::DeleteFile(browser));
  ASSERT_TRUE(base::CreateDirectory(browser));
  EXPECT_TRUE(BrowserInDirectory(application.value()).empty());
  EXPECT_TRUE(BrowserInDirectory(directory.GetPath().value()).empty());
}

TEST(DaoUpdateWinUtilTest, OpenedFileIdentity) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  const auto first = directory.GetPath().AppendASCII("first.exe");
  const auto second = directory.GetPath().AppendASCII("second.exe");
  ASSERT_TRUE(base::WriteFile(first, "first"));
  ASSERT_TRUE(base::WriteFile(second, "first"));
  EXPECT_TRUE(SameFile(first.value(), first.value()));
  EXPECT_FALSE(SameFile(first.value(), second.value()));
  EXPECT_FALSE(SameFile(first.value(), L""));
  EXPECT_TRUE(NoReparsePoints(first.value()));
}

TEST(DaoUpdateWinUtilTest, PipeCancellationAndBoundedWait) {
  std::string token = base::Uuid::GenerateRandomV4().AsLowercaseString();
  std::erase(token, '-');
  const auto name = PipeName(base::UTF8ToWide(token));
  UserSecurity security;
  ASSERT_NE(nullptr, security.get());
  Handle server(CreateNamedPipeW(
      name.c_str(),
      PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
      PIPE_TYPE_BYTE | PIPE_REJECT_REMOTE_CLIENTS, 1, 128, 128, 0,
      security.get()));
  ASSERT_TRUE(server.valid());
  Handle client(CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                            nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED,
                            nullptr));
  ASSERT_TRUE(client.valid());
  ULONG server_pid = 0, client_pid = 0;
  ASSERT_TRUE(GetNamedPipeServerProcessId(client.get(), &server_pid));
  ASSERT_TRUE(GetNamedPipeClientProcessId(server.get(), &client_pid));
  EXPECT_EQ(GetCurrentProcessId(), server_pid);
  EXPECT_EQ(GetCurrentProcessId(), client_pid);
  char message = 0;
  EXPECT_FALSE(Transfer(client.get(), &message, 1, false, 1));
  ASSERT_TRUE(Send(server.get(), 'X'));
  ASSERT_TRUE(Transfer(client.get(), &message, 1, false, 1000));
  EXPECT_EQ('X', message);
  FILETIME first{}, second{};
  ASSERT_TRUE(CreationTime(GetCurrentProcess(), &first));
  ASSERT_TRUE(CreationTime(GetCurrentProcess(), &second));
  EXPECT_EQ(0, CompareFileTime(&first, &second));
  ASSERT_TRUE(Send(server.get(), 'X'));
  EXPECT_EQ(ExitResult::kCancelled,
            WaitForCommitAndExit(client.get(), GetCurrentProcess(), 1, 1));
  ASSERT_TRUE(Send(server.get(), 'C'));
  EXPECT_EQ(ExitResult::kParentRunning,
            WaitForCommitAndExit(client.get(), GetCurrentProcess(), 1, 1));
  base::CommandLine command(
      base::CommandLine::ForCurrentProcess()->GetProgram());
  command.AppendSwitchASCII("gtest_filter", "NoSuchTest.*");
  command.AppendSwitch("single-process-tests");
  auto child = base::LaunchProcess(command, {});
  ASSERT_TRUE(child.IsValid());
  int code = -1;
  ASSERT_TRUE(child.WaitForExitWithTimeout(base::Seconds(10), &code));
  // An exited process without a commit still cannot authorize installation.
  EXPECT_EQ(ExitResult::kCancelled,
            WaitForCommitAndExit(client.get(), child.Handle(), 1, 1));
  ASSERT_TRUE(Send(server.get(), 'C'));
  EXPECT_EQ(ExitResult::kReady,
            WaitForCommitAndExit(client.get(), child.Handle(), 1, 1));
}
}  // namespace dao::update_win
