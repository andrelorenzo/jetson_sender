#include <gst/gst.h>

#include <thread>

#define PARSER_IMP
#include "file_parser.h"
#include "parser.h"

#define ETHCOMMS_IMP
#include "eth_comms.h"

#define LOGGER_IMP
#include "logger.h"

#include "app_config.h"
#include "client_transport.h"
#include "orange_cube_controller.h"
#include "realsense_capture.h"
#include "video_streamer.h"

int main(int argc, char **argv) {
    gst_init(&argc, &argv);
    LoggerSetVerbsity(INFO);
    CommsLogSetVerbosity(COMMS_WARN);

    rsapp::AppContext app;
    if (!rsapp::LoadAppConfig(argc, argv, &app.config)) {
        return -1;
    }

    rsapp::LogAppConfig(app.config);

    std::thread realsense_thread(rsapp::RunRealsenseCapture, &app);
    std::thread imu_thread(rsapp::RunImuPublisher, &app);
    std::thread control_thread(rsapp::RunOrangeCubeControl, &app);

    rsapp::RunRtspServer(&app);

    realsense_thread.join();
    imu_thread.join();
    control_thread.join();
    return 0;
}
