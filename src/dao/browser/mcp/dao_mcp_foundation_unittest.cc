// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "base/base_paths.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/path_service.h"
#include "base/values.h"
#include "build/build_config.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/tab_list/tab_list_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "dao/browser/automation/dao_agent_lease_manager.h"
#include "dao/browser/automation/dao_browser_automation_session.h"
#include "dao/browser/automation/dao_browser_tool_catalog.h"
#include "dao/browser/automation/dao_browser_tool_types.h"
#include "dao/browser/automation/dao_tool_schema_validator.h"
#include "dao/browser/mcp/dao_local_agent_integration.h"
#include "dao/browser/ui/views/dao_tab_identity.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/base/resource/resource_scale_factor.h"

#if BUILDFLAG(IS_WIN)
#include "base/test/file_path_reparse_point_win.h"
#endif

namespace dao {
namespace {

DaoBrowserToolDefinition MakeDefinition(base::DictValue schema) {
  DaoBrowserToolDefinition definition;
  definition.name = "test_tool";
  definition.input_schema = std::move(schema);
  return definition;
}

base::DictValue MakeConstrainedSchema() {
  return base::DictValue()
      .Set("type", "object")
      .Set("properties",
           base::DictValue()
               .Set("mode",
                    base::DictValue()
                        .Set("type", "string")
                        .Set("enum",
                             base::ListValue().Append("fast").Append("safe")))
               .Set("count", base::DictValue()
                                 .Set("type", "integer")
                                 .Set("minimum", 1)
                                 .Set("maximum", 3))
               .Set("enabled", base::DictValue().Set("type", "boolean"))
               .Set("labels",
                    base::DictValue()
                        .Set("type", "array")
                        .Set("items", base::DictValue().Set("type", "string")))
               .Set("options",
                    base::DictValue()
                        .Set("type", "object")
                        .Set("properties",
                             base::DictValue().Set("threshold",
                                                   base::DictValue()
                                                       .Set("type", "number")
                                                       .Set("minimum", 0.0)
                                                       .Set("maximum", 1.0)))
                        .Set("required", base::ListValue().Append("threshold"))
                        .Set("additionalProperties", false)))
      .Set("required", base::ListValue()
                           .Append("mode")
                           .Append("count")
                           .Append("enabled")
                           .Append("labels")
                           .Append("options"))
      .Set("additionalProperties", false);
}

base::DictValue MakeValidArguments() {
  return base::DictValue()
      .Set("mode", "safe")
      .Set("count", 3)
      .Set("enabled", true)
      .Set("labels", base::ListValue().Append("one").Append("two"))
      .Set("options", base::DictValue().Set("threshold", 0.5));
}

base::DictValue MakeIntegerSchema() {
  return base::DictValue()
      .Set("type", "object")
      .Set("properties", base::DictValue().Set(
                             "value", base::DictValue().Set("type", "integer")))
      .Set("required", base::ListValue().Append("value"))
      .Set("additionalProperties", false);
}

std::string MakeCatalogJson(std::string_view output_schema) {
  return std::string(
             R"({"version":1,"tools":[{"name":"test_tool","description":"Test tool","group":"page","clients":["mcp"],"sideEffect":"read","timeoutMs":1000,"inputSchema":{"type":"object","properties":{},"required":[],"additionalProperties":false},"outputSchema":)") +
         std::string(output_schema) + "}]}";
}

TEST(DaoMcpTypesTest, ExposesEveryStableErrorCode) {
  constexpr std::array expected = {
      std::pair{DaoToolErrorCode::kMcpDisabled, "MCP_DISABLED"},
      std::pair{DaoToolErrorCode::kTooManyClients, "TOO_MANY_CLIENTS"},
      std::pair{DaoToolErrorCode::kAuthorizationDenied, "AUTHORIZATION_DENIED"},
      std::pair{DaoToolErrorCode::kAuthorizationTimeout,
                "AUTHORIZATION_TIMEOUT"},
      std::pair{DaoToolErrorCode::kAgentControlBusy, "AGENT_CONTROL_BUSY"},
      std::pair{DaoToolErrorCode::kLeaseBusy, "LEASE_BUSY"},
      std::pair{DaoToolErrorCode::kTargetGone, "TARGET_GONE"},
      std::pair{DaoToolErrorCode::kTargetForbidden, "TARGET_FORBIDDEN"},
      std::pair{DaoToolErrorCode::kInvalidArgument, "INVALID_ARGUMENT"},
      std::pair{DaoToolErrorCode::kUnknownTool, "UNKNOWN_TOOL"},
      std::pair{DaoToolErrorCode::kIpcVersionUnsupported,
                "IPC_VERSION_UNSUPPORTED"},
      std::pair{DaoToolErrorCode::kDevToolsAttachFailed,
                "DEVTOOLS_ATTACH_FAILED"},
      std::pair{DaoToolErrorCode::kToolTimeout, "TOOL_TIMEOUT"},
      std::pair{DaoToolErrorCode::kToolCancelled, "TOOL_CANCELLED"},
      std::pair{DaoToolErrorCode::kInternalError, "INTERNAL_ERROR"},
      std::pair{DaoToolErrorCode::kSpaceNotFound, "SPACE_NOT_FOUND"},
      std::pair{DaoToolErrorCode::kSpaceUserInControl,
                "SPACE_USER_IN_CONTROL"},
      std::pair{DaoToolErrorCode::kSpaceClosed, "SPACE_CLOSED"},
      std::pair{DaoToolErrorCode::kSpaceLimitReached, "SPACE_LIMIT_REACHED"},
      std::pair{DaoToolErrorCode::kPageDialogOpen, "PAGE_DIALOG_OPEN"},
  };

  for (const auto& [code, name] : expected) {
    EXPECT_EQ(name, DaoToolErrorCodeToString(code));
  }
}

class DaoMcpCatalogTest : public testing::Test {
 protected:
  void SetUp() override {
    base::FilePath executable_dir;
    ASSERT_TRUE(base::PathService::Get(base::DIR_EXE, &executable_dir));
    const base::FilePath resource_pack =
        executable_dir.AppendASCII("gen").AppendASCII("chrome").AppendASCII(
            "dao_agent_resources.pak");
    ASSERT_TRUE(base::PathExists(resource_pack));
    if (ui::ResourceBundle::HasSharedInstance()) {
      ui::ResourceBundle::GetSharedInstance().AddDataPackFromPath(
          resource_pack, ui::kScaleFactorNone);
    } else {
      ui::ResourceBundle::InitSharedInstanceWithPakPath(resource_pack);
      owns_resource_bundle_ = true;
    }
  }

