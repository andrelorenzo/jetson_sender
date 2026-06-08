#include "video_streamer.h"

#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-server.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "app_constants.h"
#include "app_support.h"
#include "logger.h"

#ifdef USE_JETSON_HW
#define USE_HW_ENC 1
#else
#define USE_HW_ENC 0
#endif

namespace rsapp {
namespace {

struct AppRtspFactory {
    GstRTSPMediaFactory parent;
    AppContext *ctx;
};

struct AppRtspFactoryClass {
    GstRTSPMediaFactoryClass parent_class;
};

G_DEFINE_TYPE(AppRtspFactory, app_rtsp_factory, GST_TYPE_RTSP_MEDIA_FACTORY);

void AppendBe32(std::vector<uint8_t> &out, uint32_t value) {
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(value & 0xFF));
}

void AppendBe64(std::vector<uint8_t> &out, uint64_t value) {
    out.push_back(static_cast<uint8_t>((value >> 56) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 48) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 40) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 32) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(value & 0xFF));
}

void AppendEscapedRbsp(std::vector<uint8_t> &out, const std::vector<uint8_t> &rbsp) {
    int zero_count = 0;
    for (size_t i = 0; i < rbsp.size(); ++i) {
        const uint8_t byte = rbsp[i];
        if (zero_count >= 2 && byte <= 0x03) {
            out.push_back(0x03);
            zero_count = 0;
        }
        out.push_back(byte);
        zero_count = byte == 0x00 ? zero_count + 1 : 0;
    }
}

std::vector<uint8_t> BuildRealSenseSei(uint64_t camera_ts_us, uint32_t seq, uint32_t ts_domain) {
    static const uint8_t uuid[16] = {0x2D, 0x4F, 0x31, 0x8A, 0xB6, 0x34, 0x4F, 0x72, 0x9A, 0x42, 0x52, 0x53, 0x54, 0x53, 0x00, 0x01};

    std::vector<uint8_t> payload;
    payload.insert(payload.end(), uuid, uuid + sizeof(uuid));
    AppendBe64(payload, camera_ts_us);
    AppendBe32(payload, seq);
    AppendBe32(payload, ts_domain);

    std::vector<uint8_t> rbsp;
    rbsp.push_back(5);
    rbsp.push_back(static_cast<uint8_t>(payload.size()));
    rbsp.insert(rbsp.end(), payload.begin(), payload.end());
    rbsp.push_back(0x80);

    std::vector<uint8_t> nal;
    nal.push_back(0x00);
    nal.push_back(0x00);
    nal.push_back(0x00);
    nal.push_back(0x01);
    nal.push_back(0x06);
    AppendEscapedRbsp(nal, rbsp);
    return nal;
}

size_t StartCodeLengthAt(const uint8_t *data, size_t size, size_t pos) {
    if (pos + 3 <= size && data[pos] == 0x00 && data[pos + 1] == 0x00 && data[pos + 2] == 0x01) {
        return 3;
    }
    if (pos + 4 <= size && data[pos] == 0x00 && data[pos + 1] == 0x00 && data[pos + 2] == 0x00 && data[pos + 3] == 0x01) {
        return 4;
    }
    return 0;
}

size_t FindNextStartCode(const uint8_t *data, size_t size, size_t pos) {
    for (size_t i = pos; i + 3 < size; ++i) {
        if (data[i] == 0x00 && data[i + 1] == 0x00 && data[i + 2] == 0x01) {
            return i;
        }
        if (i + 4 < size && data[i] == 0x00 && data[i + 1] == 0x00 && data[i + 2] == 0x00 && data[i + 3] == 0x01) {
            return i;
        }
    }
    return size;
}

size_t H264SeiInsertOffset(const uint8_t *data, size_t size) {
    const size_t start_code_len = StartCodeLengthAt(data, size, 0);
    if (start_code_len == 0 || size <= start_code_len) {
        return 0;
    }
    const uint8_t nal_type = data[start_code_len] & 0x1F;
    if (nal_type == 9) {
        return FindNextStartCode(data, size, start_code_len + 1);
    }
    return 0;
}

void EraseVideoTimestamp(AppContext *ctx, guint64 pts) {
    std::lock_guard<std::mutex> lock(ctx->video_ts_mutex);
    std::map<guint64, VideoStampInfo>::iterator it = ctx->video_ts_map.find(pts);
    if (it != ctx->video_ts_map.end()) {
        ctx->video_ts_map.erase(it);
    }
}

