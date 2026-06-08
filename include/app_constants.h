#pragma once

#include <cstddef>
#include <cstdint>

#ifndef RSAPP_MSGID_TWIST_CMD
#define RSAPP_MSGID_TWIST_CMD 0x0021
#endif

namespace rsapp {
namespace constants {

constexpr uint16_t kClientListenPort = 5001;
constexpr uint16_t kControlListenPort = 5003;

constexpr uint16_t kMsgIdEstablishConnection = 0x0001;
constexpr uint16_t kMsgIdCloseConnection = 0x0002;
constexpr uint16_t kMsgIdAccelData = 0x0011;
constexpr uint16_t kMsgIdGyroData = 0x0012;
constexpr uint16_t kMsgIdTwistCmd = RSAPP_MSGID_TWIST_CMD;

constexpr char kRtspMount[] = "realsense";
constexpr uint16_t kRtspPort = 8554;

constexpr float kImuScale = 1000000.0f;
constexpr std::size_t kMaxImuQueue = 4096;
constexpr std::size_t kMaxVideoTimestampMap = 512;

constexpr uint32_t kDefaultWidth = 1280;
constexpr uint32_t kDefaultHeight = 720;
constexpr uint32_t kDefaultFps = 30;

constexpr uint32_t kDefaultControlRateHz = 30;
constexpr uint32_t kDefaultControlTimeoutMs = 250;

constexpr float kMaxControlVelocityXYMs = 3.0f;
constexpr float kMaxControlVelocityZMs = 1.5f;
constexpr float kMaxControlYawRateRadS = 1.5f;

constexpr uint8_t kCompanionSystemId = 200;
constexpr uint8_t kCompanionComponentId = 191;

constexpr uint32_t kRtspBitrate = 4000000;
constexpr uint32_t kCameraPollTimeoutMs = 5000;
constexpr uint32_t kReconnectDelayMs = 500;
constexpr uint32_t kWaitForCameraDelayMs = 250;

} // namespace constants
} // namespace rsapp
