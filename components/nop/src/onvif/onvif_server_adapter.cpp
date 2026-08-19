/**
 * @file onvif_server_adapter.cpp
 * @brief Calling layer over the vendored Happytimesoft ONVIF *server* library
 *        (Profile S subset). Compiled as C++; exposes a pure C ABI
 *        (nop_sdk/nop_onvif_server.h). Vendored sources in
 *        third_party/onvif_server/ are used UNMODIFIED — this file only renders
 *        the library's native configuration and drives its lifecycle.
 *
 * Strategy: the library is configuration-driven (it normally parses an
 * onvif.cfg XML file). Rather than ship a file, the adapter renders an
 * equivalent cfg buffer from nop_onvif_server_config_t and feeds it through the
 * library's own tested parse path:
 *     network_init(); sys_buf_init(); http_msg_buf_init();
 *     onvif_init_def_cfg(); onvif_parse_cfg(buf,len); onvif_init_cfg();
 *     onvif_start1();
 * Teardown is onvif_stop(). The vendored RTSP server is compiled out
 * (NO_RTSP_SERVER): this is the ONVIF control plane only.
 */
#include "nop_sdk/nop_onvif_server.h"
#include "nop_sdk/nop_event.h"
#include "nop_sdk/hal/hal_registry.h"
#include "nop_sdk/hal/hal_system.h"

/* Vendored ONVIF server library headers (third_party/onvif_server). */
extern "C" {
#include "sys_inc.h"
#include "sys_buf.h"
#include "util.h"
#include "http_parse.h"
#include "onvif.h"
#include "onvif_cfg.h"
#include "onvif_srv.h"
#include "onvif_event.h"
}

#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

/* Buffer-pool sizing. The vendored default keys off MAX_NUM_RUA (an RTSP
 * concept absent from the control-only build); 64 message buffers is ample for
 * the SOAP services + discovery. */
#ifndef NOP_ONVIF_SERVER_BUF_NUMS
#  define NOP_ONVIF_SERVER_BUF_NUMS 64
#endif

static int g_server_running = 0;

/* Stable tokens for the PTZ node/configuration the adapter advertises. */
#define NOP_ONVIF_PTZ_NODE_TOKEN "PTZNODE_000"
#define NOP_ONVIF_PTZ_CFG_TOKEN  "PTZCFG_000"

void nop_onvif_server_config_init(nop_onvif_server_config_t *config)
{
    if (!config)
        return;
    memset(config, 0, sizeof(*config));
    strcpy(config->manufacturer,     "NightOwl");
    strcpy(config->model,            "NOP-ONVIF-Camera");
    strcpy(config->firmware_version, "1.0");
    strcpy(config->serial_number,    "000000000000");
    strcpy(config->hardware_id,      "1.0");
    config->server_ip[0] = '\0';      /* auto-detect */
    config->http_port    = 8080;
    config->need_auth    = 1;
    strcpy(config->username, "admin");
    strcpy(config->password, "admin");

    /* Two default profiles: main 1080p + sub VGA. */
    config->profile_count = 2;
    strcpy(config->profiles[0].name, "MainStream");
    config->profiles[0].video_width  = 1920;
    config->profiles[0].video_height = 1080;
    config->profiles[0].framerate    = 25;
    config->profiles[0].bitrate_kbps = 4096;
    config->profiles[0].encoding     = NOP_ONVIF_SERVER_ENCODING_H264;
    strcpy(config->profiles[1].name, "SubStream");
    config->profiles[1].video_width  = 640;
    config->profiles[1].video_height = 480;
    config->profiles[1].framerate    = 15;
    config->profiles[1].bitrate_kbps = 512;
    config->profiles[1].encoding     = NOP_ONVIF_SERVER_ENCODING_H264;

    config->has_ptz = 0;
    config->scope_name[0] = '\0';     /* defaults to model below */
    config->scope_location[0] = '\0';
}

/* Copy @p src into @p dst (capacity @p cap) only when src is non-empty. */
static void copy_if_set(char *dst, int cap, const char *src)
{
    if (src && src[0] && cap > 0) {
        int i = 0;
        for (; src[i] && i < cap - 1; i++)
            dst[i] = src[i];
        dst[i] = '\0';
    }
}