void RegisterVideoTimestamp(AppContext *ctx, guint64 pts, uint64_t camera_ts_us, uint32_t seq, uint32_t ts_domain) {
    std::lock_guard<std::mutex> lock(ctx->video_ts_mutex);

    VideoStampInfo stamp = {};
    stamp.camera_ts_us = camera_ts_us;
    stamp.seq = seq;
    stamp.ts_domain = ts_domain;
    ctx->video_ts_map[pts] = stamp;

    while (ctx->video_ts_map.size() > constants::kMaxVideoTimestampMap) {
        ctx->video_ts_map.erase(ctx->video_ts_map.begin());
    }
}

GstPadProbeReturn H264SeiProbe(GstPad *pad, GstPadProbeInfo *info, gpointer user_data) {
    (void)pad;

    AppContext *ctx = static_cast<AppContext *>(user_data);
    if (ctx == nullptr || !(GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_BUFFER)) {
        return GST_PAD_PROBE_OK;
    }

    GstBuffer *old_buffer = GST_PAD_PROBE_INFO_BUFFER(info);
    if (old_buffer == nullptr || !GST_CLOCK_TIME_IS_VALID(GST_BUFFER_PTS(old_buffer))) {
        return GST_PAD_PROBE_OK;
    }

    VideoStampInfo stamp = {};
    bool found = false;
    {
        std::lock_guard<std::mutex> lock(ctx->video_ts_mutex);
        std::map<guint64, VideoStampInfo>::iterator it = ctx->video_ts_map.find(static_cast<guint64>(GST_BUFFER_PTS(old_buffer)));
        if (it != ctx->video_ts_map.end()) {
            stamp = it->second;
            ctx->video_ts_map.erase(it);
            found = true;
        }
    }

    if (!found) {
        return GST_PAD_PROBE_OK;
    }

    GstMapInfo old_map = {};
    if (!gst_buffer_map(old_buffer, &old_map, GST_MAP_READ)) {
        return GST_PAD_PROBE_OK;
    }

    const std::vector<uint8_t> sei = BuildRealSenseSei(stamp.camera_ts_us, stamp.seq, stamp.ts_domain);
    const size_t insert_offset = H264SeiInsertOffset(old_map.data, old_map.size);

    GstBuffer *new_buffer = gst_buffer_new_allocate(nullptr, old_map.size + sei.size(), nullptr);
    if (new_buffer == nullptr) {
        gst_buffer_unmap(old_buffer, &old_map);
        return GST_PAD_PROBE_OK;
    }

    gst_buffer_copy_into(
        new_buffer,
        old_buffer,
        static_cast<GstBufferCopyFlags>(GST_BUFFER_COPY_FLAGS | GST_BUFFER_COPY_TIMESTAMPS | GST_BUFFER_COPY_META),
        0,
        -1
    );

    GstMapInfo new_map = {};
    if (!gst_buffer_map(new_buffer, &new_map, GST_MAP_WRITE)) {
        gst_buffer_unmap(old_buffer, &old_map);
        gst_buffer_unref(new_buffer);
        return GST_PAD_PROBE_OK;
    }

    std::memcpy(new_map.data, old_map.data, insert_offset);
    std::memcpy(new_map.data + insert_offset, sei.data(), sei.size());
    std::memcpy(new_map.data + insert_offset + sei.size(), old_map.data + insert_offset, old_map.size - insert_offset);

    gst_buffer_unmap(new_buffer, &new_map);
    gst_buffer_unmap(old_buffer, &old_map);
    gst_buffer_unref(old_buffer);
    GST_PAD_PROBE_INFO_DATA(info) = new_buffer;
    return GST_PAD_PROBE_OK;
}