  void TearDown() override {
    if (owns_resource_bundle_) {
      ui::ResourceBundle::CleanupSharedInstance();
    }
  }

 private:
  bool owns_resource_bundle_ = false;
};

TEST_F(DaoMcpCatalogTest, ExposesExactlyFortyThreeToolsToMcp) {
  const auto tools = DaoBrowserToolCatalog::Get()->List(DaoToolClient::kMcp);
  EXPECT_EQ(43u, tools.size());
  EXPECT_EQ(nullptr, DaoBrowserToolCatalog::Get()->Find(
                         "resolve_element_context", DaoToolClient::kMcp));
  EXPECT_NE(nullptr, DaoBrowserToolCatalog::Get()->Find(
                         "resolve_element_context", DaoToolClient::kDaoAgent));
}

TEST_F(DaoMcpCatalogTest, EmbeddedSchemasEnforceRequiredFieldsAndTypes) {
  const DaoBrowserToolDefinition* click =
      DaoBrowserToolCatalog::Get()->Find("click_element", DaoToolClient::kMcp);
  ASSERT_NE(nullptr, click);

  auto missing = ValidateToolArguments(*click, base::DictValue());
  ASSERT_FALSE(missing.has_value());
  EXPECT_EQ(DaoToolErrorCode::kInvalidArgument, missing.error().code);

  auto wrong_type =
      ValidateToolArguments(*click, base::DictValue().Set("selector", 7));
  ASSERT_FALSE(wrong_type.has_value());
  EXPECT_EQ(DaoToolErrorCode::kInvalidArgument, wrong_type.error().code);

  EXPECT_TRUE(ValidateToolArguments(
                  *click, base::DictValue().Set("selector", "#submit"))
                  .has_value());
}

TEST_F(DaoMcpCatalogTest, PageAccessRequestsLoadAndRequireAReason) {
  auto catalog = DaoBrowserToolCatalog::LoadFromResourceBundle();
  ASSERT_TRUE(catalog.has_value()) << catalog.error().message;
  const DaoBrowserToolDefinition* request =
      catalog.value()->Find("request_tab_access", DaoToolClient::kMcp);
  ASSERT_NE(nullptr, request);
  EXPECT_EQ(nullptr, catalog.value()->Find("request_tab_access",
                                          DaoToolClient::kDaoAgent));

  EXPECT_FALSE(ValidateToolArguments(*request, base::DictValue()).has_value());
  EXPECT_FALSE(ValidateToolArguments(*request, base::DictValue().Set("reason", 7))
                   .has_value());
  base::DictValue arguments =
      base::DictValue().Set("reason", "Read the page for the requested task.");
  EXPECT_TRUE(ValidateToolArguments(*request, arguments).has_value());
  arguments.Set("tab_id", "requested-tab");
  EXPECT_TRUE(ValidateToolArguments(*request, arguments).has_value());
}

TEST_F(DaoMcpCatalogTest,
       MissingResourceIsObservableAndDoesNotPoisonAResourceRetry) {
  DaoBrowserToolCatalog::Cache cache;
  {
    ui::ResourceBundle::SharedInstanceSwapperForTesting no_resource_bundle;
    auto missing = cache.GetOrLoad(
        []() { return DaoBrowserToolCatalog::LoadFromResourceBundle(); });
    ASSERT_FALSE(missing.has_value());
    EXPECT_EQ(DaoToolErrorCode::kInternalError, missing.error().code);
  }

  auto retried = cache.GetOrLoad(
      []() { return DaoBrowserToolCatalog::LoadFromResourceBundle(); });
  ASSERT_TRUE(retried.has_value()) << retried.error().message;
  EXPECT_NE(nullptr,
            retried.value()->Find("click_element", DaoToolClient::kMcp));
}

TEST(DaoMcpCatalogFactoryTest, RejectsMalformedJson) {
  auto parsed = DaoBrowserToolCatalog::CreateFromJson("{");
  ASSERT_FALSE(parsed.has_value());
  EXPECT_EQ(DaoToolErrorCode::kInternalError, parsed.error().code);
}

TEST(DaoMcpCatalogFactoryTest, AcceptsSupportedOutputSchema) {
  auto parsed = DaoBrowserToolCatalog::CreateFromJson(MakeCatalogJson(
      R"({"type":"object","properties":{"ok":{"type":"boolean"}},"required":["ok"],"additionalProperties":false})"));
  ASSERT_TRUE(parsed.has_value()) << parsed.error().message;
  const DaoBrowserToolDefinition* definition =
      parsed.value()->Find("test_tool", DaoToolClient::kMcp);
  ASSERT_NE(nullptr, definition);
  ASSERT_TRUE(definition->output_schema.has_value());
  ASSERT_NE(nullptr, definition->output_schema->FindString("type"));
  EXPECT_EQ("object", *definition->output_schema->FindString("type"));
}

TEST(DaoMcpCatalogFactoryTest, RejectsNonDictionaryOutputSchema) {
  auto parsed =
      DaoBrowserToolCatalog::CreateFromJson(MakeCatalogJson(R"(["invalid"])"));
  ASSERT_FALSE(parsed.has_value());
  EXPECT_EQ(DaoToolErrorCode::kInternalError, parsed.error().code);
}

TEST(DaoMcpCatalogFactoryTest, RejectsUnsupportedOutputSchemaKeyword) {
  auto parsed = DaoBrowserToolCatalog::CreateFromJson(MakeCatalogJson(
      R"({"type":"object","properties":{"value":{"type":"string","pattern":"^allowed$"}},"required":["value"]})"));
  ASSERT_FALSE(parsed.has_value());
  EXPECT_EQ(DaoToolErrorCode::kInternalError, parsed.error().code);
}

TEST(DaoMcpSchemaTest, SupportsTheCatalogSchemaSubset) {
  DaoBrowserToolDefinition definition = MakeDefinition(MakeConstrainedSchema());
  EXPECT_TRUE(
      ValidateToolArguments(definition, MakeValidArguments()).has_value());
}

TEST(DaoMcpSchemaTest, RejectsUnsupportedEnumValue) {
  DaoBrowserToolDefinition definition = MakeDefinition(MakeConstrainedSchema());
  auto result = ValidateToolArguments(
      definition, MakeValidArguments().Set("mode", "unsupported"));
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(DaoToolErrorCode::kInvalidArgument, result.error().code);
}

TEST(DaoMcpSchemaTest, RejectsNumberBelowMinimum) {
  DaoBrowserToolDefinition definition = MakeDefinition(MakeConstrainedSchema());
  auto result =
      ValidateToolArguments(definition, MakeValidArguments().Set("count", 0));
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(DaoToolErrorCode::kInvalidArgument, result.error().code);
}

TEST(DaoMcpSchemaTest, RejectsNumberAboveMaximum) {
  DaoBrowserToolDefinition definition = MakeDefinition(MakeConstrainedSchema());
  auto result =
      ValidateToolArguments(definition, MakeValidArguments().Set("count", 4));
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(DaoToolErrorCode::kInvalidArgument, result.error().code);
}

TEST(DaoMcpSchemaTest, RejectsNestedAdditionalProperty) {
  DaoBrowserToolDefinition definition = MakeDefinition(MakeConstrainedSchema());
  auto result = ValidateToolArguments(
      definition,
      MakeValidArguments().Set(
          "options",
          base::DictValue().Set("threshold", 0.5).Set("unexpected", true)));
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(DaoToolErrorCode::kInvalidArgument, result.error().code);
}

TEST(DaoMcpSchemaTest, RejectsTopLevelAdditionalProperty) {
  DaoBrowserToolDefinition definition = MakeDefinition(MakeConstrainedSchema());
  auto result = ValidateToolArguments(
      definition, MakeValidArguments().Set("unexpected", true));
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(DaoToolErrorCode::kInvalidArgument, result.error().code);
}

TEST(DaoMcpSchemaTest, AcceptsSafeIntegralDoubleForInteger) {
  DaoBrowserToolDefinition definition = MakeDefinition(MakeIntegerSchema());
  EXPECT_TRUE(
      ValidateToolArguments(definition,
                            base::DictValue().Set("value", 9007199254740991.0))
          .has_value());
}

TEST(DaoMcpSchemaTest, RejectsFractionalDoubleForInteger) {
  DaoBrowserToolDefinition definition = MakeDefinition(MakeIntegerSchema());
  auto result =
      ValidateToolArguments(definition, base::DictValue().Set("value", 1.5));
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(DaoToolErrorCode::kInvalidArgument, result.error().code);
}

TEST(DaoMcpSchemaTest, RejectsIntegralDoubleOutsideSafeIntegerRange) {
  DaoBrowserToolDefinition definition = MakeDefinition(MakeIntegerSchema());
  auto result = ValidateToolArguments(
      definition, base::DictValue().Set("value", 9007199254740992.0));
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(DaoToolErrorCode::kInvalidArgument, result.error().code);
}

TEST(DaoMcpSchemaTest, ComparesNumericEnumsByNumericValue) {
  DaoBrowserToolDefinition definition = MakeDefinition(
      base::DictValue()
          .Set("type", "object")
          .Set("properties",
               base::DictValue().Set(
                   "value", base::DictValue()
                                .Set("type", "number")
                                .Set("enum", base::ListValue().Append(1))))
          .Set("required", base::ListValue().Append("value"))
          .Set("additionalProperties", false));

  EXPECT_TRUE(
      ValidateToolArguments(definition, base::DictValue().Set("value", 1.0))
          .has_value());
}

TEST(DaoMcpSchemaTest, RejectsDifferentNumericEnumValue) {
  DaoBrowserToolDefinition definition = MakeDefinition(
      base::DictValue()
          .Set("type", "object")
          .Set("properties",
               base::DictValue().Set(
                   "value", base::DictValue()
                                .Set("type", "number")
                                .Set("enum", base::ListValue().Append(1))))
          .Set("required", base::ListValue().Append("value"))
          .Set("additionalProperties", false));

  auto result =
      ValidateToolArguments(definition, base::DictValue().Set("value", 2.0));
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(DaoToolErrorCode::kInvalidArgument, result.error().code);
}

TEST(DaoMcpSchemaTest, FailsClosedForUnsupportedSchemaKeywords) {
  DaoBrowserToolDefinition definition = MakeDefinition(
      base::DictValue()
          .Set("type", "object")
          .Set("properties",
               base::DictValue().Set("value", base::DictValue()
                                                  .Set("type", "string")
                                                  .Set("pattern", "^allowed$")))
          .Set("required", base::ListValue()));

  auto result = ValidateToolArguments(
      definition, base::DictValue().Set("value", "allowed"));
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(DaoToolErrorCode::kInvalidArgument, result.error().code);
}

static_assert(!std::is_copy_constructible_v<DaoAgentLease>);
static_assert(!std::is_copy_assignable_v<DaoAgentLease>);
static_assert(std::is_move_constructible_v<DaoAgentLease>);
static_assert(std::is_move_assignable_v<DaoAgentLease>);

TEST(DaoMcpLeaseTest, RejectsSecondAgentUntilRelease) {
  DaoAgentLeaseManager leases;
  auto first = leases.TryAcquire({DaoToolClient::kMcp, "external-1", "Codex"});
  ASSERT_TRUE(first.has_value());
  EXPECT_EQ(
      DaoToolErrorCode::kAgentControlBusy,
      leases
          .TryAcquire({DaoToolClient::kDaoAgent, "dao-agent-turn", "Dao Agent"})
          .error()
          .code);
  first->Reset();
  EXPECT_TRUE(
      leases
          .TryAcquire({DaoToolClient::kDaoAgent, "dao-agent-turn", "Dao Agent"})
          .has_value());
}

TEST(DaoMcpLeaseTest, AllowsDifferentTabsButRejectsSameTab) {
  DaoAgentLeaseManager leases;
  auto first = leases.TryAcquire(
      tabs::TabHandle(1), {DaoToolClient::kMcp, "external-1", "Codex"});
  ASSERT_TRUE(first.has_value());
  auto second = leases.TryAcquire(
      tabs::TabHandle(2), {DaoToolClient::kMcp, "external-2", "Claude"});
  ASSERT_TRUE(second.has_value());

  auto blocked = leases.TryAcquire(
      tabs::TabHandle(1), {DaoToolClient::kMcp, "external-3", "Codex"});
  ASSERT_FALSE(blocked.has_value());
  EXPECT_EQ(DaoToolErrorCode::kLeaseBusy, blocked.error().code);

  first->Reset();
  EXPECT_TRUE(leases.TryAcquire(
      tabs::TabHandle(1),
      {DaoToolClient::kMcp, "external-3", "Codex"}).has_value());
}

TEST(DaoMcpLeaseTest, MoveAndDestructionPreserveRaiiOwnership) {
  DaoAgentLeaseManager leases;
  {
    auto acquired =
        leases.TryAcquire({DaoToolClient::kMcp, "external-1", "Codex"});
    ASSERT_TRUE(acquired.has_value());
    DaoAgentLease moved = std::move(acquired.value());

    auto blocked =
        leases.TryAcquire({DaoToolClient::kMcp, "external-2", "Claude"});
    ASSERT_FALSE(blocked.has_value());
    EXPECT_EQ(DaoToolErrorCode::kLeaseBusy, blocked.error().code);
  }

  EXPECT_TRUE(leases.TryAcquire({DaoToolClient::kMcp, "external-2", "Claude"})
                  .has_value());
}

TEST(DaoMcpLeaseTest, ManagerMayBeDestroyedBeforeLease) {
  auto leases = std::make_unique<DaoAgentLeaseManager>();
  auto acquired =
      leases->TryAcquire({DaoToolClient::kMcp, "external-1", "Codex"});
  ASSERT_TRUE(acquired.has_value());
  leases.reset();

  acquired->Reset();
}

TEST(DaoMcpLeaseTest, MoveAssignmentReleasesOldLeaseAndTransfersNewLease) {
  DaoAgentLeaseManager first_manager;
  DaoAgentLeaseManager second_manager;
  auto first =
      first_manager.TryAcquire({DaoToolClient::kMcp, "external-1", "Codex"});
  auto second =
      second_manager.TryAcquire({DaoToolClient::kMcp, "external-2", "Claude"});
  ASSERT_TRUE(first.has_value());
  ASSERT_TRUE(second.has_value());

  first.value() = std::move(second.value());

  EXPECT_TRUE(
      first_manager
          .TryAcquire({DaoToolClient::kDaoAgent, "dao-agent", "Dao Agent"})
          .has_value());
  EXPECT_EQ(
      DaoToolErrorCode::kAgentControlBusy,
      second_manager
          .TryAcquire({DaoToolClient::kDaoAgent, "dao-agent", "Dao Agent"})
          .error()
          .code);
  first->Reset();
  EXPECT_TRUE(
      second_manager
          .TryAcquire({DaoToolClient::kDaoAgent, "dao-agent", "Dao Agent"})
          .has_value());
}

TEST(DaoMcpLeaseTest, RepeatedResetIsIdempotent) {
  DaoAgentLeaseManager leases;
  auto acquired =
      leases.TryAcquire({DaoToolClient::kMcp, "external-1", "Codex"});
  ASSERT_TRUE(acquired.has_value());

  acquired->Reset();
  acquired->Reset();

  EXPECT_TRUE(
      leases.TryAcquire({DaoToolClient::kDaoAgent, "dao-agent", "Dao Agent"})
          .has_value());
}

class DaoMcpLeaseBrowserTest : public InProcessBrowserTest {};

IN_PROC_BROWSER_TEST_F(DaoMcpLeaseBrowserTest,
                       ProfileOwnsOneSharedLeaseManager) {
  DaoAgentLeaseManager* first =
      DaoAgentLeaseManager::GetForProfile(browser()->GetProfile());
  DaoAgentLeaseManager* second =
      DaoAgentLeaseManager::GetForProfile(browser()->GetProfile());
  ASSERT_NE(nullptr, first);
  ASSERT_EQ(first, second);

  auto external =
      first->TryAcquire({DaoToolClient::kMcp, "external-1", "Codex"});
  ASSERT_TRUE(external.has_value());
  auto blocked =
      second->TryAcquire({DaoToolClient::kDaoAgent, "dao-agent", "Dao Agent"});
  ASSERT_FALSE(blocked.has_value());
  EXPECT_EQ(DaoToolErrorCode::kAgentControlBusy, blocked.error().code);
}

class DaoMcpSessionTest : public InProcessBrowserTest {};

IN_PROC_BROWSER_TEST_F(DaoMcpSessionTest,
                       ResolvesEligibleTargetInAuthorizedWindow) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), GURL("about:blank")));
  content::WebContents* authorized_target =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_NE(nullptr, authorized_target);

  DaoBrowserAutomationSession session(browser(), authorized_target);
  EXPECT_EQ(browser(), session.browser_window());
  EXPECT_EQ(authorized_target, session.ResolveTarget().value());
  auto eligible_target = session.ResolveEligibleTarget();
  ASSERT_TRUE(eligible_target.has_value())
      << "url=" << authorized_target->GetLastCommittedURL()
      << " browser_type=" << browser()->GetType()
      << " off_the_record=" << browser()->GetProfile()->IsOffTheRecord()
      << " guest=" << browser()->GetProfile()->IsGuestSession();
  EXPECT_EQ(authorized_target, eligible_target.value());

  session.set_expected_domain("example.test");
  EXPECT_EQ("example.test", session.expected_domain());
}

