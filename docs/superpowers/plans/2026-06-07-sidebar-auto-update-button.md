# Sidebar Auto-Update Button Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a right-aligned sidebar update button that appears only when Sparkle has a downloaded update ready to install, expands on hover, and applies the update in one click.

**Architecture:** Sparkle remains responsible for checking, downloading, verifying, and installing updates. `DaoUpdaterService` becomes the process-wide update state source, `DaoSidebarUIHandler` bridges that state to the sidebar WebUI, and a new Lit component renders the compact Lucide `circle-arrow-up` action beside the plus button.

**Tech Stack:** Chromium C++ / Objective-C++ Sparkle 2, `base::ObserverList`, Views WebUI message handlers, Lit TypeScript, Vitest, Dao GRD strings.

---

## File Structure

- Modify `src/dao/browser/updater/dao_updater_service.h`: add update status types, observer interface, apply method, and testing hooks.
- Modify `src/dao/browser/updater/dao_updater_service.cc`: store update state, notify observers, consume the ready install callback, and pass callbacks into the macOS Sparkle wrapper.
- Modify `src/dao/browser/updater/dao_sparkle_updater_mac.h`: accept callbacks from the service and expose `Start(...)` with ready/finish notifications.
- Modify `src/dao/browser/updater/dao_sparkle_updater_mac.mm`: add an Objective-C delegate for `SPUUpdaterDelegate`, capture `willInstallUpdateOnQuit`, and forward the immediate install block to the service.
- Modify `src/dao/browser/ui/webui/dao_sidebar_ui.h`: make the sidebar handler observe update service state and add update message handlers/testing helpers.
- Modify `src/dao/browser/ui/webui/dao_sidebar_ui.cc`: push localized update state to WebUI and handle `requestUpdateState` / `applyReadyUpdate`.
- Modify `src/dao/browser/ui/webui/resources/sidebar/sidebar_bridge.ts`: add `UpdateStateData`.
- Create `src/dao/browser/ui/webui/resources/sidebar/dao_update_button.ts`: render the button and send `applyReadyUpdate`.
- Modify `src/dao/browser/ui/webui/resources/sidebar/dao_sidebar_app.ts`: listen for update state and render the new button in the right toolbar slot.
- Modify `src/dao/browser/ui/webui/resources/sidebar/BUILD.gn`: include the new TypeScript file.
- Create `src/dao/browser/ui/webui/resources/sidebar/__tests__/update_button.test.ts`: component behavior tests.
- Modify `src/dao/browser/ui/webui/resources/sidebar/__tests__/sidebar_app.test.ts`: placement test for the right-aligned update button.
- Modify `src/dao/browser/strings/dao_strings.grd`: add update action labels.
- Modify `src/dao/browser/ui/views/dao_browser_browsertest.cc`: focused native state tests through existing Dao browser test wiring.

Project constraints:

- Do not edit `engine/` directly.
- Do not run `autoninja`, `ninja`, `siso`, or direct Chromium build tools.
- Use `npm run rebuild` for compile confirmation.
- Do not run `i18n.sh`.
- Do not run git state-changing commands unless the latest user message explicitly authorizes them.

---

### Task 1: Add Updater State API

**Files:**
- Modify: `src/dao/browser/updater/dao_updater_service.h`
- Modify: `src/dao/browser/updater/dao_updater_service.cc`
- Test: `src/dao/browser/ui/views/dao_browser_browsertest.cc`

- [ ] **Step 1: Write failing browser tests for updater state**

Append these tests near the existing `DaoSidebarBrowserTest` sidebar state tests in `src/dao/browser/ui/views/dao_browser_browsertest.cc`.

```cpp
IN_PROC_BROWSER_TEST_F(DaoSidebarBrowserTest,
                       UpdateStateReadyIsExposedToSidebar) {
  dao::DaoUpdaterService* service = dao::DaoUpdaterService::GetInstance();
  service->ResetForTesting();

  int apply_count = 0;
  service->SetReadyUpdateForTesting(
      "1.2.3", base::BindLambdaForTesting([&]() { ++apply_count; }));

  dao::DaoSidebarUIHandler handler;
  AttachSidebarHandlerForTesting(browser(), &handler);

  const dao::DaoUpdateStatus status = service->GetUpdateStatus();
  EXPECT_EQ(dao::DaoUpdateState::kReady, status.state);
  EXPECT_EQ("1.2.3", status.display_version);

  service->ResetForTesting();
}

IN_PROC_BROWSER_TEST_F(DaoSidebarBrowserTest,
                       ApplyingReadyUpdateConsumesInstallCallbackOnce) {
  dao::DaoUpdaterService* service = dao::DaoUpdaterService::GetInstance();
  service->ResetForTesting();

  int apply_count = 0;
  service->SetReadyUpdateForTesting(
      "1.2.3", base::BindLambdaForTesting([&]() { ++apply_count; }));

  EXPECT_TRUE(service->ApplyReadyUpdate());
  EXPECT_EQ(1, apply_count);
  EXPECT_FALSE(service->ApplyReadyUpdate());
  EXPECT_EQ(1, apply_count);
  EXPECT_EQ(dao::DaoUpdateState::kIdle, service->GetUpdateStatus().state);

  service->ResetForTesting();
}
```