GstElement *CreateRtspBin(AppContext *ctx) {
    GstElement *bin = gst_bin_new("rtsp-bin");
    GstElement *src = gst_element_factory_make("appsrc", "rs_src");
    GstElement *queue = gst_element_factory_make("queue", "q");
    GstElement *videoconvert = gst_element_factory_make("videoconvert", "videoconvert");
    GstElement *caps_i420 = gst_element_factory_make("capsfilter", "caps_i420");
#if USE_HW_ENC
    GstElement *nvvidconv = gst_element_factory_make("nvvidconv", "nvvidconv");
    GstElement *caps_nvmm = gst_element_factory_make("capsfilter", "caps_nvmm");
    GstElement *encoder = gst_element_factory_make("nvv4l2h264enc", "encoder");
#else
    GstElement *encoder = gst_element_factory_make("x264enc", "encoder");
#endif
    GstElement *parser = gst_element_factory_make("h264parse", "parser");
    GstElement *caps_h264 = gst_element_factory_make("capsfilter", "caps_h264");
    GstElement *payloader = gst_element_factory_make("rtph264pay", "pay0");

    GstCaps *source_caps = nullptr;
    GstCaps *i420_caps = nullptr;
    GstCaps *nvmm_caps = nullptr;
    GstCaps *h264_caps = nullptr;
    GstCapsFeatures *features = nullptr;

#if USE_HW_ENC
    if (!bin || !src || !queue || !videoconvert || !caps_i420 || !nvvidconv || !caps_nvmm || !encoder || !parser || !caps_h264 || !payloader) {
        goto fail;
    }
#else
    if (!bin || !src || !queue || !videoconvert || !caps_i420 || !encoder || !parser || !caps_h264 || !payloader) {
        goto fail;
    }
#endif

    source_caps = gst_caps_new_simple(
        "video/x-raw",
        "format", G_TYPE_STRING, "RGB",
        "width", G_TYPE_INT, static_cast<int>(ctx->config.width),
        "height", G_TYPE_INT, static_cast<int>(ctx->config.height),
        "framerate", GST_TYPE_FRACTION, static_cast<int>(ctx->config.fps), 1,
        NULL
    );
    if (source_caps == nullptr) {
        goto fail;
    }

    g_object_set(G_OBJECT(src), "caps", source_caps, "is-live", TRUE, "format", GST_FORMAT_TIME, "do-timestamp", FALSE, "block", TRUE, NULL);
    gst_caps_unref(source_caps);
    source_caps = nullptr;

    {
        std::lock_guard<std::mutex> lock(ctx->rtsp_mutex);
        if (ctx->appsrc != nullptr) {
            gst_object_unref(ctx->appsrc);
            ctx->appsrc = nullptr;
        }
        ctx->appsrc = GST_APP_SRC(gst_object_ref(src));
        ctx->rtsp_pts = 0;
        ctx->rtsp_duration = gst_util_uint64_scale_int(1, GST_SECOND, static_cast<int>(ctx->config.fps));
        ctx->video_seq.store(0);
    }

    {
        std::lock_guard<std::mutex> lock(ctx->video_ts_mutex);
        ctx->video_ts_map.clear();
    }

    i420_caps = gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, "I420", NULL);
    if (i420_caps == nullptr) {
        goto fail;
    }
    g_object_set(G_OBJECT(caps_i420), "caps", i420_caps, NULL);
    gst_caps_unref(i420_caps);
    i420_caps = nullptr;

    h264_caps = gst_caps_new_simple("video/x-h264", "stream-format", G_TYPE_STRING, "byte-stream", "alignment", G_TYPE_STRING, "au", NULL);
    if (h264_caps == nullptr) {
        goto fail;
    }
    g_object_set(G_OBJECT(caps_h264), "caps", h264_caps, NULL);
    gst_caps_unref(h264_caps);
    h264_caps = nullptr;

#if USE_HW_ENC
    nvmm_caps = gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, "NV12", NULL);
    if (nvmm_caps == nullptr) {
        goto fail;
    }
    features = gst_caps_features_new("memory:NVMM", NULL);
    gst_caps_set_features(nvmm_caps, 0, features);
    g_object_set(G_OBJECT(caps_nvmm), "caps", nvmm_caps, NULL);
    gst_caps_unref(nvmm_caps);
    nvmm_caps = nullptr;

    g_object_set(
        G_OBJECT(encoder),
        "insert-sps-pps", TRUE,
        "bitrate", constants::kRtspBitrate,
        "iframeinterval", static_cast<int>(ctx->config.fps),
        "idrinterval", static_cast<int>(ctx->config.fps),
        NULL
    );
