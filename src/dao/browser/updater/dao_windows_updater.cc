// Copyright 2026 Dao Browser Authors. All rights reserved.
#include "dao/browser/updater/dao_windows_updater.h"

#include <algorithm>
#include "base/base_paths_win.h"
#include "base/command_line.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/path_service.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/thread_pool.h"
#include "base/uuid.h"
#include "base/win/windows_types.h"
#include "build/build_config.h"
#include "chrome/browser/lifetime/application_lifetime.h"
#include "chrome/browser/lifetime/application_lifetime_desktop.h"
#include "chrome/common/chrome_switches.h"
#include "components/version_info/version_info.h"
#include "dao/browser/updater/dao_update_win_util.h"
#include "net/base/load_flags.h"
#include "net/base/net_errors.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"

namespace dao {
namespace {
using update_win::Handle;
constexpr auto kCheckInterval = base::Hours(24);

base::Version DaoVersion() {
  std::string version = DAO_DISPLAY_VERSION;
  if (base::Version(version).components().size() == 4)
    version.resize(version.rfind('.'));
  return base::Version(version);
}
base::Version ChromiumVersion() {
  return version_info::GetVersion();
}

auto ParseManifest(const std::string& envelope) {
  return ParseSignedWindowsUpdate(envelope, DaoVersion(), ChromiumVersion());
}

bool WriteCache(const base::FilePath& directory,
                const char* name,
                const std::string& value) {
  base::FilePath temporary;
  if (!base::CreateTemporaryFileInDir(directory, &temporary))
    return false;
  const bool saved =
      base::WriteFile(temporary, value) &&
      base::ReplaceFile(temporary, directory.AppendASCII(name), nullptr);
  if (!saved)
    base::DeleteFile(temporary);
  return saved;
}

std::wstring RegisteredDirectory() {
  const auto browser = update_win::RegisteredBrowser();
  if (browser.empty() ||
      !update_win::SameFile(browser,
                            update_win::ProcessPath(GetCurrentProcess())))
    return {};
  return browser.substr(0, browser.find_last_of(L'\\'));
}
}  // namespace

struct DaoWindowsUpdater::Cache {
  base::FilePath directory;
  base::Time last_check;
  std::string envelope;
  std::optional<WindowsUpdateManifest> manifest;
  base::FilePath installer;
};

struct DaoWindowsUpdater::Handoff {
  Handle pipe;
  Handle installer_lock;
  Handle wrapper;
  bool committed = false;
  ~Handoff() {
    if (pipe.valid() && !committed)
      update_win::Send(pipe.get(), 'X');
  }
};

DaoWindowsUpdater::DaoWindowsUpdater() = default;
DaoWindowsUpdater::~DaoWindowsUpdater() = default;

DaoWindowsUpdater::Cache DaoWindowsUpdater::LoadCache() {
  Cache cache;
  if (RegisteredDirectory().empty())
    return cache;
  base::FilePath local;
  if (!base::PathService::Get(base::DIR_LOCAL_APP_DATA, &local) ||
      !update_win::NoReparsePoints(local.value()))
    return cache;
  auto parent = local.AppendASCII("Dao");
  if (!base::CreateDirectory(parent) ||
      !update_win::NoReparsePoints(parent.value()))
    return cache;
  auto directory = parent.AppendASCII("Updates");
  update_win::UserSecurity security;
  if (!security.get())
    return cache;
  if (!CreateDirectoryW(directory.value().c_str(), security.get()) &&
      GetLastError() != ERROR_ALREADY_EXISTS)
    return cache;
  if (!update_win::NoReparsePoints(directory.value()) ||
      !security.Protect(directory.value()))
    return cache;
  cache.directory = directory;
  std::string saved;
  int64_t timestamp = 0;
  if (base::ReadFileToStringWithMaxSize(directory.AppendASCII("last-check"),
                                        &saved, 64) &&
      base::StringToInt64(saved, &timestamp) && timestamp > 0 &&
      timestamp <= base::Time::Now().ToTimeT()) {
    cache.last_check = base::Time::FromTimeT(timestamp);
  }
  if (base::ReadFileToStringWithMaxSize(directory.AppendASCII("ready.json"),
                                        &cache.envelope,
                                        kWindowsUpdateManifestLimit)) {
    auto manifest = ParseManifest(cache.envelope);
    if (manifest.has_value()) {
      cache.installer = directory.AppendASCII(
          "dao-browser-" + manifest->version + "-windows-x64.exe");
      if (update_win::NoReparsePoints(cache.installer.value()) &&
          VerifyWindowsUpdateFile(cache.installer, *manifest))
        cache.manifest = std::move(*manifest);
    }
  }
  base::FileEnumerator files(directory, false, base::FileEnumerator::FILES);
  for (auto file = files.Next(); !file.empty(); file = files.Next()) {
    if ((!cache.manifest || file != cache.installer) &&
        IsWindowsUpdateCacheFileName(file.BaseName().AsUTF8Unsafe()) &&
        update_win::NoReparsePoints(file.value()))
      base::DeleteFile(file);
  }
  if (!cache.manifest)
    base::DeleteFile(directory.AppendASCII("ready.json"));
  return cache;
}

void DaoWindowsUpdater::Start(
    StatusCallback callback,
    scoped_refptr<network::SharedURLLoaderFactory> factory) {
  supported_ = false;
  factory_ = std::move(factory);
  callback_ = std::move(callback);
#if defined(COMPONENT_BUILD) || !defined(ARCH_CPU_X86_64)
  SetState(DaoUpdateState::kUnsupported);
  return;
#endif
  const auto* command = base::CommandLine::ForCurrentProcess();
  if (command->HasSwitch(switches::kUserDataDir) ||
      command->HasSwitch(switches::kIncognito) ||
      command->HasSwitch(switches::kGuest)) {
    SetState(DaoUpdateState::kUnsupported);
    return;
  }
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock()},
      base::BindOnce(&DaoWindowsUpdater::LoadCache),
      base::BindOnce(&DaoWindowsUpdater::OnCacheLoaded,
                     weak_factory_.GetWeakPtr()));
}