IN_PROC_BROWSER_TEST_F(DaoMcpSessionTest,
                       ForbiddenTargetDoesNotFallBackToEligibleTab) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), GURL("data:text/html,<title>Forbidden</title>")));
  content::WebContents* forbidden_target =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_NE(nullptr, forbidden_target);
  DaoBrowserAutomationSession session(browser(), forbidden_target);

  TabListInterface* tabs = TabListInterface::From(browser());
  ASSERT_NE(nullptr, tabs);
  ASSERT_NE(nullptr,
            tabs->OpenTab(GURL("about:blank"), TabStripModel::kNoTab, true));

  auto resolved = session.ResolveEligibleTarget();
  ASSERT_FALSE(resolved.has_value());
  EXPECT_EQ(DaoToolErrorCode::kTargetForbidden, resolved.error().code);
}

IN_PROC_BROWSER_TEST_F(
    DaoMcpSessionTest,
    RejectsSameProfileWebContentsOutsideAuthorizedWindowAtConstruction) {
  auto unauthorized_target = content::WebContents::Create(
      content::WebContents::CreateParams(browser()->GetProfile()));
  ASSERT_NE(nullptr, unauthorized_target);

  DaoBrowserAutomationSession session(browser(), unauthorized_target.get());
  auto resolved = session.ResolveEligibleTarget();
  ASSERT_FALSE(resolved.has_value());
  EXPECT_EQ(DaoToolErrorCode::kTargetGone, resolved.error().code);
}

