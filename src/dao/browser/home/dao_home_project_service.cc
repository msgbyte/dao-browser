// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/home/dao_home_project_service.h"

#include <algorithm>
#include <array>
#include <map>
#include <string_view>
#include <tuple>
#include <utility>

#include "base/barrier_closure.h"
#include "base/containers/lru_cache.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/one_shot_event.h"
#include "base/scoped_observation.h"
#include "base/strings/string_util.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "base/uuid.h"
#include "components/services/storage/public/mojom/local_storage_control.mojom.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "crypto/hash.h"
#include "dao/browser/home/dao_home_bootstrap_transaction.h"
#include "dao/common/home_local_storage.h"
#include "mojo/public/cpp/bindings/callback_helpers.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "net/base/schemeful_site.h"
#include "net/cookies/cookie_change_dispatcher.h"
#include "net/cookies/cookie_options.h"
#include "net/cookies/cookie_partition_key_collection.h"
#include "net/cookies/unique_cookie_key.h"
#include "services/network/public/mojom/cookie_manager.mojom.h"
#include "third_party/blink/public/common/storage_key/storage_key.h"
#include "third_party/blink/public/mojom/dom_storage/storage_area.mojom.h"

namespace dao {

DaoHomeProjectService::HistoryBootstrapState::HistoryBootstrapState(
    base::WeakPtr<content::WebContents> owner,
    std::string claim_token,
    HomeBootstrapBrief brief)
    : owner(std::move(owner)),
      claim_token(std::move(claim_token)),
      brief(std::move(brief)) {}

DaoHomeProjectService::HistoryBootstrapState::~HistoryBootstrapState() =
    default;

DaoHomeProjectService::HistoryBootstrapState::HistoryBootstrapState(
    HistoryBootstrapState&&) = default;

DaoHomeProjectService::HistoryBootstrapState&
DaoHomeProjectService::HistoryBootstrapState::operator=(
    HistoryBootstrapState&&) = default;
namespace {

constexpr size_t kMaxDiagnostics = 32;

bool IsSameSite(const GURL& left, const GURL& right) {
  if (left.host() == right.host()) {
    return true;
  }
  const std::string left_domain =
      net::registry_controlled_domains::GetDomainAndRegistry(
          left, net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES);
  const std::string right_domain =
      net::registry_controlled_domains::GetDomainAndRegistry(
          right, net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES);
  return !left_domain.empty() && left_domain == right_domain;
}

bool IsSupportedContentIntent(std::string_view intent) {
  constexpr std::array<std::string_view, 4> kSupportedIntents = {
      "site_feed", "following_feed", "subscription_feed", "activity_feed"};
  return std::ranges::contains(kSupportedIntents, intent);
}

bool IsSafeContentKind(std::string_view kind) {
  return !kind.empty() && kind.size() <= 32 &&
         std::ranges::all_of(kind, [](char character) {
           return base::IsAsciiLower(character) ||
                  base::IsAsciiDigit(character) || character == '_';
         });
}

void Truncate(std::string& value, size_t max_size) {
  if (value.size() > max_size) {
    value.resize(max_size);
  }
}

}  // namespace

// Shared by in-flight reads and cached results for one first-party origin.
class DaoHomeSourceWatch : public blink::mojom::StorageAreaObserver {
 public:
  DaoHomeSourceWatch(
      content::StoragePartition* partition,
      const url::Origin& origin,
      base::RepeatingCallback<
          void(const char*, DaoHomeProjectService::CollectorWrite)> invalidate)
      : invalidate_(std::move(invalidate)) {
    partition->GetLocalStorageControl()->BindStorageArea(
        blink::StorageKey::CreateFirstParty(origin),
        area_.BindNewPipeAndPassReceiver());
    area_->AddObserver(receiver_.BindNewPipeAndPassRemote());
    receiver_.set_disconnect_handler(base::BindOnce(
        &DaoHomeSourceWatch::OnDisconnect, weak_factory_.GetWeakPtr()));

    // Include HttpOnly and path-scoped cookies already present at first use.
    auto options = net::CookieOptions::MakeAllInclusive();
    options.set_return_excluded_cookies();
    partition->GetCookieManagerForBrowserProcess()->GetCookieList(
        origin.GetURL(), options,
        net::CookiePartitionKeyCollection::MatchesSite(
            net::SchemefulSite(origin)),
        mojo::WrapCallbackWithDropHandler(
            base::BindOnce(&DaoHomeSourceWatch::OnInitialCookies,
                           weak_factory_.GetWeakPtr(), origin),
            base::BindOnce(&DaoHomeSourceWatch::OnDisconnect,
                           weak_factory_.GetWeakPtr())));
  }

  void ReportCollectorStorageSource(base::Token id) {
    *StorageWriter(id) = true;
  }

  void WhenInitialized(base::OnceClosure done) {
    initialized_event_.Post(FROM_HERE, std::move(done));
  }

  bool disconnected() const { return disconnected_; }
  bool ready() const { return initialized_ && !disconnected_; }

  bool UpdateCookie(const net::CookieChangeInfo& change) {
    auto key = change.cookie.StrictlyUniqueKey();
    if (change.cause == net::CookieChangeCause::EXPIRED) {
      // Chromium may announce expiry on the first network read after Home
      // has already invalidated the old result at its wall-clock deadline.
      if (!initialized_) {
        expiries_.insert_or_assign(std::move(key), base::Time());
        return change.cookie.ExpiryDate() >= started_at_;
      }
      return expiries_.erase(key) != 0;
    }
    const auto expiry = net::CookieChangeCauseIsDeletion(change.cause)
                            ? base::Time()
                            : change.cookie.ExpiryDate();
    if (!expiry.is_null() || !initialized_) {
      expiries_.insert_or_assign(std::move(key), expiry);
    } else {
      expiries_.erase(key);
    }
    return true;
  }

  bool RemoveExpiredCookies(base::Time now) {
    return std::erase_if(expiries_, [now](const auto& item) {
             return !item.second.is_null() && item.second <= now;
           }) != 0;
  }

 private:
  void OnInitialCookies(const url::Origin& origin,
                        const net::CookieAccessResultList& included,
                        const net::CookieAccessResultList& excluded) {
    for (const auto* list : {&included, &excluded}) {
      for (const auto& item : *list) {
        const auto& cookie = item.cookie;
        if (cookie.IsDomainMatch(origin.host()) && cookie.IsPersistent()) {
          // A newer change notification can arrive before the initial snapshot.
          expiries_.try_emplace(cookie.StrictlyUniqueKey(),
                                cookie.ExpiryDate());
        }
      }
    }
    std::erase_if(expiries_,
                  [](const auto& item) { return item.second.is_null(); });
    initialized_ = true;
    if (!initialized_event_.is_signaled()) {
      initialized_event_.Signal();
    }
  }

  void OnDisconnect() {
    if (disconnected_) {
      return;
    }
    disconnected_ = true;
    if (!initialized_event_.is_signaled()) {
      initialized_event_.Signal();
    }
    invalidate_.Run("watcher_disconnected", nullptr);
  }

  std::shared_ptr<bool> StorageWriter(base::Token id) {
    auto found = storage_sources_.Get(id);
    if (found == storage_sources_.end()) {
      found = storage_sources_.Put(id, std::make_shared<bool>(false));
    }
    return found->second;
  }

  void StorageChanged(const std::vector<uint8_t>& key,
                      const blink::mojom::StorageAreaSourcePtr& source) {
    auto writer = source ? StorageWriter(source->storage_area_id)
                         : std::make_shared<bool>(false);
    invalidate_.Run("local_storage_changed",
                    std::make_shared<HomeSourceWrite>(
                        "local_storage_changed", std::move(writer),
                        HomeLocalStorageKeyHash(key)));
  }

  void KeyChanged(const std::vector<uint8_t>& key,
                  const std::vector<uint8_t>& value,
                  const std::optional<std::vector<uint8_t>>& previous,
                  blink::mojom::StorageAreaSourcePtr source) override {
    if (!previous || *previous != value) {
      StorageChanged(key, source);
    }
  }
  void KeyDeleted(const std::vector<uint8_t>& key,
                  const std::optional<std::vector<uint8_t>>& previous,
                  blink::mojom::StorageAreaSourcePtr source) override {
    if (previous) {
      StorageChanged(key, source);
    }
  }
  void AllDeleted(bool was_nonempty,
                  blink::mojom::StorageAreaSourcePtr) override {
    if (was_nonempty) {
      invalidate_.Run("local_storage_cleared", nullptr);
    }
  }
  void KeyChangeFailed(const std::vector<uint8_t>&,
                       blink::mojom::StorageAreaSourcePtr) override {}
  void ShouldSendOldValueOnMutations(bool) override {}