int nop_onvif_server_config_from_hal(nop_onvif_server_config_t *config)
{
    const hal_system_if *system_hal;
    hal_device_info_t    device_info;

    if (!config)
        return -1;
    system_hal = (const hal_system_if *)hal_registry_get(HAL_SYSTEM);
    if (!system_hal || !system_hal->get_device_info)
        return -2;
    if (system_hal->get_device_info(system_hal->ctx, &device_info) != NOP_OK)
        return -3;

    /* DeviceInformation identity only — exactly what getDeviceInfo reports.
     * Deliberately NOT mapped: device IP (server auto-detects the bind IP; a
     * HAL-reported IP may not be locally bindable) and device name (may contain
     * spaces, which are invalid in an ONVIF scope URI). */
    copy_if_set(config->model,            sizeof(config->model),            device_info.model);
    copy_if_set(config->firmware_version, sizeof(config->firmware_version), device_info.firmware_version);
    copy_if_set(config->serial_number,    sizeof(config->serial_number),    device_info.serial_number);
    copy_if_set(config->hardware_id,      sizeof(config->hardware_id),      device_info.type);
    return 0;
}

/* Append a full PTZ node + configuration block (SOAP cfg format, mirrors the
 * library's own build_PTZNode_xml / build_PTZConfiguration_xml round-trip).
 * Generic ONVIF spaces; pan/tilt normalized [-1,1], zoom [0,1]. Must be emitted
 * BEFORE <profile> blocks so the profile's PTZConfiguration lookup resolves. */
static int render_ptz_xml(char *out, int cap)
{
    return snprintf(out, cap,
        "  <PTZNodes token=\"%s\">\n"
        "    <tt:Name>PTZNode</tt:Name>\n"
        "    <tt:SupportedPTZSpaces>\n"
        "      <tt:AbsolutePanTiltPositionSpace>\n"
        "        <tt:URI>http://www.onvif.org/ver10/tptz/PanTiltSpaces/PositionGenericSpace</tt:URI>\n"
        "        <tt:XRange><tt:Min>-1.0</tt:Min><tt:Max>1.0</tt:Max></tt:XRange>\n"
        "        <tt:YRange><tt:Min>-1.0</tt:Min><tt:Max>1.0</tt:Max></tt:YRange>\n"
        "      </tt:AbsolutePanTiltPositionSpace>\n"
        "      <tt:AbsoluteZoomPositionSpace>\n"
        "        <tt:URI>http://www.onvif.org/ver10/tptz/ZoomSpaces/PositionGenericSpace</tt:URI>\n"
        "        <tt:XRange><tt:Min>0.0</tt:Min><tt:Max>1.0</tt:Max></tt:XRange>\n"
        "      </tt:AbsoluteZoomPositionSpace>\n"
        "      <tt:RelativePanTiltTranslationSpace>\n"
        "        <tt:URI>http://www.onvif.org/ver10/tptz/PanTiltSpaces/TranslationGenericSpace</tt:URI>\n"
        "        <tt:XRange><tt:Min>-1.0</tt:Min><tt:Max>1.0</tt:Max></tt:XRange>\n"
        "        <tt:YRange><tt:Min>-1.0</tt:Min><tt:Max>1.0</tt:Max></tt:YRange>\n"
        "      </tt:RelativePanTiltTranslationSpace>\n"
        "      <tt:RelativeZoomTranslationSpace>\n"
        "        <tt:URI>http://www.onvif.org/ver10/tptz/ZoomSpaces/TranslationGenericSpace</tt:URI>\n"
        "        <tt:XRange><tt:Min>-1.0</tt:Min><tt:Max>1.0</tt:Max></tt:XRange>\n"
        "      </tt:RelativeZoomTranslationSpace>\n"
        "      <tt:ContinuousPanTiltVelocitySpace>\n"
        "        <tt:URI>http://www.onvif.org/ver10/tptz/PanTiltSpaces/VelocityGenericSpace</tt:URI>\n"
        "        <tt:XRange><tt:Min>-1.0</tt:Min><tt:Max>1.0</tt:Max></tt:XRange>\n"
        "        <tt:YRange><tt:Min>-1.0</tt:Min><tt:Max>1.0</tt:Max></tt:YRange>\n"
        "      </tt:ContinuousPanTiltVelocitySpace>\n"
        "      <tt:ContinuousZoomVelocitySpace>\n"
        "        <tt:URI>http://www.onvif.org/ver10/tptz/ZoomSpaces/VelocityGenericSpace</tt:URI>\n"
        "        <tt:XRange><tt:Min>-1.0</tt:Min><tt:Max>1.0</tt:Max></tt:XRange>\n"
        "      </tt:ContinuousZoomVelocitySpace>\n"
        "    </tt:SupportedPTZSpaces>\n"
        "    <tt:MaximumNumberOfPresets>32</tt:MaximumNumberOfPresets>\n"
        "    <tt:HomeSupported>true</tt:HomeSupported>\n"
        "  </PTZNodes>\n"
        "  <PTZConfigurations token=\"%s\" MoveRamp=\"0\" PresetRamp=\"0\" PresetTourRamp=\"0\">\n"
        "    <tt:Name>PTZConfig</tt:Name>\n"
        "    <tt:UseCount>1</tt:UseCount>\n"
        "    <tt:NodeToken>%s</tt:NodeToken>\n"
        "    <tt:DefaultContinuousPanTiltVelocitySpace>http://www.onvif.org/ver10/tptz/PanTiltSpaces/VelocityGenericSpace</tt:DefaultContinuousPanTiltVelocitySpace>\n"
        "    <tt:DefaultContinuousZoomVelocitySpace>http://www.onvif.org/ver10/tptz/ZoomSpaces/VelocityGenericSpace</tt:DefaultContinuousZoomVelocitySpace>\n"
        "    <tt:DefaultPTZTimeout>PT5S</tt:DefaultPTZTimeout>\n"
        "  </PTZConfigurations>\n",
        NOP_ONVIF_PTZ_NODE_TOKEN, NOP_ONVIF_PTZ_CFG_TOKEN, NOP_ONVIF_PTZ_NODE_TOKEN);
}

