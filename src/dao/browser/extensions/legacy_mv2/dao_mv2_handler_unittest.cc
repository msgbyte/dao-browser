// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include "base/test/scoped_feature_list.h"
#include "chrome/browser/extensions/test_extension_environment.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/testing_profile.h"
#include "components/sync_preferences/testing_pref_service_syncable.h"
#include "content/public/test/browser_test.h"
#include "dao/browser/extensions/legacy_mv2/dao_mv2_features.h"
#include "dao/browser/extensions/legacy_mv2/dao_mv2_pref_defaults.h"
#include "extensions/browser/api/web_request/extension_web_request_event_router.h"
#include "extensions/browser/api/web_request/web_request_api_constants.h"
#include "extensions/browser/disable_reason.h"
#include "extensions/browser/extension_registrar.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/manifest_v2_handler.h"
#include "extensions/browser/pref_names.h"
#include "extensions/common/constants.h"
#include "extensions/common/extension_builder.h"
#include "extensions/common/extension_features.h"
#include "extensions/common/mojom/manifest.mojom.h"
#include "third_party/blink/public/mojom/service_worker/service_worker_database.mojom.h"

namespace dao {

// Use the browser runner's Mojo and Content lifecycle for the test profile.
class DaoMV2HandlerTest : public InProcessBrowserTest {
 public:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    profile_ = TestingProfile::Builder().Build();
    extensions::TestExtensionEnvironment::CreateExtensionServiceForProfile(
        profile());
  }

  void TearDownOnMainThread() override {
    profile_.reset();
    InProcessBrowserTest::TearDownOnMainThread();
  }

 protected:
  TestingProfile* profile() { return profile_.get(); }

  extensions::ExtensionRegistrar* registrar() {
    return extensions::ExtensionRegistrar::Get(profile());
  }

  extensions::ExtensionRegistry* registry() {
    return extensions::ExtensionRegistry::Get(profile());
  }

  extensions::ManifestV2Handler* handler() {
    return extensions::ManifestV2Handler::Get(profile());
  }

  void SetAvailability(int value) {
    profile()->GetTestingPrefService()->SetManagedPref(
        DaoMV2PrefDefaults::kAvailabilityPref, base::Value(value));
  }

 private:
  std::unique_ptr<TestingProfile> profile_;
};

IN_PROC_BROWSER_TEST_F(DaoMV2HandlerTest,
                       ManagedDisableAndAllowUpdateEnabledState) {
  auto extension = extensions::ExtensionBuilder("Dao MV2")
                       .SetManifestVersion(2)
                       .Build();
  registrar()->AddExtension(extension.get());
  ASSERT_TRUE(registry()->enabled_extensions().Contains(extension->id()));

  SetAvailability(1);
  EXPECT_TRUE(handler()->ShouldBlockExtensionEnable(*extension));
  EXPECT_TRUE(registry()->disabled_extensions().Contains(extension->id()));

  SetAvailability(2);
  EXPECT_FALSE(handler()->ShouldBlockExtensionEnable(*extension));
  EXPECT_TRUE(registry()->enabled_extensions().Contains(extension->id()));

  registrar()->DisableExtension(
      extension->id(), {extensions::disable_reason::DISABLE_USER_ACTION});
  SetAvailability(1);
  SetAvailability(2);
  EXPECT_TRUE(registry()->disabled_extensions().Contains(extension->id()));
}