- [ ] **Step 2: Run the focused browser tests and verify they fail**

Run only if the `browser_tests` binary is already available:

```bash
engine/src/out/dao-debug/browser_tests --gtest_filter="DaoSidebarBrowserTest.UpdateStateReadyIsExposedToSidebar:DaoSidebarBrowserTest.ApplyingReadyUpdateConsumesInstallCallbackOnce"
```

Expected: fails to compile or fails at runtime because `DaoUpdaterService`, `SetReadyUpdateForTesting`, `ResetForTesting`, `ApplyReadyUpdate`, and `DaoUpdateState` are not defined yet.

- [ ] **Step 3: Add the public service model**

In `src/dao/browser/updater/dao_updater_service.h`, add these includes:

```cpp
#include <string>

#include "base/functional/callback.h"
#include "base/observer_list_types.h"
```

Add these declarations inside `namespace dao` before `class DaoUpdaterService`:

```cpp
enum class DaoUpdateState {
  kIdle,
  kReady,
  kApplying,
  kUnsupported,
};

struct DaoUpdateStatus {
  DaoUpdateState state = DaoUpdateState::kIdle;
  std::string display_version;
};

class DaoUpdaterServiceObserver : public base::CheckedObserver {
 public:
  virtual void OnDaoUpdateStatusChanged(const DaoUpdateStatus& status) = 0;
};
```

Add these public methods to `DaoUpdaterService`:

```cpp
  void AddObserver(DaoUpdaterServiceObserver* observer);
  void RemoveObserver(DaoUpdaterServiceObserver* observer);

  DaoUpdateStatus GetUpdateStatus() const;
  bool ApplyReadyUpdate();

  void SetReadyUpdateForTesting(std::string display_version,
                                base::OnceClosure install_callback);
  void ResetForTesting();
```

- [ ] **Step 4: Implement the state model in the service impl**

In `src/dao/browser/updater/dao_updater_service.cc`, add:

```cpp
#include <utility>

#include "base/observer_list.h"
```

Inside `DaoUpdaterService::Impl`, add these members:

```cpp
  DaoUpdateStatus update_status_;
  base::OnceClosure ready_install_callback_;
  base::ObserverList<DaoUpdaterServiceObserver> observers_;
```

Add these methods to `DaoUpdaterService::Impl`:

```cpp
  void AddObserver(DaoUpdaterServiceObserver* observer) {
    observers_.AddObserver(observer);
  }

  void RemoveObserver(DaoUpdaterServiceObserver* observer) {
    observers_.RemoveObserver(observer);
  }

  DaoUpdateStatus GetUpdateStatus() const {
#if BUILDFLAG(IS_MAC)
    return update_status_;
#else
    DaoUpdateStatus status;
    status.state = DaoUpdateState::kUnsupported;
    return status;
#endif
  }

  void SetReadyUpdate(std::string display_version,
                      base::OnceClosure install_callback) {
    ready_install_callback_ = std::move(install_callback);
    update_status_.state = DaoUpdateState::kReady;
    update_status_.display_version = std::move(display_version);
    NotifyUpdateStatusChanged();
  }

  void ClearReadyUpdate() {
    ready_install_callback_.Reset();
    update_status_ = DaoUpdateStatus();
    NotifyUpdateStatusChanged();
  }

  bool ApplyReadyUpdate() {
    if (update_status_.state != DaoUpdateState::kReady ||
        ready_install_callback_.is_null()) {
      ClearReadyUpdate();
      return false;
    }

    update_status_.state = DaoUpdateState::kApplying;
    NotifyUpdateStatusChanged();

    base::OnceClosure callback = std::move(ready_install_callback_);
    std::move(callback).Run();

    update_status_ = DaoUpdateStatus();
    NotifyUpdateStatusChanged();
    return true;
  }

  void ResetForTesting() {
    ready_install_callback_.Reset();
    update_status_ = DaoUpdateStatus();
    NotifyUpdateStatusChanged();
  }

  void NotifyUpdateStatusChanged() {
    const DaoUpdateStatus status = GetUpdateStatus();
    for (DaoUpdaterServiceObserver& observer : observers_) {
      observer.OnDaoUpdateStatusChanged(status);
    }
  }
```

Add public forwarding methods after `IsSupported()`:

```cpp
void DaoUpdaterService::AddObserver(DaoUpdaterServiceObserver* observer) {
  impl_->AddObserver(observer);
}

void DaoUpdaterService::RemoveObserver(DaoUpdaterServiceObserver* observer) {
  impl_->RemoveObserver(observer);
}

DaoUpdateStatus DaoUpdaterService::GetUpdateStatus() const {
  return impl_->GetUpdateStatus();
}

bool DaoUpdaterService::ApplyReadyUpdate() {
  return impl_->ApplyReadyUpdate();
}

void DaoUpdaterService::SetReadyUpdateForTesting(
    std::string display_version,
    base::OnceClosure install_callback) {
  impl_->SetReadyUpdate(std::move(display_version),
                        std::move(install_callback));
}

void DaoUpdaterService::ResetForTesting() {
  impl_->ResetForTesting();
}
```

