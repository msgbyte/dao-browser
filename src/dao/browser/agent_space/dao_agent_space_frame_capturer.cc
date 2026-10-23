// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/agent_space/dao_agent_space_frame_capturer.h"

#include <algorithm>
#include <utility>

#include "base/memory/read_only_shared_memory_region.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "media/base/video_frame.h"
#include "media/capture/mojom/video_capture_buffer.mojom.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "third_party/skia/include/core/SkColorSpace.h"
#include "third_party/skia/include/core/SkImageInfo.h"
#include "ui/gfx/geometry/skia_conversions.h"

namespace dao {

DaoAgentSpaceFrameCapturer::DaoAgentSpaceFrameCapturer(
    content::WebContents* contents,
    gfx::Size target_size,
    int max_frames_per_second,
    FrameCallback callback)
    : contents_(contents ? contents->GetWeakPtr() : nullptr),
      target_size_(target_size),
      max_frames_per_second_(std::max(1, max_frames_per_second)),
      callback_(std::move(callback)) {}

DaoAgentSpaceFrameCapturer::~DaoAgentSpaceFrameCapturer() {
  Stop();
}

bool DaoAgentSpaceFrameCapturer::Start() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (video_capturer_) {
    return true;
  }
  content::RenderWidgetHostView* view =
      contents_ ? contents_->GetRenderWidgetHostView() : nullptr;
  if (!view) {
    return false;
  }
  visible_capture_ = contents_->IncrementCapturerCount(
      target_size_, /*stay_hidden=*/false, /*stay_awake=*/false,
      /*is_activity=*/false);
  video_capturer_ = view->CreateVideoCapturer();
  video_capturer_->SetResolutionConstraints(target_size_, target_size_,
                                            /*use_fixed_aspect_ratio=*/false);
  video_capturer_->SetAutoThrottlingEnabled(false);
  video_capturer_->SetMinSizeChangePeriod(base::TimeDelta());
  video_capturer_->SetFormat(media::PIXEL_FORMAT_ARGB);
  video_capturer_->SetMinCapturePeriod(base::Seconds(1) /
                                       max_frames_per_second_);
  video_capturer_->Start(this, viz::mojom::BufferFormatPreference::kDefault);
  return true;
}

void DaoAgentSpaceFrameCapturer::Stop() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (video_capturer_) {
    video_capturer_->Stop();
    video_capturer_.reset();
  }
  visible_capture_.RunAndReset();
}

void DaoAgentSpaceFrameCapturer::OnFrameCaptured(
    ::media::mojom::VideoBufferHandlePtr data,
    ::media::mojom::VideoFrameInfoPtr info,
    const gfx::Rect& content_rect,
    mojo::PendingRemote<::viz::mojom::FrameSinkVideoConsumerFrameCallbacks>
        callbacks) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // Releasing the remote when this scope ends returns the buffer to the pool.
  mojo::Remote<::viz::mojom::FrameSinkVideoConsumerFrameCallbacks>
      callbacks_remote(std::move(callbacks));
  if (!video_capturer_ || !data->is_read_only_shmem_region()) {
    return;
  }
  base::ReadOnlySharedMemoryMapping mapping =
      data->get_read_only_shmem_region().Map();
  if (!mapping.IsValid() ||
      mapping.size() < media::VideoFrame::AllocationSize(info->pixel_format,
                                                         info->coded_size)) {
    return;
  }
  const SkImageInfo image_info = SkImageInfo::MakeN32(
      content_rect.right(), content_rect.bottom(), kPremul_SkAlphaType,
      info->color_space.ToSkColorSpace());
  const size_t row_bytes =
      media::VideoFrame::RowBytes(media::VideoFrame::Plane::kARGB,
                                  info->pixel_format, info->coded_size.width());
  if (mapping.size() < image_info.computeByteSize(row_bytes)) {
    return;
  }
  SkBitmap frame;
  if (!frame.installPixels(image_info, const_cast<void*>(mapping.memory()),
                           row_bytes)) {
    return;
  }
  SkBitmap cropped;
  if (!frame.extractSubset(&cropped, gfx::RectToSkIRect(content_rect))) {
    return;
  }
  callback_.Run(cropped);
}

}  // namespace dao