IN_PROC_BROWSER_TEST_F(DaoMcpSessionTest,
                       DetachedTargetIsGoneEvenWhileWebContentsRemainsAlive) {
  TabListInterface* tab_list = TabListInterface::From(browser());
  ASSERT_NE(nullptr, tab_list);
  tabs::TabInterface* target_tab =
      tab_list->OpenTab(GURL("about:blank"), TabStripModel::kNoTab, true);
  ASSERT_NE(nullptr, target_tab);
  content::WebContents* authorized_target = target_tab->GetContents();
  ASSERT_NE(nullptr, authorized_target);
  DaoBrowserAutomationSession session(browser(), authorized_target);
  ASSERT_EQ(authorized_target, session.ResolveTarget().value());

  std::unique_ptr<content::WebContents> detached =
      tab_list->DetachWebContents(target_tab->GetHandle());
  ASSERT_EQ(authorized_target, detached.get());

  auto resolved = session.ResolveTarget();
  ASSERT_FALSE(resolved.has_value());
  EXPECT_EQ(DaoToolErrorCode::kTargetGone, resolved.error().code);
}

IN_PROC_BROWSER_TEST_F(DaoMcpSessionTest,
                       TargetMovedToSameProfileWindowIsGoneForSession) {
  TabListInterface* source_tabs = TabListInterface::From(browser());
  ASSERT_NE(nullptr, source_tabs);
  tabs::TabInterface* target_tab =
      source_tabs->OpenTab(GURL("about:blank"), TabStripModel::kNoTab, true);
  ASSERT_NE(nullptr, target_tab);
  content::WebContents* authorized_target = target_tab->GetContents();
  const tabs::TabHandle target_handle = target_tab->GetHandle();
  DaoBrowserAutomationSession session(browser(), authorized_target);
  ASSERT_EQ(authorized_target, session.ResolveTarget().value());

  BrowserWindowInterface* destination = CreateBrowser(browser()->GetProfile());
  TabListInterface* destination_tabs = TabListInterface::From(destination);
  ASSERT_NE(nullptr, destination_tabs);
  source_tabs->MoveTabToWindow(target_handle, destination->GetSessionID(), 0);
  EXPECT_GE(destination_tabs->GetIndexOfTab(target_handle), 0);

  auto resolved = session.ResolveTarget();
  ASSERT_FALSE(resolved.has_value());
  EXPECT_EQ(DaoToolErrorCode::kTargetGone, resolved.error().code);
}