- [ ] **Step 5: Run focused tests again**

```bash
engine/src/out/dao-debug/browser_tests --gtest_filter="DaoSidebarBrowserTest.UpdateStateReadyIsExposedToSidebar:DaoSidebarBrowserTest.ApplyingReadyUpdateConsumesInstallCallbackOnce"
```

Expected: both Task 1 updater-service tests pass if the binary is already built. If the binary is stale or missing, defer running until the browser test build step in Task 6.

- [ ] **Step 6: Checkpoint**

Do not run `git add` or `git commit`. Suggested commit title after explicit authorization: `feat(update): add updater ready state`.

---

### Task 2: Bridge Updater State Into Sidebar Native Handler

**Files:**
- Modify: `src/dao/browser/ui/webui/dao_sidebar_ui.h`
- Modify: `src/dao/browser/ui/webui/dao_sidebar_ui.cc`
- Modify: `src/dao/browser/strings/dao_strings.grd`
- Test: `src/dao/browser/ui/views/dao_browser_browsertest.cc`

- [ ] **Step 1: Add localized strings**

First add this browser test near the Task 1 update tests in `src/dao/browser/ui/views/dao_browser_browsertest.cc`:

```cpp
IN_PROC_BROWSER_TEST_F(DaoSidebarBrowserTest,
                       SidebarHandlerSerializesReadyUpdateState) {
  dao::DaoUpdaterService* service = dao::DaoUpdaterService::GetInstance();
  service->ResetForTesting();
  service->SetReadyUpdateForTesting("1.2.3", base::DoNothing());

  dao::DaoSidebarUIHandler handler;
  AttachSidebarHandlerForTesting(browser(), &handler);

  base::DictValue state = handler.GetUpdateStateForTesting();
  EXPECT_EQ("ready", GetStringField(state, "state"));
  EXPECT_EQ("1.2.3", GetStringField(state, "displayVersion"));
  EXPECT_EQ("Update", GetStringField(state, "label"));
  EXPECT_EQ("Applying", GetStringField(state, "applyingLabel"));

  service->ResetForTesting();
}
```

Run only if the `browser_tests` binary is already available:

```bash
engine/src/out/dao-debug/browser_tests --gtest_filter="DaoSidebarBrowserTest.SidebarHandlerSerializesReadyUpdateState"
```

Expected: fails to compile or fails at runtime because `GetUpdateStateForTesting` and the sidebar update bridge are not implemented yet.

Add these messages near the other sidebar messages in `src/dao/browser/strings/dao_strings.grd`:

```xml
      <message name="IDS_DAO_UPDATE_READY_ACTION" desc="Label and tooltip for the sidebar button that applies a downloaded Dao Browser update.">
        Update
      </message>
      <message name="IDS_DAO_UPDATE_APPLYING_ACTION" desc="Transient label for the sidebar update button after the user clicks it and Dao starts applying the update.">
        Applying
      </message>
```

Do not run `i18n.sh`.

- [ ] **Step 2: Update the sidebar handler declaration**

In `src/dao/browser/ui/webui/dao_sidebar_ui.h`, include the updater header:

```cpp
#include "dao/browser/updater/dao_updater_service.h"
```

Change the class inheritance:

```cpp
class DaoSidebarUIHandler : public content::WebUIMessageHandler,
                            public TabStripModelObserver,
                            public download::AllDownloadItemNotifier::Observer,
                            public ui::SimpleMenuModel::Delegate,
                            public media_session::mojom::MediaSessionObserver,
                            public DaoUpdaterServiceObserver {
```

Add the observer method:

```cpp
  void OnDaoUpdateStatusChanged(const DaoUpdateStatus& status) override;
```

Add testing and private helpers:

```cpp
  base::DictValue GetUpdateStateForTesting();

  void PushUpdateState();
  base::DictValue BuildUpdateState();
  static std::string UpdateStateToString(DaoUpdateState state);
  void HandleRequestUpdateState(const base::ListValue& args);
  void HandleApplyReadyUpdate(const base::ListValue& args);
```

- [ ] **Step 3: Implement sidebar update state serialization**

In `src/dao/browser/ui/webui/dao_sidebar_ui.cc`, update the constructor and destructor:

```cpp
DaoSidebarUIHandler::DaoSidebarUIHandler() {
  DaoUpdaterService::GetInstance()->AddObserver(this);
}

DaoSidebarUIHandler::~DaoSidebarUIHandler() {
  DaoUpdaterService::GetInstance()->RemoveObserver(this);
  download_notifier_.reset();
  if (browser_) {
    browser_->tab_strip_model()->RemoveObserver(this);
  }
}
```

Add `PushUpdateState()` to `OnJavascriptAllowed()`:

```cpp
void DaoSidebarUIHandler::OnJavascriptAllowed() {
  if (browser_) {
    PushFullState();
    PushUpdateState();
  }
}
```

Register the two WebUI messages in `RegisterMessages()`:

```cpp
  web_ui()->RegisterMessageCallback(
      "requestUpdateState",
      base::BindRepeating(&DaoSidebarUIHandler::HandleRequestUpdateState,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "applyReadyUpdate",
      base::BindRepeating(&DaoSidebarUIHandler::HandleApplyReadyUpdate,
                          base::Unretained(this)));
```

