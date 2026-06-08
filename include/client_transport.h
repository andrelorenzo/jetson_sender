#pragma once

#include "app_types.h"

namespace rsapp {

bool SendPayload(AppContext *ctx, uint16_t msg_id, const void *data, size_t data_size);
void ClearImuQueues(AppContext *ctx);
void QueueAccelSample(AppContext *ctx, float x, float y, float z, uint64_t ts_us, uint32_t ts_domain);
void QueueGyroSample(AppContext *ctx, float x, float y, float z, uint64_t ts_us, uint32_t ts_domain);
void RunImuPublisher(AppContext *ctx);

} // namespace rsapp