IN_PROC_BROWSER_TEST_F(DaoMV2HandlerTest,
                       ManagedPolicyRestrictsInstallationAndEnablement) {
  auto extension = extensions::ExtensionBuilder("Dao MV2")
                       .SetManifestVersion(2)
                       .Build();
  SetAvailability(3);
  EXPECT_TRUE(handler()->ShouldBlockExtensionInstallation(
      2, extension->GetType(), extension->location(), extension->id()));
  EXPECT_TRUE(handler()->ShouldBlockExtensionEnable(*extension));

  for (const char* mode : {"force_installed", "normal_installed"}) {
    SCOPED_TRACE(mode);
    base::DictValue settings;
    settings.Set(extension->id(),
                 base::DictValue()
                     .Set("installation_mode", mode)
                     .Set("update_url",
                          "https://clients2.google.com/service/update2/crx"));
    profile()->GetTestingPrefService()->SetManagedPref(
        extensions::pref_names::kExtensionManagement, std::move(settings));
    EXPECT_FALSE(handler()->ShouldBlockExtensionInstallation(
        2, extension->GetType(), extension->location(), extension->id()));
    EXPECT_FALSE(handler()->ShouldBlockExtensionEnable(*extension));
  }

  SetAvailability(1);
  EXPECT_TRUE(handler()->ShouldBlockExtensionEnable(*extension));
  SetAvailability(0);
  EXPECT_TRUE(handler()->ShouldBlockExtensionEnable(*extension));
}

class DaoMV2HandlerRestoreTest : public DaoMV2HandlerTest {
 public:
  DaoMV2HandlerRestoreTest() {
    features_.InitAndEnableFeature(kRestoreManifestV2Deprecation);
  }

 private:
  base::test::ScopedFeatureList features_;
};

IN_PROC_BROWSER_TEST_F(DaoMV2HandlerRestoreTest,
                       ManagedAllowOverridesRestoreFlag) {
  auto extension = extensions::ExtensionBuilder("Dao MV2")
                       .SetManifestVersion(2)
                       .Build();
  EXPECT_TRUE(handler()->ShouldBlockExtensionEnable(*extension));
  SetAvailability(2);
  EXPECT_FALSE(handler()->ShouldBlockExtensionEnable(*extension));
  registrar()->AddExtension(extension.get());
  handler()->DisableAffectedExtensionsForTesting();
  EXPECT_TRUE(registry()->enabled_extensions().Contains(extension->id()));
}

// Duplicates can arise when two active worker registrations are deactivated.
// Collapsing them must release the redundant extra-headers count.
IN_PROC_BROWSER_TEST_F(DaoMV2HandlerTest,
                       DuplicateWebRequestListenersReleaseCounts) {
  using Router = extensions::WebRequestEventRouter;
  Router* router = Router::Get(profile());
  const std::string extension_id(32, 'a');
  const std::string event = extensions::api::web_request::OnBeforeRequest::kEventName;
  const std::string sub_event = event + "/s1";
  constexpr int kExtraInfo = extension_web_request_api_helpers::ExtraInfoSpec::EXTRA_HEADERS;
  for (int worker : {1, 2}) {
    ASSERT_TRUE(router->AddEventListener(
        profile(), extension_id, "Dao MV2", event, sub_event,
        Router::RequestFilter(), kExtraInfo, /*render_process_id=*/worker,
        /*web_view_instance_id=*/0, /*worker_thread_id=*/worker,
        /*service_worker_version_id=*/worker, /*is_lazy=*/false));
  }
  for (int worker : {1, 2}) {
    router->UpdateActiveListenerForTesting(
        profile(), Router::ListenerUpdateType::kDeactivate, extension_id,
        sub_event, /*worker_thread_id=*/worker,
        /*service_worker_version_id=*/worker);
  }
  ASSERT_EQ(2u, router->GetInactiveListenerCount(profile(), event));
  ASSERT_TRUE(router->HasAnyExtraHeadersListenerForTesting(profile()));
  ASSERT_TRUE(router->AddEventListener(
      profile(), extension_id, "Dao MV2", event, sub_event,
      Router::RequestFilter(), kExtraInfo, /*render_process_id=*/3,
      /*web_view_instance_id=*/0, /*worker_thread_id=*/3,
      /*service_worker_version_id=*/3, /*is_lazy=*/false));
  EXPECT_EQ(0u, router->GetInactiveListenerCount(profile(), event));
  EXPECT_EQ(1u, router->GetListenerCountForTesting(profile(), event));
  router->UpdateActiveListenerForTesting(
      profile(), Router::ListenerUpdateType::kDeactivate, extension_id,
      sub_event, /*worker_thread_id=*/3, /*service_worker_version_id=*/3);
  ASSERT_TRUE(router->AddEventListener(
      profile(), extension_id, "Dao MV2", event, sub_event,
      Router::RequestFilter(), /*extra_info_spec=*/0, /*render_process_id=*/0,
      /*web_view_instance_id=*/0, extensions::kMainThreadId,
      blink::mojom::kInvalidServiceWorkerVersionId, /*is_lazy=*/true));
  EXPECT_EQ(1u, router->GetInactiveListenerCount(profile(), event));
  EXPECT_FALSE(router->HasAnyExtraHeadersListenerForTesting(profile()));
}