Add the implementation near the download handlers:

```cpp
base::DictValue DaoSidebarUIHandler::GetUpdateStateForTesting() {
  return BuildUpdateState();
}

std::string DaoSidebarUIHandler::UpdateStateToString(DaoUpdateState state) {
  switch (state) {
    case DaoUpdateState::kReady:
      return "ready";
    case DaoUpdateState::kApplying:
      return "applying";
    case DaoUpdateState::kUnsupported:
      return "unsupported";
    case DaoUpdateState::kIdle:
      return "idle";
  }
}

base::DictValue DaoSidebarUIHandler::BuildUpdateState() {
  const DaoUpdateStatus status =
      DaoUpdaterService::GetInstance()->GetUpdateStatus();
  base::DictValue state;
  state.Set("state", UpdateStateToString(status.state));
  state.Set("displayVersion", status.display_version);
  state.Set("label",
            base::UTF16ToUTF8(
                l10n_util::GetStringUTF16(IDS_DAO_UPDATE_READY_ACTION)));
  state.Set("applyingLabel",
            base::UTF16ToUTF8(
                l10n_util::GetStringUTF16(IDS_DAO_UPDATE_APPLYING_ACTION)));
  return state;
}

void DaoSidebarUIHandler::PushUpdateState() {
  if (!IsJavascriptAllowed()) {
    return;
  }
  FireWebUIListener("updateStateChanged", BuildUpdateState());
}

void DaoSidebarUIHandler::OnDaoUpdateStatusChanged(
    const DaoUpdateStatus& status) {
  PushUpdateState();
}

void DaoSidebarUIHandler::HandleRequestUpdateState(
    const base::ListValue& args) {
  AllowJavascript();
  PushUpdateState();
}

void DaoSidebarUIHandler::HandleApplyReadyUpdate(
    const base::ListValue& args) {
  DaoUpdaterService::GetInstance()->ApplyReadyUpdate();
  PushUpdateState();
}
```

- [ ] **Step 4: Run focused browser tests**

```bash
engine/src/out/dao-debug/browser_tests --gtest_filter="DaoSidebarBrowserTest.SidebarHandlerSerializesReadyUpdateState:DaoSidebarBrowserTest.UpdateStateReadyIsExposedToSidebar:DaoSidebarBrowserTest.ApplyingReadyUpdateConsumesInstallCallbackOnce"
```

Expected: both tests pass if the binary is already built. If the binary is stale or missing, defer running until the browser test build step in Task 6.

- [ ] **Step 5: Checkpoint**

Do not run git state-changing commands. Suggested commit title after explicit authorization: `feat(sidebar): bridge update ready state`.

---

### Task 3: Wire Sparkle Ready Install Callback

**Files:**
- Modify: `src/dao/browser/updater/dao_sparkle_updater_mac.h`
- Modify: `src/dao/browser/updater/dao_sparkle_updater_mac.mm`
- Modify: `src/dao/browser/updater/dao_updater_service.cc`

- [ ] **Step 1: Update the Sparkle wrapper API**

In `src/dao/browser/updater/dao_sparkle_updater_mac.h`, add:

```cpp
#include <string>

#include "base/functional/callback.h"
```

Inside `class DaoSparkleUpdaterMac`, add:

```cpp
  using ReadyToInstallCallback =
      base::RepeatingCallback<void(std::string, base::OnceClosure)>;
  using UpdateSessionFinishedCallback = base::RepeatingClosure;
```

Replace `void Start();` with:

```cpp
  void Start(ReadyToInstallCallback ready_to_install_callback,
             UpdateSessionFinishedCallback update_session_finished_callback);
```

Add one more retained Objective-C slot:

```cpp
  RAW_PTR_EXCLUSION void* delegate_ = nullptr;
```

- [ ] **Step 2: Create a Sparkle delegate in Objective-C++**

In `src/dao/browser/updater/dao_sparkle_updater_mac.mm`, add imports/includes:

```objc
#include <utility>

#include "base/strings/sys_string_conversions.h"
```

Add this delegate above `namespace dao`:

