#define ETHCOMMS_IMP
#include "eth_comms.h"

#include <cstdlib>
#include <cstring>

#include "comms_runtime.h"

namespace rsapp {

commh_t *CreateCommsHandle() {
    commh_t *handle = static_cast<commh_t *>(std::calloc(1, sizeof(commh_t)));
    return handle;
}

void DestroyCommsHandle(commh_t *handle) {
    if (handle == nullptr) {
        return;
    }

    CommsClose(handle);
    std::free(handle);
}

bool InitUdpReceiver(commh_t *handle, uint16_t local_port, comm_recvcb_t callback) {
    if (handle == nullptr) {
        return false;
    }

    comms_opt_t opt = {};
    opt.local_port = local_port;
    opt.recvcb = callback;
    return comms_udp_init__opt(handle, opt);
}

bool SendFrame(commh_t *handle, uint8_t *data, size_t len) {
    if (handle == nullptr || data == nullptr || len == 0) {
        return false;
    }

    comms_send_opt_t opt = {};
    return comms_send__opt(handle, data, len, opt);
}

} // namespace rsapp
