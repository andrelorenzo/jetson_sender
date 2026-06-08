#pragma once

#include <gst/app/gstappsrc.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>

#include "eth_comms.h"

namespace rsapp {

#pragma pack(push, 1)
struct RsAccelMsg {
    uint32_t seq;
    uint64_t ts_us;
    uint32_t ts_domain;
    int32_t acc[3];
};

struct RsGyroMsg {
    uint32_t seq;
    uint64_t ts_us;
    uint32_t ts_domain;
    int32_t gyro[3];
};

struct TwistCmd {
    uint32_t seq;
    uint64_t ts_us;
    float lin[3];
    float ang[3];
};
#pragma pack(pop)

struct AppConfig {
    bool debug = false;
    bool record_bag = false;
    char bag_file[256] = {};
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t fps = 0;
    bool control_enabled = true;
    char fcu_dev[128] = {};
    uint32_t fcu_baud = 0;
    uint32_t ctrl_timeout_ms = 0;
    uint32_t mav_target_system = 1;
    uint32_t mav_target_component = 1;
    bool ctrl_ros_convention = true;
};

struct VideoStampInfo {
    uint64_t camera_ts_us;
    uint32_t seq;
    uint32_t ts_domain;
};

struct AppContext {
    AppConfig config;

    commh_t data_commh = {};
    commh_t control_commh = {};

    std::atomic<bool> client_connected{false};
    std::atomic<bool> camera_connected{false};

    std::mutex twist_mutex;
    TwistCmd latest_twist = {};
    std::chrono::steady_clock::time_point latest_twist_time = {};
    std::atomic<bool> twist_valid{false};

    int mav_fd = -1;
    std::mutex mav_mutex;

    std::mutex imu_mutex;
    std::deque<RsAccelMsg> accel_queue;
    std::deque<RsGyroMsg> gyro_queue;
    std::atomic<uint32_t> accel_seq{0};
    std::atomic<uint32_t> gyro_seq{0};

    std::mutex rtsp_mutex;
    GstAppSrc *appsrc = nullptr;
    guint64 rtsp_pts = 0;
    guint64 rtsp_duration = 0;
    std::atomic<uint32_t> video_seq{0};

    std::mutex video_ts_mutex;
    std::map<guint64, VideoStampInfo> video_ts_map;
};

} // namespace rsapp