IN_PROC_BROWSER_TEST_F(DaoMcpSessionTest,
                       FollowsSameTabAcrossWebContentsReplacement) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), GURL("about:blank")));
  TabListInterface* tab_list = TabListInterface::From(browser());
  ASSERT_NE(nullptr, tab_list);
  tabs::TabInterface* target_tab = tab_list->GetActiveTab();
  ASSERT_NE(nullptr, target_tab);
  const tabs::TabHandle target_handle = target_tab->GetHandle();
  content::WebContents* original_contents = target_tab->GetContents();
  const std::string stable_id = GetOrCreateSidebarTabId(original_contents);
  DaoBrowserAutomationSession session(browser(), original_contents);
  ASSERT_NE(nullptr, tab_list->OpenTab(GURL("about:blank"),
                                       TabStripModel::kNoTab, true));

  auto replacement = content::WebContents::Create(
      content::WebContents::CreateParams(browser()->GetProfile()));
  content::WebContents* replacement_contents = replacement.get();
  const int target_index = tab_list->GetIndexOfTab(target_handle);
  ASSERT_GE(target_index, 0);
  std::unique_ptr<content::WebContents> discarded =
      browser()->tab_strip_model()->DiscardWebContents(
          browser()->tab_strip_model()->GetWebContentsAt(target_index),
          std::move(replacement));
  ASSERT_EQ(original_contents, discarded.get());
  ASSERT_TRUE(
      content::NavigateToURL(replacement_contents, GURL("about:blank")));

  auto resolved = session.ResolveEligibleTarget();

  ASSERT_TRUE(resolved.has_value());
  EXPECT_EQ(replacement_contents, resolved.value());
  EXPECT_EQ(target_handle.Get()->GetContents(), resolved.value());
  EXPECT_EQ(stable_id, GetOrCreateSidebarTabId(replacement_contents));
  content::NavigationEntry* entry =
      replacement_contents->GetController().GetLastCommittedEntry();
  ASSERT_NE(nullptr, entry);
  EXPECT_EQ(
      replacement_contents->GetPrimaryMainFrame()->GetLastCommittedOrigin(),
      session.committed_origin());
  EXPECT_EQ(entry->GetMainFrameDocumentSequenceNumber(),
            session.document_sequence_number());
}

class DaoLocalAgentIntegrationTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(temp_.CreateUniqueTempDir());
    home_ = temp_.GetPath().AppendASCII("home");
#if BUILDFLAG(IS_WIN)
    helper_ = temp_.GetPath().AppendASCII("Dao/154.0.1/dao-browser.exe");
#else
    helper_ = temp_.GetPath().AppendASCII("Dao.app/Contents/Helpers/dao-browser");
#endif
    ASSERT_TRUE(base::CreateDirectory(helper_.DirName()));
    ASSERT_TRUE(base::WriteFile(helper_, "#!/bin/sh\n"));
  }

  base::FilePath LauncherPath() {
#if BUILDFLAG(IS_WIN)
    return home_.AppendASCII(".local/bin/dao-browser.cmd");
#else
    return home_.AppendASCII(".local/bin/dao-browser");
#endif
  }

  base::FilePath SkillPath(const char* root) {
    return home_.AppendASCII(root).AppendASCII("skills/dao-browser/SKILL.md");
  }

  void WriteSkill(const char* root, const std::string& contents) {
    const auto path = SkillPath(root);
    ASSERT_TRUE(base::CreateDirectory(path.DirName()));
    ASSERT_TRUE(base::WriteFile(path, contents));
  }

  base::DictValue SharedStatus() {
    auto status = GetDaoLocalAgentIntegrationStatus(home_, helper_);
    for (const auto& agent : *status.FindList("agents")) {
      if (*agent.GetDict().FindString("id") == "shared") {
        return agent.GetDict().Clone();
      }
    }
    ADD_FAILURE() << "Missing shared integration status";
    return base::DictValue();
  }

  base::ScopedTempDir temp_;
  base::FilePath home_;
  base::FilePath helper_;
};

TEST_F(DaoLocalAgentIntegrationTest, ReadsStatusWithoutInstalling) {
  EXPECT_EQ(false, SharedStatus().FindBool("installed"));
  EXPECT_FALSE(base::PathExists(home_));
}

TEST_F(DaoLocalAgentIntegrationTest, InstallsAndRemovesSelectedIntegration) {
  ASSERT_TRUE(UpdateDaoLocalAgentIntegration(home_, helper_, "shared", true));
  std::string contents;
  ASSERT_TRUE(base::ReadFileToString(SkillPath(".agents"), &contents));
  EXPECT_EQ(BuildDaoEntrySkill(helper_), contents);
  EXPECT_TRUE(
      base::PathExists(home_.AppendASCII(".agents/skills/dao-lite/SKILL.md")));
  EXPECT_EQ(false, SharedStatus().FindBool("needsUpdate"));
  EXPECT_FALSE(base::PathExists(home_.AppendASCII(".codex")));
  EXPECT_FALSE(base::PathExists(home_.AppendASCII(".claude")));
  EXPECT_FALSE(base::PathExists(home_.AppendASCII(".local")));

  ASSERT_TRUE(UpdateDaoLocalAgentIntegration(home_, helper_, "claude", true));
  ASSERT_TRUE(UpdateDaoLocalAgentIntegration(home_, helper_, "cli", true));
  const auto link = LauncherPath();
#if BUILDFLAG(IS_WIN)
  EXPECT_TRUE(base::PathExists(link));
  EXPECT_FALSE(base::IsLink(link));
#else
  base::FilePath target;
  ASSERT_TRUE(base::ReadSymbolicLink(link, &target));
  EXPECT_EQ(helper_, target);
#endif

  ASSERT_TRUE(UpdateDaoLocalAgentIntegration(home_, helper_, "shared", false));
  EXPECT_FALSE(base::PathExists(SkillPath(".agents")));
  EXPECT_FALSE(
      base::PathExists(home_.AppendASCII(".agents/skills/dao-lite/SKILL.md")));
  EXPECT_TRUE(
      base::PathExists(home_.AppendASCII(".claude/skills/dao-lite/SKILL.md")));
  EXPECT_TRUE(base::PathExists(SkillPath(".claude")));
  EXPECT_TRUE(base::PathExists(link));
  ASSERT_TRUE(UpdateDaoLocalAgentIntegration(home_, helper_, "cli", false));
  EXPECT_FALSE(base::PathExists(link));
}

