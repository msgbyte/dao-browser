// Copyright 2026 Dao Browser Authors. All rights reserved.
#include "dao/browser/updater/dao_windows_updater.h"

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/test/task_environment.h"
#include "dao/browser/updater/dao_update_manifest_test_data.h"
#include "mojo/core/embedder/embedder.h"
#include "net/base/load_flags.h"
#include "net/http/http_status_code.h"
#include "net/url_request/redirect_info.h"
#include "services/network/public/cpp/url_loader_completion_status.h"
#include "services/network/public/cpp/weak_wrapper_shared_url_loader_factory.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "services/network/test/test_url_loader_factory.h"
#include "testing/gtest/include/gtest/gtest.h"

// This small unit-test binary never links a browser or closes user windows.
namespace chrome {
void AttemptExit() {
  ADD_FAILURE() << "Unexpected browser shutdown";
}
base::CallbackListSubscription AddClosingAllBrowsersCallback(
    base::RepeatingCallback<void(bool)>) {
  return {};
}
}  // namespace chrome

namespace dao {
class DaoWindowsUpdaterTest : public testing::Test {
 protected:
  static void SetUpTestSuite() { mojo::core::Init(); }
  void SetUp() override {
    ASSERT_TRUE(directory_.CreateUniqueTempDir());
    updater_.callback_ = base::BindRepeating([](const DaoUpdateStatus&) {});
    updater_.factory_ = factory_.GetSafeWeakWrapper();
    updater_.supported_ = true;
    updater_.cache_dir_ = directory_.GetPath();
  }
  void TearDown() override {
    updater_.Shutdown();
    environment_.RunUntilIdle();
  }
  void Schedule(base::Time last) { updater_.ScheduleNextCheck(last); }
  DaoUpdateState State() { return updater_.status_.state; }
  DaoUpdateError Error() { return updater_.status_.error; }
  void Download(std::string_view bytes) {
    auto parsed = ParseSignedWindowsUpdateWithKeyForTesting(
        kTestUpdateEnvelope, base::Version("1.2.2"),
        base::Version("149.0.7827.201"), kTestUpdateKey);
    ASSERT_TRUE(parsed.has_value());
    factory_.AddResponse(parsed->url.spec(), bytes);
    updater_.manifest_ = std::move(*parsed);
    updater_.envelope_ = kTestUpdateEnvelope;
    updater_.DownloadUpdate();
    environment_.RunUntilIdle();
  }
  base::test::TaskEnvironment environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  network::TestURLLoaderFactory factory_;
  base::ScopedTempDir directory_;
  DaoWindowsUpdater updater_;
  const std::string feed_ =
      std::string(kWindowsUpdateOrigin) + "/windows-update.json";
};

TEST_F(DaoWindowsUpdaterTest, PortableExecutableIsUnsupported) {
  updater_.Start(base::BindRepeating([](const DaoUpdateStatus&) {}),
                 factory_.GetSafeWeakWrapper());
  environment_.RunUntilIdle();
  EXPECT_EQ(DaoUpdateState::kUnsupported, State());
  updater_.CheckForUpdates(true);
  EXPECT_EQ(0, factory_.NumPending());
}

TEST_F(DaoWindowsUpdaterTest, RedirectsAreRejected) {
  net::RedirectInfo info;
  info.status_code = 302;
  info.new_url = GURL("https://example.com/installer.exe");
  network::TestURLLoaderFactory::Redirects redirects;
  redirects.emplace_back(info, network::mojom::URLResponseHead::New());
  factory_.AddResponse(GURL(feed_), network::mojom::URLResponseHead::New(), "",
                       network::URLLoaderCompletionStatus(),
                       std::move(redirects));
  updater_.CheckForUpdates(true);
  environment_.RunUntilIdle();
  EXPECT_EQ(DaoUpdateState::kError, State());
  EXPECT_EQ(DaoUpdateError::kNetwork, Error());
  EXPECT_EQ(0, factory_.NumPending());
}

TEST_F(DaoWindowsUpdaterTest, DueCheckWaitsAndOmitsCredentials) {
  Schedule({});
  environment_.FastForwardBy(base::Seconds(29));
  EXPECT_EQ(0, factory_.NumPending());
  environment_.FastForwardBy(base::Seconds(1));
  ASSERT_EQ(1, factory_.NumPending());
  const auto& request = factory_.GetPendingRequest(0)->request;
  EXPECT_EQ(network::mojom::CredentialsMode::kOmit, request.credentials_mode);
  EXPECT_TRUE(request.load_flags & net::LOAD_DISABLE_CACHE);
  updater_.CheckForUpdates(true);
  EXPECT_EQ(1, factory_.NumPending());
  updater_.Shutdown();
  EXPECT_EQ(0, factory_.NumPending());
}

TEST_F(DaoWindowsUpdaterTest, RecentCheckWaitsADayAndManualCheckBypassesIt) {
  Schedule(base::Time::Now());
  environment_.FastForwardBy(base::Hours(23));
  EXPECT_EQ(0, factory_.NumPending());
  updater_.CheckForUpdates(true);
  EXPECT_EQ(1, factory_.NumPending());
}

TEST_F(DaoWindowsUpdaterTest, FutureClockDoesNotDisableChecks) {
  Schedule(base::Time::Now() + base::Days(365));
  environment_.FastForwardBy(base::Seconds(30));
  EXPECT_EQ(1, factory_.NumPending());
}

TEST_F(DaoWindowsUpdaterTest, FailedAndUnsignedResponsesNeverReportCurrent) {
  factory_.AddResponse(feed_, "unavailable", net::HTTP_SERVICE_UNAVAILABLE);
  updater_.CheckForUpdates(true);
  environment_.RunUntilIdle();
  EXPECT_EQ(DaoUpdateState::kError, State());
  EXPECT_EQ(DaoUpdateError::kNetwork, Error());
  factory_.AddResponse(feed_, kTestUpdateEnvelope);
  updater_.CheckForUpdates(true);
  environment_.RunUntilIdle();
  EXPECT_EQ(DaoUpdateState::kError, State());
  EXPECT_EQ(DaoUpdateError::kVerification, Error());
  factory_.AddResponse(feed_, std::string(65537, ' '));
  updater_.CheckForUpdates(true);
  environment_.RunUntilIdle();
  EXPECT_EQ(DaoUpdateState::kError, State());
  EXPECT_FALSE(updater_.ApplyReadyUpdate());
}

TEST_F(DaoWindowsUpdaterTest, DownloadRequiresExactSizeAndHash) {
  Download("truncated");
  EXPECT_EQ(DaoUpdateState::kError, State());
  Download("evil installer");
  EXPECT_EQ(DaoUpdateError::kVerification, Error());
  Download("oversized installer");
  EXPECT_EQ(DaoUpdateState::kError, State());
  Download("test installer");
  EXPECT_EQ(DaoUpdateState::kReady, State());
  EXPECT_TRUE(base::PathExists(directory_.GetPath().AppendASCII("ready.json")));
}

TEST_F(DaoWindowsUpdaterTest, ShutdownCancelsTimeoutAndFutureChecks) {
  updater_.CheckForUpdates(true);
  updater_.Shutdown();
  environment_.FastForwardBy(base::Days(2));
  EXPECT_EQ(0, factory_.NumPending());
  updater_.CheckForUpdates(true);
  EXPECT_EQ(0, factory_.NumPending());
  EXPECT_FALSE(updater_.PrepareForExit());
}
}  // namespace dao
