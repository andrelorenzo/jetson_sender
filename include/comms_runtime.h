#pragma once

#include <cstddef>
#include <cstdint>

#include "eth_comms.h"

namespace rsapp {

commh_t *CreateCommsHandle();
void DestroyCommsHandle(commh_t *handle);

bool InitUdpReceiver(commh_t *handle, uint16_t local_port, comm_recvcb_t callback);
bool SendFrame(commh_t *handle, uint8_t *data, size_t len);

} // namespace rsapp
