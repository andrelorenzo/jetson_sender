#include "orange_cube_controller.h"

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstring>
#include <thread>

#include "app_constants.h"
#include "comms_common.h"
#include "logger.h"
#include "mavlink_bridge.h"

namespace rsapp {
namespace {

AppContext *g_ctx = nullptr;

float Clamp(float value, float min_value, float max_value) {
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

uint32_t BootTimeMs() {
    static const auto start = std::chrono::steady_clock::now();
    const auto now = std::chrono::steady_clock::now();
    return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count());
}

speed_t BaudToTermios(uint32_t baud) {
    switch (baud) {
        case 57600: return B57600;
        case 115200: return B115200;
        case 230400: return B230400;
        case 460800: return B460800;
#ifdef B921600
        case 921600: return B921600;
#endif
        default: return B115200;
    }
}

int OpenSerialPort(const char *device, uint32_t baud) {
    const int fd = open(device, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        Logger(ERROR, "No se pudo abrir FCU serial %s: %s", device, strerror(errno));
        return -1;
    }

    struct termios tty = {};
    if (tcgetattr(fd, &tty) != 0) {
        Logger(ERROR, "tcgetattr(%s) fallo: %s", device, strerror(errno));
        close(fd);
        return -1;
    }

    cfmakeraw(&tty);
    const speed_t speed = BaudToTermios(baud);
    cfsetispeed(&tty, speed);
    cfsetospeed(&tty, speed);

    tty.c_cflag |= CLOCAL | CREAD;
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;
    tty.c_cflag &= ~PARENB;
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CRTSCTS;
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 0;

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        Logger(ERROR, "tcsetattr(%s) fallo: %s", device, strerror(errno));
        close(fd);
        return -1;
    }

    tcflush(fd, TCIOFLUSH);
    Logger(INFO, "OrangeCube serial abierto: %s @ %u", device, baud);
    return fd;
}

bool SerialWriteAll(int fd, const uint8_t *data, size_t len) {
    size_t sent = 0;

    while (sent < len) {
        const ssize_t count = write(fd, data + sent, len - sent);
        if (count > 0) {
            sent += static_cast<size_t>(count);
            continue;
        }
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        Logger(ERROR, "Error escribiendo MAVLink serial: %s", strerror(errno));
        return false;
    }

    return true;
}

bool SendMavlinkMessage(const mavlink_message_t &message) {
    if (g_ctx == nullptr) {
        return false;
    }

    uint8_t buffer[MAVLINK_MAX_PACKET_LEN] = {};
    const uint16_t len = mavlink_msg_to_send_buffer(buffer, &message);

    std::lock_guard<std::mutex> lock(g_ctx->mav_mutex);
    if (g_ctx->mav_fd < 0) {
        return false;
    }

    return SerialWriteAll(g_ctx->mav_fd, buffer, len);
}

void SendHeartbeat() {
    mavlink_message_t message = {};
    mavlink_msg_heartbeat_pack(
        constants::kCompanionSystemId,
        constants::kCompanionComponentId,
        &message,
        MAV_TYPE_ONBOARD_CONTROLLER,
        MAV_AUTOPILOT_INVALID,
        0,
        0,
        MAV_STATE_ACTIVE
    );
    SendMavlinkMessage(message);
}

bool SendTwistSetpoint(const TwistCmd &cmd, bool fresh) {
    if (g_ctx == nullptr) {
        return false;
    }

    float vx = 0.0f;
    float vy = 0.0f;
    float vz = 0.0f;
    float yaw_rate = 0.0f;

    if (fresh) {
        if (g_ctx->config.ctrl_ros_convention) {
            vx = cmd.lin[0];
            vy = -cmd.lin[1];
            vz = -cmd.lin[2];
            yaw_rate = -cmd.ang[2];
        } else {
            vx = cmd.lin[0];
            vy = cmd.lin[1];
            vz = cmd.lin[2];
            yaw_rate = cmd.ang[2];
        }

        vx = Clamp(vx, -constants::kMaxControlVelocityXYMs, constants::kMaxControlVelocityXYMs);
        vy = Clamp(vy, -constants::kMaxControlVelocityXYMs, constants::kMaxControlVelocityXYMs);
        vz = Clamp(vz, -constants::kMaxControlVelocityZMs, constants::kMaxControlVelocityZMs);
        yaw_rate = Clamp(yaw_rate, -constants::kMaxControlYawRateRadS, constants::kMaxControlYawRateRadS);
    }

    const uint16_t type_mask =
        POSITION_TARGET_TYPEMASK_X_IGNORE |
        POSITION_TARGET_TYPEMASK_Y_IGNORE |
        POSITION_TARGET_TYPEMASK_Z_IGNORE |
        POSITION_TARGET_TYPEMASK_AX_IGNORE |
        POSITION_TARGET_TYPEMASK_AY_IGNORE |
        POSITION_TARGET_TYPEMASK_AZ_IGNORE |
        POSITION_TARGET_TYPEMASK_YAW_IGNORE;

    mavlink_message_t message = {};
    mavlink_msg_set_position_target_local_ned_pack(
        constants::kCompanionSystemId,
        constants::kCompanionComponentId,
        &message,
        BootTimeMs(),
        static_cast<uint8_t>(g_ctx->config.mav_target_system),
        static_cast<uint8_t>(g_ctx->config.mav_target_component),
        MAV_FRAME_BODY_NED,
        type_mask,
        0.0f,
        0.0f,
        0.0f,
        vx,
        vy,
        vz,
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        yaw_rate
    );

    return SendMavlinkMessage(message);
}

void CloseSerial() {
    if (g_ctx == nullptr) {
        return;
    }

    std::lock_guard<std::mutex> lock(g_ctx->mav_mutex);
    if (g_ctx->mav_fd >= 0) {
        close(g_ctx->mav_fd);
        g_ctx->mav_fd = -1;
    }
}

void HandleControlCommand(uint8_t *msg, size_t len, const char *ip, uint16_t port, uint16_t cid) {
    (void)ip;
    (void)port;

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

    if (payload.msg_id != constants::kMsgIdTwistCmd) {
        return;
    }

    if (payload.data_size < sizeof(TwistCmd)) {
        Logger(WARN, "TWIST cmd size incorrecto: %u", payload.data_size);
        return;
    }

    TwistCmd cmd = {};
    std::memcpy(&cmd, payload.data, sizeof(cmd));

    {
        std::lock_guard<std::mutex> lock(g_ctx->twist_mutex);
        g_ctx->latest_twist = cmd;
        g_ctx->latest_twist_time = std::chrono::steady_clock::now();
        g_ctx->twist_valid.store(true);
    }
}

} // namespace