```objc
@interface DaoSparkleUpdaterDelegate : NSObject <SPUUpdaterDelegate> {
 @private
  dao::DaoSparkleUpdaterMac::ReadyToInstallCallback ready_to_install_callback_;
  dao::DaoSparkleUpdaterMac::UpdateSessionFinishedCallback
      update_session_finished_callback_;
}

- (instancetype)initWithReadyToInstallCallback:
                    (dao::DaoSparkleUpdaterMac::ReadyToInstallCallback)
                        readyToInstallCallback
          updateSessionFinishedCallback:
                    (dao::DaoSparkleUpdaterMac::UpdateSessionFinishedCallback)
                        updateSessionFinishedCallback;
@end

@implementation DaoSparkleUpdaterDelegate

- (instancetype)initWithReadyToInstallCallback:
                    (dao::DaoSparkleUpdaterMac::ReadyToInstallCallback)
                        readyToInstallCallback
          updateSessionFinishedCallback:
                    (dao::DaoSparkleUpdaterMac::UpdateSessionFinishedCallback)
                        updateSessionFinishedCallback {
  self = [super init];
  if (self) {
    ready_to_install_callback_ = std::move(readyToInstallCallback);
    update_session_finished_callback_ = std::move(updateSessionFinishedCallback);
  }
  return self;
}

- (BOOL)updater:(SPUUpdater*)updater
    willInstallUpdateOnQuit:(SUAppcastItem*)item
    immediateInstallationBlock:(void (^)(void))immediateInstallHandler {
  void (^copied_handler)(void) = [immediateInstallHandler copy];
  base::OnceClosure install_closure =
      base::BindOnce([](void (^handler)(void)) { handler(); },
                     copied_handler);

  std::string display_version;
  if (item.displayVersionString) {
    display_version = base::SysNSStringToUTF8(item.displayVersionString);
  }

  if (!ready_to_install_callback_.is_null()) {
    ready_to_install_callback_.Run(display_version,
                                   std::move(install_closure));
  }

  // Returning YES lets Dao expose the immediate install action. Sparkle still
  // attempts to install the downloaded update when the app terminates.
  return YES;
}

- (void)updater:(SPUUpdater*)updater
    didFinishUpdateCycleForUpdateCheck:(SPUUpdateCheck)updateCheck
                                 error:(NSError*)error {
  if (!update_session_finished_callback_.is_null()) {
    update_session_finished_callback_.Run();
  }
}

- (void)updater:(SPUUpdater*)updater didAbortWithError:(NSError*)error {
  if (!update_session_finished_callback_.is_null()) {
    update_session_finished_callback_.Run();
  }
}

@end
```

- [ ] **Step 3: Retain and pass the delegate to Sparkle**

Update the destructor:

```objc
DaoSparkleUpdaterMac::~DaoSparkleUpdaterMac() {
  if (controller_) {
    SPUStandardUpdaterController* c =
        (__bridge_transfer SPUStandardUpdaterController*)controller_;
    (void)c;
    controller_ = nullptr;
  }
  if (delegate_) {
    DaoSparkleUpdaterDelegate* d =
        (__bridge_transfer DaoSparkleUpdaterDelegate*)delegate_;
    (void)d;
    delegate_ = nullptr;
  }
}
```

Update `Start(...)`:

```objc
void DaoSparkleUpdaterMac::Start(
    ReadyToInstallCallback ready_to_install_callback,
    UpdateSessionFinishedCallback update_session_finished_callback) {
  if (controller_) {
    return;
  }

  DaoSparkleUpdaterDelegate* delegate =
      [[DaoSparkleUpdaterDelegate alloc]
          initWithReadyToInstallCallback:std::move(ready_to_install_callback)
            updateSessionFinishedCallback:
                std::move(update_session_finished_callback)];
  delegate_ = (__bridge_retained void*)delegate;

  SPUStandardUpdaterController* controller =
      [[SPUStandardUpdaterController alloc]
          initWithStartingUpdater:YES
                  updaterDelegate:delegate
               userDriverDelegate:nil];

  if (!controller) {
    LOG(ERROR) << "DaoSparkleUpdaterMac: failed to construct "
                  "SPUStandardUpdaterController.";
    return;
  }

  controller_ = (__bridge_retained void*)controller;

  LOG(INFO) << "DaoSparkleUpdaterMac: started. Feed URL and check interval "
               "are configured via Info.plist.";
}
```

- [ ] **Step 4: Pass service callbacks into Sparkle**

In `src/dao/browser/updater/dao_updater_service.cc`, update the macOS `Init()` branch:

```cpp
#if BUILDFLAG(IS_MAC)
    sparkle_ = std::make_unique<DaoSparkleUpdaterMac>();
    sparkle_->Start(
        base::BindRepeating(&DaoUpdaterService::Impl::SetReadyUpdate,
                            base::Unretained(this)),
        base::BindRepeating(&DaoUpdaterService::Impl::ClearReadyUpdate,
                            base::Unretained(this)));
#else
```

- [ ] **Step 5: Verify Objective-C++ syntax through compile confirmation later**

Do not run direct build tools. This task is verified by `npm run rebuild` in Task 6.

- [ ] **Step 6: Checkpoint**

Do not run git state-changing commands. Suggested commit title after explicit authorization: `feat(update): connect sparkle ready install callback`.

---

### Task 4: Add Sidebar Update Button WebUI

**Files:**
- Modify: `src/dao/browser/ui/webui/resources/sidebar/sidebar_bridge.ts`
- Create: `src/dao/browser/ui/webui/resources/sidebar/dao_update_button.ts`
- Modify: `src/dao/browser/ui/webui/resources/sidebar/dao_sidebar_app.ts`
- Modify: `src/dao/browser/ui/webui/resources/sidebar/BUILD.gn`
- Test: `src/dao/browser/ui/webui/resources/sidebar/__tests__/update_button.test.ts`
- Test: `src/dao/browser/ui/webui/resources/sidebar/__tests__/sidebar_app.test.ts`

- [ ] **Step 1: Add bridge type**

In `sidebar_bridge.ts`, add after `DownloadState`:

```ts
// ---- Application Update Data Types ----

export type UpdateState = 'idle' | 'ready' | 'applying' | 'unsupported';

export interface UpdateStateData {
  state: UpdateState;
  displayVersion: string;
  label: string;
  applyingLabel: string;
}
```

