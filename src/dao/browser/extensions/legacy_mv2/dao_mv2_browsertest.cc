// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <tuple>
#include <utility>

#include "base/files/file_path.h"
#include "base/path_service.h"
#include "base/run_loop.h"
#include "base/test/bind.h"
#include "base/test/scoped_feature_list.h"
#include "chrome/browser/extensions/extension_browsertest.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/policy/core/browser/browser_policy_connector.h"
#include "components/policy/core/common/mock_configuration_policy_provider.h"
#include "components/policy/core/common/policy_map.h"
#include "components/policy/policy_constants.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/testing_pref_service.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/test_navigation_observer.h"
#include "dao/browser/extensions/legacy_mv2/dao_mv2_api_router.h"
#include "dao/browser/extensions/legacy_mv2/dao_mv2_features.h"
#include "dao/browser/extensions/legacy_mv2/dao_mv2_install_notice.h"
#include "dao/browser/extensions/legacy_mv2/dao_mv2_pref_defaults.h"
#include "extensions/browser/disable_reason.h"
#include "extensions/browser/extension_host.h"
#include "extensions/browser/extension_registrar.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/manifest_v2_handler.h"
#include "extensions/browser/process_manager.h"
#include "extensions/browser/unpacked_installer.h"
#include "extensions/common/extension.h"
#include "net/base/net_errors.h"
#include "testing/gmock/include/gmock/gmock.h"

namespace dao {

namespace {

base::FilePath TestDataDir() {
  base::FilePath dir;
  // DIR_SRC_TEST_DATA_ROOT points at the source-tree root for test data
  // lookups (the modern name for what used to be DIR_SOURCE_ROOT).
  base::PathService::Get(base::DIR_SRC_TEST_DATA_ROOT, &dir);
  return dir.AppendASCII("dao")
      .AppendASCII("browser")
      .AppendASCII("extensions")
      .AppendASCII("legacy_mv2")
      .AppendASCII("test_data");
}

std::u16string TryUnpackedInstall(Profile* profile,
                                  const base::FilePath& path) {
  base::RunLoop run_loop;
  std::u16string install_error;
  auto installer = extensions::UnpackedInstaller::Create(profile);
  installer->set_be_noisy_on_failure(false);
  installer->set_completion_callback(base::BindLambdaForTesting(
      [&](const extensions::Extension* extension, const base::FilePath&,
          const std::u16string& error) {
        EXPECT_EQ(nullptr, extension);
        install_error = error;
        run_loop.Quit();
      }));
  installer->Load(path);
  run_loop.Run();
  return install_error;
}

}  // namespace

// ----- Baseline: MV2 enabled by default ---------------------------------

class DaoMV2BrowserTest : public extensions::ExtensionBrowserTest {
 protected:
  // Exercise Dao's policy without Chromium's test-only MV2 exemption.
  bool ShouldAllowMV2Extensions() override { return false; }
};

IN_PROC_BROWSER_TEST_F(DaoMV2BrowserTest, DefaultPolicy_AllowsUnpackedLoad) {
  const extensions::Extension* extension =
      LoadExtension(TestDataDir().AppendASCII("webrequest_blocking"));
  ASSERT_TRUE(extension);
  EXPECT_EQ(2, extension->manifest_version());
  EXPECT_TRUE(extensions::ExtensionRegistry::Get(profile())
                  ->enabled_extensions()
                  .Contains(extension->id()));
}

IN_PROC_BROWSER_TEST_F(DaoMV2BrowserTest, DefaultPolicy_AllowsCRXInstall) {
  // Pack the unpacked fixture into a .crx using a freshly-generated key,
  // then install through Chromium's standard install pathway.
  base::FilePath crx_path =
      PackExtension(TestDataDir().AppendASCII("webrequest_blocking"));
  ASSERT_FALSE(crx_path.empty());

  // InstallExtensionWithUIAutoConfirm simulates the realistic CRX install
  // flow (user sees prompt, clicks "Install"); the silent InstallExtension
  // no-ops in modern Chromium without an auto-confirmed prompt.
  const extensions::Extension* extension =
      InstallExtensionWithUIAutoConfirm(crx_path, /*expected_change=*/1);
  ASSERT_TRUE(extension);
  EXPECT_EQ(2, extension->manifest_version());
  EXPECT_TRUE(extensions::ExtensionRegistry::Get(profile())
                  ->enabled_extensions()
                  .Contains(extension->id()));
}

IN_PROC_BROWSER_TEST_F(DaoMV2BrowserTest, PrefDefault_IsExempt) {
  // The Dao default for kManifestV2Availability is kEnabled (=2).
  EXPECT_EQ(2, DaoMV2PrefDefaults::DefaultManifestV2Availability());

  const extensions::Extension* extension =
      LoadExtension(TestDataDir().AppendASCII("webrequest_blocking"));
  ASSERT_TRUE(extension);
  auto* handler = extensions::ManifestV2Handler::Get(profile());
  ASSERT_TRUE(handler);
  EXPECT_FALSE(handler->IsExtensionAffected(*extension));
  EXPECT_FALSE(handler->ShouldBlockExtensionEnable(*extension));
}

// ----- Flag ON: fall back to Chromium deprecation -----------------------

class DaoMV2BrowserTestRestoreDeprecation : public DaoMV2BrowserTest {
 public:
  DaoMV2BrowserTestRestoreDeprecation() {
    feature_list_.InitAndEnableFeature(kRestoreManifestV2Deprecation);
  }

