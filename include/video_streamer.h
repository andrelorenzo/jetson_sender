#pragma once

#include <librealsense2/rs.hpp>

#include "app_types.h"

namespace rsapp {

void PushColorFrame(AppContext *ctx, const rs2::video_frame &frame, uint64_t camera_ts_us, uint32_t ts_domain);
void RunRtspServer(AppContext *ctx);

} // namespace rsapp