- [ ] **Step 2: Create failing component tests**

Create `src/dao/browser/ui/webui/resources/sidebar/__tests__/update_button.test.ts`:

```ts
// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';

import type {UpdateStateData} from '../sidebar_bridge.js';

vi.mock('//resources/lit/v3_0/lit.rollup.js', async () => {
  return await import('./lit_test_shim.js');
});

function readyState(extra: Partial<UpdateStateData> = {}): UpdateStateData {
  return {
    state: 'ready',
    displayVersion: '1.2.3',
    label: 'Update',
    applyingLabel: 'Applying',
    ...extra,
  };
}

async function loadButton() {
  const send = vi.fn();
  (globalThis as unknown as {chrome: {send: typeof send}}).chrome = {send};
  await import('../dao_update_button.js');
  const el = document.createElement('dao-update-button') as HTMLElement & {
    updateState: UpdateStateData | null;
    updateComplete: Promise<boolean>;
  };
  document.body.appendChild(el);
  return {el, send};
}

describe('dao-update-button', () => {
  beforeEach(() => {
    document.body.innerHTML = '';
  });

  afterEach(() => {
    document.body.innerHTML = '';
    vi.restoreAllMocks();
    delete (globalThis as unknown as {chrome?: unknown}).chrome;
    delete (window as unknown as {cr?: unknown}).cr;
  });

  it('does not render when the updater is idle', async () => {
    const {el} = await loadButton();
    el.updateState = readyState({state: 'idle'});
    await el.updateComplete;

    expect(el.shadowRoot!.querySelector('button')).toBeNull();
  });

  it('renders the circle-arrow-up icon when an update is ready', async () => {
    const {el} = await loadButton();
    el.updateState = readyState();
    await el.updateComplete;

    const button = el.shadowRoot!.querySelector('button')!;
    expect(button.title).toBe('Update');
    expect(button.textContent).toContain('Update');
    expect(el.shadowRoot!.querySelector('circle[cx="12"][cy="12"][r="10"]'))
        .not.toBeNull();
  });

  it('sends applyReadyUpdate and disables itself after click', async () => {
    const {el, send} = await loadButton();
    el.updateState = readyState();
    await el.updateComplete;

    el.shadowRoot!.querySelector('button')!.click();
    await el.updateComplete;

    expect(send).toHaveBeenCalledWith('applyReadyUpdate', []);
    expect(el.shadowRoot!.querySelector('button')!.disabled).toBe(true);
  });
});
```

- [ ] **Step 3: Run the component test and verify it fails**

```bash
npm run test:webui -- src/dao/browser/ui/webui/resources/sidebar/__tests__/update_button.test.ts
```

Expected: FAIL because `dao_update_button.js` does not exist.

- [ ] **Step 4: Implement `dao-update-button`**

Create `src/dao/browser/ui/webui/resources/sidebar/dao_update_button.ts`:

```ts
// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {CrLitElement, html, css, nothing} from '//resources/lit/v3_0/lit.rollup.js';

import {sendNative} from './sidebar_bridge.js';
import type {UpdateStateData} from './sidebar_bridge.js';

export class DaoUpdateButton extends CrLitElement {
  static get is() {
    return 'dao-update-button';
  }

  static override get styles() {
    return css`
      :host {
        display: flex;
        align-items: center;
        justify-content: flex-end;
        flex-shrink: 0;
      }

      .update-btn {
        width: 26px;
        height: 26px;
        border: none;
        border-radius: 8px;
        padding: 0;
        background: rgb(70, 120, 190);
        color: white;
        cursor: default;
        display: flex;
        align-items: center;
        justify-content: center;
        gap: 6px;
        overflow: hidden;
        white-space: nowrap;
        box-shadow: 0 5px 14px rgba(70, 120, 190, 0.28);
        transition:
          width 0.16s ease,
          padding 0.16s ease,
          background 0.16s ease,
          opacity 0.16s ease;
      }

      .update-btn:hover:not(:disabled),
      .update-btn.applying {
        width: 86px;
        padding: 0 10px;
      }

      .update-btn.applying {
        width: 104px;
        background: rgb(54, 59, 64);
        box-shadow: 0 5px 14px rgba(54, 59, 64, 0.22);
      }

      .update-btn:disabled {
        opacity: 0.9;
      }

      .update-btn svg {
        flex: 0 0 auto;
      }

      .label {
        opacity: 0;
        max-width: 0;
        overflow: hidden;
        font-size: 12px;
        font-weight: 650;
      }

      .update-btn:hover:not(:disabled) .label,
      .update-btn.applying .label {
        opacity: 1;
        max-width: 72px;
      }
    `;
  }

  static override get properties() {
    return {
      updateState: {type: Object},
      applying_: {type: Boolean},
    };
  }

  declare updateState: UpdateStateData | null;
  declare protected applying_: boolean;

  constructor() {
    super();
    this.updateState = null;
    this.applying_ = false;
  }

  override updated(changed: Map<PropertyKey, unknown>) {
    if (changed.has('updateState') &&
        this.updateState?.state !== 'applying' &&
        this.updateState?.state !== 'ready') {
      this.applying_ = false;
    }
  }

  override render() {
    const state = this.updateState?.state || 'idle';
    if (state !== 'ready' && state !== 'applying') {
      return nothing;
    }

    const isApplying = this.applying_ || state === 'applying';
    const label = isApplying ?
        this.updateState?.applyingLabel || '' :
        this.updateState?.label || '';

    return html`
      <button class="update-btn ${isApplying ? 'applying' : ''}"
              title=${this.updateState?.label || ''}
              ?disabled=${isApplying}
              @click=${this.onClick_}>
        <svg width="14" height="14" viewBox="0 0 24 24" fill="none"
             stroke="currentColor" stroke-width="2"
             stroke-linecap="round" stroke-linejoin="round"
             aria-hidden="true">
          <circle cx="12" cy="12" r="10"></circle>
          <path d="m16 12-4-4-4 4"></path>
          <path d="M12 16V8"></path>
        </svg>
        <span class="label">${label}</span>
      </button>
    `;
  }

  private onClick_() {
    if (this.applying_) {
      return;
    }
    this.applying_ = true;
    sendNative('applyReadyUpdate');
  }
}

customElements.define('dao-update-button', DaoUpdateButton);
```