 private:
  base::test::ScopedFeatureList feature_list_;
};

IN_PROC_BROWSER_TEST_F(DaoMV2BrowserTestRestoreDeprecation,
                       FlagOn_FallsBackToChromiumDeprecation) {
  // The pref default flips to kDefault (=0) under the flag.
  EXPECT_EQ(0, DaoMV2PrefDefaults::DefaultManifestV2Availability());

  auto* handler = extensions::ManifestV2Handler::Get(profile());
  ASSERT_TRUE(handler);
  EXPECT_TRUE(handler->ShouldBlockExtensionInstallation(
      2, extensions::Manifest::Type::kExtension,
      extensions::mojom::ManifestLocation::kInternal));

  base::FilePath crx_path =
      PackExtension(TestDataDir().AppendASCII("webrequest_blocking"));
  ASSERT_FALSE(crx_path.empty());
  EXPECT_EQ(nullptr,
            InstallExtensionWithUIAutoConfirm(crx_path, /*expected_change=*/0));
}

IN_PROC_BROWSER_TEST_F(DaoMV2BrowserTestRestoreDeprecation,
                       FlagOn_RejectsUnpackedInstall) {
  EXPECT_FALSE(TryUnpackedInstall(
                   profile(), TestDataDir().AppendASCII("webrequest_blocking"))
                   .empty());
}

IN_PROC_BROWSER_TEST_F(DaoMV2BrowserTest, DefaultPolicy_AllowsManualReenable) {
  const extensions::Extension* extension =
      LoadExtension(TestDataDir().AppendASCII("webrequest_blocking"));
  ASSERT_TRUE(extension);
  const std::string id = extension->id();
  auto* registrar = extensions::ExtensionRegistrar::Get(profile());
  registrar->DisableExtension(
      id, {extensions::disable_reason::DISABLE_USER_ACTION});
  ASSERT_TRUE(extensions::ExtensionRegistry::Get(profile())
                  ->disabled_extensions().Contains(id));
  registrar->EnableExtension(id);
  EXPECT_TRUE(extensions::ExtensionRegistry::Get(profile())
                  ->enabled_extensions().Contains(id));
}

// ----- Install dialog notice helper ------------------------------------

IN_PROC_BROWSER_TEST_F(DaoMV2BrowserTest, InstallNotice_MV2Shows) {
  const extensions::Extension* extension =
      LoadExtension(TestDataDir().AppendASCII("webrequest_blocking"));
  ASSERT_TRUE(extension);
  EXPECT_TRUE(DaoMV2InstallNotice::ShouldShowLegacyMV2Notice(extension));
  EXPECT_FALSE(DaoMV2InstallNotice::GetLegacyMV2NoticeText().empty());
}

IN_PROC_BROWSER_TEST_F(DaoMV2BrowserTest, InstallNotice_NoNoticeForNull) {
  EXPECT_FALSE(DaoMV2InstallNotice::ShouldShowLegacyMV2Notice(nullptr));
}

IN_PROC_BROWSER_TEST_F(DaoMV2BrowserTest, InstallNotice_NoNoticeForMV3) {
  const extensions::Extension* extension =
      LoadExtension(TestDataDir().AppendASCII("mv3_minimal"));
  ASSERT_TRUE(extension);
  EXPECT_EQ(3, extension->manifest_version());
  EXPECT_FALSE(DaoMV2InstallNotice::ShouldShowLegacyMV2Notice(extension));
}

// ----- Canary: webRequest blocking still works -------------------------

IN_PROC_BROWSER_TEST_F(DaoMV2BrowserTest,
                       WebRequestBlocking_StillIntercepts) {
  const extensions::Extension* extension =
      LoadExtension(TestDataDir().AppendASCII("webrequest_blocking"));
  ASSERT_TRUE(extension);

  // Navigate to a URL the listener cancels. The block manifests as a
  // committed error page (ERR_BLOCKED_BY_CLIENT under the hood). The host
  // doesn't need to resolve — onBeforeRequest fires before DNS.
  const GURL blocked_url("http://dao-mv2-blocked.test/manifest.json");
  content::WebContents* active_tab =
      browser()->tab_strip_model()->GetActiveWebContents();
  content::TestNavigationObserver navigation(active_tab);
  std::ignore = ui_test_utils::NavigateToURL(browser(), blocked_url);
  EXPECT_EQ(net::ERR_BLOCKED_BY_CLIENT, navigation.last_net_error_code());

  content::WebContents* tab =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(tab);

  EXPECT_EQ(content::PAGE_TYPE_ERROR,
            tab->GetController().GetLastCommittedEntry()->GetPageType());
}

// ----- Canary: background page persistence -----------------------------

IN_PROC_BROWSER_TEST_F(DaoMV2BrowserTest,
                       BackgroundPage_PersistsAcrossNav) {
  const extensions::Extension* extension =
      LoadExtension(TestDataDir().AppendASCII("persistent_background"));
  ASSERT_TRUE(extension);

  // Open a tab, navigate, then re-inspect the extension's background page.
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), GURL("about:blank")));

  extensions::ExtensionHost* host =
      extensions::ProcessManager::Get(profile())
          ->GetBackgroundHostForExtension(extension->id());
  ASSERT_TRUE(host);

  EXPECT_EQ("persisted",
            content::EvalJs(host->host_contents(),
                            "window.__daoMV2Marker__ || ''"));
}

