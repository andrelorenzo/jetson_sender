#include "realsense_capture.h"

#include <librealsense2/rs.hpp>

#include <chrono>
#include <cmath>
#include <string>
#include <thread>

#include "app_constants.h"
#include "client_transport.h"
#include "logger.h"
#include "video_streamer.h"

namespace rsapp {
namespace {

uint64_t FrameTimestampUs(const rs2::frame &frame) {
    const double ts_ms = frame.get_timestamp();
    if (ts_ms <= 0.0) {
        return 0;
    }
    return static_cast<uint64_t>(std::llround(ts_ms * 1000.0));
}

void SafeStopPipeline(rs2::pipeline &pipe) {
    try {
        pipe.stop();
    } catch (...) {
    }
}

rs2::device SelectDevice(const rs2::device_list &devices) {
    for (rs2::device device : devices) {
        if (!device.supports(RS2_CAMERA_INFO_NAME)) {
            continue;
        }
        const std::string name = device.get_info(RS2_CAMERA_INFO_NAME);
        if (name.find("D435I") != std::string::npos || name.find("D435i") != std::string::npos) {
            return device;
        }
    }
    return devices.front();
}

void HandleFrame(AppContext *ctx, const rs2::frame &frame) {
    if (!frame) {
        return;
    }

    const uint64_t ts_us = FrameTimestampUs(frame);
    const uint32_t ts_domain = static_cast<uint32_t>(frame.get_frame_timestamp_domain());

    switch (frame.get_profile().stream_type()) {
        case RS2_STREAM_COLOR: {
            PushColorFrame(ctx, frame.as<rs2::video_frame>(), ts_us, ts_domain);
            break;
        }
        case RS2_STREAM_ACCEL: {
            const rs2_vector sample = frame.as<rs2::motion_frame>().get_motion_data();
            QueueAccelSample(ctx, sample.x, sample.y, sample.z, ts_us, ts_domain);
            break;
        }
        case RS2_STREAM_GYRO: {
            const rs2_vector sample = frame.as<rs2::motion_frame>().get_motion_data();
            QueueGyroSample(ctx, sample.x, sample.y, sample.z, ts_us, ts_domain);
            break;
        }
        case RS2_STREAM_DEPTH:
        default:
            break;
    }
}

void CaptureLoop(AppContext *ctx, rs2::pipeline &pipe, bool from_bag) {
    while (true) {
        rs2::frameset frames;
        if (!pipe.try_wait_for_frames(&frames, constants::kCameraPollTimeoutMs)) {
            Logger(WARN, from_bag ? "Fin/timeout del .bag" : "Timeout de RealSense; se intentara reconectar");
            break;
        }

        for (rs2::frame frame : frames) {
            HandleFrame(ctx, frame);
        }
    }
}

void StartBagPlayback(AppContext *ctx, rs2::pipeline &pipe) {
    if (ctx->config.bag_file[0] == '\0') {
        Logger(ERROR, "DEBUG=true pero BAG_FILE esta vacio");
        std::this_thread::sleep_for(std::chrono::milliseconds(constants::kReconnectDelayMs));
        return;
    }

    rs2::config cfg;
    cfg.enable_device_from_file(ctx->config.bag_file);
    rs2::pipeline_profile profile = pipe.start(cfg);
    rs2::device bag_device = profile.get_device();
    if (bag_device.is<rs2::playback>()) {
        bag_device.as<rs2::playback>().set_real_time(true);
    }

    Logger(INFO, "Bag conectado: %s", ctx->config.bag_file);
    ctx->camera_connected.store(true);
    CaptureLoop(ctx, pipe, true);
}

bool StartLiveDevice(AppContext *ctx, rs2::context &context, rs2::pipeline &pipe, bool &waiting_logged) {
    const rs2::device_list devices = context.query_devices();
    if (devices.size() == 0) {
        if (ctx->camera_connected.exchange(false) || !waiting_logged) {
            Logger(INFO, "Esperando camara RealSense D435i...");
            waiting_logged = true;
        }
        ClearImuQueues(ctx);
        std::this_thread::sleep_for(std::chrono::milliseconds(constants::kWaitForCameraDelayMs));
        return false;
    }

    waiting_logged = false;

    const rs2::device device = SelectDevice(devices);
    const std::string name = device.supports(RS2_CAMERA_INFO_NAME) ? device.get_info(RS2_CAMERA_INFO_NAME) : "RealSense";
    const std::string serial = device.supports(RS2_CAMERA_INFO_SERIAL_NUMBER) ? device.get_info(RS2_CAMERA_INFO_SERIAL_NUMBER) : "";

    Logger(INFO, "Camara conectada: %s SN=%s", name.c_str(), serial.c_str());

    rs2::config cfg;
    if (!serial.empty()) {
        cfg.enable_device(serial);
    }
    cfg.enable_stream(RS2_STREAM_COLOR, static_cast<int>(ctx->config.width), static_cast<int>(ctx->config.height), RS2_FORMAT_RGB8, static_cast<int>(ctx->config.fps));
    cfg.enable_stream(RS2_STREAM_DEPTH, static_cast<int>(ctx->config.width), static_cast<int>(ctx->config.height), RS2_FORMAT_Z16, static_cast<int>(ctx->config.fps));
    cfg.enable_stream(RS2_STREAM_GYRO, RS2_FORMAT_MOTION_XYZ32F);
    cfg.enable_stream(RS2_STREAM_ACCEL, RS2_FORMAT_MOTION_XYZ32F);

    if (!ctx->config.debug && ctx->config.record_bag && ctx->config.bag_file[0] != '\0') {
        cfg.enable_record_to_file(ctx->config.bag_file);
    }

    pipe.start(cfg);
    ctx->camera_connected.store(true);
    Logger(INFO, "RealSense activa: COLOR %ux%u@%u + DEPTH + GYRO + ACCEL", ctx->config.width, ctx->config.height, ctx->config.fps);
    CaptureLoop(ctx, pipe, false);
    return true;
}

} // namespace

void RunRealsenseCapture(AppContext *ctx) {
    if (ctx == nullptr) {
        return;
    }

    rs2::context context;
    bool waiting_logged = false;

    while (true) {
        rs2::pipeline pipe(context);
        bool had_active_stream = false;

        try {
            if (ctx->config.debug) {
                StartBagPlayback(ctx, pipe);
                had_active_stream = true;
            } else {
                had_active_stream = StartLiveDevice(ctx, context, pipe, waiting_logged);
            }
        } catch (const rs2::error &error) {
            Logger(ERROR, "RealSense error calling %s(%s): %s", error.get_failed_function(), error.get_failed_args(), error.what());
            had_active_stream = true;
        } catch (const std::exception &error) {
            Logger(ERROR, "RealSense std::exception: %s", error.what());
            had_active_stream = true;
        }

        SafeStopPipeline(pipe);
        ctx->camera_connected.store(false);
        ClearImuQueues(ctx);

        if (!ctx->config.debug && had_active_stream) {
            Logger(WARN, "Captura RealSense detenida; bucle de reconexion activo");
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(constants::kReconnectDelayMs));
    }
}

} // namespace rsapp
