#include "app_config.h"

#include <limits.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

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

bool IsAbsolutePath(const char *path) {
    return path != nullptr && path[0] == '/';
}

std::string DirName(const std::string &path) {
    const std::string::size_type slash = path.find_last_of('/');
    if (slash == std::string::npos) {
        return ".";
    }
    if (slash == 0) {
        return "/";
    }
    return path.substr(0, slash);
}

std::string JoinPath(const std::string &base, const std::string &path) {
    if (base.empty() || path.empty()) {
        return path;
    }
    if (base.back() == '/') {
        return base + path;
    }
    return base + "/" + path;
}

std::string CanonicalizeIfExists(const std::string &path) {
    char resolved[PATH_MAX] = {};
    if (realpath(path.c_str(), resolved) != nullptr) {
        return resolved;
    }
    return path;
}

bool PathExists(const std::string &path) {
    return access(path.c_str(), F_OK) == 0;
}

std::string GetCurrentDir() {
    char cwd[PATH_MAX] = {};
    if (getcwd(cwd, sizeof(cwd)) != nullptr) {
        return cwd;
    }
    return ".";
}

std::string GetExecutableDir() {
    char exe_path[PATH_MAX] = {};
    const ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len <= 0) {
        return GetCurrentDir();
    }

    exe_path[len] = '\0';
    return DirName(exe_path);
}

bool ResolveConfigPath(const char *requested, std::string *resolved_path) {
    if (requested == nullptr || requested[0] == '\0' || resolved_path == nullptr) {
        return false;
    }

    const std::string requested_path(requested);
    std::vector<std::string> candidates;

    if (IsAbsolutePath(requested)) {
        candidates.push_back(requested_path);
    } else {
        const std::string exe_dir = GetExecutableDir();
        const std::string repo_dir = DirName(exe_dir);

        candidates.push_back(requested_path);
        candidates.push_back(JoinPath(exe_dir, requested_path));
        if (repo_dir != exe_dir) {
            candidates.push_back(JoinPath(repo_dir, requested_path));
        }
    }

    for (size_t i = 0; i < candidates.size(); ++i) {
        if (PathExists(candidates[i])) {
            *resolved_path = CanonicalizeIfExists(candidates[i]);
            return true;
        }
    }

    Logger(ERROR, "No se encontro el archivo de configuracion solicitado: %s", requested);
    for (size_t i = 0; i < candidates.size(); ++i) {
        Logger(ERROR, "  ruta probada[%zu]: %s", i, candidates[i].c_str());
    }
    return false;
}

void ResolveRelativePathInPlace(char *buffer, size_t size, const std::string &base_dir) {
    if (buffer == nullptr || size == 0 || buffer[0] == '\0' || IsAbsolutePath(buffer)) {
        return;
    }

    const std::string resolved = JoinPath(base_dir, buffer);
    std::snprintf(buffer, size, "%s", resolved.c_str());
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

    std::string config_path;
    if (!ResolveConfigPath(*filename, &config_path)) {
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

    if (!ParamParse(config_path.c_str(), FILE_TYPE_TXT)) {
        Logger(ERROR, "No se pudo parsear el archivo de configuracion: %s", config_path.c_str());
        ParamPrintError(stdout);
        return false;
    }

    ResolveRelativePathInPlace(config->bag_file, sizeof(config->bag_file), DirName(config_path));
    Logger(INFO, "Config cargada desde: %s", config_path.c_str());

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
