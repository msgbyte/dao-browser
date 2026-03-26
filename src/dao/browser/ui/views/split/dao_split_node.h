// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_VIEWS_SPLIT_DAO_SPLIT_NODE_H_
#define DAO_BROWSER_UI_VIEWS_SPLIT_DAO_SPLIT_NODE_H_

#include <memory>

#include "base/memory/raw_ptr.h"
#include "base/values.h"
#include "ui/gfx/geometry/rect.h"

namespace content {
class WebContents;
}

namespace dao {

enum class SplitDirection { kHorizontal, kVertical };

// Maximum allowed tree depth to limit pane count.
constexpr int kMaxSplitDepth = 4;

// Abstract base for the binary split tree.
//
//   DaoSplitNode
//   +-- DaoSplitBranchNode (internal: splits space between two children)
//   +-- DaoSplitLeafNode   (leaf: holds a WebContents pointer)
//
class DaoSplitNode {
 public:
  virtual ~DaoSplitNode() = default;

  // Recursively compute bounds for this subtree.
  virtual void Layout(const gfx::Rect& bounds) = 0;

  // Serialize to a dict for persistence.
  virtual base::Value::Dict Serialize() const = 0;

  // Number of leaf nodes in this subtree.
  virtual int CountLeaves() const = 0;

  // Maximum depth of this subtree (leaf = 0).
  virtual int MaxDepth() const = 0;

  // Find the leaf that holds |web_contents|.  Returns nullptr if not found.
  virtual class DaoSplitLeafNode* FindLeaf(
      content::WebContents* web_contents) = 0;

  // Type checking (replacement for dynamic_cast, since Chromium has no RTTI).
  virtual bool IsLeaf() const;
  virtual bool IsBranch() const;
  DaoSplitLeafNode* AsLeaf();
  class DaoSplitBranchNode* AsBranch();

  // The computed bounds from the most recent Layout() call.
  const gfx::Rect& bounds() const { return bounds_; }

  // Parent pointer (set by branch when adopting children).
  class DaoSplitBranchNode* parent() const { return parent_; }
  void set_parent(DaoSplitBranchNode* p) { parent_ = p; }

 protected:
  gfx::Rect bounds_;
  raw_ptr<DaoSplitBranchNode> parent_ = nullptr;
};

// Internal node: splits space between two children.
class DaoSplitBranchNode : public DaoSplitNode {
 public:
  DaoSplitBranchNode(SplitDirection direction,
                     float ratio,
                     std::unique_ptr<DaoSplitNode> first,
                     std::unique_ptr<DaoSplitNode> second);
  ~DaoSplitBranchNode() override;

  // DaoSplitNode:
  void Layout(const gfx::Rect& bounds) override;
  base::Value::Dict Serialize() const override;
  int CountLeaves() const override;
  int MaxDepth() const override;
  DaoSplitLeafNode* FindLeaf(content::WebContents* web_contents) override;
  bool IsBranch() const override;

  SplitDirection direction() const { return direction_; }
  float ratio() const { return ratio_; }
  void set_ratio(float r);

  DaoSplitNode* first() const { return first_.get(); }
  DaoSplitNode* second() const { return second_.get(); }

  // Release ownership of a child and return it.
  std::unique_ptr<DaoSplitNode> TakeFirst();
  std::unique_ptr<DaoSplitNode> TakeSecond();

 private:
  SplitDirection direction_;
  float ratio_;  // proportion allocated to first child [0.1, 0.9]
  std::unique_ptr<DaoSplitNode> first_;
  std::unique_ptr<DaoSplitNode> second_;
};

// Leaf node: holds a non-owning pointer to a WebContents.
class DaoSplitLeafNode : public DaoSplitNode {
 public:
  explicit DaoSplitLeafNode(content::WebContents* web_contents);
  ~DaoSplitLeafNode() override;

  // DaoSplitNode:
  void Layout(const gfx::Rect& bounds) override;
  base::Value::Dict Serialize() const override;
  int CountLeaves() const override;
  int MaxDepth() const override;
  DaoSplitLeafNode* FindLeaf(content::WebContents* web_contents) override;
  bool IsLeaf() const override;

  content::WebContents* web_contents() const { return web_contents_; }
  void set_web_contents(content::WebContents* wc) { web_contents_ = wc; }

 private:
  raw_ptr<content::WebContents> web_contents_;
};

// ---- Tree operations --------------------------------------------------------

// Replace |leaf| with a branch node containing |leaf|'s content and
// |new_contents| as the new sibling.  Returns nullptr if the tree already
// reached kMaxSplitDepth.  The caller must rebuild the view tree afterward.
//
// |root| is an in/out parameter: if |leaf| is the root, |root| is updated to
// point at the new branch node.
std::unique_ptr<DaoSplitBranchNode> SplitLeaf(
    std::unique_ptr<DaoSplitNode>& root,
    DaoSplitLeafNode* leaf,
    SplitDirection direction,
    bool new_contents_first,
    content::WebContents* new_contents);

// Close |leaf| by promoting its sibling in the parent branch.
// Returns the promoted sibling.  If |leaf| is the root (last pane), returns
// nullptr (no-op).  |root| is updated if the root changes.
DaoSplitNode* CloseLeaf(std::unique_ptr<DaoSplitNode>& root,
                         DaoSplitLeafNode* leaf);

// Deserialize a tree from a dict previously produced by Serialize().
// Returns nullptr on invalid data.
std::unique_ptr<DaoSplitNode> DeserializeTree(const base::Value::Dict& dict);

}  // namespace dao

#endif  // DAO_BROWSER_UI_VIEWS_SPLIT_DAO_SPLIT_NODE_H_