#else
    g_object_set(
        G_OBJECT(encoder),
        "tune", 4,
        "speed-preset", 1,
        "key-int-max", static_cast<int>(ctx->config.fps),
        "bframes", 0,
        "byte-stream", TRUE,
        NULL
    );
#endif

    g_object_set(G_OBJECT(payloader), "pt", 96, "config-interval", 1, NULL);

#if USE_HW_ENC
    gst_bin_add_many(GST_BIN(bin), src, queue, videoconvert, caps_i420, nvvidconv, caps_nvmm, encoder, parser, caps_h264, payloader, NULL);
    if (!gst_element_link_many(src, queue, videoconvert, caps_i420, nvvidconv, caps_nvmm, encoder, parser, caps_h264, payloader, NULL)) {
        goto fail;
    }
#else
    gst_bin_add_many(GST_BIN(bin), src, queue, videoconvert, caps_i420, encoder, parser, caps_h264, payloader, NULL);
    if (!gst_element_link_many(src, queue, videoconvert, caps_i420, encoder, parser, caps_h264, payloader, NULL)) {
        goto fail;
    }
#endif

    {
        GstPad *pad = gst_element_get_static_pad(caps_h264, "src");
        if (pad != nullptr) {
            gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BUFFER, H264SeiProbe, ctx, NULL);
            gst_object_unref(pad);
        }
    }

    return bin;

fail:
    if (source_caps != nullptr) gst_caps_unref(source_caps);
    if (i420_caps != nullptr) gst_caps_unref(i420_caps);
    if (nvmm_caps != nullptr) gst_caps_unref(nvmm_caps);
    if (h264_caps != nullptr) gst_caps_unref(h264_caps);
    {
        std::lock_guard<std::mutex> lock(ctx->rtsp_mutex);
        if (ctx->appsrc != nullptr) {
            gst_object_unref(ctx->appsrc);
            ctx->appsrc = nullptr;
        }
    }
    if (bin != nullptr) gst_object_unref(bin);
    Logger(ERROR, "RTSP: error creando pipeline H264");
    return nullptr;
}

GstElement *AppRtspFactoryCreateElement(GstRTSPMediaFactory *factory, const GstRTSPUrl *url) {
    (void)url;
    return CreateRtspBin(reinterpret_cast<AppRtspFactory *>(factory)->ctx);
}

void app_rtsp_factory_class_init(AppRtspFactoryClass *klass) {
    GstRTSPMediaFactoryClass *factory_class = GST_RTSP_MEDIA_FACTORY_CLASS(klass);
    factory_class->create_element = AppRtspFactoryCreateElement;
}

void app_rtsp_factory_init(AppRtspFactory *self) {
    self->ctx = nullptr;
}

} // namespace