/* Render the vendored library's native cfg XML from @p c into @p out.
 * Element order matters: information → user → PTZ → profiles → scope/event,
 * because the cfg parser resolves a profile's PTZConfiguration by token, which
 * must already be parsed (see onvif_parse_cfg: PTZConfigurations before profile). */
static int render_config_xml(const nop_onvif_server_config_t *c, char *out, int cap)
{
    const char *scope_name = c->scope_name[0] ? c->scope_name : c->model;
    int profile_count = c->profile_count > 0 ? c->profile_count : 1;
    int n = 0, i;

    if (profile_count > NOP_ONVIF_SERVER_MAX_PROFILES)
        profile_count = NOP_ONVIF_SERVER_MAX_PROFILES;

    n += snprintf(out + n, cap - n,
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<config>\n"
        "  <server_ip>%s</server_ip>\n"
        "  <http_enable>1</http_enable>\n"
        "  <http_port>%d</http_port>\n"
        "  <https_enable>0</https_enable>\n"
        "  <ipv6_enable>0</ipv6_enable>\n"
        "  <need_auth>%d</need_auth>\n"
        "  <log_enable>0</log_enable>\n"
        "  <information>\n"
        "    <Manufacturer>%s</Manufacturer>\n"
        "    <Model>%s</Model>\n"
        "    <FirmwareVersion>%s</FirmwareVersion>\n"
        "    <SerialNumber>%s</SerialNumber>\n"
        "    <HardwareId>%s</HardwareId>\n"
        "  </information>\n"
        "  <user>\n"
        "    <username>%s</username>\n"
        "    <password>%s</password>\n"
        "    <userlevel>Administrator</userlevel>\n"
        "  </user>\n",
        c->server_ip, c->http_port, c->need_auth ? 1 : 0,
        c->manufacturer, c->model, c->firmware_version,
        c->serial_number, c->hardware_id,
        c->username, c->password);

    if (c->has_ptz)
        n += render_ptz_xml(out + n, cap - n);

    for (i = 0; i < profile_count; i++) {
        const nop_onvif_server_profile_t *p = &c->profiles[i];
        const char *encoding = (p->encoding == NOP_ONVIF_SERVER_ENCODING_H265)
                             ? "H265" : "H264";
        int width  = p->video_width  > 0 ? p->video_width  : 1280;
        int height = p->video_height > 0 ? p->video_height : 720;
        n += snprintf(out + n, cap - n,
            "  <profile>\n"
            "    <Name>%s</Name>\n"
            "    <video_source><width>%d</width><height>%d</height></video_source>\n"
            "    <video_encoder>\n"
            "      <width>%d</width><height>%d</height>\n"
            "      <quality>4</quality><session_timeout>10</session_timeout>\n"
            "      <framerate>%d</framerate><encoding_interval>1</encoding_interval>\n"
            "      <bitrate_limit>%d</bitrate_limit>\n"
            "      <encoding>%s</encoding>\n"
            "      <h264><gov_length>25</gov_length><h264_profile>Main</h264_profile></h264>\n"
            "    </video_encoder>\n",
            p->name[0] ? p->name : "Profile",
            width, height, width, height,
            p->framerate > 0 ? p->framerate : 25,
            p->bitrate_kbps > 0 ? p->bitrate_kbps : 2048, encoding);
        /* Bind PTZ to the first profile only. */
        if (c->has_ptz && i == 0)
            n += snprintf(out + n, cap - n,
                "    <PTZConfiguration token=\"%s\"></PTZConfiguration>\n",
                NOP_ONVIF_PTZ_CFG_TOKEN);
        n += snprintf(out + n, cap - n,
            "    <stream_uri append_params=\"0\">%s</stream_uri>\n"
            "  </profile>\n",
            p->stream_uri);
    }

    if (c->scope_location[0])
        n += snprintf(out + n, cap - n,
            "  <scope>onvif://www.onvif.org/location/%s</scope>\n", c->scope_location);
    n += snprintf(out + n, cap - n,
        "  <scope>onvif://www.onvif.org/name/%s</scope>\n"
        "  <scope>onvif://www.onvif.org/hardware/%s</scope>\n"
        "  <event><renew_interval>60</renew_interval><simulate_enable>0</simulate_enable></event>\n"
        "</config>\n",
        scope_name, c->model);

    return n;
}