class DaoMV2PerContextWebRequestTest : public DaoMV2HandlerTest {
 public:
  DaoMV2PerContextWebRequestTest() {
    features_.InitAndEnableFeature(
        extensions_features::kWebRequestPerContextEventDispatch);
  }

 private:
  base::test::ScopedFeatureList features_;
};

// Parent event names are shared by distinct registrations. Collapse exact
// duplicates without removing a different registration under the same name.
IN_PROC_BROWSER_TEST_F(DaoMV2PerContextWebRequestTest,
                       DuplicateCleanupPreservesDistinctRegistrations) {
  using Router = extensions::WebRequestEventRouter;
  Router* router = Router::Get(profile());
  const std::string extension_id(32, 'a');
  const std::string event =
      extensions::api::web_request::OnBeforeRequest::kEventName;
  constexpr int kExtraInfo =
      extension_web_request_api_helpers::ExtraInfoSpec::EXTRA_HEADERS;
  for (int worker : {1, 2}) {
    ASSERT_TRUE(router->AddEventListener(
        profile(), extension_id, "Dao MV2", event, event,
        Router::RequestFilter(), kExtraInfo, /*render_process_id=*/worker,
        /*web_view_instance_id=*/0, /*worker_thread_id=*/worker,
        /*service_worker_version_id=*/worker, /*is_lazy=*/false));
  }
  for (int worker : {1, 2}) {
    router->UpdateActiveListenerForTesting(
        profile(), Router::ListenerUpdateType::kDeactivate, extension_id, event,
        /*worker_thread_id=*/worker, /*service_worker_version_id=*/worker);
  }
  ASSERT_EQ(2u, router->GetInactiveListenerCount(profile(), event));

  // A distinct non-extra-headers registration must survive replacement of
  // the two exact extra-headers registrations.
  ASSERT_TRUE(router->AddEventListener(
      profile(), extension_id, "Dao MV2", event, event, Router::RequestFilter(),
      /*extra_info_spec=*/0, /*render_process_id=*/0, /*web_view_instance_id=*/0,
      extensions::kMainThreadId, blink::mojom::kInvalidServiceWorkerVersionId,
      /*is_lazy=*/true));
  ASSERT_EQ(3u, router->GetInactiveListenerCount(profile(), event));
  ASSERT_TRUE(router->AddEventListener(
      profile(), extension_id, "Dao MV2", event, event, Router::RequestFilter(),
      kExtraInfo, /*render_process_id=*/3, /*web_view_instance_id=*/0,
      /*worker_thread_id=*/3, /*service_worker_version_id=*/3,
      /*is_lazy=*/false));
  EXPECT_EQ(1u, router->GetInactiveListenerCount(profile(), event));
  EXPECT_EQ(1u, router->GetListenerCountForTesting(profile(), event));
  EXPECT_TRUE(router->HasAnyExtraHeadersListenerForTesting(profile()));

  Router::RequestFilter filter;
  router->RemoveLazyListenerForTesting(profile(), extension_id, event, &filter,
                                       kExtraInfo);
  EXPECT_EQ(1u, router->GetInactiveListenerCount(profile(), event));
  EXPECT_EQ(0u, router->GetListenerCountForTesting(profile(), event));
  EXPECT_FALSE(router->HasAnyExtraHeadersListenerForTesting(profile()));
}

}  // namespace dao