void DaoWindowsUpdater::OnCacheLoaded(Cache cache) {
  supported_ = !cache.directory.empty();
  cache_dir_ = cache.directory;
  if (!supported_) {
    SetState(DaoUpdateState::kUnsupported);
    return;
  }
  if (cache.manifest) {
    manifest_ = std::move(cache.manifest);
    envelope_ = std::move(cache.envelope);
    installer_ = cache.installer;
    SetState(DaoUpdateState::kReady);
  } else {
    SetState(DaoUpdateState::kIdle);
    ScheduleNextCheck(cache.last_check);
  }
}

void DaoWindowsUpdater::ScheduleNextCheck(base::Time last_check) {
  const auto now = base::Time::Now();
  const auto delay =
      last_check.is_null() || last_check > now
          ? base::Seconds(30)
          : std::max(base::Seconds(30), kCheckInterval - (now - last_check));
  timer_.Start(FROM_HERE, delay,
               base::BindOnce(&DaoWindowsUpdater::CheckForUpdates,
                              weak_factory_.GetWeakPtr(), false));
}

void DaoWindowsUpdater::SetState(DaoUpdateState state, DaoUpdateError error) {
  status_ = {state, manifest_ ? manifest_->version : std::string(), error};
  callback_.Run(status_);
}

void DaoWindowsUpdater::Fail(DaoUpdateError error) {
  loader_.reset();
  SetState(DaoUpdateState::kError, error);
  timer_.Start(FROM_HERE, kCheckInterval,
               base::BindOnce(&DaoWindowsUpdater::CheckForUpdates,
                              weak_factory_.GetWeakPtr(), false));
}

void DaoWindowsUpdater::StartRequest(const GURL& url, base::TimeDelta timeout) {
  auto request = std::make_unique<network::ResourceRequest>();
  request->url = url;
  request->credentials_mode = network::mojom::CredentialsMode::kOmit;
  request->load_flags = net::LOAD_BYPASS_CACHE | net::LOAD_DISABLE_CACHE;
  const auto annotation =
      net::DefineNetworkTrafficAnnotation("dao_windows_update", R"(
    semantics {
      sender: "Dao Windows Updater"
      description: "Checks signed Dao release metadata and downloads verified installers."
      trigger: "Once per day in a registered installation or a manual update check."
      data: "No user data; the requested installer version."
      destination: OTHER
      destination_other: "Dao release storage at dao-release.msgbyte.com"
    }
    policy { cookies_allowed: NO setting: "Only active in installed Dao builds."
      policy_exception_justification: "Not implemented." }
  )");
  loader_ = network::SimpleURLLoader::Create(std::move(request), annotation);
  loader_->SetTimeoutDuration(timeout);
  loader_->SetOnRedirectCallback(base::BindRepeating(
      [](base::WeakPtr<DaoWindowsUpdater> self, const GURL&,
         const net::RedirectInfo&, const network::mojom::URLResponseHead&,
         std::vector<std::string>*) {
        if (self)
          self->Fail(DaoUpdateError::kNetwork);
      },
      weak_factory_.GetWeakPtr()));
}

