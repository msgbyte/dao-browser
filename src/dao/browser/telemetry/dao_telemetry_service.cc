// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/telemetry/dao_telemetry_service.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/no_destructor.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/storage_partition.h"
#include "net/base/load_flags.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "url/gurl.h"

namespace dao {

namespace {

// Tianji application tracking endpoint and identifier. See
// https://tianji.dev/docs/application/tracking for the protocol.
constexpr char kTianjiEventEndpoint[] =
    "https://app.tianji.dev/api/application/send";
constexpr char kTianjiApplicationId[] = "cmpb4zdf0he9p78rcrkg6j7vg";

}  // namespace

class DaoTelemetryService::Impl {
 public:
  Impl() = default;
  ~Impl() = default;

  Impl(const Impl&) = delete;
  Impl& operator=(const Impl&) = delete;

  void SetProfile(Profile* profile) { profile_ = profile; }

  void ReportBrowserOpenedOnce() {
    if (browser_opened_reported_) {
      return;
    }
    browser_opened_reported_ = true;
    ReportEvent("open", base::DictValue());
  }

  void ReportEvent(const std::string& event_name,
                   base::DictValue event_data) {
    if (!profile_) {
      return;
    }

    base::DictValue payload;
    payload.Set("application", kTianjiApplicationId);
    payload.Set("name", event_name);
    payload.Set("data", std::move(event_data));

    base::DictValue envelope;
    envelope.Set("type", "event");
    envelope.Set("payload", std::move(payload));

    std::string body;
    if (!base::JSONWriter::Write(envelope, &body)) {
      return;
    }

    auto request = std::make_unique<network::ResourceRequest>();
    request->url = GURL(kTianjiEventEndpoint);
    request->method = "POST";
    request->credentials_mode = network::mojom::CredentialsMode::kOmit;
    request->load_flags = net::LOAD_BYPASS_CACHE | net::LOAD_DISABLE_CACHE;

    static const net::NetworkTrafficAnnotationTag annotation =
        net::DefineNetworkTrafficAnnotation("dao_telemetry_tianji", R"(
          semantics {
            sender: "Dao Browser Telemetry"
            description:
              "Reports anonymous application events (browser opened, "
              "agent message sent, etc.) to the Tianji application "
              "tracking endpoint so the Dao Browser team can understand "
              "aggregate feature usage."
            trigger:
              "Browser startup, or specific user-initiated actions like "
              "sending a message to the agent."
            data:
              "An application identifier, an event name (e.g. 'open'), "
              "and optionally a small dict of event metadata. No URLs, "
              "no page content, no user credentials, no PII."
            destination: WEBSITE
          }
          policy {
            cookies_allowed: NO
            setting:
              "There is no in-product toggle yet; telemetry can be "
              "disabled by blocking app.tianji.dev at the network layer."
            policy_exception_justification:
              "Anonymous aggregate usage metrics for product "
              "improvement; no user identifiers are sent."
          })");

    std::unique_ptr<network::SimpleURLLoader> loader =
        network::SimpleURLLoader::Create(std::move(request), annotation);
    loader->SetTimeoutDuration(base::Seconds(10));
    loader->AttachStringForUpload(body, "application/json");

    network::SimpleURLLoader* loader_ptr = loader.get();
    inflight_[loader_ptr] = std::move(loader);

    scoped_refptr<network::SharedURLLoaderFactory> factory =
        profile_->GetDefaultStoragePartition()
            ->GetURLLoaderFactoryForBrowserProcess();

    loader_ptr->DownloadToString(
        factory.get(),
        base::BindOnce(&Impl::OnRequestComplete, weak_factory_.GetWeakPtr(),
                       loader_ptr),
        /*max_body_size=*/8 * 1024);
  }

 private:
  void OnRequestComplete(network::SimpleURLLoader* loader_ptr,
                         std::optional<std::string> body) {
    inflight_.erase(loader_ptr);
  }

  raw_ptr<Profile> profile_ = nullptr;
  bool browser_opened_reported_ = false;
  std::map<network::SimpleURLLoader*, std::unique_ptr<network::SimpleURLLoader>>
      inflight_;
  base::WeakPtrFactory<Impl> weak_factory_{this};
};

// static
DaoTelemetryService* DaoTelemetryService::GetInstance() {
  static base::NoDestructor<DaoTelemetryService> instance;
  return instance.get();
}

DaoTelemetryService::DaoTelemetryService() : impl_(std::make_unique<Impl>()) {}
DaoTelemetryService::~DaoTelemetryService() = default;

void DaoTelemetryService::SetProfile(Profile* profile) {
  impl_->SetProfile(profile);
}

void DaoTelemetryService::ReportBrowserOpenedOnce() {
  impl_->ReportBrowserOpenedOnce();
}

void DaoTelemetryService::ReportEvent(const std::string& event_name,
                                      base::DictValue event_data) {
  impl_->ReportEvent(event_name, std::move(event_data));
}

}  // namespace dao