TEST_F(DaoLocalAgentIntegrationTest, RepairsPartialSkillPairAndPreservesCustomLite) {
  WriteSkill(".agents", BuildDaoEntrySkill(helper_));
  EXPECT_EQ(true, SharedStatus().FindBool("needsUpdate"));
  ASSERT_TRUE(UpdateDaoLocalAgentIntegration(home_, helper_, "shared", true));
  const auto lite = home_.AppendASCII(".agents/skills/dao-lite/SKILL.md");
  std::string contents;
  ASSERT_TRUE(base::ReadFileToString(lite, &contents));
  EXPECT_EQ(BuildDaoEntrySkill(helper_, true), contents);
  EXPECT_EQ(false, SharedStatus().FindBool("needsUpdate"));
  ASSERT_TRUE(base::WriteFile(lite, "custom lite skill"));
  EXPECT_FALSE(UpdateDaoLocalAgentIntegration(home_, helper_, "shared", true));
  EXPECT_EQ(true, SharedStatus().FindBool("needsUpdate"));
  ASSERT_TRUE(UpdateDaoLocalAgentIntegration(home_, helper_, "shared", false));
  EXPECT_FALSE(base::PathExists(SkillPath(".agents")));
  ASSERT_TRUE(base::ReadFileToString(lite, &contents));
  EXPECT_EQ("custom lite skill", contents);
}

TEST_F(DaoLocalAgentIntegrationTest, DetectsAndUpdatesOutdatedSkills) {
  WriteSkill(".agents", BuildDaoEntrySkill(helper_) + "old content\n");
  EXPECT_EQ(true, SharedStatus().FindBool("needsUpdate"));
  ASSERT_TRUE(UpdateDaoLocalAgentIntegration(home_, helper_, "shared", true));
  EXPECT_EQ(false, SharedStatus().FindBool("needsUpdate"));
  // An app move also makes the embedded fallback command stale.
  const auto moved =
      temp_.GetPath().AppendASCII("Moved.app/Contents/Helpers/dao-browser");
  auto status = GetDaoLocalAgentIntegrationStatus(home_, moved);
  EXPECT_EQ(true,
            (*status.FindList("agents"))[0].GetDict().FindBool("needsUpdate"));
}

TEST_F(DaoLocalAgentIntegrationTest, MigratesOnlyManagedCodexSkillOnRequest) {
  WriteSkill(".codex", BuildDaoEntrySkill(helper_));
  EXPECT_EQ(true, SharedStatus().FindBool("installed"));
  EXPECT_EQ(true, SharedStatus().FindBool("needsUpdate"));
  EXPECT_FALSE(base::PathExists(SkillPath(".agents")));
  EXPECT_TRUE(base::PathExists(SkillPath(".codex")));
  ASSERT_TRUE(UpdateDaoLocalAgentIntegration(home_, helper_, "shared", true));
  EXPECT_TRUE(base::PathExists(SkillPath(".agents")));
  EXPECT_FALSE(base::PathExists(SkillPath(".codex")));

  WriteSkill(".codex", BuildDaoEntrySkill(helper_));
  ASSERT_TRUE(UpdateDaoLocalAgentIntegration(home_, helper_, "shared", false));
  EXPECT_FALSE(base::PathExists(SkillPath(".agents")));
  EXPECT_FALSE(base::PathExists(SkillPath(".codex")));
}

TEST_F(DaoLocalAgentIntegrationTest, PreservesCustomFilesAndReportsConflicts) {
  const auto empty_skill_dir = SkillPath(".agents").DirName();
  ASSERT_TRUE(base::CreateDirectory(empty_skill_dir));
  ASSERT_TRUE(UpdateDaoLocalAgentIntegration(home_, helper_, "shared", false));
  EXPECT_TRUE(base::DirectoryExists(empty_skill_dir));
  WriteSkill(".agents", "my notes");
  WriteSkill(".codex", BuildDaoEntrySkill(helper_));
  EXPECT_FALSE(UpdateDaoLocalAgentIntegration(home_, helper_, "shared", true));
  // Failed migration must leave the working legacy skill available.
  EXPECT_TRUE(base::PathExists(SkillPath(".codex")));
  ASSERT_TRUE(UpdateDaoLocalAgentIntegration(home_, helper_, "shared", false));
  std::string contents;
  ASSERT_TRUE(base::ReadFileToString(SkillPath(".agents"), &contents));
  EXPECT_EQ("my notes", contents);

  WriteSkill(".codex", "custom codex skill");
  WriteSkill(".agents", BuildDaoEntrySkill(helper_));
  const auto extra = SkillPath(".agents").DirName().AppendASCII("notes.txt");
  ASSERT_TRUE(base::WriteFile(extra, "keep me"));
  ASSERT_TRUE(UpdateDaoLocalAgentIntegration(home_, helper_, "shared", false));
  EXPECT_TRUE(base::PathExists(extra));
  ASSERT_TRUE(base::ReadFileToString(SkillPath(".codex"), &contents));
  EXPECT_EQ("custom codex skill", contents);
}

