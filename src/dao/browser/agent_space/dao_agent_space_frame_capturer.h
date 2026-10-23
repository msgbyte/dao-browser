// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_AGENT_SPACE_DAO_AGENT_SPACE_FRAME_CAPTURER_H_
#define DAO_BROWSER_AGENT_SPACE_DAO_AGENT_SPACE_FRAME_CAPTURER_H_

#include <memory>

#include "base/functional/callback.h"
#include "base/functional/callback_helpers.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "components/viz/host/client_frame_sink_video_capturer.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/gfx/geometry/size.h"

namespace content {
class WebContents;
}  // namespace content

namespace dao {

// Streams downscaled frames of one page while the user watches an Agent
// Space. Capture only runs while an instance exists, so a space nobody is
// looking at costs no frame copies.
class DaoAgentSpaceFrameCapturer : public viz::mojom::FrameSinkVideoConsumer {
 public:
  // The bitmap only stays valid during the callback.
  using FrameCallback = base::RepeatingCallback<void(const SkBitmap&)>;

  DaoAgentSpaceFrameCapturer(content::WebContents* contents,
                             gfx::Size target_size,
                             int max_frames_per_second,
                             FrameCallback callback);
  ~DaoAgentSpaceFrameCapturer() override;

  DaoAgentSpaceFrameCapturer(const DaoAgentSpaceFrameCapturer&) = delete;
  DaoAgentSpaceFrameCapturer& operator=(const DaoAgentSpaceFrameCapturer&) =
      delete;

  // Returns false when the page has no view to capture yet.
  bool Start();
  void Stop();
  content::WebContents* contents() const { return contents_.get(); }

 private:
  // viz::mojom::FrameSinkVideoConsumer:
  void OnFrameCaptured(
      ::media::mojom::VideoBufferHandlePtr data,
      ::media::mojom::VideoFrameInfoPtr info,
      const gfx::Rect& content_rect,
      mojo::PendingRemote<::viz::mojom::FrameSinkVideoConsumerFrameCallbacks>
          callbacks) override;
  void OnNewCaptureVersion(
      const media::CaptureVersion& capture_version) override {}
  void OnFrameWithEmptyRegionCapture() override {}
  void OnStopped() override {}
  void OnLog(const std::string& message) override {}

  base::WeakPtr<content::WebContents> contents_;
  const gfx::Size target_size_;
  const int max_frames_per_second_;
  FrameCallback callback_;
  std::unique_ptr<viz::ClientFrameSinkVideoCapturer> video_capturer_;
  // Keeps the page rendering while it is watched, even if the space is
  // waiting for the user and released its own keep-alive.
  base::ScopedClosureRunner visible_capture_;
  SEQUENCE_CHECKER(sequence_checker_);
};

}  // namespace dao

#endif  // DAO_BROWSER_AGENT_SPACE_DAO_AGENT_SPACE_FRAME_CAPTURER_H_