int nop_onvif_server_start(const nop_onvif_server_config_t *config)
{
    char cfg_xml[16384];
    char *cfg_buf;
    int   len;

    if (!config)
        return -1;
    if (g_server_running)
        return -2;          /* already running */

    len = render_config_xml(config, cfg_xml, (int)sizeof(cfg_xml));
    if (len <= 0 || len >= (int)sizeof(cfg_xml))
        return -3;          /* config too large to render */

    /* Subsystems the server expects up before onvif_start (cf. main.cpp). */
    network_init();
    sys_buf_init(NOP_ONVIF_SERVER_BUF_NUMS);
    http_msg_buf_init(NOP_ONVIF_SERVER_BUF_NUMS);

    /* onvif_parse_cfg may tokenize in place — hand it a mutable copy. */
    cfg_buf = (char *)malloc((size_t)len + 1);
    if (!cfg_buf)
        return -4;
    memcpy(cfg_buf, cfg_xml, (size_t)len);
    cfg_buf[len] = '\0';

    onvif_init_def_cfg();
    if (!onvif_parse_cfg(cfg_buf, len)) {
        free(cfg_buf);
        return -5;
    }
    free(cfg_buf);

    onvif_init_cfg();
    onvif_start1();         /* spins SOAP services + WS-Discovery responder */

    g_server_running = 1;
    return 0;
}

int nop_onvif_server_is_running(void)
{
    return g_server_running;
}

/* ------------------------------------------------------------------------- *
 * ONVIF-events bridge: forward detection-hub events to ONVIF consumers.
 *
 * A subscription on the SDK's nop_event_hub maps each detection to its ONVIF
 * analytics topic and queues a NotificationMessage via the vendored
 * onvif_put_NotificationMessage(), so a client's PullMessages / base-
 * notification subscription receives SDK detections. Confined to this
 * onvif-server-only translation unit.
 * ------------------------------------------------------------------------- */

static nop_event_hub_t          *g_event_hub          = NULL;
static nop_event_subscription_t *g_event_subscription = NULL;
/* Serializes attach/detach of the process-wide event-hub subscription. */
static pthread_mutex_t           g_event_lock         = PTHREAD_MUTEX_INITIALIZER;

/* Map a detection type to its ONVIF analytics topic (tns1: namespace). Motion-
 * family types are delivered via the dedicated CellMotionDetector helper and so
 * are not listed here. @return the topic, or NULL if the type has no ONVIF
 * topic (it is then dropped by the bridge). */
static const char *onvif_topic_for_detection(nop_detect_type_t detection_type)
{
    switch (detection_type) {
    /* ObjectDetection 家族(human/face/vehicle/animal):按 NOPMappingONVIF.md,统一走标准
     * ObjectDetection topic + Data ClassTypes 区分类别,而非厂商私有 per-class topic
     * (与入站 class_to_detect 读 ClassTypes 对称)。类别由 onvif_class_for_detection 给出。 */
    case NOP_DETECT_HUMAN:
    case NOP_DETECT_VEHICLE:
    case NOP_DETECT_ANIMAL:
    case NOP_DETECT_FACE:
    case NOP_DETECT_FACIAL_RECOGNITION:
    case NOP_DETECT_OBJECT_DETECTION:   return "tns1:RuleEngine/ObjectDetector/Object";
    case NOP_DETECT_PACKAGE:            return "tns1:RuleEngine/MyRuleDetector/PackageDetect";
    case NOP_DETECT_LINE_CROSS:         return "tns1:RuleEngine/LineDetector/Crossed";
    case NOP_DETECT_FIELD_INTRUSION:    return "tns1:RuleEngine/FieldDetector/ObjectsInside";
    case NOP_DETECT_DOORBELL_RING:      return "tns1:Device/Trigger/DigitalInput";
    case NOP_DETECT_BABY_CRY:
    case NOP_DETECT_GUN_SHOT:
    case NOP_DETECT_FIRE_ALARM:         return "tns1:AudioAnalytics/Audio/DetectedSound";
    default:                            return NULL;
    }
}