TEST_F(DaoLocalAgentIntegrationTest, RejectsInvalidTargetsAndUnavailableHelper) {
  EXPECT_FALSE(UpdateDaoLocalAgentIntegration(home_, helper_, "../other", true));
  EXPECT_FALSE(base::PathExists(home_));
  ASSERT_TRUE(base::CreateDirectory(home_));
  ASSERT_TRUE(base::WriteFile(home_.AppendASCII(".agents"), "blocked"));
  EXPECT_FALSE(UpdateDaoLocalAgentIntegration(home_, helper_, "shared", true));
  ASSERT_TRUE(base::DeleteFile(helper_));
  EXPECT_FALSE(UpdateDaoLocalAgentIntegration(home_, helper_, "cli", true));
}

#if !BUILDFLAG(IS_WIN)
TEST_F(DaoLocalAgentIntegrationTest, PreservesSymlinksToUserSkills) {
  const auto source = temp_.GetPath().AppendASCII("user-skill.md");
  ASSERT_TRUE(base::WriteFile(source, BuildDaoEntrySkill(helper_)));
  const auto skill = SkillPath(".agents");
  ASSERT_TRUE(base::CreateDirectory(skill.DirName()));
  ASSERT_TRUE(base::CreateSymbolicLink(source, skill));
  EXPECT_FALSE(UpdateDaoLocalAgentIntegration(home_, helper_, "shared", true));
  EXPECT_TRUE(UpdateDaoLocalAgentIntegration(home_, helper_, "shared", false));
  EXPECT_TRUE(base::IsLink(skill));
  EXPECT_TRUE(base::PathExists(source));
}
#endif

TEST_F(DaoLocalAgentIntegrationTest, DetectsOldCliLinksAndPreservesCustomCommands) {
  const auto link = LauncherPath();
  const auto old =
      temp_.GetPath().AppendASCII("Old.app/Contents/Helpers/dao-browser");
  ASSERT_TRUE(base::CreateDirectory(link.DirName()));
#if BUILDFLAG(IS_WIN)
  ASSERT_TRUE(base::CreateDirectory(old.DirName()));
  ASSERT_TRUE(base::WriteFile(old, "old helper"));
  ASSERT_TRUE(UpdateDaoLocalAgentIntegration(home_, old, "cli", true));
#else
  ASSERT_TRUE(base::CreateSymbolicLink(old, link));
#endif
  auto status = GetDaoLocalAgentIntegrationStatus(home_, helper_);
  EXPECT_EQ(true, status.FindBool("cliLinked"));
  EXPECT_EQ(true, status.FindBool("cliNeedsUpdate"));
  ASSERT_TRUE(UpdateDaoLocalAgentIntegration(home_, helper_, "cli", true));
  EXPECT_EQ(false, GetDaoLocalAgentIntegrationStatus(home_, helper_)
                       .FindBool("cliNeedsUpdate"));
  ASSERT_TRUE(base::DeleteFile(link));
  ASSERT_TRUE(base::WriteFile(link, "custom command"));
  EXPECT_FALSE(UpdateDaoLocalAgentIntegration(home_, helper_, "cli", true));
  EXPECT_TRUE(UpdateDaoLocalAgentIntegration(home_, helper_, "cli", false));
  EXPECT_TRUE(base::PathExists(link));
}

#if BUILDFLAG(IS_WIN)
TEST_F(DaoLocalAgentIntegrationTest, QuotesWindowsPathsInLauncherAndSkills) {
  helper_ = temp_.GetPath().Append(base::FilePath::FromUTF8Unsafe(
      "Dao 100% ready! O'Brien 中文/dao-browser.exe"));
  ASSERT_TRUE(base::CreateDirectory(helper_.DirName()));
  ASSERT_TRUE(base::WriteFile(helper_, "helper"));
  ASSERT_TRUE(UpdateDaoLocalAgentIntegration(home_, helper_, "cli", true));
  std::string contents;
  ASSERT_TRUE(base::ReadFileToString(LauncherPath(), &contents));
  EXPECT_NE(std::string::npos, contents.find("100%% ready! O'Brien 中文"));
  EXPECT_NE(std::string::npos, contents.find("setlocal DisableDelayedExpansion"));
  EXPECT_NE(std::string::npos, contents.find("chcp 65001 >nul"));
  EXPECT_NE(std::string::npos, contents.find("\" %*\r\n"));
  EXPECT_NE(std::string::npos, contents.find("exit /b %_DAO_EXIT_CODE%"));
  for (bool lite : {false, true}) {
    const std::string skill = BuildDaoEntrySkill(helper_, lite);
    EXPECT_NE(std::string::npos, skill.find("O''Brien 中文"));
    EXPECT_NE(std::string::npos, skill.find("run `& '"));
  }
}

TEST_F(DaoLocalAgentIntegrationTest, PreservesJunctionsToUserSkills) {
  const auto source = temp_.GetPath().AppendASCII("user-skill");
  ASSERT_TRUE(base::CreateDirectory(source));
  const auto source_skill = source.AppendASCII("SKILL.md");
  ASSERT_TRUE(base::WriteFile(source_skill, BuildDaoEntrySkill(helper_)));
  const auto skill_dir = SkillPath(".agents").DirName();
  ASSERT_TRUE(base::CreateDirectory(skill_dir));
  auto junction = base::test::FilePathReparsePoint::Create(skill_dir, source);
  ASSERT_TRUE(junction.has_value());
  EXPECT_FALSE(UpdateDaoLocalAgentIntegration(home_, helper_, "shared", true));
  EXPECT_TRUE(UpdateDaoLocalAgentIntegration(home_, helper_, "shared", false));
  EXPECT_TRUE(base::IsLink(skill_dir));
  EXPECT_TRUE(base::PathExists(source_skill));
}
#endif

}  // namespace
}  // namespace dao