void PushColorFrame(AppContext *ctx, const rs2::video_frame &frame, uint64_t camera_ts_us, uint32_t ts_domain) {
    if (ctx == nullptr || !ctx->client_connected.load()) {
        return;
    }

    const int width = frame.get_width();
    const int height = frame.get_height();
    if (width != static_cast<int>(ctx->config.width) || height != static_cast<int>(ctx->config.height)) {
        static bool logged_bad_size = false;
        if (!logged_bad_size) {
            Logger(WARN, "COLOR descartado: tamano %dx%d distinto de RTSP %ux%u", width, height, ctx->config.width, ctx->config.height);
            logged_bad_size = true;
        }
        return;
    }

    const rs2_format format = frame.get_profile().format();
    if (format != RS2_FORMAT_RGB8 && format != RS2_FORMAT_BGR8) {
        static bool logged_bad_format = false;
        if (!logged_bad_format) {
            Logger(WARN, "COLOR descartado: formato %d no soportado", static_cast<int>(format));
            logged_bad_format = true;
        }
        return;
    }

    GstAppSrc *appsrc = nullptr;
    guint64 pts = 0;
    guint64 duration = 0;
    const uint32_t seq = ctx->video_seq.fetch_add(1) + 1;

    {
        std::lock_guard<std::mutex> lock(ctx->rtsp_mutex);
        appsrc = ctx->appsrc != nullptr ? GST_APP_SRC(gst_object_ref(ctx->appsrc)) : nullptr;
        if (ctx->rtsp_duration == 0) {
            ctx->rtsp_duration = gst_util_uint64_scale_int(1, GST_SECOND, static_cast<int>(ctx->config.fps));
        }
        duration = ctx->rtsp_duration;
        pts = ctx->rtsp_pts;
        ctx->rtsp_pts += duration;
    }

    if (appsrc == nullptr) {
        return;
    }

    const size_t bytes = static_cast<size_t>(width) * static_cast<size_t>(height) * 3;
    GstBuffer *buffer = gst_buffer_new_allocate(nullptr, bytes, nullptr);
    if (buffer == nullptr) {
        gst_object_unref(appsrc);
        return;
    }

    GST_BUFFER_PTS(buffer) = pts;
    GST_BUFFER_DTS(buffer) = pts;
    GST_BUFFER_DURATION(buffer) = duration;
    GST_BUFFER_OFFSET(buffer) = seq;
    GST_BUFFER_OFFSET_END(buffer) = camera_ts_us;

    GstMapInfo map = {};
    if (!gst_buffer_map(buffer, &map, GST_MAP_WRITE)) {
        gst_buffer_unref(buffer);
        gst_object_unref(appsrc);
        return;
    }

    const uint8_t *src = static_cast<const uint8_t *>(frame.get_data());
    if (format == RS2_FORMAT_RGB8) {
        std::memcpy(map.data, src, bytes);
    } else {
        const size_t pixel_count = static_cast<size_t>(width) * static_cast<size_t>(height);
        for (size_t i = 0; i < pixel_count; ++i) {
            map.data[3 * i + 0] = src[3 * i + 2];
            map.data[3 * i + 1] = src[3 * i + 1];
            map.data[3 * i + 2] = src[3 * i + 0];
        }
    }

    gst_buffer_unmap(buffer, &map);
    RegisterVideoTimestamp(ctx, pts, camera_ts_us, seq, ts_domain);

    const GstFlowReturn result = gst_app_src_push_buffer(appsrc, buffer);
    if (result != GST_FLOW_OK) {
        EraseVideoTimestamp(ctx, pts);
        Logger(WARN, "appsrc push_buffer flow=%d", static_cast<int>(result));
        if (result == GST_FLOW_FLUSHING || result == GST_FLOW_EOS) {
            std::lock_guard<std::mutex> lock(ctx->rtsp_mutex);
            if (ctx->appsrc != nullptr) {
                gst_object_unref(ctx->appsrc);
                ctx->appsrc = nullptr;
            }
        }
    }

    gst_object_unref(appsrc);
}

void RunRtspServer(AppContext *ctx) {
    if (ctx == nullptr) {
        return;
    }

    GMainLoop *loop = g_main_loop_new(nullptr, FALSE);
    GstRTSPServer *server = gst_rtsp_server_new();
    GstRTSPMountPoints *mounts = server != nullptr ? gst_rtsp_server_get_mount_points(server) : nullptr;
    if (loop == nullptr || server == nullptr || mounts == nullptr) {
        Logger(ERROR, "RTSP: no se pudieron crear los objetos base");
        if (mounts != nullptr) g_object_unref(mounts);
        if (server != nullptr) g_object_unref(server);
        if (loop != nullptr) g_main_loop_unref(loop);
        return;
    }

    char port[16] = {};
    std::snprintf(port, sizeof(port), "%u", constants::kRtspPort);
    gst_rtsp_server_set_service(server, port);

    AppRtspFactory *factory = reinterpret_cast<AppRtspFactory *>(g_object_new(app_rtsp_factory_get_type(), NULL));
    factory->ctx = ctx;
    gst_rtsp_media_factory_set_shared(GST_RTSP_MEDIA_FACTORY(factory), TRUE);

    const std::string mount_path = std::string("/") + constants::kRtspMount;
    gst_rtsp_mount_points_add_factory(mounts, mount_path.c_str(), GST_RTSP_MEDIA_FACTORY(factory));
    g_object_unref(mounts);

    if (gst_rtsp_server_attach(server, nullptr) == 0) {
        Logger(ERROR, "RTSP: gst_rtsp_server_attach() fallo");
        g_main_loop_unref(loop);
        g_object_unref(server);
        return;
    }

    Logger(INFO, "RTSP server listo en rtsp://%s:%u/%s", DetectLocalIpv4().c_str(), constants::kRtspPort, constants::kRtspMount);

    g_main_loop_run(loop);
    g_main_loop_unref(loop);
    g_object_unref(server);
}

} // namespace rsapp
