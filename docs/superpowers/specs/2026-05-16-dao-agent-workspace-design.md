# Dao Agent Workspace — Design Spec

**Date:** 2026-05-16
**Status:** Draft for review
**Owner:** Dao Agent team

## 1. Problem

The Dao Agent (built-in AI assistant) currently has no place to persist its own
notes, intermediate research, plans, or anything else it wants to remember
across turns or sessions. Skills and memory services exist for structured
state, but there is no general-purpose, file-shaped scratchpad the agent can
read and write freely.

This spec adds a private, per-profile **workspace** — a sandboxed folder
under the Chromium profile directory — plus four tools the agent can call to
operate on it: `read`, `write`, `edit`, `apply_patch`. Reference
implementation for the tool surface is
[openclaw](https://github.com/openclaw/openclaw).

## 2. Goals & Non-goals

**Goals**
- Per-profile, sandboxed folder the agent can read from and write to
- Four tools modeled on openclaw: `read`, `write`, `edit`, `apply_patch`
- User can open the folder in the OS file manager from settings
- Strong safety boundaries: no path traversal, text-only, quota-enforced
- All writes produce a visible audit trail

**Non-goals**
- No embedded file browser UI in the agent panel
- No `list_files` tool — discovery is convention-driven via an auto-maintained
  `WORKSPACE.md` index (see §10 for rationale)
- No binary file support
- No cross-profile sharing or sync

## 3. Architecture

```
┌──────────────────────────── WebUI (TypeScript) ────────────────────────────┐
│  agent_bridge.ts        tool defs + executeTool dispatch                   │
│  tool_catalog.ts        'workspace' group                                  │
│  dao_settings_view.ts   "Open workspace folder" button + activity list     │
└────────────────────────────────────────────────────────────────────────────┘
                                   │ chrome.send
                                   ▼
┌──────────────────── DaoAgentUIHandler (C++, UI thread) ────────────────────┐
│  HandleWorkspaceRead / Write / Edit / ApplyPatch / OpenFolder              │
└────────────────────────────────────────────────────────────────────────────┘
                                   │
                                   ▼
┌────────────────── DaoAgentWorkspaceService (KeyedService) ─────────────────┐
│  workspace_root_ = <Profile>/DaoAgentWorkspace/                            │
│  io_runner_ (SequencedTaskRunner, MayBlock + USER_VISIBLE)                 │
│  recent_audit_ (in-memory ring buffer, 200 entries)                        │
│                                                                            │
│  Public API (all async, callback-based, return                             │
│  base::expected<T, WorkspaceError>):                                       │
│    Read / Write / Edit / ApplyPatch / OpenInFileManager                    │
│                                                                            │
│  Helpers (all on io_runner_):                                              │
│    NormalizePath / EnforceQuota / RewriteIndex / AppendAudit               │
└────────────────────────────────────────────────────────────────────────────┘
                                   │
                                   ▼
                       <Profile>/DaoAgentWorkspace/
                        ├─ WORKSPACE.md       (auto-maintained index)
                        ├─ .audit.log         (append-only audit trail)
                        ├─ .workspace_tmp/    (atomic-rename staging)
                        └─ <user files...>
```

**Key decisions**
- **Single security boundary**: `NormalizePath()` is the only entry point for
  path validation; all four tools route through it. TypeScript does not
  validate paths.
- **Service lifetime**: `KeyedService` is automatically destroyed with the
  Profile.
- **Threading**: handler runs on UI thread; service posts all IO to a single
  `SequencedTaskRunner` (serializing all reads and writes for consistency).
- **Index is service-owned**: `WORKSPACE.md` is rewritten by C++ after every
  mutation. TypeScript never writes the index.
- **Audit is dual-channel**: `.audit.log` for persistence (user can inspect)
  and an in-memory ring buffer for settings-page display.

## 4. Components

### 4.1 `DaoAgentWorkspaceService`

```cpp
namespace dao {

enum class WorkspaceError {
  kOk = 0,
  kInvalidPath,
  kNotFound,
  kAlreadyExists,
  kQuotaExceeded,
  kBinaryRejected,
  kPatchParseError,
  kPatchContextMismatch,
  kEditNotUnique,
  kIoError,
};

struct ReadResult {
  std::string content;
  int total_lines;
  int returned_lines;
  bool truncated;
};

struct WriteResult {
  size_t bytes_written;
  bool created;
};

struct PatchResult {
  std::vector<std::string> added;
  std::vector<std::string> updated;
  std::vector<std::string> deleted;
  std::vector<std::pair<std::string, std::string>> moved;
};

class DaoAgentWorkspaceService : public KeyedService {
 public:
  using ReadCallback =
      base::OnceCallback<void(base::expected<ReadResult, WorkspaceError>)>;
  using WriteCallback =
      base::OnceCallback<void(base::expected<WriteResult, WorkspaceError>)>;
  using PatchCallback =
      base::OnceCallback<void(base::expected<PatchResult, WorkspaceError>)>;

  explicit DaoAgentWorkspaceService(const base::FilePath& profile_path);
  ~DaoAgentWorkspaceService() override;

  void Read(const std::string& rel_path, int offset_lines, int limit_lines,
            ReadCallback callback);
  void Write(const std::string& rel_path, const std::string& content,
             WriteCallback callback);
  void Edit(const std::string& rel_path,
            const std::string& old_str, const std::string& new_str,
            WriteCallback callback);
  void ApplyPatch(const std::string& patch_text, PatchCallback callback);
  void OpenInFileManager();  // sync, UI thread

  const base::FilePath& workspace_root() const { return workspace_root_; }

 private:
  base::FilePath workspace_root_;
  scoped_refptr<base::SequencedTaskRunner> io_runner_;
  base::circular_deque<AuditEntry> recent_audit_;
};
}  // namespace dao
```

Factory mirrors `dao_agent_skill_service_factory.cc` exactly:
constructs the service with `profile->GetPath()`, registered in
`EnsureBrowserContextKeyedServiceFactoriesBuilt`.

### 4.2 Path normalization

`NormalizePath(rel_path) -> base::expected<base::FilePath, WorkspaceError>`:

1. Reject absolute paths (POSIX `/...` and defensively Windows `C:\...`)
2. Reject any path component equal to `..` (syntactic check first)
3. `workspace_root_.Append(rel_path)`, then `base::MakeAbsoluteFilePath()` to
   resolve symlinks
4. Verify the resolved path is still under `workspace_root_`
   (`workspace_root_.IsParent(resolved)` plus equality check)
5. Reject path components starting with `.` **except** the allowlisted
   `.audit.log` (prevents writes to things like `.git/hooks/`)

`NormalizePath` is only invoked at tool entry points. Internal service IO
(creating `.workspace_tmp/<request_id>/`, writing `WORKSPACE.md` and
`.audit.log`) builds paths directly from `workspace_root_` and bypasses
normalization — these paths are trusted because the service constructs them.

### 4.3 Quotas

| Limit | Value | Enforced in |
|---|---|---|
| Per-file bytes | 5 MB | Write / Edit / ApplyPatch pre-write |
| Workspace total bytes | 100 MB | Same |
| Total entry count | 500 | Same |
| Read default `limit` | 500 lines | Read |
| Read max `limit` | 5000 lines | Read (clamped) |

Total-size and entry-count computation runs on the IO sequence and the result
is cached; the cache invalidates on every mutation, so steady-state reads
don't pay for re-scans.

### 4.4 Text-only allowlist

```cpp
constexpr auto kAllowedExtensions = std::to_array<std::string_view>({
    ".txt", ".md", ".json", ".csv", ".yaml", ".yml",
    ".html", ".xml", ".log", ".tsv",
});
```

Plus a NUL-byte probe on the first 8 KB at write time as a defense in depth.

### 4.5 V4A patch grammar (openclaw-compatible subset)

```
*** Begin Patch
*** Add File: path/to/new.md
+content line 1
+content line 2
*** Update File: path/to/existing.md
*** Move to: path/to/renamed.md      ← optional, immediately after Update File
@@ optional context anchor
 unchanged line
-removed line
+added line
*** End of File                       ← terminates Update File section
*** Delete File: path/to/gone.md
*** End Patch
```

- No `@@` line numbers; context lines (unchanged + removed) must match
  uniquely in the file.
- Multiple `@@` hunks per Update File allowed.
- Any hunk failure → entire patch rolls back. Writes go to
  `<workspace_root>/.workspace_tmp/<request_id>/` first; only after every hunk
  succeeds does the service do atomic renames into place.

### 4.6 `WORKSPACE.md` (auto-generated)

```markdown
<!-- Auto-generated by Dao Agent. DO NOT EDIT — your changes will be overwritten. -->
# Workspace Index

Last updated: 2026-05-16 14:32:01

## Files
- `notes.md` (1.2 KB, 42 lines)
- `research/competitors.md` (5.8 KB, 180 lines)
- `data/raw.csv` (12.0 KB)

## Stats
- 3 files, 19.0 KB total
- Quota: 19.0 KB / 100 MB used
```

Rewritten after every successful mutation. User edits are silently
overwritten on next mutation (header warning declares this).

### 4.7 `.audit.log`

Append-only, one JSON object per line:

```json
{"ts":"2026-05-16T14:32:01Z","op":"write","path":"notes.md","bytes":1234,"created":true}
{"ts":"2026-05-16T14:33:15Z","op":"edit","path":"notes.md","old_len":50,"new_len":60}
{"ts":"2026-05-16T14:35:02Z","op":"apply_patch","added":["a.md"],"updated":["b.md"],"deleted":[]}
```

Settings page displays last 200 from the in-memory ring buffer.

## 5. Data flow (apply_patch example)

```
LLM → agent_bridge.ts → callNative('workspaceApplyPatch', {patch})
    → DaoAgentUIHandler::HandleWorkspaceApplyPatch
    → service->ApplyPatch(..., callback)
    → io_runner: ApplyPatchOnIO
        1. Parse V4A
        2. NormalizePath × N (one per target)
        3. EnforceQuota
        4. Read originals
        5. Apply hunks (in-memory)
        6. Write all results to .workspace_tmp/<request_id>/
        7. Atomic rename .tmp → final paths
        8. AppendAudit
        9. RewriteIndex
    → reply to UI thread → chrome.send → tool_result to LLM
```

Failure at any step (1–7) → no audit entry, no index rewrite, original files
untouched. Step 7 partial failure attempts best-effort reverse-rename.

`.workspace_tmp/` residue from a crashed previous run is cleared on service
construction.

## 6. Concurrency

Single `SequencedTaskRunner` for all IO. Mutations are serialized; reads also
run on the same runner to avoid seeing half-written state. Workspace is not a
high-throughput path — consistency wins over parallelism. TypeScript does not
need locks; concurrent tool calls queue automatically in C++.

## 7. Error handling & TS contract

C++ returns `WorkspaceError`; TS handler returns:

```typescript
type WorkspaceReply<T> =
  | ({ok: true} & T)
  | {ok: false; code: ErrorCode; message: string; hint?: string};

type ErrorCode =
  'invalid_path' | 'not_found' | 'already_exists' | 'quota_exceeded' |
  'binary_rejected' | 'patch_parse_error' | 'patch_context_mismatch' |
  'edit_not_unique' | 'io_error';
```

LLM-facing error format:

| code | message | hint |
|---|---|---|
| `invalid_path` | `Path "X" is outside the workspace.` | `Use a relative path under the workspace root, no ".." segments.` |
| `not_found` | `File "X" does not exist.` | `Use 'write' to create it, or check WORKSPACE.md.` |
| `already_exists` | `Cannot add "X": file already exists.` | `Use '*** Update File:' instead of '*** Add File:'.` |
| `quota_exceeded` | `Write would push workspace to X MB (limit 100 MB).` | `Delete unused files via apply_patch.` |
| `binary_rejected` | `Extension ".X" is not allowed. Workspace is text-only.` | `Allowed: .txt .md .json .csv .yaml .yml .html .xml .log .tsv` |
| `patch_parse_error` | `Patch syntax error at line N: ...` | `Each Update File section must end with "*** End of File".` |
| `patch_context_mismatch` | `Context for hunk in "X" not found, or matches multiple locations.` | `Include 2–3 unchanged lines around your change for unique anchoring.` |
| `edit_not_unique` | `old_str appears N times in "X".` | `Add surrounding context so it matches exactly once.` |
| `io_error` | `Failed to write "X": <reason>.` | (none) |

`formatWorkspaceReply` returns `Error (<code>): <message>\nHint: <hint>` to
the LLM on failure.

## 8. Tool schemas (agent_bridge.ts)

```typescript
// workspace_read
{
  name: 'workspace_read',
  description:
    'Read a file from the Agent workspace (a private, persistent folder ' +
    'scoped to this profile). Supports pagination for large files. ' +
    'Check WORKSPACE.md first to discover available files.',
  parameters: {
    type: 'object',
    properties: {
      path:   {type: 'string'},
      offset: {type: 'integer', minimum: 0, default: 0},
      limit:  {type: 'integer', minimum: 1, maximum: 5000, default: 500},
    },
    required: ['path'],
  },
}

// workspace_write
{
  name: 'workspace_write',
  description:
    'Create or fully overwrite a file in the Agent workspace. ' +
    'For partial edits prefer workspace_edit or workspace_apply_patch. ' +
    'Text files only; max 5MB per file, 100MB total.',
  parameters: {
    type: 'object',
    properties: { path: {type:'string'}, content: {type:'string'} },
    required: ['path', 'content'],
  },
}

// workspace_edit
{
  name: 'workspace_edit',
  description:
    'Replace exactly one occurrence of old_str with new_str in a workspace ' +
    'file. old_str must match uniquely — include context if needed. ' +
    'For multi-file or multi-hunk changes use workspace_apply_patch.',
  parameters: {
    type: 'object',
    properties: {
      path:    {type:'string'},
      old_str: {type:'string'},
      new_str: {type:'string'},
    },
    required: ['path', 'old_str', 'new_str'],
  },
}

// workspace_apply_patch
{
  name: 'workspace_apply_patch',
  description:
    'Apply a multi-file V4A-format patch. Supports Add File / Update File ' +
    '/ Delete File / Move to. All hunks succeed or the patch rolls back.',
  parameters: {
    type: 'object',
    properties: { patch: {type:'string'} },
    required: ['patch'],
  },
}
```

`executeTool` dispatches each via `callNative('workspaceRead'|...)` and runs
the reply through `formatWorkspaceReply`.

## 9. System prompt addition

Append to `BASE_SYSTEM_PROMPT`:

> You have access to a persistent private workspace at
> `<Profile>/DaoAgentWorkspace/`. Use it to store notes, intermediate
> research, plans, and any state you want to remember across turns or
> sessions. Discover existing files by reading `WORKSPACE.md`
> (auto-maintained index). Available tools: `workspace_read`,
> `workspace_write`, `workspace_edit`, `workspace_apply_patch`. Workspace is
> text-only (max 5 MB per file, 100 MB total).

## 10. Why no `list_files` tool

Researched from openclaw's `agent-workspace` doc: openclaw doesn't expose a
`list_files` tool because the workspace is **convention-driven**. Bootstrap
files (`AGENTS.md`, `MEMORY.md`) auto-load into context at session start and
declare what lives where. The agent reads the convention file, then asks for
specific paths it needs.

We mirror this with `WORKSPACE.md` as the single index, auto-rewritten by C++
after every mutation. Benefits:
- No tool surface for "find files," which the LLM tends to over-call
- Token-cheap discovery: one read of `WORKSPACE.md` lists everything with
  sizes
- Strong consistency: index can't drift from reality (only C++ writes it)

## 11. Settings UI

`dao_settings_view.ts` gains a "Workspace" section with:
- Description text (i18n)
- "Open workspace folder" button → `callNative('workspaceOpenFolder')`
- "Recent activity" list (last 200 from ring buffer, oldest at bottom)

No embedded file browser, no in-app editor — opening the folder in Finder is
the explicit escape hatch.

## 12. i18n

C++ (`dao_strings.grd`):

| Key | English |
|---|---|
| `IDS_DAO_SETTINGS_WORKSPACE_TITLE` | `Workspace` |
| `IDS_DAO_SETTINGS_WORKSPACE_DESC` | `Files the agent has stored in this profile.` |
| `IDS_DAO_SETTINGS_WORKSPACE_OPEN_BUTTON` | `Open workspace folder` |
| `IDS_DAO_SETTINGS_WORKSPACE_ACTIVITY_TITLE` | `Recent activity` |
| `IDS_DAO_SETTINGS_WORKSPACE_EMPTY` | `No activity yet.` |

WebUI (`i18n/locales/en.ts`):
- `settings.workspace.title`
- `settings.workspace.description`
- `settings.workspace.openButton`
- `settings.workspace.activityTitle`
- `settings.workspace.activityEmpty`

LLM-facing error messages stay English (LLM contract; not displayed in UI).
`zh-CN` is hand-authored; other locales via `i18n.sh` when the user runs it.

## 13. Testing

**C++ unit tests** (`dao_agent_workspace_service_unittest.cc`):

- `WorkspaceRoot_CreatedLazily`
- `NormalizePath_RejectsDotDot`
- `NormalizePath_RejectsSymlinkEscape`
- `NormalizePath_RejectsHiddenExceptAudit`
- `Read_PaginatesLargeFile`
- `Read_NotFound`
- `Write_CreatesFile`
- `Write_Overwrites`
- `Write_RejectsBinaryExtension`
- `Write_RejectsNulByte`
- `Write_QuotaPerFile`
- `Write_QuotaTotal`
- `Edit_UniqueMatch`
- `Edit_NotUnique`
- `Edit_NotFound`
- `ApplyPatch_AddUpdateDelete`
- `ApplyPatch_Move`
- `ApplyPatch_RollbackOnFailure`
- `ApplyPatch_AtomicRename`
- `Index_RewrittenAfterMutation`
- `Index_DeletedFileRemoved`
- `AuditLog_AppendOnly`
- `AuditLog_RingBufferSize`

**C++ browser tests** (added to `dao_browser_browsertest.cc`):

- `DaoAgentWorkspaceBrowserTest.ServiceBoundToProfile`
- `DaoAgentWorkspaceBrowserTest.OpenInFileManager`
- `DaoAgentWorkspaceBrowserTest.WorkspaceTmpClearedOnStartup`

**TS unit tests** (`__tests__/workspace_bridge.test.ts`, vitest):

- `formatWorkspaceReply formats success`
- `formatWorkspaceReply formats error with hint`
- `executeTool dispatches workspace_read with defaults`
- `executeTool clamps invalid limit`
- `executeTool rejects missing required args`

## 14. File inventory

**New**

```
src/dao/browser/agent/
  dao_agent_workspace_service.{h,cc}
  dao_agent_workspace_service_factory.{h,cc}
  dao_agent_workspace_service_unittest.cc
  workspace/
    path_normalizer.{h,cc}
    v4a_patch_parser.{h,cc}
    workspace_index.{h,cc}
    workspace_audit.{h,cc}

src/dao/browser/ui/webui/resources/agent/
  workspace/
    types.ts
    bridge.ts
  __tests__/
    workspace_bridge.test.ts
```

**Modified**

```
src/dao/browser/agent/
  BUILD.gn
  dao_agent_ui_handler.{h,cc}

src/dao/browser/ui/webui/resources/agent/
  agent_bridge.ts
  tool_catalog.ts
  dao_settings_view.ts

src/dao/browser/strings/dao_strings.grd
src/dao/browser/ui/webui/resources/agent/i18n/locales/en.ts
src/dao/browser/ui/views/dao_browser_browsertest.cc
```

**Patches (Chromium integration)**

If a Chromium-side `EnsureBrowserContextKeyedServiceFactoriesBuilt` patch
already exists for the skill / memory services, extend that patch to also
register `DaoAgentWorkspaceServiceFactory::GetInstance()`. Otherwise add:

```
src/patches/chrome/browser/
  chrome_browser_main_extra_parts_profiles.cc.patch
```

## 15. Implementation order

Each step must compile cleanly and pass its tests before the next begins:

1. Path normalizer + service skeleton + Read/Write + quota + unit tests
2. `workspace_index` + `workspace_audit` + tests
3. `Edit` + tests
4. V4A parser as a pure function + unit tests
5. `apply_patch` integration (parser → service) + rollback tests
6. WebUI handler + `chrome.send` routes + browser tests
7. TS tool definitions + dispatcher + TS unit tests
8. Settings UI ("Open folder" button + activity list) + i18n

`npm run rebuild` and `npm run test` must pass after each step.

## 16. Open questions

None at spec time. All clarifying decisions made during brainstorming:
- Sandbox-only, profile-scoped ✅
- `edit` = unique-match string replace, `apply_patch` = multi-file V4A ✅
- All four safety measures (size + count limits, paginated read, text-only,
  audit log) ✅
- Simple "Open folder" button, no embedded browser ✅
- No `list_files` — `WORKSPACE.md` convention instead ✅