// ----- Router defaults are pass-through --------------------------------

IN_PROC_BROWSER_TEST_F(DaoMV2BrowserTest, APIRouter_DefaultsArePassThrough) {
  EXPECT_TRUE(DaoMV2APIRouter::Get().WebRequestBlockingEnabled());
  EXPECT_TRUE(DaoMV2APIRouter::Get().BackgroundPagePersistenceEnabled());
}

// ----- Rebase canary: each EXPECT names the patch it covers ------------
//
// When upgrading Chromium, run browser_tests with
// --gtest_filter=DaoMV2BrowserTest.RebaseCanary_AllPatchesAreLive first.
// Each EXPECT failure includes a `<<` hint pointing at the exact
// patch (or upstream symbol) likely to have drifted, so triage on a
// rebase doesn't require re-deriving the MV2 control flow from scratch.
IN_PROC_BROWSER_TEST_F(DaoMV2BrowserTest, RebaseCanary_AllPatchesAreLive) {
  // Verify the new handler's real behavior without restoring deleted flags.
  const extensions::Extension* extension =
      LoadExtension(TestDataDir().AppendASCII("webrequest_blocking"));
  ASSERT_TRUE(extension);
  auto* handler = extensions::ManifestV2Handler::Get(profile());
  ASSERT_TRUE(handler);
  EXPECT_FALSE(handler->IsExtensionAffected(*extension))
      << "MV2 policy exemption no longer reaches the deprecation manager.";
  EXPECT_FALSE(handler->ShouldBlockExtensionInstallation(
      extension->manifest_version(), extension->GetType(),
      extensions::mojom::ManifestLocation::kInternal, extension->id()))
      << "ManifestV2Handler blocks policy-exempt MV2 CRX installs.";
  EXPECT_FALSE(handler->ShouldBlockExtensionEnable(*extension))
      << "ManifestV2Handler blocks policy-exempt MV2 enablement.";
  handler->DisableAffectedExtensionsForTesting();
  EXPECT_TRUE(extensions::ExtensionRegistry::Get(profile())
                  ->enabled_extensions()
                  .Contains(extension->id()))
      << "ManifestV2Handler disables a policy-exempt MV2 extension.";

  EXPECT_EQ(DaoMV2PrefDefaults::DefaultManifestV2Availability(),
            profile()->GetPrefs()->GetInteger(
                DaoMV2PrefDefaults::kAvailabilityPref))
      << "Dao's retained availability pref is no longer registered.";
  EXPECT_EQ(DaoMV2PrefDefaults::DefaultManifestV2Availability(),
            DaoMV2PrefDefaults::GetManifestV2Availability(profile()->GetPrefs()));
}

