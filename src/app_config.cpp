#include "app_config.h"

#include <cstdio>
#include <cstring>

#include "app_constants.h"
#include "file_parser.h"
#include "logger.h"
#include "parser.h"

namespace rsapp {
namespace {

void RegisterStringParam(char *buffer, size_t size, const char *name, const char *default_value, const char *desc) {
    // `ParamStr` escribe sobre la direccion registrada, asi que le pasamos el
    // propio buffer y restauramos el valor por defecto tras el registro.
    std::memset(buffer, 0, size);
    std::snprintf(buffer, size, "%s", default_value);
    ParamStr(reinterpret_cast<char **>(buffer), name, false, default_value, desc);
    std::snprintf(buffer, size, "%s", default_value);
}

const char *BoolText(bool value) {
    return value ? "true" : "false";
}

} // namespace

bool LoadAppConfig(int argc, char **argv, AppConfig *config) {
    if (config == nullptr) {
        return false;
    }

    char **filename = FlagStr("file", false, "params/config.txt", "Path to the config file");
    if (!FlagParse(argc, argv)) {
        FlagPrintHelp(stdout);
        return false;
    }

    *config = AppConfig();

    ParamBool(&config->debug, "DEBUG", false, false, "Use BAG_FILE instead of a live RealSense device");
    RegisterStringParam(config->bag_file, sizeof(config->bag_file), "BAG_FILE", "../bags/realsense_session.bag", "Bag file used for debug playback or live recording");
    ParamUint(&config->width, "WIDTH", false, constants::kDefaultWidth, "Color and depth width");
    ParamUint(&config->height, "HEIGHT", false, constants::kDefaultHeight, "Color and depth height");
    ParamUint(&config->fps, "FPS", false, constants::kDefaultFps, "Color and depth FPS");
    ParamBool(&config->record_bag, "RECORD_BAG", false, false, "Record live RealSense streams to BAG_FILE");
    ParamBool(&config->control_enabled, "CONTROL_ENABLED", false, true, "Enable OrangeCube control by UDP twist + MAVLink UART");
    RegisterStringParam(config->fcu_dev, sizeof(config->fcu_dev), "FCU_DEV", "/dev/ttyACM0", "OrangeCube serial device");
    ParamUint(&config->fcu_baud, "FCU_BAUD", false, 115200, "OrangeCube serial baudrate");
    ParamUint(&config->ctrl_timeout_ms, "CTRL_TIMEOUT_MS", false, constants::kDefaultControlTimeoutMs, "Twist watchdog timeout in ms");
    ParamUint(&config->mav_target_system, "MAV_TARGET_SYSTEM", false, 1, "OrangeCube MAVLink target system id");
    ParamUint(&config->mav_target_component, "MAV_TARGET_COMPONENT", false, 1, "OrangeCube MAVLink target component id");
    ParamBool(&config->ctrl_ros_convention, "CTRL_ROS_CONVENTION", false, true, "Interpret incoming twist with ROS body convention");

    if (!ParamParse(*filename, FILE_TYPE_TXT)) {
        ParamPrintError(stdout);
        return false;
    }

    if (config->debug && config->record_bag) {
        Logger(WARN, "RECORD_BAG se ignora cuando DEBUG=true");
    }

    return true;
}

void LogAppConfig(const AppConfig &config) {
    Logger(
        INFO,
        "Config: DEBUG=%s BAG_FILE=%s RECORD_BAG=%s VIDEO=%ux%u@%u CONTROL=%s FCU=%s@%u TARGET=%u/%u",
        BoolText(config.debug),
        config.bag_file,
        BoolText(config.record_bag),
        config.width,
        config.height,
        config.fps,
        BoolText(config.control_enabled),
        config.fcu_dev,
        config.fcu_baud,
        config.mav_target_system,
        config.mav_target_component
    );
    Logger(INFO, "RTSP fijo en rtsp://<jetson-ip>:%u/%s", constants::kRtspPort, constants::kRtspMount);
}

} // namespace rsapp