/* ObjectDetection 家族 → ONVIF 官方对象类名(ClassTypes 值);非对象类返回 NULL。 */
static const char *onvif_class_for_detection(nop_detect_type_t detection_type)
{
    switch (detection_type) {
    case NOP_DETECT_HUMAN:              return "Human";
    case NOP_DETECT_VEHICLE:            return "Vehicle";
    case NOP_DETECT_ANIMAL:             return "Animal";
    case NOP_DETECT_FACE:
    case NOP_DETECT_FACIAL_RECOGNITION: return "Face";
    default:                            return NULL;
    }
}

/* Event-hub sink: build and queue the ONVIF NotificationMessage for one
 * detection. Runs on the publisher's thread; guarded so a late event after
 * shutdown is a no-op. */
static void onvif_event_hub_sink(void *sink_context, const nop_event_t *event)
{
    NotificationMessageList *message = NULL;
    const char              *topic;
    char                     source_token[ONVIF_TOKEN_LEN];

    (void)sink_context;
    if (!g_server_running || !event)
        return;

    /* Motion family → the standard CellMotionDetector "IsMotion" message. */
    if (event->type == NOP_DETECT_MOTION ||
        event->type == NOP_DETECT_PIXEL_CHANGE ||
        event->type == NOP_DETECT_PIR) {
        message = onvif_init_MotionDetect_Message(TRUE);
        if (message)
            onvif_put_NotificationMessage(message);
        return;
    }

    topic = onvif_topic_for_detection(event->type);
    if (!topic)
        return;

    if (g_onvif_cfg.v_src_cfg)
        strncpy(source_token, g_onvif_cfg.v_src_cfg->Configuration.token, sizeof(source_token) - 1);
    else
        strncpy(source_token, "VideoSourceConfigToken_1", sizeof(source_token) - 1);
    source_token[sizeof(source_token) - 1] = '\0';

    /* Source item = the video source; Data item State=true = object present.
     * ObjectDetection 家族再带 ClassTypes=官方类名(Human/Vehicle/Animal/Face),
     * 消费端据此区分类别(与入站 class_to_detect 对称)。 */
    {
        const char *cls = onvif_class_for_detection(event->type);
        message = onvif_init_NotificationMessage3(topic, PropertyOperation_Changed,
                      "VideoSourceConfigurationToken", source_token, NULL, NULL,
                      "State", "true", cls ? "ClassTypes" : NULL, cls);
    }
    if (message)
        onvif_put_NotificationMessage(message);
}

/* Detach the current subscription. Caller must hold g_event_lock. */
static void detach_event_hub_locked(void)
{
    if (g_event_hub && g_event_subscription)
        nop_event_unsubscribe(g_event_hub, g_event_subscription);
    g_event_hub          = NULL;
    g_event_subscription = NULL;
}

int nop_onvif_server_attach_event_hub(void *event_hub)
{
    if (!event_hub)
        return -1;
    pthread_mutex_lock(&g_event_lock);
    detach_event_hub_locked();             /* replace any previous hub atomically */
    g_event_hub = (nop_event_hub_t *)event_hub;
    g_event_subscription = nop_event_subscribe(g_event_hub, onvif_event_hub_sink, NULL);
    if (!g_event_subscription) {
        g_event_hub = NULL;
        pthread_mutex_unlock(&g_event_lock);
        return -2;
    }
    pthread_mutex_unlock(&g_event_lock);
    return 0;
}

void nop_onvif_server_detach_event_hub(void)
{
    pthread_mutex_lock(&g_event_lock);
    detach_event_hub_locked();
    pthread_mutex_unlock(&g_event_lock);
}

void nop_onvif_server_stop(void)
{
    if (!g_server_running)
        return;
    /* Detach the event-hub subscription FIRST. nop_event_unsubscribe drains any
     * in-flight sink, so onvif_event_hub_sink is neither running nor can start
     * once this returns — otherwise onvif_stop() below frees the EUA context
     * (onvif_eua_deinit) out from under a concurrent detection event (UAF). */
    nop_onvif_server_detach_event_hub();
    onvif_stop();
    g_server_running = 0;
}

const char *nop_onvif_server_version(void)
{
    return ONVIF_VERSION_STRING;
}
