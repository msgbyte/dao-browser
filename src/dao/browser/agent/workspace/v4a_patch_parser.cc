// Copyright 2026 Dao Browser Authors. All rights reserved.

#include "dao/browser/agent/workspace/v4a_patch_parser.h"

#include <string_view>

#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"

namespace dao {

V4AHunk::V4AHunk() = default;
V4AHunk::V4AHunk(const V4AHunk&) = default;
V4AHunk::V4AHunk(V4AHunk&&) noexcept = default;
V4AHunk& V4AHunk::operator=(const V4AHunk&) = default;
V4AHunk& V4AHunk::operator=(V4AHunk&&) noexcept = default;
V4AHunk::~V4AHunk() = default;

V4AFileOp::V4AFileOp() = default;
V4AFileOp::V4AFileOp(const V4AFileOp&) = default;
V4AFileOp::V4AFileOp(V4AFileOp&&) noexcept = default;
V4AFileOp& V4AFileOp::operator=(const V4AFileOp&) = default;
V4AFileOp& V4AFileOp::operator=(V4AFileOp&&) noexcept = default;
V4AFileOp::~V4AFileOp() = default;

V4APatch::V4APatch() = default;
V4APatch::V4APatch(const V4APatch&) = default;
V4APatch::V4APatch(V4APatch&&) noexcept = default;
V4APatch& V4APatch::operator=(const V4APatch&) = default;
V4APatch& V4APatch::operator=(V4APatch&&) noexcept = default;
V4APatch::~V4APatch() = default;

namespace {

constexpr std::string_view kBegin = "*** Begin Patch";
constexpr std::string_view kEnd = "*** End Patch";
constexpr std::string_view kEndOfFile = "*** End of File";
constexpr std::string_view kAddPrefix = "*** Add File: ";
constexpr std::string_view kUpdatePrefix = "*** Update File: ";
constexpr std::string_view kDeletePrefix = "*** Delete File: ";
constexpr std::string_view kMoveToPrefix = "*** Move to: ";

V4AParseError Err(int line_no, const std::string& msg) {
  return {line_no, msg};
}

}  // namespace

base::expected<V4APatch, V4AParseError> ParseV4APatch(
    const std::string& patch_text) {
  V4APatch out;
  auto lines = base::SplitStringPiece(patch_text, "\n",
                                       base::KEEP_WHITESPACE,
                                       base::SPLIT_WANT_ALL);
  // Drop trailing empty terminator.
  if (!lines.empty() && lines.back().empty()) lines.pop_back();

  int i = 0;
  const int n = static_cast<int>(lines.size());

  if (n == 0 || lines[0] != kBegin) {
    return base::unexpected(Err(1, "missing '*** Begin Patch'"));
  }
  ++i;

  enum class State { kTopLevel, kInAdd, kInUpdate, kInHunk };
  State state = State::kTopLevel;
  V4AFileOp current;
  V4AHunk current_hunk;
  bool in_active_hunk = false;

  auto flush_hunk = [&]() {
    if (in_active_hunk) {
      current.hunks.push_back(std::move(current_hunk));
      current_hunk = {};
      in_active_hunk = false;
    }
  };

  for (; i < n; ++i) {
    std::string line(lines[i]);
    const int ln = i + 1;

    if (std::string_view(line) == kEnd) {
      flush_hunk();
      if (state == State::kInAdd || state == State::kInUpdate ||
          state == State::kInHunk) {
        if (state != State::kInAdd) {
          // Update must close with *** End of File before *** End Patch.
          return base::unexpected(
              Err(ln, "update file missing '*** End of File'"));
        }
        out.ops.push_back(std::move(current));
        current = {};
      }
      // Anything after End Patch is ignored.
      return out;
    }

    if (std::string_view(line) == kEndOfFile) {
      if (state != State::kInUpdate && state != State::kInHunk) {
        return base::unexpected(
            Err(ln, "'*** End of File' outside of Update File"));
      }
      flush_hunk();
      out.ops.push_back(std::move(current));
      current = {};
      state = State::kTopLevel;
      continue;
    }

    if (base::StartsWith(line, kAddPrefix)) {
      // Close any in-progress op.
      if (state == State::kInAdd) out.ops.push_back(std::move(current));
      if (state == State::kInUpdate || state == State::kInHunk) {
        return base::unexpected(
            Err(ln, "started new file before '*** End of File'"));
      }
      current = {};
      current.kind = V4AFileOp::Kind::kAdd;
      current.path = line.substr(kAddPrefix.size());
      state = State::kInAdd;
      continue;
    }

    if (base::StartsWith(line, kUpdatePrefix)) {
      if (state == State::kInAdd) out.ops.push_back(std::move(current));
      if (state == State::kInUpdate || state == State::kInHunk) {
        return base::unexpected(
            Err(ln, "started new file before '*** End of File'"));
      }
      current = {};
      current.kind = V4AFileOp::Kind::kUpdate;
      current.path = line.substr(kUpdatePrefix.size());
      state = State::kInUpdate;
      continue;
    }

    if (base::StartsWith(line, kDeletePrefix)) {
      if (state == State::kInAdd) out.ops.push_back(std::move(current));
      if (state == State::kInUpdate || state == State::kInHunk) {
        return base::unexpected(
            Err(ln, "started new file before '*** End of File'"));
      }
      V4AFileOp del;
      del.kind = V4AFileOp::Kind::kDelete;
      del.path = line.substr(kDeletePrefix.size());
      out.ops.push_back(std::move(del));
      state = State::kTopLevel;
      continue;
    }

    if (base::StartsWith(line, kMoveToPrefix)) {
      if (state != State::kInUpdate) {
        return base::unexpected(
            Err(ln, "'*** Move to:' must immediately follow Update File"));
      }
      current.move_to = line.substr(kMoveToPrefix.size());
      continue;
    }

    // Hunk lines.
    if (state == State::kInAdd) {
      if (line.empty() || line[0] != '+') {
        return base::unexpected(
            Err(ln, "Add File body lines must start with '+'"));
      }
      current.add_lines.push_back(line.substr(1));
      continue;
    }

    if (state == State::kInUpdate || state == State::kInHunk) {
      if (base::StartsWith(line, "@@")) {
        flush_hunk();
        current_hunk = {};
        // Anchor is everything after "@@ " (may be empty).
        std::string anchor =
            line.size() >= 3
                ? std::string(base::TrimWhitespaceASCII(line.substr(3),
                                                        base::TRIM_ALL))
                : std::string();
        if (!anchor.empty()) current_hunk.anchor = anchor;
        in_active_hunk = true;
        state = State::kInHunk;
        continue;
      }
      if (!in_active_hunk) {
        return base::unexpected(
            Err(ln, "hunk line outside @@ block"));
      }
      if (line.empty()) {
        current_hunk.lines.push_back({V4AHunkLine::Kind::kContext, ""});
      } else if (line[0] == ' ') {
        current_hunk.lines.push_back(
            {V4AHunkLine::Kind::kContext, line.substr(1)});
      } else if (line[0] == '+') {
        current_hunk.lines.push_back(
            {V4AHunkLine::Kind::kAdd, line.substr(1)});
      } else if (line[0] == '-') {
        current_hunk.lines.push_back(
            {V4AHunkLine::Kind::kRemove, line.substr(1)});
      } else {
        return base::unexpected(
            Err(ln, "hunk line must start with ' ', '+', or '-'"));
      }
      continue;
    }

    return base::unexpected(Err(ln, "unknown directive: " + line));
  }

  return base::unexpected(
      Err(n, "missing '*** End Patch'"));
}

}  // namespace dao
