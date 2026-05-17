// Copyright 2026 Dao Browser Authors. All rights reserved.

#ifndef DAO_BROWSER_AGENT_WORKSPACE_V4A_PATCH_PARSER_H_
#define DAO_BROWSER_AGENT_WORKSPACE_V4A_PATCH_PARSER_H_

#include <optional>
#include <string>
#include <vector>

#include "base/types/expected.h"
#include "dao/browser/agent/dao_agent_workspace_types.h"

namespace dao {

struct V4AHunkLine {
  enum class Kind { kContext, kAdd, kRemove };
  Kind kind;
  std::string text;  // no leading marker
};

struct V4AHunk {
  V4AHunk();
  V4AHunk(const V4AHunk&);
  V4AHunk(V4AHunk&&) noexcept;
  V4AHunk& operator=(const V4AHunk&);
  V4AHunk& operator=(V4AHunk&&) noexcept;
  ~V4AHunk();

  std::optional<std::string> anchor;  // text after "@@ " on the anchor line
  std::vector<V4AHunkLine> lines;
};

struct V4AFileOp {
  enum class Kind { kAdd, kUpdate, kDelete };

  V4AFileOp();
  V4AFileOp(const V4AFileOp&);
  V4AFileOp(V4AFileOp&&) noexcept;
  V4AFileOp& operator=(const V4AFileOp&);
  V4AFileOp& operator=(V4AFileOp&&) noexcept;
  ~V4AFileOp();

  Kind kind = Kind::kAdd;
  std::string path;
  std::optional<std::string> move_to;  // only valid for kUpdate
  std::vector<std::string> add_lines;  // only valid for kAdd
  std::vector<V4AHunk> hunks;          // only valid for kUpdate
};

struct V4APatch {
  V4APatch();
  V4APatch(const V4APatch&);
  V4APatch(V4APatch&&) noexcept;
  V4APatch& operator=(const V4APatch&);
  V4APatch& operator=(V4APatch&&) noexcept;
  ~V4APatch();

  std::vector<V4AFileOp> ops;
};

struct V4AParseError {
  int line_number;          // 1-based
  std::string message;
};

base::expected<V4APatch, V4AParseError> ParseV4APatch(
    const std::string& patch_text);

}  // namespace dao

#endif  // DAO_BROWSER_AGENT_WORKSPACE_V4A_PATCH_PARSER_H_