void DaoWindowsUpdater::CheckForUpdates(bool user_initiated) {
  if (stopped_ || loader_ || status_.state == DaoUpdateState::kChecking ||
      status_.state == DaoUpdateState::kDownloading ||
      status_.state == DaoUpdateState::kApplying ||
      status_.state == DaoUpdateState::kReady)
    return;
  if (!supported_) {
    if (user_initiated)
      SetState(DaoUpdateState::kUnsupported);
    return;
  }
  timer_.Stop();
  SetState(DaoUpdateState::kChecking);
  StartRequest(GURL(std::string(kWindowsUpdateOrigin) + "/windows-update.json"),
               base::Seconds(30));
  loader_->DownloadToString(factory_.get(),
                            base::BindOnce(&DaoWindowsUpdater::OnManifest,
                                           weak_factory_.GetWeakPtr()),
                            kWindowsUpdateManifestLimit);
}

void DaoWindowsUpdater::OnManifest(std::optional<std::string> envelope) {
  if (!envelope || loader_->NetError() != net::OK) {
    Fail(DaoUpdateError::kNetwork);
    return;
  }
  loader_.reset();
  auto parsed = ParseManifest(*envelope);
  if (!parsed.has_value() &&
      parsed.error() != WindowsUpdateManifestError::kNoUpdate) {
    Fail(DaoUpdateError::kVerification);
    return;
  }
  base::ThreadPool::PostTask(
      FROM_HERE, {base::MayBlock()},
      base::BindOnce(
          [](base::FilePath directory) {
            WriteCache(directory, "last-check",
                       base::NumberToString(base::Time::Now().ToTimeT()));
          },
          cache_dir_));
  if (!parsed.has_value()) {
    SetState(DaoUpdateState::kUpToDate);
    timer_.Start(FROM_HERE, kCheckInterval,
                 base::BindOnce(&DaoWindowsUpdater::CheckForUpdates,
                                weak_factory_.GetWeakPtr(), false));
    return;
  }
  manifest_ = std::move(*parsed);
  envelope_ = std::move(*envelope);
  DownloadUpdate();
}

void DaoWindowsUpdater::DownloadUpdate() {
  installer_ = cache_dir_.AppendASCII("dao-browser-" + manifest_->version +
                                      "-windows-x64.exe");
  SetState(DaoUpdateState::kDownloading);
  StartRequest(manifest_->url, base::Minutes(30));
  const auto temporary = cache_dir_.AppendASCII(
      base::Uuid::GenerateRandomV4().AsLowercaseString() + ".download");
  loader_->DownloadToFile(factory_.get(),
                          base::BindOnce(&DaoWindowsUpdater::OnDownloaded,
                                         weak_factory_.GetWeakPtr()),
                          temporary, manifest_->size);
}

void DaoWindowsUpdater::OnDownloaded(base::FilePath path) {
  if (path.empty() || loader_->NetError() != net::OK) {
    Fail(DaoUpdateError::kNetwork);
    return;
  }
  loader_.reset();
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock()},
      base::BindOnce(
          [](base::FilePath path, base::FilePath destination,
             WindowsUpdateManifest manifest, std::string envelope) {
            const bool valid =
                update_win::NoReparsePoints(path.value()) &&
                VerifyWindowsUpdateFile(path, manifest) &&
                base::ReplaceFile(path, destination, nullptr) &&
                WriteCache(destination.DirName(), "ready.json", envelope);
            base::DeleteFile(path);
            return valid;
          },
          path, installer_, *manifest_, envelope_),
      base::BindOnce(&DaoWindowsUpdater::OnVerified,
                     weak_factory_.GetWeakPtr()));
}

void DaoWindowsUpdater::OnVerified(bool valid) {
  if (valid)
    SetState(DaoUpdateState::kReady);
  else
    Fail(DaoUpdateError::kVerification);
}