void RunOrangeCubeControl(AppContext *ctx) {
    if (ctx == nullptr) {
        return;
    }

    if (!ctx->config.control_enabled) {
        Logger(INFO, "Control OrangeCube deshabilitado");
        return;
    }

    g_ctx = ctx;

    if (ctx->control_commh == nullptr) {
        Logger(ERROR, "Handle UDP de control no inicializado");
        return;
    }

    comms_opt_t opt = {};
    opt.local_port = constants::kControlListenPort;
    opt.recvcb = HandleControlCommand;
    comms_udp_init__opt(ctx->control_commh, opt);

    Logger(INFO, "Control UDP escuchando TWIST en puerto %u", constants::kControlListenPort);

    const uint32_t period_ms = 1000 / constants::kDefaultControlRateHz;
    const uint32_t timeout_ms = ctx->config.ctrl_timeout_ms > 0 ? ctx->config.ctrl_timeout_ms : constants::kDefaultControlTimeoutMs;
    auto last_heartbeat = std::chrono::steady_clock::now();

    while (true) {
        if (ctx->mav_fd < 0) {
            ctx->mav_fd = OpenSerialPort(ctx->config.fcu_dev, ctx->config.fcu_baud);
            if (ctx->mav_fd < 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                continue;
            }
        }

        const auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_heartbeat).count() >= 1000) {
            SendHeartbeat();
            last_heartbeat = now;
        }

        TwistCmd cmd = {};
        bool fresh = false;

        {
            std::lock_guard<std::mutex> lock(ctx->twist_mutex);
            if (ctx->twist_valid.load()) {
                const uint32_t age_ms = static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now - ctx->latest_twist_time).count());
                if (age_ms <= timeout_ms) {
                    cmd = ctx->latest_twist;
                    fresh = true;
                }
            }
        }

        if (!SendTwistSetpoint(cmd, fresh)) {
            Logger(WARN, "Perdida conexion MAVLink serial. Reintentando...");
            CloseSerial();
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(period_ms > 0 ? period_ms : 1));
    }
}

} // namespace rsapp
