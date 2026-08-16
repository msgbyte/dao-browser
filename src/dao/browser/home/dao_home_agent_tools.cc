// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/home/dao_home_agent_tools.h"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/types/expected.h"
#include "dao/browser/home/dao_home_history_material.h"
#include "dao/browser/home/dao_home_manifest.h"
#include "dao/browser/home/dao_home_project_service.h"
#include "dao/browser/home/dao_home_sample_shape.h"

namespace dao {
namespace {

const char* HomeErrorToString(HomeError error) {
  switch (error) {
    case HomeError::kInvalidArgument:
      return "invalid_argument";
    case HomeError::kInvalidPath:
      return "invalid_path";
    case HomeError::kNotFound:
      return "not_found";
    case HomeError::kAlreadyExists:
      return "already_exists";
    case HomeError::kUnsupportedFormat:
      return "unsupported_format";
    case HomeError::kInvalidManifest:
      return "invalid_manifest";
    case HomeError::kInvalidPatch:
      return "invalid_patch";
    case HomeError::kPatchContextMismatch:
      return "patch_context_mismatch";
    case HomeError::kQuotaExceeded:
      return "quota_exceeded";
    case HomeError::kStaleRevision:
      return "stale_revision";
    case HomeError::kInvalidDraft:
      return "invalid_draft";
    case HomeError::kPermissionRequired:
      return "permission_required";
    case HomeError::kCancelled:
      return "cancelled";
    case HomeError::kIoError:
      return "io_error";
  }
}

base::Value ErrorValue(HomeError error, std::string message) {
  return base::Value(base::DictValue()
                         .Set("error", std::move(message))
                         .Set("code", HomeErrorToString(error)));
}

base::Value InvalidArgument(std::string message) {
  return ErrorValue(HomeError::kInvalidArgument, std::move(message));
}

const char* SafeConnectorErrorCode(std::string_view code) {
  if (code == "auth_required") {
    return "auth_required";
  }
  if (code == "invalid_response") {
    return "invalid_response";
  }
  if (code == "schema_failed") {
    return "schema_failed";
  }
  if (code == "cancelled") {
    return "cancelled";
  }
  if (code == "not_found") {
    return "not_found";
  }
  if (code == "timeout") {
    return "timeout";
  }
  return "runtime_error";
}

const char* SafeConnectorErrorMessage(std::string_view code) {
  if (code == "auth_required") {
    return "The connector requires authentication.";
  }
  if (code == "invalid_response" || code == "schema_failed") {
    return "The connector result did not match its schema.";
  }
  if (code == "cancelled") {
    return "The connector test was cancelled.";
  }
  if (code == "not_found") {
    return "The connector source is unavailable.";
  }
  if (code == "timeout") {
    return "The connector test timed out.";
  }
  return "The connector test failed.";
}

base::DictValue SafeConnectorError(std::string_view untrusted_code) {
  const char* code = SafeConnectorErrorCode(untrusted_code);
  return base::DictValue()
      .Set("error", SafeConnectorErrorMessage(code))
      .Set("code", code);
}

struct ConnectorRunnerOutcome {
  std::optional<base::Value> result;
  std::string error_code;
};

ConnectorRunnerOutcome ParseConnectorRunnerOutcome(
    const base::Value& envelope_value) {
  const base::DictValue* envelope = envelope_value.GetIfDict();
  const std::optional<bool> ok =
      envelope ? envelope->FindBool("ok") : std::nullopt;
  if (ok && *ok) {
    const base::Value* result = envelope->Find("result");
    if (result) {
      return {.result = result->Clone()};
    }
  }
  const std::string* code = envelope ? envelope->FindString("code") : nullptr;
  return {.error_code = SafeConnectorErrorCode(
              ok && !*ok && code ? *code : "runtime_error")};
}

base::DictValue ConnectorToValue(const HomeConnector& connector, bool granted) {
  base::ListValue origins;
  for (const url::Origin& origin : connector.permissions.origins) {
    origins.Append(origin.Serialize());
  }
  base::ListValue paths;
  for (const std::string& path : connector.permissions.paths) {
    paths.Append(path);
  }
  base::ListValue capabilities;
  for (HomePageCapability capability : connector.permissions.capabilities) {
    capabilities.Append(HomePageCapabilityToString(capability));
  }
  return base::DictValue()
      .Set("id", connector.id)
      .Set("module", connector.module)
      .Set("schema", connector.schema)
      .Set("origins", std::move(origins))
      .Set("paths", std::move(paths))
      .Set("capabilities", std::move(capabilities))
      .Set("mode", "read")
      .Set("granted", granted);
}

base::DictValue VersionToValue(const HomeVersion& version) {
  base::ListValue changed_files;
  for (const std::string& path : version.changed_files) {
    changed_files.Append(path);
  }
  return base::DictValue()
      .Set("id", version.id)
      .Set("parent", version.parent)
      .Set("restored_from", version.restored_from)
      .Set("summary", version.summary)
      .Set("kind", HomeRevisionKindToString(version.kind))
      .Set("created_at_ms", static_cast<double>(version.created_at_ms))
      .Set("changed_files", std::move(changed_files));
}

base::DictValue DraftToValue(const HomeDraft& draft) {
  base::ListValue expanded_connectors;
  for (const std::string& connector_id :
       draft.permission_expanded_connector_ids) {
    expanded_connectors.Append(connector_id);
  }
  return base::DictValue()
      .Set("draft_id", draft.id)
      .Set("base_revision", draft.base_revision)
      .Set("summary", draft.summary)
      .Set("permission_expansion", draft.permission_expansion)
      .Set("permission_expanded_connector_ids", std::move(expanded_connectors))
      .Set("manifest", HomeManifestToValue(draft.manifest));
}

bool IsGranted(const HomeSnapshot& snapshot, std::string_view connector_id) {
  return std::ranges::find(snapshot.granted_connector_ids, connector_id) !=
         snapshot.granted_connector_ids.end();
}

const HomeConnector* FindHomeConnector(const HomeManifest& manifest,
                                       std::string_view connector_id) {
  const auto found = std::ranges::find_if(manifest.connectors,
                                          [&](const HomeConnector& connector) {
                                            return connector.id == connector_id;
                                          });
  return found == manifest.connectors.end() ? nullptr : &*found;
}

bool HasCurrentRevision(const base::DictValue& arguments,
                        const HomeSnapshot& snapshot,
                        const char* key = "base_revision") {
  const std::string* revision = arguments.FindString(key);
  return revision && *revision == snapshot.revision;
}

bool CanRunWithoutPublishedProject(std::string_view name) {
  return name == "home_apply_patch" || name == "home_preview" ||
         name == "home_publish" || name == "home_request_source_access" ||
         name == "home_request_bootstrap_sources" ||
         name == "home_test_connector" || name == "home_get_bootstrap_brief";
}

bool IsMutationTool(std::string_view name) {
  return name == "home_apply_patch" || name == "home_replace_files" ||
         name == "home_add_asset" || name == "home_preview" ||
         name == "home_publish" || name == "home_rollback" ||
         name == "home_request_source_access" ||
         name == "home_request_bootstrap_sources" ||
         name == "home_test_connector" || name == "home_get_bootstrap_brief";
}

bool IsMutationAuthorized(
    const scoped_refptr<DaoHomeMutationLease>& authorization,
    const DaoHomeAgentTools::OwnerValidator& owner_validator) {
  return authorization && authorization->IsValid() && owner_validator &&
         owner_validator.Run();
}

}  // namespace

DaoHomeAgentTools::DaoHomeAgentTools(DaoHomeProjectService* service)
    : service_(service) {}

DaoHomeAgentTools::~DaoHomeAgentTools() = default;

void DaoHomeAgentTools::SetConnectorRunner(ConnectorRunner connector_runner) {
  connector_runner_ = std::move(connector_runner);
}

void DaoHomeAgentTools::Execute(std::string name,
                                base::DictValue arguments,
                                Callback callback) {
  Execute(std::move(name), std::move(arguments),
          base::MakeRefCounted<DaoHomeMutationLease>(),
          base::MakeRefCounted<DaoHomeMutationLease>(),
          base::BindRepeating([]() { return true; }), /*agent_turn_id=*/{},
          std::move(callback));
}

void DaoHomeAgentTools::Execute(
    std::string name,
    base::DictValue arguments,
    scoped_refptr<DaoHomeMutationLease> authorization,
    scoped_refptr<DaoHomeMutationLease> turn_authorization,
    OwnerValidator owner_validator,
    std::string agent_turn_id,
    Callback callback) {
  service_->GetSnapshot(base::BindOnce(
      &DaoHomeAgentTools::ExecuteWithSnapshot, weak_factory_.GetWeakPtr(),
      std::move(name), std::move(arguments), std::move(authorization),
      std::move(turn_authorization), std::move(owner_validator),
      std::move(agent_turn_id), std::move(callback)));
}

void DaoHomeAgentTools::ExecuteWithSnapshot(
    std::string name,
    base::DictValue arguments,
    scoped_refptr<DaoHomeMutationLease> authorization,
    scoped_refptr<DaoHomeMutationLease> turn_authorization,
    OwnerValidator owner_validator,
    std::string agent_turn_id,
    Callback callback,
    HomeSnapshot snapshot) {
  if (IsMutationTool(name) &&
      !IsMutationAuthorized(authorization, owner_validator)) {
    std::move(callback).Run(
        ErrorValue(HomeError::kCancelled, "The active Dao Home turn changed."));
    return;
  }
  if (name == "home_get_manifest") {
    base::DictValue result;
    result.Set("has_project", snapshot.has_project);
    result.Set("revision", snapshot.revision);
    if (snapshot.manifest) {
      result.Set("manifest", HomeManifestToValue(*snapshot.manifest));
    }
    std::move(callback).Run(base::Value(std::move(result)));
    return;
  }

  if (!snapshot.has_project || !snapshot.manifest) {
    if (!CanRunWithoutPublishedProject(name)) {
      std::move(callback).Run(
          ErrorValue(HomeError::kNotFound, "Dao Home has no project yet."));
      return;
    }
  }

  if (name == "home_list_files") {
    if (!HasCurrentRevision(arguments, snapshot, "revision")) {
      std::move(callback).Run(ErrorValue(
          HomeError::kStaleRevision, "The requested Home revision is stale."));
      return;
    }
    service_->ListFiles(
        snapshot.revision,
        base::BindOnce(
            [](Callback callback,
               base::expected<std::vector<std::string>, HomeError> result) {
              if (!result.has_value()) {
                std::move(callback).Run(
                    ErrorValue(result.error(), "Unable to list Home files."));
                return;
              }
              base::ListValue files;
              for (std::string& path : result.value()) {
                files.Append(std::move(path));
              }
              std::move(callback).Run(base::Value(std::move(files)));
            },
            std::move(callback)));
    return;
  }

  if (name == "home_read_file") {
    const std::string* path = arguments.FindString("path");
    if (!path || !HasCurrentRevision(arguments, snapshot, "revision")) {
      std::move(callback).Run(InvalidArgument(
          "home_read_file requires the current revision and a relative path."));
      return;
    }
    service_->ReadFile(
        snapshot.revision, *path,
        base::BindOnce(
            [](Callback callback, std::string revision, std::string path,
               base::expected<std::string, HomeError> result) {
              if (!result.has_value()) {
                std::move(callback).Run(ErrorValue(
                    result.error(), "Unable to read the Home file."));
                return;
              }
              std::move(callback).Run(
                  base::Value(base::DictValue()
                                  .Set("revision", std::move(revision))
                                  .Set("path", std::move(path))
                                  .Set("contents", std::move(result.value()))));
            },
            std::move(callback), snapshot.revision, *path));
    return;
  }

  if (name == "home_get_diagnostics") {
    base::ListValue diagnostics;
    for (const HomeDiagnostic& diagnostic :
         service_->GetDiagnostics(snapshot.revision)) {
      diagnostics.Append(
          base::DictValue()
              .Set("connector_id", diagnostic.connector_id)
              .Set("stage", diagnostic.stage)
              .Set("code", diagnostic.code)
              .Set("origin", diagnostic.origin)
              .Set("path", diagnostic.path)
              .Set("detail", diagnostic.detail)
              .Set("created_at_ms",
                   static_cast<double>(diagnostic.created_at_ms)));
    }
    std::move(callback).Run(
        base::Value(base::DictValue()
                        .Set("revision", snapshot.revision)
                        .Set("diagnostics", std::move(diagnostics))));
    return;
  }

  if (name == "home_get_selected_element") {
    const std::string node_id = service_->selected_node();
    if (node_id.empty()) {
      std::move(callback).Run(
          base::Value(base::DictValue()
                          .Set("revision", snapshot.revision)
                          .Set("selected", false)));
      return;
    }
    service_->ReadFile(
        snapshot.revision, "dao/node-map.json",
        base::BindOnce(
            [](Callback callback, std::string revision, std::string node_id,
               base::expected<std::string, HomeError> result) {
              if (!result.has_value()) {
                std::move(callback).Run(ErrorValue(
                    result.error(), "The Home node map is unavailable."));
                return;
              }
              std::optional<base::Value> parsed =
                  base::JSONReader::Read(*result, base::JSON_PARSE_RFC);
              const base::DictValue* mapping =
                  parsed && parsed->is_dict()
                      ? parsed->GetDict().FindDict(node_id)
                      : nullptr;
              if (!mapping) {
                std::move(callback).Run(ErrorValue(
                    HomeError::kNotFound,
                    "The selected Home node is not in the current node map."));
                return;
              }
              base::DictValue response;
              response.Set("revision", std::move(revision));
              response.Set("selected", true);
              response.Set("node_id", std::move(node_id));
              response.Set("mapping", mapping->Clone());
              std::move(callback).Run(base::Value(std::move(response)));
            },
            std::move(callback), snapshot.revision, node_id));
    return;
  }

  if (name == "home_apply_patch") {
    const std::string* base_revision = arguments.FindString("base_revision");
    const std::string* patch = arguments.FindString("patch");
    const std::string* summary = arguments.FindString("summary");
    if (!base_revision || !patch || !summary ||
        *base_revision != snapshot.revision) {
      std::move(callback).Run(ErrorValue(
          HomeError::kStaleRevision,
          "home_apply_patch requires the exact current base revision."));
      return;
    }
    service_->ApplyPatch(
        *base_revision, *patch, *summary,
        base::BindOnce(
            [](Callback callback, base::expected<HomeDraft, HomeError> result) {
              if (!result.has_value()) {
                std::move(callback).Run(ErrorValue(
                    result.error(), "Unable to apply the Home patch."));
                return;
              }
              std::move(callback).Run(
                  base::Value(DraftToValue(result.value())));
            },
            std::move(callback)),
        service_->HasActiveHistoryBootstrapForTurn(agent_turn_id)
            ? agent_turn_id
            : std::string());
    return;
  }

  if (name == "home_replace_files") {
    const std::string* base_revision = arguments.FindString("base_revision");
    const base::ListValue* file_values = arguments.FindList("files");
    const std::string* summary = arguments.FindString("summary");
    if (!base_revision || !file_values || !summary) {
      std::move(callback).Run(InvalidArgument(
          "home_replace_files requires a base revision, one or more existing "
          "text files with complete contents, and a summary."));
      return;
    }
    if (*base_revision != snapshot.revision) {
      std::move(callback).Run(ErrorValue(
          HomeError::kStaleRevision,
          "home_replace_files requires the exact current base revision."));
      return;
    }
    std::vector<std::pair<std::string, std::string>> files;
    files.reserve(file_values->size());
    for (const base::Value& value : *file_values) {
      const base::DictValue* file = value.GetIfDict();
      const std::string* path = file ? file->FindString("path") : nullptr;
      const std::string* contents =
          file ? file->FindString("contents") : nullptr;
      if (!path || !contents) {
        std::move(callback).Run(InvalidArgument(
            "Each home_replace_files item requires path and contents."));
        return;
      }
      files.emplace_back(*path, *contents);
    }
    service_->ReplaceFiles(
        *base_revision, files, *summary,
        base::BindOnce(
            [](Callback callback, base::expected<HomeDraft, HomeError> result) {
              if (!result.has_value()) {
                std::move(callback).Run(ErrorValue(
                    result.error(), "Unable to replace the Home files."));
                return;
              }
              std::move(callback).Run(
                  base::Value(DraftToValue(result.value())));
            },
            std::move(callback)),
        service_->HasActiveHistoryBootstrapForTurn(agent_turn_id)
            ? agent_turn_id
            : std::string());
    return;
  }

  if (name == "home_add_asset") {
    const std::string* base_revision = arguments.FindString("base_revision");
    const std::string* path = arguments.FindString("path");
    const std::string* contents = arguments.FindString("base64_contents");
    const std::string* summary = arguments.FindString("summary");
    if (!base_revision || !path || !contents || !summary ||
        *base_revision != snapshot.revision) {
      std::move(callback).Run(
          ErrorValue(HomeError::kStaleRevision,
                     "home_add_asset requires the exact current base revision, "
                     "an assets path, base64 contents, and a summary."));
      return;
    }
    service_->AddAsset(
        *base_revision, *path, *contents, *summary,
        base::BindOnce(
            [](Callback callback, base::expected<HomeDraft, HomeError> result) {
              if (!result.has_value()) {
                std::move(callback).Run(ErrorValue(
                    result.error(), "Unable to add the Home asset."));
                return;
              }
              std::move(callback).Run(
                  base::Value(DraftToValue(result.value())));
            },
            std::move(callback)),
        service_->HasActiveHistoryBootstrapForTurn(agent_turn_id)
            ? agent_turn_id
            : std::string());
    return;
  }

  if (name == "home_preview") {
    const std::string* draft_id = arguments.FindString("draft_id");
    if (!draft_id || !HasCurrentRevision(arguments, snapshot)) {
      std::move(callback).Run(ErrorValue(
          HomeError::kStaleRevision,
          "home_preview requires the exact current base revision and draft."));
      return;
    }
    service_->GetDraft(
        *draft_id,
        base::BindOnce(
            [](base::WeakPtr<DaoHomeAgentTools> self, Callback callback,
               std::string expected_revision,
               scoped_refptr<DaoHomeMutationLease> authorization,
               OwnerValidator owner_validator, std::string agent_turn_id,
               base::expected<HomeDraft, HomeError> result) {
              if (!result.has_value()) {
                std::move(callback).Run(ErrorValue(
                    result.error(), "Unable to preview the Home draft."));
                return;
              }
              if (result->base_revision != expected_revision) {
                std::move(callback).Run(ErrorValue(HomeError::kStaleRevision,
                                                   "The Home draft is stale."));
                return;
              }
              if (!IsMutationAuthorized(authorization, owner_validator)) {
                std::move(callback).Run(
                    ErrorValue(HomeError::kCancelled,
                               "The active Dao Home turn changed."));
                return;
              }
              if (!self) {
                std::move(callback).Run(
                    ErrorValue(HomeError::kCancelled,
                               "The Home tool host was destroyed."));
                return;
              }
              if (!self->preview_runner_) {
                std::move(callback).Run(ErrorValue(
                    HomeError::kInvalidDraft,
                    "The trusted Home preview host is unavailable."));
                return;
              }
              HomeDraft draft = std::move(result.value());
              if (!self->service_->HasActiveHistoryBootstrapForTurn(
                      agent_turn_id)) {
                self->RunPreview(std::move(draft), HomePreviewRequirements(),
                                 std::move(callback), std::move(authorization),
                                 std::move(owner_validator),
                                 std::move(agent_turn_id));
                return;
              }
              HomeDraft callback_draft = draft;
              self->service_->PrepareBootstrapPreview(
                  agent_turn_id, draft,
                  base::BindOnce(
                      [](base::WeakPtr<DaoHomeAgentTools> self,
                         Callback callback, HomeDraft draft,
                         scoped_refptr<DaoHomeMutationLease> authorization,
                         OwnerValidator owner_validator,
                         std::string agent_turn_id,
                         base::expected<HomePreviewRequirements, HomeError>
                             requirements) {
                        if (!self) {
                          std::move(callback).Run(
                              ErrorValue(HomeError::kCancelled,
                                         "The Home tool host was destroyed."));
                          return;
                        }
                        if (!requirements.has_value()) {
                          std::move(callback).Run(ErrorValue(
                              requirements.error(),
                              "The history bootstrap draft is not ready for "
                              "preview."));
                          return;
                        }
                        self->RunPreview(
                            std::move(draft), std::move(requirements.value()),
                            std::move(callback), std::move(authorization),
                            std::move(owner_validator),
                            std::move(agent_turn_id));
                      },
                      self, std::move(callback), std::move(callback_draft),
                      std::move(authorization), std::move(owner_validator),
                      agent_turn_id));
            },
            weak_factory_.GetWeakPtr(), std::move(callback), snapshot.revision,
            authorization, owner_validator, agent_turn_id));
    return;
  }

  if (name == "home_publish") {
    const std::string* draft_id = arguments.FindString("draft_id");
    if (!draft_id || !HasCurrentRevision(arguments, snapshot)) {
      std::move(callback).Run(ErrorValue(
          HomeError::kStaleRevision,
          "home_publish requires the exact current base revision and draft."));
      return;
    }
    std::optional<HomeRevisionKind> kind = HomeRevisionKindFromString(
        arguments.FindString("kind") ? *arguments.FindString("kind")
                                     : "user_request");
    const bool history_bootstrap =
        service_->HasActiveHistoryBootstrapForTurn(agent_turn_id);
    if (!kind || *kind == HomeRevisionKind::kRollback ||
        *kind == HomeRevisionKind::kImport ||
        (!history_bootstrap && *kind == HomeRevisionKind::kHistoryBootstrap)) {
      std::move(callback).Run(
          InvalidArgument("Invalid Home publication kind."));
      return;
    }
    service_->GetDraft(
        *draft_id,
        base::BindOnce(
            [](base::WeakPtr<DaoHomeAgentTools> self, Callback callback,
               std::string expected_revision, HomeRevisionKind kind,
               scoped_refptr<DaoHomeMutationLease> authorization,
               OwnerValidator owner_validator, std::string agent_turn_id,
               base::expected<HomeDraft, HomeError> draft) {
              if (!self) {
                std::move(callback).Run(
                    ErrorValue(HomeError::kCancelled,
                               "The Home tool host was destroyed."));
                return;
              }
              if (!draft.has_value()) {
                std::move(callback).Run(ErrorValue(
                    draft.error(), "Unable to publish the Home draft."));
                return;
              }
              if (draft->base_revision != expected_revision) {
                std::move(callback).Run(ErrorValue(HomeError::kStaleRevision,
                                                   "The Home draft is stale."));
                return;
              }
              if (!IsMutationAuthorized(authorization, owner_validator)) {
                std::move(callback).Run(
                    ErrorValue(HomeError::kCancelled,
                               "The active Dao Home turn changed."));
                return;
              }
              auto publish_callback = base::BindOnce(
                  [](Callback callback,
                     base::expected<HomeVersion, HomeError> result) {
                    if (!result.has_value()) {
                      std::move(callback).Run(
                          ErrorValue(result.error(),
                                     "Unable to publish the Home draft. "
                                     "Expanded sources must be approved and "
                                     "successfully tested first."));
                      return;
                    }
                    std::move(callback).Run(
                        base::Value(VersionToValue(result.value())));
                  },
                  std::move(callback));
              if (self->service_->HasActiveHistoryBootstrapForTurn(
                      agent_turn_id)) {
                self->service_->PublishBootstrapDraft(
                    agent_turn_id, draft.value(), std::move(publish_callback),
                    std::move(authorization));
                return;
              }
              if (draft->permission_expansion) {
                self->service_->PublishApprovedDraft(
                    draft.value(), kind, std::move(publish_callback),
                    std::move(authorization));
              } else {
                self->service_->PublishPreviewedDraft(
                    draft.value(), kind, std::move(publish_callback),
                    std::move(authorization));
              }
            },
            weak_factory_.GetWeakPtr(), std::move(callback), snapshot.revision,
            *kind, authorization, owner_validator, agent_turn_id));
    return;
  }

  if (name == "home_rollback") {
    const std::string* target = arguments.FindString("target_revision");
    if (!target || !HasCurrentRevision(arguments, snapshot)) {
      std::move(callback).Run(ErrorValue(HomeError::kStaleRevision,
                                         "home_rollback requires the exact "
                                         "current base revision and target."));
      return;
    }
    const std::string* supplied_summary = arguments.FindString("summary");
    service_->Rollback(
        snapshot.revision, *target,
        supplied_summary && !supplied_summary->empty() ? *supplied_summary
                                                       : "Restore Home version",
        base::BindOnce(
            [](Callback callback,
               base::expected<HomeVersion, HomeError> result) {
              if (!result.has_value()) {
                std::move(callback).Run(ErrorValue(
                    result.error(), "Unable to restore the Home version."));
                return;
              }
              std::move(callback).Run(
                  base::Value(VersionToValue(result.value())));
            },
            std::move(callback)),
        std::move(authorization));
    return;
  }

  if (name == "home_list_connectors") {
    base::ListValue connectors;
    for (const HomeConnector& connector : snapshot.manifest->connectors) {
      connectors.Append(
          ConnectorToValue(connector, IsGranted(snapshot, connector.id)));
    }
    std::move(callback).Run(base::Value(std::move(connectors)));
    return;
  }

  if (name == "home_request_bootstrap_sources") {
    const std::string* draft_id = arguments.FindString("draft_id");
    const base::ListValue* source_values = arguments.FindList("sources");
    if (!draft_id || !source_values ||
        !HasCurrentRevision(arguments, snapshot) || source_values->size() > 3) {
      std::move(callback).Run(InvalidArgument(
          "Bootstrap source access requires the current base revision, "
          "draft, and zero to three source proposals."));
      return;
    }
    std::vector<HomeSourceProposal> proposals;
    base::flat_set<std::string> unique_connector_ids;
    for (const base::Value& value : *source_values) {
      const base::DictValue* source = value.GetIfDict();
      const std::string* connector_id =
          source ? source->FindString("connector_id") : nullptr;
      const std::string* collection_url =
          source ? source->FindString("collection_url") : nullptr;
      const std::string* content_intent =
          source ? source->FindString("content_intent") : nullptr;
      const base::ListValue* content_kind_values =
          source ? source->FindList("content_kinds") : nullptr;
      if (!connector_id || connector_id->empty() || !collection_url ||
          !content_intent || !content_kind_values ||
          !unique_connector_ids.insert(*connector_id).second) {
        std::move(callback).Run(InvalidArgument(
            "Bootstrap source proposals must be complete and have unique "
            "connector IDs."));
        return;
      }
      HomeSourceProposal proposal;
      proposal.launch_target_id = *connector_id;
      proposal.collection_url = GURL(*collection_url);
      proposal.content_intent = *content_intent;
      for (const base::Value& content_kind : *content_kind_values) {
        if (!content_kind.is_string()) {
          std::move(callback).Run(
              InvalidArgument("Bootstrap content kinds must be strings."));
          return;
        }
        proposal.content_kinds.push_back(content_kind.GetString());
      }
      proposals.push_back(std::move(proposal));
    }
    service_->GetDraft(
        *draft_id,
        base::BindOnce(
            [](base::WeakPtr<DaoHomeAgentTools> self, Callback callback,
               std::string agent_turn_id, std::string expected_revision,
               std::vector<HomeSourceProposal> proposals,
               scoped_refptr<DaoHomeMutationLease> authorization,
               OwnerValidator owner_validator,
               base::expected<HomeDraft, HomeError> draft) {
              if (!self) {
                std::move(callback).Run(
                    ErrorValue(HomeError::kCancelled,
                               "The Home tool host was destroyed."));
                return;
              }
              if (!draft.has_value()) {
                std::move(callback).Run(ErrorValue(
                    draft.error(), "Unable to inspect bootstrap sources."));
                return;
              }
              if (draft->base_revision != expected_revision) {
                std::move(callback).Run(ErrorValue(HomeError::kStaleRevision,
                                                   "The Home draft is stale."));
                return;
              }
              if (!IsMutationAuthorized(authorization, owner_validator)) {
                std::move(callback).Run(
                    ErrorValue(HomeError::kCancelled,
                               "The active Dao Home turn changed."));
                return;
              }
              self->service_->RequestBootstrapPermissionsWithProposals(
                  agent_turn_id, draft.value(), std::move(proposals),
                  base::BindOnce(
                      [](Callback callback, std::string draft_id,
                         std::string base_revision,
                         base::expected<base::flat_set<std::string>, HomeError>
                             selected) {
                        if (!selected.has_value()) {
                          std::move(callback).Run(ErrorValue(
                              selected.error(),
                              "Unable to resolve bootstrap source access."));
                          return;
                        }
                        base::ListValue connector_ids;
                        for (const std::string& connector_id :
                             selected.value()) {
                          connector_ids.Append(connector_id);
                        }
                        std::move(callback).Run(base::Value(
                            base::DictValue()
                                .Set("draft_id", std::move(draft_id))
                                .Set("base_revision", std::move(base_revision))
                                .Set("connector_ids",
                                     std::move(connector_ids))));
                      },
                      std::move(callback), draft->id, draft->base_revision));
            },
            weak_factory_.GetWeakPtr(), std::move(callback), agent_turn_id,
            snapshot.revision, std::move(proposals), authorization,
            owner_validator));
    return;
  }

  if (name == "home_request_source_access") {
    if (service_->HasActiveHistoryBootstrapForTurn(agent_turn_id)) {
      std::move(callback).Run(InvalidArgument(
          "History bootstrap source access must use "
          "home_request_bootstrap_sources; use an empty sources list "
          "to build a launchpad-only Home."));
      return;
    }
    const std::string* draft_id = arguments.FindString("draft_id");
    const std::string* connector_id = arguments.FindString("connector_id");
    if (!draft_id || !connector_id ||
        !HasCurrentRevision(arguments, snapshot)) {
      std::move(callback).Run(
          InvalidArgument("Source access requires the current base revision, "
                          "draft, and connector."));
      return;
    }
    service_->GetDraft(
        *draft_id,
        base::BindOnce(
            [](base::WeakPtr<DaoHomeAgentTools> self, Callback callback,
               std::string expected_revision, std::string connector_id,
               scoped_refptr<DaoHomeMutationLease> authorization,
               OwnerValidator owner_validator,
               base::expected<HomeDraft, HomeError> result) {
              if (!self) {
                std::move(callback).Run(
                    ErrorValue(HomeError::kCancelled,
                               "The Home tool host was destroyed."));
                return;
              }
              if (!result.has_value()) {
                std::move(callback).Run(ErrorValue(
                    result.error(), "Unable to inspect the source request."));
                return;
              }
              if (result->base_revision != expected_revision) {
                std::move(callback).Run(ErrorValue(HomeError::kStaleRevision,
                                                   "The Home draft is stale."));
                return;
              }
              if (!IsMutationAuthorized(authorization, owner_validator)) {
                std::move(callback).Run(
                    ErrorValue(HomeError::kCancelled,
                               "The active Dao Home turn changed."));
                return;
              }
              auto request = self->service_->RequestPermission(result.value(),
                                                               connector_id);
              if (!request.has_value()) {
                std::move(callback).Run(ErrorValue(
                    request.error(), "Unable to request source access."));
                return;
              }
              base::DictValue response =
                  ConnectorToValue(request->connector, /*granted=*/false);
              response.Set("request_id", request->id);
              response.Set("draft_id", result->id);
              response.Set("base_revision", result->base_revision);
              response.Set("permission_required", true);
              std::move(callback).Run(base::Value(std::move(response)));
            },
            weak_factory_.GetWeakPtr(), std::move(callback), snapshot.revision,
            *connector_id, authorization, owner_validator));
    return;
  }

  if (name == "home_list_versions") {
    service_->ListVersions(base::BindOnce(
        [](Callback callback, std::vector<HomeVersion> versions) {
          base::ListValue result;
          for (const HomeVersion& version : versions) {
            result.Append(VersionToValue(version));
          }
          std::move(callback).Run(base::Value(std::move(result)));
        },
        std::move(callback)));
    return;
  }

  if (name == "home_export_project") {
    service_->ExportProject(base::BindOnce(
        [](Callback callback, base::expected<std::string, HomeError> result) {
          if (!result.has_value()) {
            std::move(callback).Run(
                ErrorValue(result.error(), "Unable to export Dao Home."));
            return;
          }
          std::move(callback).Run(base::Value(base::DictValue().Set(
              "package_json", std::move(result.value()))));
        },
        std::move(callback)));
    return;
  }

  if (name == "home_collect_sample") {
    const std::string* connector_id = arguments.FindString("connector_id");
    const std::string input_json = arguments.FindString("input_json")
                                       ? *arguments.FindString("input_json")
                                       : "{}";
    if (!connector_id || input_json.size() > 64 * 1024) {
      std::move(callback).Run(
          InvalidArgument("home_collect_sample requires a connector ID and "
                          "bounded JSON input."));
      return;
    }
    const HomeConnector* connector =
        FindHomeConnector(*snapshot.manifest, *connector_id);
    if (!connector) {
      std::move(callback).Run(ErrorValue(HomeError::kNotFound,
                                         "The Home connector does not exist."));
      return;
    }
    if (!IsGranted(snapshot, *connector_id)) {
      std::move(callback).Run(ErrorValue(
          HomeError::kPermissionRequired,
          "The trusted Home host must approve this source before collection."));
      return;
    }
    std::optional<base::Value> input =
        base::JSONReader::Read(input_json, base::JSON_PARSE_RFC);
    if (!input) {
      std::move(callback).Run(
          InvalidArgument("The Home connector input is not valid JSON."));
      return;
    }
    if (!connector_runner_) {
      std::move(callback).Run(
          ErrorValue(HomeError::kIoError,
                     "The active Home connector host is unavailable."));
      return;
    }
    connector_runner_.Run(
        /*draft_id=*/"", *connector_id, std::move(*input),
        base::BindOnce(
            [](Callback callback, std::string revision,
               std::string connector_id, base::Value envelope) {
              ConnectorRunnerOutcome outcome =
                  ParseConnectorRunnerOutcome(envelope);
              if (!outcome.result) {
                std::move(callback).Run(
                    base::Value(SafeConnectorError(outcome.error_code)));
                return;
              }
              std::move(callback).Run(base::Value(
                  base::DictValue()
                      .Set("revision", std::move(revision))
                      .Set("connector_id", std::move(connector_id))
                      .Set("sample_shape",
                           BuildHomeConnectorSampleShape(*outcome.result))));
            },
            std::move(callback), snapshot.revision, *connector_id));
    return;
  }

  if (name == "home_test_connector") {
    const std::string* draft_id = arguments.FindString("draft_id");
    const std::string* connector_id = arguments.FindString("connector_id");
    const std::string input_json = arguments.FindString("input_json")
                                       ? *arguments.FindString("input_json")
                                       : "{}";
    std::optional<base::Value> input =
        base::JSONReader::Read(input_json, base::JSON_PARSE_RFC);
    if (!draft_id || !connector_id || !input || input_json.size() > 64 * 1024) {
      std::move(callback).Run(InvalidArgument(
          "home_test_connector requires a draft, connector ID, and bounded "
          "JSON input."));
      return;
    }
    service_->GetDraft(
        *draft_id,
        base::BindOnce(
            [](base::WeakPtr<DaoHomeAgentTools> self, Callback callback,
               std::string revision, std::string connector_id,
               scoped_refptr<DaoHomeMutationLease> authorization,
               OwnerValidator owner_validator, std::string agent_turn_id,
               base::Value input, base::expected<HomeDraft, HomeError> draft) {
              if (!self) {
                std::move(callback).Run(
                    ErrorValue(HomeError::kCancelled,
                               "The Home tool host was destroyed."));
                return;
              }
              if (!draft.has_value()) {
                std::move(callback).Run(ErrorValue(
                    draft.error(), "The Home connector draft is unavailable."));
                return;
              }
              if (draft->base_revision != revision ||
                  !FindHomeConnector(draft->manifest, connector_id)) {
                std::move(callback).Run(ErrorValue(
                    HomeError::kStaleRevision,
                    "The Home connector draft is stale or incomplete."));
                return;
              }
              if (!IsMutationAuthorized(authorization, owner_validator)) {
                std::move(callback).Run(
                    ErrorValue(HomeError::kCancelled,
                               "The active Dao Home turn changed."));
                return;
              }
              const bool history_bootstrap =
                  self->service_->HasActiveHistoryBootstrapForTurn(
                      agent_turn_id);
              if (history_bootstrap) {
                if (!self->connector_runner_) {
                  std::move(callback).Run(ErrorValue(
                      HomeError::kIoError,
                      "The active Home connector host is unavailable."));
                  return;
                }
                auto started = self->service_->BeginBootstrapConnectorTest(
                    agent_turn_id, draft->id, connector_id);
                if (!started.has_value()) {
                  std::move(callback).Run(
                      ErrorValue(started.error(),
                                 "The bootstrap connector test cannot start."));
                  return;
                }
              } else if (!self->service_->IsDraftConnectorApproved(
                             draft->id, connector_id)) {
                std::move(callback).Run(ErrorValue(
                    HomeError::kPermissionRequired,
                    "Approve this exact draft connector scope in the trusted "
                    "Home host before testing it."));
                return;
              }
              if (!self->connector_runner_) {
                std::move(callback).Run(ErrorValue(
                    HomeError::kIoError,
                    "The active Home connector host is unavailable."));
                return;
              }
              const std::string draft_id = draft->id;
              self->connector_runner_.Run(
                  draft_id, connector_id, std::move(input),
                  base::BindOnce(
                      [](base::WeakPtr<DaoHomeAgentTools> self,
                         Callback callback, std::string revision,
                         std::string draft_id, std::string connector_id,
                         scoped_refptr<DaoHomeMutationLease> authorization,
                         OwnerValidator owner_validator,
                         std::string agent_turn_id, bool history_bootstrap,
                         base::Value envelope) {
                        if (!self) {
                          std::move(callback).Run(
                              ErrorValue(HomeError::kCancelled,
                                         "The Home tool host was destroyed."));
                          return;
                        }
                        if (!IsMutationAuthorized(authorization,
                                                  owner_validator)) {
                          std::move(callback).Run(
                              ErrorValue(HomeError::kCancelled,
                                         "The active Dao Home turn changed."));
                          return;
                        }
                        ConnectorRunnerOutcome outcome =
                            ParseConnectorRunnerOutcome(envelope);
                        std::optional<base::DictValue> sample_shape =
                            outcome.result ? std::make_optional(
                                                 BuildHomeConnectorSampleShape(
                                                     *outcome.result))
                                           : std::nullopt;
                        if (history_bootstrap) {
                          HomeConnectorTestStatus status =
                              HomeConnectorTestStatus::kSucceeded;
                          const char* response_status = "succeeded";
                          std::optional<base::Value> sample =
                              outcome.result
                                  ? std::make_optional(outcome.result->Clone())
                                  : std::nullopt;
                          std::string error_code;
                          if (!outcome.result) {
                            error_code = outcome.error_code;
                            status =
                                error_code == "auth_required"
                                    ? HomeConnectorTestStatus::
                                          kAuthenticationRequired
                                : (error_code == "invalid_response" ||
                                   error_code == "schema_failed")
                                    ? HomeConnectorTestStatus::kSchemaFailed
                                    : HomeConnectorTestStatus::kRuntimeFailed;
                            response_status =
                                status == HomeConnectorTestStatus::
                                              kAuthenticationRequired
                                    ? "authentication_required"
                                : status ==
                                        HomeConnectorTestStatus::kSchemaFailed
                                    ? "schema_failed"
                                    : "runtime_failed";
                          }
                          auto recorded =
                              self->service_->RecordBootstrapConnectorOutcome(
                                  agent_turn_id, connector_id, status,
                                  std::move(sample), error_code);
                          if (!recorded.has_value()) {
                            std::move(callback).Run(ErrorValue(
                                recorded.error(),
                                "The bootstrap connector result expired."));
                            return;
                          }
                          if (!outcome.result) {
                            base::DictValue response =
                                SafeConnectorError(error_code);
                            response.Set("status", response_status);
                            std::move(callback).Run(
                                base::Value(std::move(response)));
                            return;
                          }
                        } else if (!outcome.result) {
                          std::move(callback).Run(base::Value(
                              SafeConnectorError(outcome.error_code)));
                          return;
                        }
                        auto marked = self->service_->MarkDraftConnectorTested(
                            draft_id, connector_id);
                        if (!marked.has_value()) {
                          std::move(callback).Run(ErrorValue(
                              marked.error(),
                              "The draft connector approval expired."));
                          return;
                        }
                        std::move(callback).Run(base::Value(
                            base::DictValue()
                                .Set("revision", std::move(revision))
                                .Set("draft_id", std::move(draft_id))
                                .Set("connector_id", std::move(connector_id))
                                .Set("status", "succeeded")
                                .Set("sample_shape",
                                     std::move(*sample_shape))));
                      },
                      self, std::move(callback), revision, draft_id,
                      connector_id, std::move(authorization),
                      std::move(owner_validator), agent_turn_id,
                      history_bootstrap));
            },
            weak_factory_.GetWeakPtr(), std::move(callback), snapshot.revision,
            *connector_id, authorization, owner_validator, agent_turn_id,
            std::move(*input)));
    return;
  }

  if (name == "home_get_bootstrap_brief") {
    auto brief = service_->BeginHistoryBootstrap(
        agent_turn_id, snapshot.revision, std::move(turn_authorization),
        owner_validator);
    if (!brief.has_value()) {
      std::move(callback).Run(ErrorValue(
          brief.error(),
          "No one-shot Home bootstrap brief is available for this turn."));
      return;
    }
    std::move(callback).Run(
        base::Value(HomeBootstrapBriefToValue(brief.value())));
    return;
  }

  std::move(callback).Run(InvalidArgument("Unknown Dao Home tool."));
}

void DaoHomeAgentTools::RunPreview(
    HomeDraft draft,
    HomePreviewRequirements requirements,
    Callback callback,
    scoped_refptr<DaoHomeMutationLease> authorization,
    OwnerValidator owner_validator,
    std::string agent_turn_id) {
  const std::string draft_id = draft.id;
  preview_runner_.Run(
      draft_id, draft.manifest.entry, std::move(requirements),
      base::BindOnce(
          [](base::WeakPtr<DaoHomeAgentTools> self, Callback callback,
             HomeDraft draft, scoped_refptr<DaoHomeMutationLease> authorization,
             OwnerValidator owner_validator, std::string agent_turn_id,
             base::Value preview_result) {
            if (!self) {
              std::move(callback).Run(ErrorValue(
                  HomeError::kCancelled, "The Home tool host was destroyed."));
              return;
            }
            const base::DictValue* result = preview_result.GetIfDict();
            if (!result || !result->FindBool("valid").value_or(false)) {
              if (self->service_->HasActiveHistoryBootstrapForTurn(
                      agent_turn_id)) {
                auto rejected = self->service_->RejectBootstrapPreview(
                    agent_turn_id, draft.id);
                if (!rejected.has_value()) {
                  std::move(callback).Run(ErrorValue(
                      rejected.error(),
                      "The failed history bootstrap preview could not be "
                      "released."));
                  return;
                }
              }
              std::move(callback).Run(std::move(preview_result));
              return;
            }
            if (!IsMutationAuthorized(authorization, owner_validator)) {
              std::move(callback).Run(ErrorValue(
                  HomeError::kCancelled, "The active Dao Home turn changed."));
              return;
            }
            if (self->service_->HasActiveHistoryBootstrapForTurn(
                    agent_turn_id)) {
              auto marked = self->service_->CompleteBootstrapPreview(
                  agent_turn_id, draft.id);
              if (!marked.has_value()) {
                std::move(callback).Run(ErrorValue(
                    marked.error(), "The history bootstrap preview expired."));
                return;
              }
            } else {
              self->service_->MarkDraftPreviewed(draft.id);
            }
            base::DictValue value = DraftToValue(draft);
            value.Set("valid", true);
            std::move(callback).Run(base::Value(std::move(value)));
          },
          weak_factory_.GetWeakPtr(), std::move(callback), std::move(draft),
          std::move(authorization), std::move(owner_validator),
          std::move(agent_turn_id)));
}

void DaoHomeAgentTools::SetPreviewRunner(PreviewRunner preview_runner) {
  preview_runner_ = std::move(preview_runner);
}

}  // namespace dao