std::unique_ptr<DaoWindowsUpdater::Handoff> DaoWindowsUpdater::PrepareHandoff(
    base::FilePath installer,
    WindowsUpdateManifest manifest) {
  if (RegisteredDirectory().empty() ||
      !update_win::NoReparsePoints(installer.value()))
    return nullptr;
  auto handoff = std::make_unique<Handoff>();
  // Deny replacement or mutation until the helper has opened its own read lock.
  handoff->installer_lock = Handle(
      CreateFileW(installer.value().c_str(), GENERIC_READ, FILE_SHARE_READ,
                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
  if (!handoff->installer_lock.valid() ||
      !VerifyWindowsUpdateFile(installer, manifest))
    return nullptr;
  std::string token = base::Uuid::GenerateRandomV4().AsLowercaseString();
  std::erase(token, '-');
  const std::wstring pipe_name = update_win::PipeName(base::UTF8ToWide(token));
  update_win::UserSecurity security;
  if (!security.get())
    return nullptr;
  handoff->pipe = Handle(CreateNamedPipeW(
      pipe_name.c_str(),
      PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
      PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT |
          PIPE_REJECT_REMOTE_CLIENTS,
      1, 128, 128, 0, security.get()));
  if (!handoff->pipe.valid())
    return nullptr;
  Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
  OVERLAPPED connect{};
  connect.hEvent = event.get();
  if (!event.valid())
    return nullptr;
  ConnectNamedPipe(handoff->pipe.get(), &connect);
  std::wstring command =
      L"\"" + installer.value() + L"\" /DAOUPDATE=" + base::UTF8ToWide(token);
  STARTUPINFOW startup{sizeof(startup)};
  PROCESS_INFORMATION process{};
  const bool launched = CreateProcessW(
      installer.value().c_str(), command.data(), nullptr, nullptr, FALSE,
      CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process);
  if (launched) {
    handoff->wrapper = Handle(process.hProcess);
    CloseHandle(process.hThread);
  }
  DWORD count = 0;
  if (!launched || WaitForSingleObject(event.get(), 30000) != WAIT_OBJECT_0) {
    CancelIoEx(handoff->pipe.get(), &connect);
    GetOverlappedResult(handoff->pipe.get(), &connect, &count, TRUE);
    return nullptr;
  }
  if (!GetOverlappedResult(handoff->pipe.get(), &connect, &count, TRUE))
    return nullptr;
  ULONG client = 0;
  char ready = 0;
  if (!GetNamedPipeClientProcessId(handoff->pipe.get(), &client) ||
      update_win::ParentPid(client) != process.dwProcessId ||
      WaitForSingleObject(handoff->wrapper.get(), 0) != WAIT_TIMEOUT ||
      !update_win::Transfer(handoff->pipe.get(), &ready, 1, false, 30000) ||
      ready != 'R')
    return nullptr;
  FILETIME creation{};
  if (!update_win::CreationTime(GetCurrentProcess(), &creation) ||
      !update_win::Transfer(handoff->pipe.get(), &creation, sizeof(creation),
                            true, 1000))
    return nullptr;
  return handoff;
}

bool DaoWindowsUpdater::ApplyReadyUpdate() {
  if (stopped_ || status_.state != DaoUpdateState::kReady || !manifest_)
    return false;
  SetState(DaoUpdateState::kApplying);
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock()},
      base::BindOnce(&DaoWindowsUpdater::PrepareHandoff, installer_,
                     *manifest_),
      base::BindOnce(&DaoWindowsUpdater::OnHandoff,
                     weak_factory_.GetWeakPtr()));
  return true;
}

void DaoWindowsUpdater::OnHandoff(std::unique_ptr<Handoff> handoff) {
  if (!handoff) {
    Fail(DaoUpdateError::kInstall);
    return;
  }
  handoff_ = std::move(handoff);
  closing_subscription_ =
      chrome::AddClosingAllBrowsersCallback(base::BindRepeating(
          &DaoWindowsUpdater::OnClosingBrowsers, weak_factory_.GetWeakPtr()));
  handoff_timer_.Start(FROM_HERE,
                       base::Milliseconds(update_win::kHandoffTimeout - 10000),
                       base::BindOnce(&DaoWindowsUpdater::OnClosingBrowsers,
                                      weak_factory_.GetWeakPtr(), false));
  chrome::AttemptExit();
}

void DaoWindowsUpdater::OnClosingBrowsers(bool closing) {
  if (closing || !handoff_)
    return;
  handoff_.reset();
  exit_confirmed_ = false;
  handoff_timer_.Stop();
  closing_subscription_ = {};
  SetState(DaoUpdateState::kReady);
}

bool DaoWindowsUpdater::PrepareForExit() {
  if (!handoff_)
    return false;
  exit_confirmed_ = true;
  handoff_timer_.Stop();
  return true;
}

void DaoWindowsUpdater::Shutdown() {
  // Called only after the browser message loop ends, never on a quit request.
  stopped_ = true;
  timer_.Stop();
  handoff_timer_.Stop();
  closing_subscription_ = {};
  loader_.reset();
  weak_factory_.InvalidateWeakPtrs();
  if (handoff_ && exit_confirmed_)
    handoff_->committed = update_win::Send(handoff_->pipe.get(), 'C');
  handoff_.reset();
}
}  // namespace dao
