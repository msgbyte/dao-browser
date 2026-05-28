// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <tuple>

#include "base/files/file_path.h"
#include "base/path_service.h"
#include "base/test/scoped_feature_list.h"
#include "chrome/browser/extensions/extension_browsertest.h"
#include "chrome/browser/extensions/extension_management.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "dao/browser/extensions/legacy_mv2/dao_mv2_api_router.h"
#include "dao/browser/extensions/legacy_mv2/dao_mv2_features.h"
#include "dao/browser/extensions/legacy_mv2/dao_mv2_install_notice.h"
#include "dao/browser/extensions/legacy_mv2/dao_mv2_pref_defaults.h"
#include "extensions/browser/extension_host.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/process_manager.h"
#include "extensions/common/extension.h"

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

}  // namespace

// ----- Baseline: MV2 enabled by default ---------------------------------

using DaoMV2BrowserTest = extensions::ExtensionBrowserTest;

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

  // And the management object exposes that as "exempt" for an MV2 extension.
  auto* mgmt =
      extensions::ExtensionManagementFactory::GetForBrowserContext(profile());
  ASSERT_TRUE(mgmt);
  EXPECT_TRUE(mgmt->IsExemptFromMV2DeprecationByPolicy(
      /*manifest_version=*/2,
      /*extension_id=*/"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
      extensions::Manifest::Type::TYPE_EXTENSION));
}

// ----- Flag ON: fall back to Chromium deprecation -----------------------

class DaoMV2BrowserTestRestoreDeprecation
    : public extensions::ExtensionBrowserTest {
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

  // And the management object is no longer exempt.
  auto* mgmt =
      extensions::ExtensionManagementFactory::GetForBrowserContext(profile());
  ASSERT_TRUE(mgmt);
  EXPECT_FALSE(mgmt->IsExemptFromMV2DeprecationByPolicy(
      /*manifest_version=*/2,
      /*extension_id=*/"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
      extensions::Manifest::Type::TYPE_EXTENSION));
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
  std::ignore = ui_test_utils::NavigateToURL(browser(), blocked_url);

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

}  // namespace dao