// Explicit enterprise extension restrictions must still win over Dao's default.
class DaoMV2ManagedPolicyBrowserTest : public DaoMV2BrowserTest {
 protected:
  void SetUpInProcessBrowserTestFixture() override {
    DaoMV2BrowserTest::SetUpInProcessBrowserTestFixture();
    policy_provider_.SetDefaultReturns(true, true);
    policy::BrowserPolicyConnector::SetPolicyProviderForTesting(&policy_provider_);
  }

  void BlockExtensionInstallation() {
    base::DictValue entry;
    entry.Set("installation_mode", "blocked");
    base::DictValue settings;
    settings.Set("*", std::move(entry));
    policy::PolicyMap policies;
    policies.Set(policy::key::kExtensionSettings,
                 policy::POLICY_LEVEL_MANDATORY, policy::POLICY_SCOPE_USER,
                 policy::POLICY_SOURCE_CLOUD, base::Value(std::move(settings)),
                 nullptr);
    policy_provider_.UpdateChromePolicy(policies);
    base::RunLoop().RunUntilIdle();
  }

 private:
  testing::NiceMock<policy::MockConfigurationPolicyProvider> policy_provider_;
};

IN_PROC_BROWSER_TEST_F(DaoMV2ManagedPolicyBrowserTest,
                       BlockPolicyOverridesDefaultCRXSupport) {
  BlockExtensionInstallation();
  base::FilePath crx_path =
      PackExtension(TestDataDir().AppendASCII("webrequest_blocking"));
  ASSERT_FALSE(crx_path.empty());
  EXPECT_EQ(nullptr,
            InstallExtensionWithUIAutoConfirm(crx_path, /*expected_change=*/0));
}

IN_PROC_BROWSER_TEST_F(DaoMV2ManagedPolicyBrowserTest,
                       BlockPolicyOverridesDefaultUnpackedSupport) {
  BlockExtensionInstallation();
  EXPECT_FALSE(TryUnpackedInstall(
                   profile(), TestDataDir().AppendASCII("webrequest_blocking"))
                   .empty());
}

TEST(DaoMV2PrefDefaultsTest, OnlyManagedValuesOverrideDefault) {
  TestingPrefServiceSimple prefs;
  prefs.registry()->RegisterIntegerPref(
      DaoMV2PrefDefaults::kAvailabilityPref,
      DaoMV2PrefDefaults::DefaultManifestV2Availability());
  prefs.SetInteger(DaoMV2PrefDefaults::kAvailabilityPref, 1);
  EXPECT_EQ(2, DaoMV2PrefDefaults::GetManifestV2Availability(&prefs));
  for (int availability : {0, 1, 2, 3}) {
    prefs.SetManagedPref(DaoMV2PrefDefaults::kAvailabilityPref,
                         base::Value(availability));
    EXPECT_EQ(availability,
              DaoMV2PrefDefaults::GetManifestV2Availability(&prefs));
  }
  prefs.SetManagedPref(DaoMV2PrefDefaults::kAvailabilityPref, base::Value(4));
  EXPECT_EQ(0, DaoMV2PrefDefaults::GetManifestV2Availability(&prefs));
}

TEST(DaoMV2PrefDefaultsTest, ManagedAllowOverridesRestoreDeprecation) {
  base::test::ScopedFeatureList features;
  features.InitAndEnableFeature(kRestoreManifestV2Deprecation);
  TestingPrefServiceSimple prefs;
  prefs.registry()->RegisterIntegerPref(
      DaoMV2PrefDefaults::kAvailabilityPref,
      DaoMV2PrefDefaults::DefaultManifestV2Availability());
  EXPECT_EQ(0, DaoMV2PrefDefaults::GetManifestV2Availability(&prefs));
  prefs.SetManagedPref(DaoMV2PrefDefaults::kAvailabilityPref, base::Value(2));
  EXPECT_EQ(2, DaoMV2PrefDefaults::GetManifestV2Availability(&prefs));
}

}  // namespace dao