  base::RepeatingCallback<void(const char*,
                               DaoHomeProjectService::CollectorWrite)>
      invalidate_;
  base::LRUCache<base::Token, std::shared_ptr<bool>> storage_sources_{128};
  base::OneShotEvent initialized_event_;
  mojo::Remote<blink::mojom::StorageArea> area_;
  mojo::Receiver<blink::mojom::StorageAreaObserver> receiver_{this};
  std::map<net::UniqueCookieKey, base::Time> expiries_;
  const base::Time started_at_ = base::Time::Now();
  bool initialized_ = false;
  bool disconnected_ = false;
  base::WeakPtrFactory<DaoHomeSourceWatch> weak_factory_{this};
};

class DaoHomeProjectService::SourceDataObserver
    : public content::StoragePartition::DataRemovalObserver,
      public network::mojom::CookieChangeListener {
 public:
  SourceDataObserver(DaoHomeProjectService* service,
                     content::StoragePartition* partition)
      : service_(service) {
    observation_.Observe(partition);
    Listen();
  }

  HomeSourceWatches Watch(const HomeCollectionKey& key) {
    std::erase_if(watches_,
                  [](const auto& item) { return item.second.expired(); });
    HomeSourceWatches result;
    for (const auto& origin : key.origins) {
      auto& weak = watches_[origin];
      auto watch = weak.lock();
      if (!watch || watch->disconnected()) {
        watch = std::make_shared<DaoHomeSourceWatch>(
            observation_.GetSource(), origin,
            base::BindRepeating(&SourceDataObserver::InvalidateOrigins,
                                weak_factory_.GetWeakPtr(),
                                std::set<url::Origin>{origin}));
        weak = watch;
      }
      result.push_back(std::move(watch));
    }
    return result;
  }

  void CheckCookieExpiry() {
    auto now = base::Time::Now();
    std::set<url::Origin> expired;
    for (const auto& [origin, weak] : watches_) {
      if (auto watch = weak.lock(); watch && watch->RemoveExpiredCookies(now)) {
        expired.insert(origin);
      }
    }
    InvalidateOrigins(expired, "cookie_expired", nullptr);
  }

  void ReportCollectorStorageSource(const url::Origin& origin, base::Token id) {
    auto it = watches_.find(origin);
    if (it != watches_.end()) {
      if (auto watch = it->second.lock()) {
        watch->ReportCollectorStorageSource(id);
      }
    }
  }

  void ReportCollectorCookieWrite(const net::CanonicalCookie& cookie) {
    if (std::ranges::any_of(watches_, [&](const auto& item) {
          return !item.second.expired() &&
                 cookie.IsDomainMatch(item.first.host());
        })) {
      CookieWrite(cookie, true);
    }
  }

 private:
  // Match both IPC orders once, without retaining the cookie value.
  using CookieVersion =
      std::tuple<net::UniqueCookieKey, base::Time, std::array<uint8_t, 32>>;
  std::shared_ptr<HomeSourceWrite> CookieWrite(
      const net::CanonicalCookie& cookie,
      bool from_collector) {
    CookieVersion version(
        cookie.StrictlyUniqueKey(), cookie.LastUpdateDate(),
        crypto::hash::Sha256(
            net::CanonicalCookie::BuildCookieAttributesLine(cookie)));
    auto found = cookie_writes_.Get(version);
    if (found == cookie_writes_.end()) {
      auto write = std::make_shared<HomeSourceWrite>(
          "cookie_changed", std::make_shared<bool>(from_collector));
      cookie_writes_.Put(std::move(version), write);
      return write;
    }
    auto write = found->second;
    if (*write->from_collector != from_collector) {
      *write->from_collector = true;
      cookie_writes_.Erase(found);
    }
    return write;
  }

  struct ReplacedCookie {
    net::UniqueCookieKey key;
    bool secure;
    bool http_only;
    net::CookieSameSite same_site;
    base::Time expiry;

    bool SameScope(const net::CanonicalCookie& cookie) const {
      return key == cookie.StrictlyUniqueKey() &&
             secure == cookie.SecureAttribute() &&
             http_only == cookie.IsHttpOnly() && same_site == cookie.SameSite();
    }

    bool IsRenewedBy(const net::CanonicalCookie& cookie) const {
      return !expiry.is_null() && cookie.ExpiryDate() > expiry &&
             SameScope(cookie);
    }
  };

  void Listen() {
    observation_.GetSource()
        ->GetCookieManagerForBrowserProcess()
        ->AddGlobalChangeListener(receiver_.BindNewPipeAndPassRemote());
    receiver_.set_disconnect_handler(base::BindOnce(
        &SourceDataObserver::OnDisconnect, base::Unretained(this)));
  }

  void OnDisconnect() {
    receiver_.reset();
    watches_.clear();
    replaced_cookie_.reset();
    cookie_writes_.Clear();
    service_->InvalidateCollections(
        [](const HomeCollectionKey&) { return true; }, "watcher_disconnected");
    Listen();
  }

  void OnCookieChange(const net::CookieChangeInfo& change) override {
    const auto& cookie = change.cookie;
    if (change.cause == net::CookieChangeCause::OVERWRITE) {
      // The next insertion reports whether the value changed.
      replaced_cookie_ = ReplacedCookie{
          cookie.StrictlyUniqueKey(), cookie.SecureAttribute(),
          cookie.IsHttpOnly(), cookie.SameSite(), cookie.ExpiryDate()};
      return;
    }
    auto previous = std::exchange(replaced_cookie_, std::nullopt);
    const bool renewal =
        change.cause == net::CookieChangeCause::INSERTED_NO_CHANGE_OVERWRITE ||
        (change.cause ==
             net::CookieChangeCause::INSERTED_NO_VALUE_CHANGE_OVERWRITE &&
         previous && previous->IsRenewedBy(cookie));
    std::set<url::Origin> changed;
    for (const auto& [origin, weak] : watches_) {
      if (cookie.IsDomainMatch(origin.host())) {
        if (auto watch = weak.lock();
            watch && watch->UpdateCookie(change) && !renewal) {
          changed.insert(origin);
        }
      }
    }
    if (changed.empty()) {
      return;
    }
    using Cause = net::CookieChangeCause;
    if (change.cause == Cause::INSERTED && !cookie.LastUpdateDate().is_null() &&
        cookie.SourceType() != net::CookieSourceType::kOther &&
        (!previous || previous->SameScope(cookie))) {
      InvalidateOrigins(changed, "cookie_changed", CookieWrite(cookie, false));
    } else {
      const bool expired = change.cause == Cause::EXPIRED ||
                           change.cause == Cause::EXPIRED_OVERWRITE;
      const char* reason = expired ? "cookie_expired"
                           : net::CookieChangeCauseIsDeletion(change.cause)
                               ? "cookie_deleted"
                               : "cookie_changed";
      InvalidateOrigins(changed, reason, nullptr);
    }
  }

  void InvalidateOrigins(const std::set<url::Origin>& origins,
                         const char* reason,
                         CollectorWrite write) {
    if (!origins.empty()) {
      service_->InvalidateCollections(
          [&](const HomeCollectionKey& key) {
            return std::ranges::any_of(key.origins,
                                       [&](const url::Origin& origin) {
                                         return origins.contains(origin);
                                       });
          },
          reason, std::move(write));
    }
  }

  void OnStorageKeyDataCleared(
      uint32_t mask,
      content::StoragePartition::StorageKeyMatcherFunction matcher,
      base::Time begin,
      base::Time end) override {
    using Partition = content::StoragePartition;
    if (!(mask & (Partition::REMOVE_DATA_MASK_COOKIES |
                  Partition::REMOVE_DATA_MASK_LOCAL_STORAGE |
                  Partition::REMOVE_DATA_MASK_INDEXEDDB |
                  Partition::REMOVE_DATA_MASK_DEVICE_BOUND_SESSIONS))) {
      return;
    }
    service_->InvalidateCollections(
        [&](const HomeCollectionKey& key) {
          return std::ranges::any_of(
              key.origins, [&](const url::Origin& origin) {
                return !matcher ||
                       matcher.Run(blink::StorageKey::CreateFirstParty(origin));
              });
        },
        "browsing_data_removed");
  }

  base::LRUCache<CookieVersion, std::shared_ptr<HomeSourceWrite>>
      cookie_writes_{256};
  raw_ptr<DaoHomeProjectService> service_;
  base::ScopedObservation<content::StoragePartition, DataRemovalObserver>
      observation_{this};
  mojo::Receiver<network::mojom::CookieChangeListener> receiver_{this};
  std::optional<ReplacedCookie> replaced_cookie_;
  std::map<url::Origin, std::weak_ptr<DaoHomeSourceWatch>> watches_;
  base::WeakPtrFactory<SourceDataObserver> weak_factory_{this};
};

DaoHomeProjectService::DaoHomeProjectService(const base::FilePath& profile_path)
    : store_(base::ThreadPool::CreateSequencedTaskRunner(
                 {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
                  base::TaskShutdownBehavior::BLOCK_SHUTDOWN}),
             profile_path) {
  store_.AsyncCall(&DaoHomeProjectStore::Initialize)
      .Then(base::BindOnce([](base::expected<void, HomeError> result) {
        if (!result.has_value()) {
          LOG(ERROR) << "Failed to initialize Dao Home project store.";
        }
      }));
}

DaoHomeProjectService::~DaoHomeProjectService() {
  FinishOrCancelBootstrap();
}

void DaoHomeProjectService::Shutdown() {
  source_data_observer_.reset();
}

HomeSourceWatches DaoHomeProjectService::WatchSourceData(
    content::StoragePartition* partition,
    const HomeCollectionKey& key) {
  if (!source_data_observer_) {
    source_data_observer_ =
        std::make_unique<SourceDataObserver>(this, partition);
  }
  CheckSourceCookieExpiry();
  return source_data_observer_->Watch(key);
}

void DaoHomeProjectService::CheckSourceCookieExpiry() {
  if (source_data_observer_) {
    source_data_observer_->CheckCookieExpiry();
  }
}

void DaoHomeProjectService::ReportCollectorStorageSource(
    const url::Origin& origin,
    base::Token id) {
  if (source_data_observer_ && !id.is_zero()) {
    source_data_observer_->ReportCollectorStorageSource(origin, id);
  }
}

void DaoHomeProjectService::ReportCollectorCookieWrite(
    const net::CanonicalCookie& cookie) {
  if (source_data_observer_) {
    source_data_observer_->ReportCollectorCookieWrite(cookie);
  }
}

void DaoHomeProjectService::WhenSourceWatchesInitialized(
    const HomeSourceWatches& watches,
    base::OnceClosure done) {
  auto barrier = base::BarrierClosure(watches.size(), std::move(done));
  for (const auto& watch : watches) {
    watch->WhenInitialized(barrier);
  }
}

bool DaoHomeProjectService::SourceWatchesReady(
    const HomeSourceWatches& watches) {
  return std::ranges::all_of(watches, &DaoHomeSourceWatch::ready);
}

void DaoHomeProjectService::InvalidateSourceSession(
    const std::vector<url::Origin>& origins) {
  InvalidateCollections(
      [&](const HomeCollectionKey& key) {
        return std::ranges::any_of(origins, [&](const url::Origin& origin) {
          return std::ranges::contains(key.origins, origin);
        });
      },
      "auth_required");
}

void DaoHomeProjectService::InvalidateCollections(CollectionMatcher matches,
                                                  const char* reason,
                                                  CollectorWrite write) {
  std::erase_if(collections_,
                [&](const auto& entry) { return matches(entry.key); });
  collection_invalidation_observers_.Notify(matches, reason, std::move(write));
}

base::CallbackListSubscription
DaoHomeProjectService::AddCollectionInvalidationObserver(
    InvalidationCallback callback) {
  return collection_invalidation_observers_.Add(std::move(callback));
}

void DaoHomeProjectService::RecordDiagnostic(HomeDiagnostic diagnostic) {
  Truncate(diagnostic.revision, 64);
  Truncate(diagnostic.connector_id, 128);
  Truncate(diagnostic.stage, 64);
  Truncate(diagnostic.code, 64);
  Truncate(diagnostic.origin, 512);
  Truncate(diagnostic.path, 512);
  Truncate(diagnostic.detail, 512);
  if (diagnostic.created_at_ms == 0) {
    diagnostic.created_at_ms = base::Time::Now().InMillisecondsSinceUnixEpoch();
  }
  diagnostics_.push_back(std::move(diagnostic));
  if (diagnostics_.size() > kMaxDiagnostics) {
    diagnostics_.erase(
        diagnostics_.begin(),
        diagnostics_.begin() + (diagnostics_.size() - kMaxDiagnostics));
  }
}

std::vector<HomeDiagnostic> DaoHomeProjectService::GetDiagnostics(
    const std::string& revision) const {
  std::vector<HomeDiagnostic> result;
  for (const HomeDiagnostic& diagnostic : diagnostics_) {
    if (diagnostic.revision == revision) {
      result.push_back(diagnostic);
    }
  }
  return result;
}

void DaoHomeProjectService::GetSnapshot(
    base::OnceCallback<void(HomeSnapshot)> callback) {
  store_.AsyncCall(&DaoHomeProjectStore::GetSnapshot)
      .Then(base::BindOnce(
          [](base::WeakPtr<DaoHomeProjectService> service,
             base::OnceCallback<void(HomeSnapshot)> callback,
             HomeSnapshot snapshot) {
            if (!service) {
              return;
            }
            if (service->before_snapshot_reply_callback_for_testing_) {
              std::move(service->before_snapshot_reply_callback_for_testing_)
                  .Run();
            }
            service->current_revision_ = snapshot.revision;
            std::move(callback).Run(std::move(snapshot));
          },
          weak_factory_.GetWeakPtr(), std::move(callback)));
}

void DaoHomeProjectService::ListVersions(
    base::OnceCallback<void(std::vector<HomeVersion>)> callback) {
  store_.AsyncCall(&DaoHomeProjectStore::ListVersions)
      .Then(std::move(callback));
}

void DaoHomeProjectService::GetFileSnapshot(
    const std::string& revision,
    ResultCallback<HomeFileSnapshot> callback) {
  store_.AsyncCall(&DaoHomeProjectStore::GetFileSnapshot)
      .WithArgs(revision)
      .Then(std::move(callback));
}

void DaoHomeProjectService::StageSnapshot(const std::string& base_revision,
                                          const HomeFileSnapshot& files,
                                          const std::string& summary,
                                          ResultCallback<HomeDraft> callback) {
  store_.AsyncCall(&DaoHomeProjectStore::StageSnapshot)
      .WithArgs(base_revision, files, summary)
      .Then(std::move(callback));
}

void DaoHomeProjectService::GetOperation(
    const std::string& operation_id,
    base::OnceCallback<void(std::optional<HomeOperationResult>)> callback) {
  store_.AsyncCall(&DaoHomeProjectStore::GetOperation)
      .WithArgs(operation_id)
      .Then(std::move(callback));
}

void DaoHomeProjectService::AuthorizeGrantedDraftConnectors(
    const std::string& draft_id,
    ResultCallback<void> callback) {
  store_.AsyncCall(&DaoHomeProjectStore::GetGrantedDraftConnectorIds)
      .WithArgs(draft_id)
      .Then(base::BindOnce(
          [](base::WeakPtr<DaoHomeProjectService> self, std::string draft_id,
             ResultCallback<void> callback,
             base::expected<std::vector<std::string>, HomeError> result) {
            if (!self) {
              std::move(callback).Run(base::unexpected(HomeError::kCancelled));
              return;
            }
            if (!result.has_value()) {
              std::move(callback).Run(base::unexpected(result.error()));
              return;
            }
            for (const std::string& connector_id : *result) {
              self->approved_draft_connectors_.insert(
                  DraftConnectorKey(draft_id, connector_id));
            }
            std::move(callback).Run(base::ok());
          },
          weak_factory_.GetWeakPtr(), draft_id, std::move(callback)));
}

void DaoHomeProjectService::ListFiles(
    const std::string& revision,
    ResultCallback<std::vector<std::string>> callback) {
  store_.AsyncCall(&DaoHomeProjectStore::ListFiles)
      .WithArgs(revision)
      .Then(std::move(callback));
}

void DaoHomeProjectService::ReadFile(const std::string& revision,
                                     const std::string& relative_path,
                                     ResultCallback<std::string> callback) {
  store_.AsyncCall(&DaoHomeProjectStore::ReadFile)
      .WithArgs(revision, relative_path)
      .Then(std::move(callback));
}

void DaoHomeProjectService::GetDraft(const std::string& draft_id,
                                     ResultCallback<HomeDraft> callback) {
  store_.AsyncCall(&DaoHomeProjectStore::GetDraft)
      .WithArgs(draft_id)
      .Then(std::move(callback));
}

void DaoHomeProjectService::ReadActivePreviewFile(
    const std::string& draft_id,
    const std::string& relative_path,
    ResultCallback<std::string> callback) {
  if (!active_preview_drafts_.contains(draft_id)) {
    std::move(callback).Run(base::unexpected(HomeError::kPermissionRequired));
    return;
  }
  store_.AsyncCall(&DaoHomeProjectStore::ReadDraftFile)
      .WithArgs(draft_id, relative_path)
      .Then(std::move(callback));
}

void DaoHomeProjectService::GetConnectorBundle(
    const std::string& revision,
    const std::string& connector_id,
    ResultCallback<HomeConnectorBundle> callback) {
  store_.AsyncCall(&DaoHomeProjectStore::GetConnectorBundle)
      .WithArgs(revision, connector_id)
      .Then(std::move(callback));
}

void DaoHomeProjectService::GetApprovedDraftConnectorBundle(
    const std::string& draft_id,
    const std::string& connector_id,
    ResultCallback<HomeConnectorBundle> callback) {
  if (!IsDraftConnectorApproved(draft_id, connector_id)) {
    std::move(callback).Run(base::unexpected(HomeError::kPermissionRequired));
    return;
  }
  store_.AsyncCall(&DaoHomeProjectStore::GetDraftConnectorBundle)
      .WithArgs(draft_id, connector_id)
      .Then(std::move(callback));
}

DaoHomeProjectService::CollectionAge DaoHomeProjectService::GetCollectionAge(
    base::Time collected_at) {
  const auto age = base::Time::Now() - collected_at;
  if (age.is_negative() || age >= kCollectionHardTtl) {
    return CollectionAge::kExpired;
  }
  return age >= kCollectionSoftTtl ? CollectionAge::kStale
                                   : CollectionAge::kFresh;
}

std::optional<base::Value> DaoHomeProjectService::GetCachedCollection(
    const HomeCollectionKey& key,
    bool* stale,
    HomeSourceDependencies* dependencies) {
  CheckSourceCookieExpiry();
  auto it = std::ranges::find(collections_, key, &CachedCollection::key);
  if (it == collections_.end()) {
    return std::nullopt;
  }
  const auto age = GetCollectionAge(it->collected_at);
  if (age == CollectionAge::kExpired) {
    collections_.erase(it);
    return std::nullopt;
  }
  if (stale) {
    *stale = age == CollectionAge::kStale;
  }
  if (dependencies) {
    *dependencies = it->dependencies;
  }
  collections_.splice(collections_.begin(), collections_, it);
  return base::JSONReader::Read(it->json, base::JSON_PARSE_RFC);
}

void DaoHomeProjectService::CacheCollection(const HomeCollectionKey& key,
                                            const base::Value& value,
                                            HomeSourceWatches watches,
                                            HomeStorageReads storage_reads) {
  constexpr size_t kMaxEntries = 64;
  constexpr size_t kMaxBytes = 8 * 1024 * 1024;
  auto json = base::WriteJson(value);
  std::erase_if(collections_,
                [&](const auto& entry) { return entry.key == key; });
  // Media capabilities belong to one Home document.
  if (!json || json->size() > kMaxBytes || json->contains("dao-media:")) {
    return;
  }
  collections_.push_front({key,
                           std::move(*json),
                           base::Time::Now(),
                           {std::move(watches), std::move(storage_reads)}});
  size_t bytes = 0;
  size_t count = 0;
  for (auto it = collections_.begin(); it != collections_.end(); ++it) {
    bytes += it->json.size();
    if (++count > kMaxEntries || bytes > kMaxBytes) {
      collections_.erase(it, collections_.end());
      break;
    }
  }
}

void DaoHomeProjectService::ApplyPatch(const std::string& base_revision,
                                       const std::string& patch,
                                       const std::string& summary,
                                       ResultCallback<HomeDraft> callback,
                                       std::string agent_turn_id) {
  store_.AsyncCall(&DaoHomeProjectStore::ApplyPatch)
      .WithArgs(base_revision, patch, summary)
      .Then(base::BindOnce(
          [](base::WeakPtr<DaoHomeProjectService> self,
             std::string agent_turn_id, ResultCallback<HomeDraft> callback,
             base::expected<HomeDraft, HomeError> result) {
            if (!agent_turn_id.empty() && result.has_value()) {
              if (!self) {
                std::move(callback).Run(
                    base::unexpected(HomeError::kCancelled));
                return;
              }
              auto registered = self->RegisterBootstrapDraftForTurn(
                  agent_turn_id, result.value());
              if (!registered.has_value()) {
                self->DiscardDraft(
                    result->id,
                    base::BindOnce([](base::expected<void, HomeError>) {}));
                std::move(callback).Run(base::unexpected(registered.error()));
                return;
              }
            }
            std::move(callback).Run(std::move(result));
          },
          weak_factory_.GetWeakPtr(), std::move(agent_turn_id),
          std::move(callback)));
}

void DaoHomeProjectService::ReplaceFiles(
    const std::string& base_revision,
    const std::vector<std::pair<std::string, std::string>>& files,
    const std::string& summary,
    ResultCallback<HomeDraft> callback,
    std::string agent_turn_id) {
  store_.AsyncCall(&DaoHomeProjectStore::ReplaceFiles)
      .WithArgs(base_revision, files, summary)
      .Then(base::BindOnce(
          [](base::WeakPtr<DaoHomeProjectService> self,
             std::string agent_turn_id, ResultCallback<HomeDraft> callback,
             base::expected<HomeDraft, HomeError> result) {
            if (!agent_turn_id.empty() && result.has_value()) {
              if (!self) {
                std::move(callback).Run(
                    base::unexpected(HomeError::kCancelled));
                return;
              }
              auto registered = self->RegisterBootstrapDraftForTurn(
                  agent_turn_id, result.value());
              if (!registered.has_value()) {
                self->DiscardDraft(
                    result->id,
                    base::BindOnce([](base::expected<void, HomeError>) {}));
                std::move(callback).Run(base::unexpected(registered.error()));
                return;
              }
            }
            std::move(callback).Run(std::move(result));
          },
          weak_factory_.GetWeakPtr(), std::move(agent_turn_id),
          std::move(callback)));
}

void DaoHomeProjectService::AddAsset(const std::string& base_revision,
                                     const std::string& relative_path,
                                     const std::string& base64_contents,
                                     const std::string& summary,
                                     ResultCallback<HomeDraft> callback,
                                     std::string agent_turn_id) {
  store_.AsyncCall(&DaoHomeProjectStore::AddAsset)
      .WithArgs(base_revision, relative_path, base64_contents, summary)
      .Then(base::BindOnce(
          [](base::WeakPtr<DaoHomeProjectService> self,
             std::string agent_turn_id, ResultCallback<HomeDraft> callback,
             base::expected<HomeDraft, HomeError> result) {
            if (!agent_turn_id.empty() && result.has_value()) {
              if (!self) {
                std::move(callback).Run(
                    base::unexpected(HomeError::kCancelled));
                return;
              }
              auto registered = self->RegisterBootstrapDraftForTurn(
                  agent_turn_id, result.value());
              if (!registered.has_value()) {
                self->DiscardDraft(
                    result->id,
                    base::BindOnce([](base::expected<void, HomeError>) {}));
                std::move(callback).Run(base::unexpected(registered.error()));
                return;
              }
            }
            std::move(callback).Run(std::move(result));
          },
          weak_factory_.GetWeakPtr(), std::move(agent_turn_id),
          std::move(callback)));
}

void DaoHomeProjectService::Publish(
    const std::string& draft_id,
    HomeRevisionKind kind,
    ResultCallback<HomeVersion> callback,
    scoped_refptr<DaoHomeMutationLease> authorization,
    std::optional<HomeOperationRequest> operation) {
  store_.AsyncCall(&DaoHomeProjectStore::Publish)
      .WithArgs(draft_id, kind, std::move(authorization), std::move(operation))
      .Then(base::BindOnce(
          [](base::WeakPtr<DaoHomeProjectService> self,
             ResultCallback<HomeVersion> callback,
             base::expected<HomeVersion, HomeError> result) {
            if (self && result.has_value()) {
              self->NotifyProjectChanged();
            }
            std::move(callback).Run(std::move(result));
          },
          weak_factory_.GetWeakPtr(), std::move(callback)));
}

void DaoHomeProjectService::PublishPreviewedDraft(
    const HomeDraft& draft,
    HomeRevisionKind kind,
    ResultCallback<HomeVersion> callback,
    scoped_refptr<DaoHomeMutationLease> authorization,
    std::optional<HomeOperationRequest> operation) {
  if (authorization && !authorization->IsValid()) {
    std::move(callback).Run(base::unexpected(HomeError::kCancelled));
    return;
  }
  if (!IsDraftPreviewed(draft.id)) {
    std::move(callback).Run(base::unexpected(HomeError::kPermissionRequired));
    return;
  }
  Publish(draft.id, kind, std::move(callback), std::move(authorization),
          std::move(operation));
}

base::expected<void, HomeError> DaoHomeProjectService::ApprovePermission(
    const std::string& request_id,
    const base::flat_set<std::string>& selected_ids) {
  if (!pending_permission_ || pending_permission_->id != request_id) {
    return base::unexpected(HomeError::kNotFound);
  }
  for (const auto& id : selected_ids) {
    if (!std::ranges::contains(pending_permission_->items, id,
                               &HomePermissionBatchItem::connector_id)) {
      return base::unexpected(HomeError::kInvalidArgument);
    }
  }
  for (const auto& id : selected_ids) {
    approved_draft_connectors_.insert(
        DraftConnectorKey(pending_permission_->draft_id, id));
  }
  pending_permission_.reset();
  NotifyPermissionChanged();
  return base::ok();
}

void DaoHomeProjectService::PublishApprovedDraft(
    const HomeDraft& draft,
    HomeRevisionKind kind,
    ResultCallback<HomeVersion> callback,
    scoped_refptr<DaoHomeMutationLease> authorization,
    std::optional<HomeOperationRequest> operation) {
  if (authorization && !authorization->IsValid()) {
    std::move(callback).Run(base::unexpected(HomeError::kCancelled));
    return;
  }
  if (!IsDraftPreviewed(draft.id)) {
    std::move(callback).Run(base::unexpected(HomeError::kPermissionRequired));
    return;
  }
  for (const std::string& connector_id :
       draft.permission_expanded_connector_ids) {
    const std::string key = DraftConnectorKey(draft.id, connector_id);
    if (!approved_draft_connectors_.contains(key) ||
        !tested_draft_connectors_.contains(key)) {
      std::move(callback).Run(base::unexpected(HomeError::kPermissionRequired));
      return;
    }
  }
  for (const auto& connector_id : draft.test_required_connector_ids) {
    if (!tested_draft_connectors_.contains(
            DraftConnectorKey(draft.id, connector_id))) {
      std::move(callback).Run(base::unexpected(HomeError::kPermissionRequired));
      return;
    }
  }
  store_.AsyncCall(&DaoHomeProjectStore::PublishWithGrants)
      .WithArgs(draft.id, draft.permission_expanded_connector_ids, kind,
                std::move(authorization), std::move(operation))
      .Then(base::BindOnce(
          [](base::WeakPtr<DaoHomeProjectService> self,
             ResultCallback<HomeVersion> callback,
             base::expected<HomeVersion, HomeError> result) {
            if (self && result.has_value()) {
              self->NotifyProjectChanged();
            }
            std::move(callback).Run(std::move(result));
          },
          weak_factory_.GetWeakPtr(), std::move(callback)));
}

void DaoHomeProjectService::Rollback(
    const std::string& base_revision,
    const std::string& target_revision,
    const std::string& summary,
    ResultCallback<HomeVersion> callback,
    scoped_refptr<DaoHomeMutationLease> authorization,
    std::optional<HomeOperationRequest> operation) {
  store_.AsyncCall(&DaoHomeProjectStore::Rollback)
      .WithArgs(base_revision, target_revision, summary,
                std::move(authorization), std::move(operation))
      .Then(base::BindOnce(
          [](base::WeakPtr<DaoHomeProjectService> self,
             ResultCallback<HomeVersion> callback,
             base::expected<HomeVersion, HomeError> result) {
            if (self && result.has_value()) {
              self->NotifyProjectChanged();
            }
            std::move(callback).Run(std::move(result));
          },
          weak_factory_.GetWeakPtr(), std::move(callback)));
}

void DaoHomeProjectService::Reset(
    const std::string& base_revision,
    ResultCallback<void> callback,
    scoped_refptr<DaoHomeMutationLease> authorization) {
  store_.AsyncCall(&DaoHomeProjectStore::Reset)
      .WithArgs(base_revision, std::move(authorization))
      .Then(base::BindOnce(
          [](base::WeakPtr<DaoHomeProjectService> self,
             ResultCallback<void> callback,
             base::expected<void, HomeError> result) {
            if (self && result.has_value()) {
              self->current_revision_.clear();
              self->diagnostics_.clear();
              self->NotifyProjectChanged();
            }
            std::move(callback).Run(std::move(result));
          },
          weak_factory_.GetWeakPtr(), std::move(callback)));
}

void DaoHomeProjectService::ExportProject(
    ResultCallback<std::string> callback) {
  store_.AsyncCall(&DaoHomeProjectStore::ExportProject)
      .Then(std::move(callback));
}

void DaoHomeProjectService::ImportProject(const std::string& base_revision,
                                          const std::string& package_json,
                                          const std::string& summary,
                                          ResultCallback<HomeDraft> callback) {
  store_.AsyncCall(&DaoHomeProjectStore::ImportProject)
      .WithArgs(base_revision, package_json, summary)
      .Then(std::move(callback));
}

void DaoHomeProjectService::DiscardDraft(const std::string& draft_id,
                                         ResultCallback<void> callback) {
  const bool had_pending_permission =
      pending_permission_ && pending_permission_->draft_id == draft_id;
  if (had_pending_permission) {
    pending_permission_.reset();
  }
  EndDraftPreview(draft_id);
  previewed_drafts_.erase(draft_id);
  const std::string connector_key_prefix = draft_id + "\n";
  std::erase_if(approved_draft_connectors_, [&](const std::string& key) {
    return key.starts_with(connector_key_prefix);
  });
  std::erase_if(tested_draft_connectors_, [&](const std::string& key) {
    return key.starts_with(connector_key_prefix);
  });
  if (had_pending_permission) {
    NotifyPermissionChanged();
  }
  store_.AsyncCall(&DaoHomeProjectStore::DiscardDraft)
      .WithArgs(draft_id)
      .Then(std::move(callback));
}

void DaoHomeProjectService::GrantConnector(const std::string& connector_id,
                                           ResultCallback<void> callback) {
  store_.AsyncCall(&DaoHomeProjectStore::GrantConnector)
      .WithArgs(connector_id)
      .Then(base::BindOnce(
          [](base::WeakPtr<DaoHomeProjectService> self,
             std::string connector_id, ResultCallback<void> callback,
             base::expected<void, HomeError> result) {
            if (self && result.has_value()) {
              self->NotifyProjectChanged(connector_id);
            }
            std::move(callback).Run(std::move(result));
          },
          weak_factory_.GetWeakPtr(), connector_id, std::move(callback)));
}

void DaoHomeProjectService::RevokeConnector(const std::string& connector_id,
                                            ResultCallback<void> callback) {
  store_.AsyncCall(&DaoHomeProjectStore::RevokeConnector)
      .WithArgs(connector_id)
      .Then(base::BindOnce(
          [](base::WeakPtr<DaoHomeProjectService> self,
             std::string connector_id, ResultCallback<void> callback,
             base::expected<void, HomeError> result) {
            if (self && result.has_value()) {
              self->NotifyProjectChanged(connector_id);
            }
            std::move(callback).Run(std::move(result));
          },
          weak_factory_.GetWeakPtr(), connector_id, std::move(callback)));
}

base::expected<HomePermissionBatchRequest, HomeError>
DaoHomeProjectService::RequestPermission(
    const HomeDraft& draft,
    const base::flat_set<std::string>& connector_ids) {
  if (pending_permission_ || bootstrap_permission_preparing_ ||
      bootstrap_decision_callback_) {
    return base::unexpected(HomeError::kAlreadyExists);
  }
  HomePermissionBatchRequest request;
  request.id = base::Uuid::GenerateRandomV4().AsLowercaseString();
  request.draft_id = draft.id;
  request.base_revision = draft.base_revision;
  for (const HomeConnector& connector : draft.manifest.connectors) {
    if (!connector_ids.contains(connector.id)) {
      continue;
    }
    if (!std::ranges::contains(draft.permission_expanded_connector_ids,
                               connector.id)) {
      return base::unexpected(HomeError::kInvalidDraft);
    }
    HomePermissionBatchItem& item = request.items.emplace_back();
    item.connector_id = connector.id;
    item.label = connector.id;
    item.connector = connector;
    item.previous_limits = draft.previous_limits;
    item.requested_limits = draft.manifest.limits;
  }
  if (request.items.empty() || request.items.size() != connector_ids.size()) {
    return base::unexpected(HomeError::kInvalidDraft);
  }
  pending_permission_ = request;
  NotifyPermissionChanged();
  return request;
}

const std::optional<HomePermissionBatchRequest>&
DaoHomeProjectService::GetPendingPermission() const {
  return pending_permission_;
}

bool DaoHomeProjectService::CancelPermission(const std::string& request_id) {
  if (!pending_permission_ || pending_permission_->id != request_id) {
    return false;
  }
  pending_permission_.reset();
  NotifyPermissionChanged();
  return true;
}

bool DaoHomeProjectService::IsDraftConnectorApproved(
    const std::string& draft_id,
    const std::string& connector_id) const {
  return approved_draft_connectors_.contains(
      DraftConnectorKey(draft_id, connector_id));
}

base::expected<void, HomeError> DaoHomeProjectService::MarkDraftConnectorTested(
    const std::string& draft_id,
    const std::string& connector_id) {
  const std::string key = DraftConnectorKey(draft_id, connector_id);
  if (!approved_draft_connectors_.contains(key)) {
    return base::unexpected(HomeError::kPermissionRequired);
  }
  tested_draft_connectors_.insert(key);
  return base::ok();
}

void DaoHomeProjectService::BeginDraftPreview(const std::string& draft_id) {
  active_preview_drafts_.insert(draft_id);
}

void DaoHomeProjectService::EndDraftPreview(const std::string& draft_id) {
  if (draft_id.empty()) {
    return;
  }
  active_preview_drafts_.erase(draft_id);
  std::erase_if(collections_, [&](const auto& entry) {
    return entry.key.draft_id == draft_id;
  });
}

void DaoHomeProjectService::MarkDraftPreviewed(const std::string& draft_id) {
  previewed_drafts_.insert(draft_id);
}

void DaoHomeProjectService::InvalidateDraftPreview(
    const std::string& draft_id) {
  previewed_drafts_.erase(draft_id);
}

bool DaoHomeProjectService::IsDraftPreviewed(
    const std::string& draft_id) const {
  return previewed_drafts_.contains(draft_id);
}

base::CallbackListSubscription DaoHomeProjectService::AddProjectChangedObserver(
    base::RepeatingClosure callback) {
  return project_changed_observers_.Add(std::move(callback));
}

base::CallbackListSubscription DaoHomeProjectService::AddPermissionObserver(
    base::RepeatingCallback<
        void(const std::optional<HomePermissionBatchRequest>&)> callback) {
  return permission_observers_.Add(std::move(callback));
}

base::CallbackListSubscription
DaoHomeProjectService::AddBootstrapPermissionObserver(
    base::RepeatingClosure callback) {
  return bootstrap_permission_observers_.Add(std::move(callback));
}

void DaoHomeProjectService::NotifyProjectChanged(
    std::string_view changed_connector) {
  std::erase_if(collections_, [&](const auto& entry) {
    return changed_connector.empty() || !entry.key.draft_id.empty() ||
           entry.key.connector_id == changed_connector;
  });
  FinishOrCancelBootstrap();
  ClearSelectedNode();
  const bool had_pending_permission = pending_permission_.has_value();
  pending_permission_.reset();
  approved_draft_connectors_.clear();
  tested_draft_connectors_.clear();
  active_preview_drafts_.clear();
  previewed_drafts_.clear();
  if (had_pending_permission) {
    NotifyPermissionChanged();
  }
  project_changed_observers_.Notify();
}

void DaoHomeProjectService::NotifyPermissionChanged() {
  permission_observers_.Notify(pending_permission_);
}

void DaoHomeProjectService::NotifyBootstrapPermissionChanged() {
  bootstrap_permission_observers_.Notify();
}

void DaoHomeProjectService::SetHistoryBootstrapBrief(
    base::WeakPtr<content::WebContents> owner,
    std::string claim_token,
    HomeBootstrapBrief brief) {
  if (!owner || claim_token.empty()) {
    history_bootstrap_.reset();
    return;
  }
  history_bootstrap_.emplace(std::move(owner), std::move(claim_token),
                             std::move(brief));
}

bool DaoHomeProjectService::ClaimHistoryBootstrap(
    content::WebContents* owner,
    const std::string& claim_token,
    const std::string& agent_turn_id) {
  if (!history_bootstrap_ || !history_bootstrap_->owner ||
      history_bootstrap_->owner.get() != owner || claim_token.empty() ||
      history_bootstrap_->claim_token != claim_token || agent_turn_id.empty() ||
      !history_bootstrap_->agent_turn_id.empty()) {
    return false;
  }
  history_bootstrap_->agent_turn_id = agent_turn_id;
  return true;
}

base::expected<HomeBootstrapBrief, HomeError>
DaoHomeProjectService::ConsumeHistoryBootstrapBrief(
    const std::string& agent_turn_id) {
  if (!history_bootstrap_ || agent_turn_id.empty() ||
      history_bootstrap_->agent_turn_id != agent_turn_id) {
    return base::unexpected(HomeError::kNotFound);
  }
  HomeBootstrapBrief brief = std::move(history_bootstrap_->brief);
  history_bootstrap_.reset();
  return brief;
}

base::expected<HomeBootstrapBrief, HomeError>
DaoHomeProjectService::BeginHistoryBootstrap(
    const std::string& agent_turn_id,
    const std::string& base_revision,
    scoped_refptr<DaoHomeMutationLease> turn_authorization,
    base::RepeatingCallback<bool()> owner_validator) {
  if (bootstrap_transaction_) {
    if (ValidateBootstrapContext(agent_turn_id, base_revision)) {
      return active_bootstrap_brief_;
    }
    return base::unexpected(HomeError::kAlreadyExists);
  }
  if (!history_bootstrap_ || agent_turn_id.empty() ||
      history_bootstrap_->agent_turn_id != agent_turn_id ||
      !history_bootstrap_->owner ||
      history_bootstrap_->owner->GetVisibility() ==
          content::Visibility::HIDDEN ||
      base_revision != current_revision_ || !turn_authorization ||
      !turn_authorization->IsValid() || !owner_validator ||
      !owner_validator.Run()) {
    return base::unexpected(HomeError::kCancelled);
  }

  active_bootstrap_brief_ = history_bootstrap_->brief;
  active_bootstrap_owner_ = history_bootstrap_->owner;
  active_bootstrap_turn_id_ = agent_turn_id;
  active_bootstrap_base_revision_ = base_revision;
  active_bootstrap_owner_validator_ = std::move(owner_validator);
  active_bootstrap_transaction_id_ =
      base::Uuid::GenerateRandomV4().AsLowercaseString();
  bootstrap_transaction_ = std::make_unique<DaoHomeBootstrapTransaction>(
      active_bootstrap_transaction_id_, agent_turn_id, active_bootstrap_owner_,
      base_revision, active_bootstrap_brief_, std::move(turn_authorization),
      base::BindRepeating(
          [](base::WeakPtr<DaoHomeProjectService> self,
             const std::string& expected_turn_id,
             const std::string& expected_base_revision) {
            return self && self->ValidateBootstrapContext(
                               expected_turn_id, expected_base_revision);
          },
          weak_factory_.GetWeakPtr()));
  history_bootstrap_.reset();
  return active_bootstrap_brief_;
}

void DaoHomeProjectService::RequestBootstrapPermissionsWithProposals(
    const std::string& agent_turn_id,
    const HomeDraft& draft,
    std::vector<HomeSourceProposal> proposals,
    BootstrapDecisionCallback callback) {
  if (bootstrap_permission_request_ && proposals.empty()) {
    RequestBootstrapPermissions(agent_turn_id, draft, {}, std::move(callback));
    return;
  }
  if (!HasActiveHistoryBootstrapForTurn(agent_turn_id) ||
      !ValidateBootstrapContext(agent_turn_id, draft.base_revision) ||
      proposals.size() != active_bootstrap_brief_.source_candidates.size() ||
      proposals.size() > 3) {
    std::move(callback).Run(base::unexpected(HomeError::kInvalidArgument));
    return;
  }

  base::flat_set<std::string> proposal_ids;
  HomeBootstrapBrief resolved_brief = active_bootstrap_brief_;
  for (HomeSourceCandidate& candidate : resolved_brief.source_candidates) {
    const auto proposal =
        std::ranges::find_if(proposals, [&](const HomeSourceProposal& value) {
          return value.launch_target_id == candidate.launch_target_id;
        });
    const auto target = std::ranges::find_if(
        resolved_brief.launch_targets, [&](const HomeLaunchTarget& value) {
          return value.id == candidate.launch_target_id;
        });
    if (proposal == proposals.end() ||
        target == resolved_brief.launch_targets.end() ||
        !proposal_ids.insert(proposal->launch_target_id).second ||
        !proposal->collection_url.is_valid() ||
        !proposal->collection_url.SchemeIsHTTPOrHTTPS() ||
        !proposal->collection_url.username().empty() ||
        !proposal->collection_url.password().empty() ||
        !IsSameSite(proposal->collection_url, target->url) ||
        !IsSupportedContentIntent(proposal->content_intent) ||
        proposal->content_kinds.empty() || proposal->content_kinds.size() > 4 ||
        !std::ranges::all_of(proposal->content_kinds, IsSafeContentKind)) {
      std::move(callback).Run(base::unexpected(HomeError::kInvalidArgument));
      return;
    }
    candidate.collection_url = proposal->collection_url;
    candidate.content_intent = proposal->content_intent;
    candidate.content_kinds = proposal->content_kinds;
  }

  active_bootstrap_brief_.source_candidates =
      std::move(resolved_brief.source_candidates);
  std::vector<std::string> connector_ids;
  connector_ids.reserve(proposals.size());
  for (const HomeSourceProposal& proposal : proposals) {
    connector_ids.push_back(proposal.launch_target_id);
  }
  RequestBootstrapPermissions(agent_turn_id, draft, std::move(connector_ids),
                              std::move(callback));
}

void DaoHomeProjectService::RequestBootstrapPermissions(
    const std::string& agent_turn_id,
    const HomeDraft& draft,
    std::vector<std::string> connector_ids,
    BootstrapDecisionCallback callback) {
  if (!HasActiveHistoryBootstrapForTurn(agent_turn_id) ||
      !ValidateBootstrapContext(agent_turn_id, draft.base_revision)) {
    std::move(callback).Run(base::unexpected(HomeError::kCancelled));
    return;
  }
  if (bootstrap_permission_request_) {
    if (bootstrap_permission_request_->draft_id != draft.id) {
      std::move(callback).Run(base::unexpected(HomeError::kAlreadyExists));
      return;
    }
    if (!bootstrap_decision_callback_) {
      std::move(callback).Run(bootstrap_selected_connector_ids_);
      return;
    }
    if (!connector_ids.empty()) {
      std::move(callback).Run(base::unexpected(HomeError::kAlreadyExists));
      return;
    }
    auto resolved = bootstrap_transaction_->ResolvePermissionBatch(
        bootstrap_permission_request_->id, {});
    if (!resolved.has_value()) {
      std::move(callback).Run(base::unexpected(resolved.error()));
      return;
    }
    bootstrap_selected_connector_ids_.clear();
    bootstrap_completed_connector_ids_.clear();
    bootstrap_successful_connector_ids_.clear();
    BootstrapDecisionCallback pending = std::move(bootstrap_decision_callback_);
    NotifyBootstrapPermissionChanged();
    std::move(pending).Run(base::flat_set<std::string>());
    std::move(callback).Run(base::flat_set<std::string>());
    return;
  }
  // The browser owns source selection, scopes, and the result schema. The
  // Agent authors each collector module in the provisional draft; preparation
  // binds that code to canonical permissions without trusting its manifest.
  connector_ids.clear();
  connector_ids.reserve(active_bootstrap_brief_.source_candidates.size());
  for (const HomeSourceCandidate& candidate :
       active_bootstrap_brief_.source_candidates) {
    connector_ids.push_back(candidate.launch_target_id);
  }
  base::flat_set<std::string> unique_ids(connector_ids);
  if (connector_ids.size() > 3 || unique_ids.size() != connector_ids.size()) {
    std::move(callback).Run(base::unexpected(HomeError::kInvalidArgument));
    return;
  }
  if (pending_permission_ || bootstrap_permission_preparing_) {
    std::move(callback).Run(base::unexpected(HomeError::kAlreadyExists));
    return;
  }

  bootstrap_permission_preparing_ = true;
  store_.AsyncCall(&DaoHomeProjectStore::PrepareHistoryBootstrapDraft)
      .WithArgs(draft.id, active_bootstrap_brief_, connector_ids)
      .Then(base::BindOnce(
          [](base::WeakPtr<DaoHomeProjectService> self,
             std::string agent_turn_id, std::vector<std::string> connector_ids,
             BootstrapDecisionCallback callback,
             base::expected<HomeDraft, HomeError> prepared) {
            if (!self) {
              std::move(callback).Run(base::unexpected(HomeError::kCancelled));
              return;
            }
            if (!prepared.has_value()) {
              self->bootstrap_permission_preparing_ = false;
              std::move(callback).Run(base::unexpected(prepared.error()));
              return;
            }
            if (!self->HasActiveHistoryBootstrapForTurn(agent_turn_id) ||
                !self->ValidateBootstrapContext(agent_turn_id,
                                                prepared->base_revision)) {
              self->bootstrap_permission_preparing_ = false;
              std::move(callback).Run(base::unexpected(HomeError::kCancelled));
              return;
            }
            connector_ids.clear();
            connector_ids.reserve(prepared->manifest.connectors.size());
            for (const HomeConnector& connector :
                 prepared->manifest.connectors) {
              connector_ids.push_back(connector.id);
            }
            self->store_
                .AsyncCall(
                    &DaoHomeProjectStore::GetDraftConnectorAuthorizations)
                .WithArgs(prepared->id, connector_ids)
                .Then(base::BindOnce(
                    [](base::WeakPtr<DaoHomeProjectService> self,
                       std::string agent_turn_id, HomeDraft prepared,
                       BootstrapDecisionCallback callback,
                       base::expected<std::vector<HomeConnectorAuthorization>,
                                      HomeError> authorizations) {
                      if (!self) {
                        std::move(callback).Run(
                            base::unexpected(HomeError::kCancelled));
                        return;
                      }
                      self->bootstrap_permission_preparing_ = false;
                      if (!self->HasActiveHistoryBootstrapForTurn(
                              agent_turn_id) ||
                          !self->ValidateBootstrapContext(
                              agent_turn_id, prepared.base_revision)) {
                        std::move(callback).Run(
                            base::unexpected(HomeError::kCancelled));
                        return;
                      }
                      if (!authorizations.has_value()) {
                        std::move(callback).Run(
                            base::unexpected(authorizations.error()));
                        return;
                      }
                      auto request =
                          self->bootstrap_transaction_->PreparePermissionBatch(
                              prepared, std::move(authorizations.value()));
                      if (!request.has_value()) {
                        std::move(callback).Run(
                            base::unexpected(request.error()));
                        return;
                      }
                      if (request->items.empty()) {
                        auto resolved =
                            self->bootstrap_transaction_
                                ->ResolvePermissionBatch(request->id, {});
                        if (!resolved.has_value()) {
                          std::move(callback).Run(
                              base::unexpected(resolved.error()));
                          return;
                        }
                        self->bootstrap_permission_request_ =
                            std::move(request.value());
                        self->bootstrap_selected_connector_ids_.clear();
                        self->bootstrap_completed_connector_ids_.clear();
                        self->bootstrap_successful_connector_ids_.clear();
                        std::move(callback).Run(base::flat_set<std::string>());
                        return;
                      }
                      self->bootstrap_permission_request_ =
                          std::move(request.value());
                      self->bootstrap_decision_callback_ = std::move(callback);
                      self->NotifyBootstrapPermissionChanged();
                    },
                    self, agent_turn_id, std::move(prepared.value()),
                    std::move(callback)));
          },
          weak_factory_.GetWeakPtr(), agent_turn_id, connector_ids,
          std::move(callback)));
}

void DaoHomeProjectService::ResolveBootstrapPermissions(
    content::WebContents* owner,
    const std::string& request_id,
    base::flat_set<std::string> selected_connector_ids,
    ResultCallback<void> callback) {
  if (!owner || active_bootstrap_owner_.get() != owner ||
      !bootstrap_permission_request_ || !bootstrap_decision_callback_ ||
      bootstrap_permission_request_->id != request_id) {
    std::move(callback).Run(base::unexpected(HomeError::kNotFound));
    return;
  }
  auto resolved = bootstrap_transaction_->ResolvePermissionBatch(
      request_id, selected_connector_ids);
  if (!resolved.has_value()) {
    std::move(callback).Run(base::unexpected(resolved.error()));
    return;
  }

  bootstrap_selected_connector_ids_ = selected_connector_ids;
  bootstrap_completed_connector_ids_.clear();
  bootstrap_successful_connector_ids_.clear();
  for (const std::string& connector_id : selected_connector_ids) {
    approved_draft_connectors_.insert(DraftConnectorKey(
        bootstrap_permission_request_->draft_id, connector_id));
  }
  BootstrapDecisionCallback decision = std::move(bootstrap_decision_callback_);
  NotifyBootstrapPermissionChanged();
  std::move(decision).Run(std::move(selected_connector_ids));
  std::move(callback).Run(base::ok());
}

std::optional<HomePermissionBatchRequest>
DaoHomeProjectService::GetPendingBootstrapPermission(
    content::WebContents* owner) const {
  if (!owner || active_bootstrap_owner_.get() != owner ||
      !bootstrap_decision_callback_) {
    return std::nullopt;
  }
  return bootstrap_permission_request_;
}

base::expected<void, HomeError>
DaoHomeProjectService::BeginBootstrapConnectorTest(
    const std::string& agent_turn_id,
    const std::string& draft_id,
    const std::string& connector_id) {
  if (!HasActiveHistoryBootstrapForTurn(agent_turn_id) ||
      !ValidateBootstrapContext(agent_turn_id,
                                active_bootstrap_base_revision_)) {
    return base::unexpected(HomeError::kCancelled);
  }
  if (!bootstrap_permission_request_ ||
      bootstrap_permission_request_->draft_id != draft_id ||
      !bootstrap_selected_connector_ids_.contains(connector_id)) {
    return base::unexpected(HomeError::kPermissionRequired);
  }
  if (!bootstrap_connector_test_in_flight_.empty()) {
    return base::unexpected(HomeError::kAlreadyExists);
  }
  if (bootstrap_completed_connector_ids_.contains(connector_id)) {
    return base::unexpected(HomeError::kInvalidArgument);
  }
  bootstrap_connector_test_in_flight_ = connector_id;
  return base::ok();
}

base::expected<void, HomeError>
DaoHomeProjectService::RecordBootstrapConnectorOutcome(
    const std::string& agent_turn_id,
    const std::string& connector_id,
    HomeConnectorTestStatus status,
    std::optional<base::Value> sample,
    std::string error_code) {
  if (!HasActiveHistoryBootstrapForTurn(agent_turn_id) ||
      bootstrap_connector_test_in_flight_ != connector_id ||
      !bootstrap_permission_request_) {
    return base::unexpected(HomeError::kCancelled);
  }
  const auto item =
      std::ranges::find_if(bootstrap_permission_request_->items,
                           [&](const HomePermissionBatchItem& candidate) {
                             return candidate.connector_id == connector_id;
                           });
  if (item == bootstrap_permission_request_->items.end()) {
    return base::unexpected(HomeError::kPermissionRequired);
  }
  HomeConnectorTestOutcome outcome;
  outcome.connector_id = connector_id;
  outcome.fingerprint = item->fingerprint;
  outcome.status = status;
  outcome.sample = std::move(sample);
  outcome.error_code = std::move(error_code);
  auto recorded =
      bootstrap_transaction_->RecordConnectorOutcome(std::move(outcome));
  if (!recorded.has_value()) {
    return recorded;
  }
  bootstrap_connector_test_in_flight_.clear();
  bootstrap_completed_connector_ids_.insert(connector_id);
  if (status == HomeConnectorTestStatus::kSucceeded) {
    bootstrap_successful_connector_ids_.insert(connector_id);
  }
  tested_draft_connectors_.insert(
      DraftConnectorKey(bootstrap_permission_request_->draft_id, connector_id));
  return base::ok();
}

void DaoHomeProjectService::PrepareBootstrapPreview(
    const std::string& agent_turn_id,
    const HomeDraft& draft,
    ResultCallback<HomePreviewRequirements> callback) {
  if (!HasActiveHistoryBootstrapForTurn(agent_turn_id) ||
      !ValidateBootstrapContext(agent_turn_id, draft.base_revision) ||
      !bootstrap_connector_test_in_flight_.empty() ||
      !bootstrap_permission_request_) {
    std::move(callback).Run(base::unexpected(HomeError::kCancelled));
    return;
  }
  std::vector<std::string> successful_connector_ids(
      bootstrap_successful_connector_ids_.begin(),
      bootstrap_successful_connector_ids_.end());
  store_.AsyncCall(&DaoHomeProjectStore::PrepareHistoryBootstrapFinalDraft)
      .WithArgs(draft.id, bootstrap_permission_request_->draft_id,
                std::move(successful_connector_ids))
      .Then(base::BindOnce(
          [](base::WeakPtr<DaoHomeProjectService> self,
             std::string agent_turn_id,
             ResultCallback<HomePreviewRequirements> callback,
             base::expected<HomeDraft, HomeError> prepared) {
            if (!self) {
              std::move(callback).Run(base::unexpected(HomeError::kCancelled));
              return;
            }
            if (!prepared.has_value()) {
              std::move(callback).Run(base::unexpected(prepared.error()));
              return;
            }
            self->NormalizeAndBindBootstrapPreview(
                agent_turn_id, prepared.value(), std::move(callback));
          },
          weak_factory_.GetWeakPtr(), agent_turn_id, std::move(callback)));
}

void DaoHomeProjectService::NormalizeAndBindBootstrapPreview(
    const std::string& agent_turn_id,
    const HomeDraft& draft,
    ResultCallback<HomePreviewRequirements> callback) {
  if (!HasActiveHistoryBootstrapForTurn(agent_turn_id) ||
      !ValidateBootstrapContext(agent_turn_id, draft.base_revision) ||
      !bootstrap_connector_test_in_flight_.empty()) {
    std::move(callback).Run(base::unexpected(HomeError::kCancelled));
    return;
  }
  HomeExperience canonical_experience;
  const size_t primary_action_count =
      std::min<size_t>(4, active_bootstrap_brief_.launch_targets.size());
  for (size_t index = 0; index < primary_action_count; ++index) {
    canonical_experience.primary_actions.push_back(
        active_bootstrap_brief_.launch_targets[index].id);
  }
  for (const HomeConnector& connector : draft.manifest.connectors) {
    canonical_experience.source_slots.push_back(connector.id);
  }
  store_.AsyncCall(&DaoHomeProjectStore::NormalizeHistoryBootstrapExperience)
      .WithArgs(draft.id, std::move(canonical_experience))
      .Then(base::BindOnce(
          [](base::WeakPtr<DaoHomeProjectService> self,
             std::string agent_turn_id, HomeDraft draft,
             ResultCallback<HomePreviewRequirements> callback,
             base::expected<void, HomeError> validation) {
            if (!self) {
              std::move(callback).Run(base::unexpected(HomeError::kCancelled));
              return;
            }
            if (!validation.has_value()) {
              std::move(callback).Run(base::unexpected(validation.error()));
              return;
            }
            if (!self->HasActiveHistoryBootstrapForTurn(agent_turn_id) ||
                !self->ValidateBootstrapContext(agent_turn_id,
                                                draft.base_revision) ||
                !self->bootstrap_connector_test_in_flight_.empty()) {
              std::move(callback).Run(base::unexpected(HomeError::kCancelled));
              return;
            }
            std::vector<std::string> connector_ids;
            connector_ids.reserve(draft.manifest.connectors.size());
            for (const HomeConnector& connector : draft.manifest.connectors) {
              connector_ids.push_back(connector.id);
            }
            self->store_
                .AsyncCall(
                    &DaoHomeProjectStore::GetDraftConnectorAuthorizations)
                .WithArgs(draft.id, connector_ids)
                .Then(base::BindOnce(
                    [](base::WeakPtr<DaoHomeProjectService> self,
                       std::string agent_turn_id, HomeDraft draft,
                       ResultCallback<HomePreviewRequirements> callback,
                       base::expected<std::vector<HomeConnectorAuthorization>,
                                      HomeError> authorizations) {
                      if (!self) {
                        std::move(callback).Run(
                            base::unexpected(HomeError::kCancelled));
                        return;
                      }
                      if (!authorizations.has_value()) {
                        std::move(callback).Run(
                            base::unexpected(authorizations.error()));
                        return;
                      }
                      self->store_
                          .AsyncCall(&DaoHomeProjectStore::GetDraftExperience)
                          .WithArgs(draft.id)
                          .Then(base::BindOnce(
                              [](base::WeakPtr<DaoHomeProjectService> self,
                                 std::string agent_turn_id, HomeDraft draft,
                                 std::vector<HomeConnectorAuthorization>
                                     authorizations,
                                 ResultCallback<HomePreviewRequirements>
                                     callback,
                                 base::expected<HomeExperience, HomeError>
                                     experience) {
                                if (!self) {
                                  std::move(callback).Run(
                                      base::unexpected(HomeError::kCancelled));
                                  return;
                                }
                                if (!self->HasActiveHistoryBootstrapForTurn(
                                        agent_turn_id) ||
                                    !self->ValidateBootstrapContext(
                                        agent_turn_id, draft.base_revision)) {
                                  std::move(callback).Run(
                                      base::unexpected(HomeError::kCancelled));
                                  return;
                                }
                                if (!experience.has_value()) {
                                  std::move(callback).Run(
                                      base::unexpected(experience.error()));
                                  return;
                                }
                                std::move(callback).Run(
                                    self->bootstrap_transaction_
                                        ->BindFinalDraft(
                                            draft, std::move(authorizations),
                                            std::move(experience.value())));
                              },
                              self, agent_turn_id, std::move(draft),
                              std::move(authorizations.value()),
                              std::move(callback)));
                    },
                    self, agent_turn_id, std::move(draft),
                    std::move(callback)));
          },
          weak_factory_.GetWeakPtr(), agent_turn_id, draft,
          std::move(callback)));
}

void DaoHomeProjectService::PublishBootstrapDraft(
    const std::string& agent_turn_id,
    const HomeDraft& draft,
    ResultCallback<HomeVersion> callback,
    scoped_refptr<DaoHomeMutationLease> authorization) {
  if (!HasActiveHistoryBootstrapForTurn(agent_turn_id) ||
      !ValidateBootstrapContext(agent_turn_id, draft.base_revision) ||
      !authorization || !authorization->IsValid()) {
    std::move(callback).Run(base::unexpected(HomeError::kCancelled));
    return;
  }
  store_.AsyncCall(&DaoHomeProjectStore::ValidateHistoryBootstrapFiles)
      .WithArgs(draft.id)
      .Then(base::BindOnce(
          [](base::WeakPtr<DaoHomeProjectService> self,
             std::string agent_turn_id, HomeDraft draft,
             ResultCallback<HomeVersion> callback,
             scoped_refptr<DaoHomeMutationLease> authorization,
             base::expected<void, HomeError> validation) {
            if (!self) {
              std::move(callback).Run(base::unexpected(HomeError::kCancelled));
              return;
            }
            if (!validation.has_value()) {
              std::move(callback).Run(base::unexpected(validation.error()));
              return;
            }
            if (!self->HasActiveHistoryBootstrapForTurn(agent_turn_id) ||
                !self->ValidateBootstrapContext(agent_turn_id,
                                                draft.base_revision) ||
                !authorization || !authorization->IsValid()) {
              std::move(callback).Run(base::unexpected(HomeError::kCancelled));
              return;
            }
            auto publishing =
                self->bootstrap_transaction_->BeginPublish(draft.id);
            if (!publishing.has_value()) {
              std::move(callback).Run(base::unexpected(publishing.error()));
              return;
            }
            std::vector<std::string> connector_ids;
            connector_ids.reserve(draft.manifest.connectors.size());
            for (const HomeConnector& connector : draft.manifest.connectors) {
              connector_ids.push_back(connector.id);
            }
            const std::string transaction_id =
                self->active_bootstrap_transaction_id_;
            self->store_.AsyncCall(&DaoHomeProjectStore::PublishWithGrants)
                .WithArgs(draft.id, connector_ids,
                          HomeRevisionKind::kHistoryBootstrap,
                          std::move(authorization), std::nullopt)
                .Then(base::BindOnce(
                    [](base::WeakPtr<DaoHomeProjectService> self,
                       std::string transaction_id,
                       ResultCallback<HomeVersion> callback,
                       base::expected<HomeVersion, HomeError> result) {
                      if (!self) {
                        std::move(callback).Run(
                            base::unexpected(HomeError::kCancelled));
                        return;
                      }
                      if (self->before_bootstrap_publish_reply_callback_for_testing_) {
                        std::move(
                            self->before_bootstrap_publish_reply_callback_for_testing_)
                            .Run();
                      }
                      if (!self) {
                        std::move(callback).Run(
                            base::unexpected(HomeError::kCancelled));
                        return;
                      }
                      const bool transaction_matches =
                          self->bootstrap_transaction_ &&
                          self->active_bootstrap_transaction_id_ ==
                              transaction_id;
                      if (!result.has_value()) {
                        if (transaction_matches) {
                          self->FinishOrCancelBootstrap();
                        }
                        std::move(callback).Run(std::move(result));
                        return;
                      }
                      if (transaction_matches) {
                        self->bootstrap_transaction_->MarkPublished();
                        self->FinishOrCancelBootstrap();
                      }
                      self->current_revision_ = result->id;
                      self->NotifyProjectChanged();
                      std::move(callback).Run(std::move(result));
                    },
                    self, transaction_id, std::move(callback)));
          },
          weak_factory_.GetWeakPtr(), agent_turn_id, draft, std::move(callback),
          std::move(authorization)));
}

base::expected<void, HomeError> DaoHomeProjectService::CompleteBootstrapPreview(
    const std::string& agent_turn_id,
    const std::string& draft_id) {
  if (!HasActiveHistoryBootstrapForTurn(agent_turn_id) ||
      !ValidateBootstrapContext(agent_turn_id,
                                active_bootstrap_base_revision_)) {
    return base::unexpected(HomeError::kCancelled);
  }
  auto marked = bootstrap_transaction_->MarkPreviewed(draft_id);
  if (marked.has_value()) {
    previewed_drafts_.insert(draft_id);
  }
  return marked;
}

base::expected<void, HomeError> DaoHomeProjectService::RejectBootstrapPreview(
    const std::string& agent_turn_id,
    const std::string& draft_id) {
  if (!HasActiveHistoryBootstrapForTurn(agent_turn_id) ||
      !ValidateBootstrapContext(agent_turn_id,
                                active_bootstrap_base_revision_)) {
    return base::unexpected(HomeError::kCancelled);
  }
  return bootstrap_transaction_->RejectPreview(draft_id);
}

bool DaoHomeProjectService::HasActiveHistoryBootstrapForTurn(
    const std::string& agent_turn_id) const {
  return bootstrap_transaction_ && !agent_turn_id.empty() &&
         active_bootstrap_turn_id_ == agent_turn_id;
}

void DaoHomeProjectService::CancelHistoryBootstrapForOwner(
    content::WebContents* owner) {
  if (bootstrap_transaction_ && active_bootstrap_owner_.get() == owner) {
    FinishOrCancelBootstrap();
  }
}

void DaoHomeProjectService::CancelHistoryBootstrapForTurn(
    const std::string& agent_turn_id) {
  if (HasActiveHistoryBootstrapForTurn(agent_turn_id)) {
    FinishOrCancelBootstrap();
  }
}

void DaoHomeProjectService::ClearHistoryBootstrapForOwner(
    content::WebContents* owner) {
  CancelHistoryBootstrapForOwner(owner);
  if (history_bootstrap_ && history_bootstrap_->owner.get() == owner) {
    history_bootstrap_.reset();
  }
}

void DaoHomeProjectService::ClearHistoryBootstrapForClaim(
    const std::string& claim_token) {
  if (history_bootstrap_ && !claim_token.empty() &&
      history_bootstrap_->claim_token == claim_token) {
    history_bootstrap_.reset();
  }
}

void DaoHomeProjectService::ClearHistoryBootstrapForTurn(
    const std::string& agent_turn_id) {
  CancelHistoryBootstrapForTurn(agent_turn_id);
  if (history_bootstrap_ && !agent_turn_id.empty() &&
      history_bootstrap_->agent_turn_id == agent_turn_id) {
    history_bootstrap_.reset();
  }
}

bool DaoHomeProjectService::ValidateBootstrapContext(
    const std::string& expected_turn_id,
    const std::string& expected_base_revision) const {
  return bootstrap_transaction_ && active_bootstrap_owner_ &&
         active_bootstrap_owner_->GetVisibility() !=
             content::Visibility::HIDDEN &&
         !expected_turn_id.empty() &&
         expected_turn_id == active_bootstrap_turn_id_ &&
         expected_base_revision == active_bootstrap_base_revision_ &&
         expected_base_revision == current_revision_ &&
         active_bootstrap_owner_validator_ &&
         active_bootstrap_owner_validator_.Run();
}

base::expected<void, HomeError>
DaoHomeProjectService::RegisterBootstrapDraftForTurn(
    const std::string& agent_turn_id,
    const HomeDraft& draft) {
  if (!HasActiveHistoryBootstrapForTurn(agent_turn_id) ||
      !ValidateBootstrapContext(agent_turn_id, draft.base_revision)) {
    return base::unexpected(HomeError::kCancelled);
  }
  return bootstrap_transaction_->RegisterDraft(draft);
}

void DaoHomeProjectService::FinishOrCancelBootstrap() {
  if (!bootstrap_transaction_) {
    return;
  }
  std::unique_ptr<DaoHomeBootstrapTransaction> transaction =
      std::move(bootstrap_transaction_);
  std::vector<std::string> cleanup_inventory = transaction->Cancel();
  BootstrapDecisionCallback decision = std::move(bootstrap_decision_callback_);
  const bool notify_permission = bootstrap_permission_preparing_ ||
                                 bootstrap_permission_request_.has_value() ||
                                 static_cast<bool>(decision);

  active_bootstrap_transaction_id_.clear();
  active_bootstrap_brief_ = HomeBootstrapBrief();
  active_bootstrap_owner_.reset();
  active_bootstrap_turn_id_.clear();
  active_bootstrap_base_revision_.clear();
  active_bootstrap_owner_validator_.Reset();
  bootstrap_permission_preparing_ = false;
  bootstrap_permission_request_.reset();
  bootstrap_decision_callback_.Reset();
  bootstrap_selected_connector_ids_.clear();
  bootstrap_completed_connector_ids_.clear();
  bootstrap_successful_connector_ids_.clear();
  bootstrap_connector_test_in_flight_.clear();

  for (const std::string& draft_id : cleanup_inventory) {
    DiscardDraft(draft_id,
                 base::BindOnce([](base::expected<void, HomeError>) {}));
  }
  if (notify_permission) {
    NotifyBootstrapPermissionChanged();
  }
  if (decision) {
    std::move(decision).Run(base::unexpected(HomeError::kCancelled));
  }
}

void DaoHomeProjectService::SetSelectedNode(std::string node_id) {
  selected_node_ = std::move(node_id);
}

void DaoHomeProjectService::ClearSelectedNode() {
  selected_node_.clear();
}

std::string DaoHomeProjectService::DraftConnectorKey(
    const std::string& draft_id,
    const std::string& connector_id) {
  return draft_id + "\n" + connector_id;
}

base::WeakPtr<DaoHomeProjectService> DaoHomeProjectService::GetWeakPtr() {
  return weak_factory_.GetWeakPtr();
}

}  // namespace dao
