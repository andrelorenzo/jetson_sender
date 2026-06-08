#include "client_transport.h"

#include <chrono>
#include <cstring>
#include <thread>

#include "app_constants.h"
#include "comms_common.h"
#include "comms_runtime.h"
#include "logger.h"

namespace rsapp {
namespace {

AppContext *g_ctx = nullptr;

void HandleClientCommand(uint8_t *msg, size_t len, const char *ip, uint16_t port, uint16_t cid) {
    if (g_ctx == nullptr || len == 0 || cid != 0) {
        return;
    }

    comms_payload_t payload = {};
    Crc32 crc;
    if (DecodeFrame(msg, static_cast<int>(len), &payload) < 0) {
        return;
    }
    if (!crc.Check(&payload)) {
        return;
    }

    switch (payload.msg_id) {
        case constants::kMsgIdEstablishConnection:
            Logger(INFO, "Recibido MSGID_ESTABLISH_CONNECTION desde %s:%u", ip, port);
            if (g_ctx->data_commh != nullptr) {
                CommsConnect(g_ctx->data_commh, ip, port);
            }
            ClearImuQueues(g_ctx);
            g_ctx->client_connected.store(true);
            break;

        case constants::kMsgIdCloseConnection:
            Logger(INFO, "Recibido MSGID_UNSESTABLISH_CONNECTION desde %s:%u", ip, port);
            g_ctx->client_connected.store(false);
            ClearImuQueues(g_ctx);
            break;

        default:
            break;
    }
}

int32_t ScaleImu(float value) {
    return static_cast<int32_t>(value * constants::kImuScale);
}

} // namespace

bool SendPayload(AppContext *ctx, uint16_t msg_id, const void *data, size_t data_size) {
    if (ctx == nullptr || ctx->data_commh == nullptr || data == nullptr || data_size == 0) {
        return false;
    }

    comms_payload_t payload = {};
    uint8_t frame[BUFFER_DEFAULT_SIZE * 2] = {};
    Crc32 crc;

    if (data_size > sizeof(payload.data)) {
        Logger(ERROR, "UDP payload demasiado grande: %zu bytes", data_size);
        return false;
    }

    payload.msg_id = msg_id;
    payload.data_size = static_cast<uint16_t>(data_size);
    std::memcpy(payload.data, data, data_size);
    payload.crc_32 = crc.Calculate(&payload);

    const int frame_len = EncodeFrame(&payload, frame);
    if (frame_len <= 0) {
        return false;
    }

    return SendFrame(ctx->data_commh, frame, static_cast<size_t>(frame_len));
}

void ClearImuQueues(AppContext *ctx) {
    if (ctx == nullptr) {
        return;
    }

    std::lock_guard<std::mutex> lock(ctx->imu_mutex);
    ctx->accel_queue.clear();
    ctx->gyro_queue.clear();
}

void QueueAccelSample(AppContext *ctx, float x, float y, float z, uint64_t ts_us, uint32_t ts_domain) {
    if (ctx == nullptr || !ctx->client_connected.load()) {
        return;
    }

    RsAccelMsg msg = {};
    msg.seq = ctx->accel_seq.fetch_add(1) + 1;
    msg.ts_us = ts_us;
    msg.ts_domain = ts_domain;
    msg.acc[0] = ScaleImu(x);
    msg.acc[1] = ScaleImu(y);
    msg.acc[2] = ScaleImu(z);

    std::lock_guard<std::mutex> lock(ctx->imu_mutex);
    if (ctx->accel_queue.size() >= constants::kMaxImuQueue) {
        ctx->accel_queue.pop_front();
    }
    ctx->accel_queue.push_back(msg);
}

void QueueGyroSample(AppContext *ctx, float x, float y, float z, uint64_t ts_us, uint32_t ts_domain) {
    if (ctx == nullptr || !ctx->client_connected.load()) {
        return;
    }

    RsGyroMsg msg = {};
    msg.seq = ctx->gyro_seq.fetch_add(1) + 1;
    msg.ts_us = ts_us;
    msg.ts_domain = ts_domain;
    msg.gyro[0] = ScaleImu(x);
    msg.gyro[1] = ScaleImu(y);
    msg.gyro[2] = ScaleImu(z);

    std::lock_guard<std::mutex> lock(ctx->imu_mutex);
    if (ctx->gyro_queue.size() >= constants::kMaxImuQueue) {
        ctx->gyro_queue.pop_front();
    }
    ctx->gyro_queue.push_back(msg);
}

void RunImuPublisher(AppContext *ctx) {
    if (ctx == nullptr) {
        return;
    }

    g_ctx = ctx;

    if (ctx->data_commh == nullptr) {
        Logger(ERROR, "Handle UDP de datos no inicializado");
        return;
    }

    InitUdpReceiver(ctx->data_commh, constants::kClientListenPort, HandleClientCommand);

    Logger(INFO, "UDP IMU/control de sesion escuchando en puerto %u", constants::kClientListenPort);

    while (true) {
        if (ctx->client_connected.load()) {
            std::deque<RsAccelMsg> accel_local;
            std::deque<RsGyroMsg> gyro_local;

            {
                std::lock_guard<std::mutex> lock(ctx->imu_mutex);
                accel_local.swap(ctx->accel_queue);
                gyro_local.swap(ctx->gyro_queue);
            }

            for (size_t i = 0; i < accel_local.size(); ++i) {
                if (!SendPayload(ctx, constants::kMsgIdAccelData, &accel_local[i], sizeof(RsAccelMsg))) {
                    Logger(WARN, "Error enviando muestra ACCEL");
                }
            }

            for (size_t i = 0; i < gyro_local.size(); ++i) {
                if (!SendPayload(ctx, constants::kMsgIdGyroData, &gyro_local[i], sizeof(RsGyroMsg))) {
                    Logger(WARN, "Error enviando muestra GYRO");
                }
            }
        } else {
            ClearImuQueues(ctx);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

} // namespace rsapp