- [ ] **Step 5: Add the file to WebUI build**

In `src/dao/browser/ui/webui/resources/sidebar/BUILD.gn`, add:

```gn
    "dao_update_button.ts",
```

Place it next to `dao_download_button.ts`.

- [ ] **Step 6: Run the component test**

```bash
npm run test:webui -- src/dao/browser/ui/webui/resources/sidebar/__tests__/update_button.test.ts
```

Expected: PASS.

- [ ] **Step 7: Checkpoint**

Do not run git state-changing commands. Suggested commit title after explicit authorization: `feat(sidebar): add update button component`.

---

### Task 5: Render the Update Button in the Sidebar App

**Files:**
- Modify: `src/dao/browser/ui/webui/resources/sidebar/dao_sidebar_app.ts`
- Modify: `src/dao/browser/ui/webui/resources/sidebar/__tests__/sidebar_app.test.ts`

- [ ] **Step 1: Write a failing placement test**

In `sidebar_app.test.ts`, update imports:

```ts
import type {PinnedItemData, UpdateStateData} from '../sidebar_bridge.js';
```

Update the element type returned by `loadApp()`:

```ts
  const el = document.createElement('dao-sidebar-app') as HTMLElement & {
    pinnedItems_: PinnedItemData[];
    updateState_: UpdateStateData | null;
    updateComplete: Promise<boolean>;
  };
```

Add this test:

```ts
  it('renders the update button before the plus button when ready', async () => {
    const {el} = await loadApp();
    el.updateState_ = {
      state: 'ready',
      displayVersion: '1.2.3',
      label: 'Update',
      applyingLabel: 'Applying',
    };
    await el.updateComplete;

    const toolbar = el.shadowRoot!.querySelector('.toolbar-end-actions')!;
    const children = Array.from(toolbar.children);

    expect(children[0]!.tagName.toLowerCase()).toBe('dao-update-button');
    expect(children[1]!.classList.contains('plus-menu-container')).toBe(true);
  });
```

- [ ] **Step 2: Run the sidebar app test and verify it fails**

```bash
npm run test:webui -- src/dao/browser/ui/webui/resources/sidebar/__tests__/sidebar_app.test.ts
```

Expected: FAIL because `updateState_` and `.toolbar-end-actions` are not implemented.

- [ ] **Step 3: Import and register the update button**

In `dao_sidebar_app.ts`, add:

```ts
import './dao_update_button.js';
```

Update the bridge import:

```ts
import type {
  SidebarState, TabData, PinnedItemData, FolderAction, UpdateStateData
} from './sidebar_bridge.js';
```

Add CSS:

```css
      .toolbar-end-actions {
        display: flex;
        align-items: center;
        justify-content: flex-end;
        gap: 6px;
      }
```

Add the property:

```ts
      updateState_: {type: Object},
```

Add the field:

```ts
  declare protected updateState_: UpdateStateData | null;
```

Initialize it:

```ts
    this.updateState_ = null;
```

Listen for native state:

```ts
    addListener('updateStateChanged', (...args: unknown[]) => {
      this.updateState_ = args[0] as UpdateStateData;
    });
```

Request current state after initial app messages:

```ts
    sendNative('requestUpdateState');
```

- [ ] **Step 4: Render the right action group**

Replace the existing slotted plus container inside `<dao-download-button>` with:

```ts
          <div class="toolbar-end-actions" slot="toolbar-end">
            <dao-update-button .updateState=${this.updateState_}>
            </dao-update-button>
            <div class="plus-menu-container">
              ${this.showPlusMenu_ ? html`
                <div class="plus-menu">
                  <button class="plus-menu-item" @click=${this.onNewTab_}>
                    <svg width="14" height="14" viewBox="0 0 24 24" fill="none"
                         stroke="currentColor" stroke-width="2"
                         stroke-linecap="round" stroke-linejoin="round">
                      <line x1="12" y1="5" x2="12" y2="19"></line>
                      <line x1="5" y1="12" x2="19" y2="12"></line>
                    </svg>
                    New Tab
                  </button>
                  <button class="plus-menu-item" @click=${this.onNewFolder_}>
                    <svg width="14" height="14" viewBox="0 0 24 24" fill="none"
                         stroke="currentColor" stroke-width="2"
                         stroke-linecap="round" stroke-linejoin="round">
                      <path d="M22 19a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h5l2 3h9a2 2 0 0 1 2 2z"></path>
                    </svg>
                    New Folder
                  </button>
                </div>
              ` : nothing}
              <button class="plus-btn" @click=${this.onPlusClick_}>
                <svg width="14" height="14" viewBox="0 0 24 24" fill="none"
                     stroke="currentColor" stroke-width="2"
                     stroke-linecap="round" stroke-linejoin="round">
                  <line x1="12" y1="5" x2="12" y2="19"></line>
                  <line x1="5" y1="12" x2="19" y2="12"></line>
                </svg>
              </button>
            </div>
          </div>
```

- [ ] **Step 5: Run sidebar WebUI tests**

```bash
npm run test:webui -- src/dao/browser/ui/webui/resources/sidebar/__tests__/sidebar_app.test.ts src/dao/browser/ui/webui/resources/sidebar/__tests__/update_button.test.ts
```

Expected: PASS.

- [ ] **Step 6: Checkpoint**

Do not run git state-changing commands. Suggested commit title after explicit authorization: `feat(sidebar): render ready update action`.

---

### Task 6: Verify Build, Tests, and Integration

**Files:**
- No new source files unless previous tasks exposed compile issues.

- [ ] **Step 1: Run WebUI tests**

```bash
npm run test:webui -- src/dao/browser/ui/webui/resources/sidebar/__tests__/update_button.test.ts src/dao/browser/ui/webui/resources/sidebar/__tests__/sidebar_app.test.ts src/dao/browser/ui/webui/resources/sidebar/__tests__/sidebar_bridge.test.ts
```

Expected: PASS.

- [ ] **Step 2: Run Lit reactive field lint**

```bash
npm run lint:lit
```

Expected: PASS. If it flags a reactive field, update the component to use `declare` fields plus constructor assignment, matching existing sidebar patterns.

- [ ] **Step 3: Import canonical changes into Chromium checkout**

```bash
npm run import
```

Expected: patches apply and Dao files copy into `engine/src/`.

- [ ] **Step 4: Compile-confirm with the only allowed compile path**

```bash
npm run rebuild
```

Expected: PASS. This is the compile confirmation.

- [ ] **Step 5: Build or run focused browser tests if available**

If `browser_tests` exists after the local build state, run:

```bash
engine/src/out/dao-debug/browser_tests --gtest_filter="DaoSidebarBrowserTest.UpdateStateReadyIsExposedToSidebar:DaoSidebarBrowserTest.ApplyingReadyUpdateConsumesInstallCallbackOnce"
```

Expected: PASS.

If the binary is missing, do not run direct Chromium build tools. Use `npm run test` only when broad Dao browser test coverage is intentionally acceptable for the session.

- [ ] **Step 6: Manual ready-state smoke test**

Use the `SetReadyUpdateForTesting()` path from a temporary test hook or browser test only. Do not ship a permanent user-facing fake update trigger. Verify visually:

- no update button when state is `idle`;
- button appears at the right, immediately left of `+`, when state is `ready`;
- hover expands leftward and the `+` stays fixed at the far right;
- click disables the button and sends `applyReadyUpdate`;
- regular Downloads hover and folder opening still work.

- [ ] **Step 7: Final diff review**

Run read-only git commands only:

```bash
git diff -- src/dao/browser/updater src/dao/browser/ui/webui src/dao/browser/ui/webui/resources/sidebar src/dao/browser/strings/dao_strings.grd src/dao/browser/ui/views/dao_browser_browsertest.cc docs/superpowers
```

Expected: diff only contains the planned updater state, Sparkle delegate, sidebar bridge, WebUI button, strings, tests, and docs.

- [ ] **Step 8: Checkpoint**

Do not run git state-changing commands. Suggested commit title after explicit authorization: `feat(update): show ready update action in sidebar`.

---

## Self-Review

Spec coverage:

- The button appears only for downloaded ready updates: Tasks 1, 2, and 3.
- It is independent from Downloads and right-aligned beside the plus button: Tasks 4 and 5.
- It uses Lucide `circle-arrow-up`: Task 4.
- It expands on hover and applies the update in one click: Tasks 3 and 4.
- Multiple windows synchronize through process-level service observers: Tasks 1 and 2.
- Error cases for duplicate apply and unsupported state: Tasks 1 and 2.
- Tests and verification paths are included: Tasks 1, 4, 5, and 6.

Incomplete detail scan:

- No incomplete sections are intentionally left in this plan.
- No direct Chromium build tools are used.
- No state-changing git commands are requested.

Type consistency:

- Native state enum: `DaoUpdateState`.
- Native payload struct: `DaoUpdateStatus`.
- WebUI payload type: `UpdateStateData`.
- WebUI message names: `requestUpdateState`, `applyReadyUpdate`, `updateStateChanged`.
- Component name: `dao-update-button`.
