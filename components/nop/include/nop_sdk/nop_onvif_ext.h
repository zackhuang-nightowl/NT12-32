/**
 * @file nop_onvif_ext.h
 * @brief Extended C ABI for the built-in ONVIF client — the operations the
 *        NOP<->ONVIF mapping layer needs that are not in nop_onvif.h.
 *
 * Same contract as nop_onvif.h: this is the ONLY surface the pure-C mapping
 * layer uses to reach these ONVIF calls; the vendored Happytimesoft client
 * (third_party/onvif/, unmodified) stays hidden behind the C++ adapter
 * (src/onvif/onvif_adapter_ext.cpp). Every op reuses the same opaque
 * nop_onvif_device_t handle created via nop_onvif.h.
 *
 * Available only when the SDK is built with NOP_WITH_ONVIF=ON. All int-returning
 * calls use 0 for success and a negative value for failure.
 *
 * Grouped by mapping-spec domain; declared incrementally as each domain lands.
 */
#ifndef NOP_SDK_ONVIF_EXT_H
#define NOP_SDK_ONVIF_EXT_H

#include "nop_sdk/nop_onvif.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ======================================================================== */
/* §2 PTZ — preset speed / home                                             */
/* ======================================================================== */

/**
 * GotoPreset with an explicit speed. @p speed is normalized [0,1] and applied
 * to pan/tilt/zoom (NOP 1..10 -> ONVIF 0.1..1.0 is done by the mapper). Pass
 * speed <= 0 to let the device use its default speed.
 */
int nop_onvif_ptz_goto_preset_speed(nop_onvif_device_t *device,
                                    const char *profile_token,
                                    const char *preset_token, float speed);

/** SetHomePosition: store the current position as the profile's home. */
int nop_onvif_ptz_set_home(nop_onvif_device_t *device, const char *profile_token);

/** Imaging continuous focus move on a video source. @p speed in [-1,1]. */
int nop_onvif_img_focus_move(nop_onvif_device_t *device, const char *video_source_token,
                             float speed);
/** Stop imaging focus movement. */
int nop_onvif_img_focus_stop(nop_onvif_device_t *device, const char *video_source_token);

/**
 * SetPreset: @p token_in "" creates a new preset (device assigns a token),
 * otherwise updates that preset. @p name optional. The resulting token is
 * written to @p out_token (size @p out_size). @return 0 or negative.
 */
int nop_onvif_ptz_set_preset_ex(nop_onvif_device_t *device, const char *profile_token,
                                const char *token_in, const char *name,
                                char *out_token, int out_size);

/* ---- PTZ patrol (PresetTour) ------------------------------------------- */

/** Max spots carried per patrol across the ABI. */
#define NOP_ONVIF_TOUR_MAX_SPOTS 32

/** One tour spot: a preset to visit and how long to dwell (seconds). */
typedef struct nop_onvif_tour_spot {
    char preset_token[100];
    int  dwell_s;
} nop_onvif_tour_spot_t;

/** One preset tour (patrol). */
typedef struct nop_onvif_tour {
    char token[100];
    char name[64];
    int  auto_start;
    char status[16];   /**< "Idle"/"Touring"/"Paused" (get-only) */
    int  spot_count;
    nop_onvif_tour_spot_t spots[NOP_ONVIF_TOUR_MAX_SPOTS];
} nop_onvif_tour_t;

/** GetPresetTours into @p out (capped). @return count (>=0) or negative. */
int nop_onvif_ptz_get_tours(nop_onvif_device_t *device, const char *profile_token,
                            nop_onvif_tour_t *out, int max);
/** CreatePresetTour (empty); device assigns a token into @p out_token. */
int nop_onvif_ptz_create_tour(nop_onvif_device_t *device, const char *profile_token,
                              char *out_token, int out_size);
/** ModifyPresetTour with name + spots (+ auto_start) of @p tour. */
int nop_onvif_ptz_modify_tour(nop_onvif_device_t *device, const char *profile_token,
                              const nop_onvif_tour_t *tour);
/** OperatePresetTour: @p op is "Start"/"Stop"/"Pause". */
int nop_onvif_ptz_operate_tour(nop_onvif_device_t *device, const char *profile_token,
                               const char *token, const char *op);
/** RemovePresetTour by token. */
int nop_onvif_ptz_remove_tour(nop_onvif_device_t *device, const char *profile_token,
                              const char *token);

/* ======================================================================== */
/* §7 Privacy Zone — Media2 Mask                                            */
/* ======================================================================== */

/** Max polygon vertices carried per mask across the ABI (AABB uses 4). */
#define NOP_ONVIF_MASK_MAX_POINTS 16

/** One privacy mask, flattened to a plain-C polygon (ONVIF normalized coords). */
typedef struct nop_onvif_mask {
    char  token[100];         /**< mask token (empty when creating)         */
    char  config_token[100];  /**< VideoSourceConfiguration token it binds  */
    int   enabled;            /**< 1 = mask active                          */
    int   point_count;        /**< number of valid vertices                 */
    float x[NOP_ONVIF_MASK_MAX_POINTS]; /**< vertex x in [-1,1]             */
    float y[NOP_ONVIF_MASK_MAX_POINTS]; /**< vertex y in [-1,1]             */
} nop_onvif_mask_t;

/**
 * GetMasks: list all privacy masks into @p out (capped at @p max).
 * @return the mask count (>=0), or negative on failure.
 */
/** GetMasks. @p config_token 非空则只取该 VSC 的 mask（写入 ConfigurationToken）。 */
int nop_onvif_media2_get_masks(nop_onvif_device_t *device,
                               nop_onvif_mask_t *out, int max,
                               const char *config_token);

/**
 * CreateMask from a polygon. @p config_token binds the mask to a video source
 * configuration (see nop_onvif_media2_video_source_token). @p type is the ONVIF
 * mask type ("Color"/"Pixelated"/"Blurred"; "" -> "Color").
 * 成功时把设备分配的 Mask Token 写入 @p out_token（可为 NULL）。
 */
int nop_onvif_media2_create_mask(nop_onvif_device_t *device,
                                 const char *config_token,
                                 const float *xs, const float *ys, int npoints,
                                 int enabled, const char *type,
                                 char *out_token, int out_size);

/** DeleteMask by token. */
int nop_onvif_media2_delete_mask(nop_onvif_device_t *device, const char *token);

/**
 * Resolve the first profile's VideoSourceConfiguration token (Media2), needed
 * as the ConfigurationToken when creating the first mask/OSD. Writes at most
 * @p size bytes. @return 0 on success, negative on failure.
 */
int nop_onvif_media2_video_source_token(nop_onvif_device_t *device,
                                        char *out, int size);

/* ======================================================================== */
/* §5 OSD — Media2 OSD                                                       */
/* ======================================================================== */

/** OSD position, matching onvif_OSDPosType. */
typedef enum nop_onvif_osd_pos {
    NOP_OSD_POS_UPPER_LEFT = 0,
    NOP_OSD_POS_UPPER_RIGHT,
    NOP_OSD_POS_LOWER_LEFT,
    NOP_OSD_POS_LOWER_RIGHT,
    NOP_OSD_POS_CUSTOM
} nop_onvif_osd_pos_t;

/** OSD text kind, matching onvif_OSDTextType. */
typedef enum nop_onvif_osd_text {
    NOP_OSD_TEXT_PLAIN = 0,
    NOP_OSD_TEXT_DATE,
    NOP_OSD_TEXT_TIME,
    NOP_OSD_TEXT_DATETIME
} nop_onvif_osd_text_t;

/** One OSD, flattened to plain C (ONVIF normalized coords for a custom pos). */
typedef struct nop_onvif_osd {
    char token[100];         /**< OSD token (empty when creating)           */
    char config_token[100];  /**< VideoSourceConfiguration token            */
    int  is_image;           /**< 1 = Image OSD, 0 = Text OSD               */
    int  text_type;          /**< nop_onvif_osd_text_t (text OSD)           */
    int  pos_type;           /**< nop_onvif_osd_pos_t                       */
    float pos_x;             /**< custom position x in [-1,1]               */
    float pos_y;             /**< custom position y in [-1,1]               */
    char plain_text[256];    /**< text OSD plain content                    */
    char img_path[256];      /**< image OSD URI                             */
} nop_onvif_osd_t;

/** GetOSDs. @p config_token 非空则只取该 VSC 的 OSD（写入 ConfigurationToken）。 */
int nop_onvif_media2_get_osds(nop_onvif_device_t *device,
                              nop_onvif_osd_t *out, int max,
                              const char *config_token);

/** SetOSD (update an existing OSD identified by @p osd->token). */
int nop_onvif_media2_set_osd(nop_onvif_device_t *device, const nop_onvif_osd_t *osd);

/** CreateOSD (token empty; @p osd->config_token required).
 *  成功时把设备分配的 OSDToken 写入 @p out_token（可为 NULL）。 */
int nop_onvif_media2_create_osd(nop_onvif_device_t *device, const nop_onvif_osd_t *osd,
                                char *out_token, int out_size);

/** DeleteOSD by token. */
int nop_onvif_media2_delete_osd(nop_onvif_device_t *device, const char *token);

/* ======================================================================== */
/* §3 Media — Media2 VideoEncoder configuration                             */
/* ======================================================================== */

/** Max resolutions carried in the encoder options. */
#define NOP_ONVIF_VENC_MAX_RES 32

/** One video encoder configuration (current values), flattened to plain C. */
typedef struct nop_onvif_venc {
    char token[100];              /**< configuration token                 */
    char encoding[16];            /**< "H264" / "H265" / "JPEG" ...        */
    int  width;
    int  height;
    int  fps;                     /**< RateControl.FrameRateLimit          */
    int  bitrate_kbps;            /**< RateControl.BitrateLimit            */
    int  gov_length;
    int  quality;
    int  guaranteed_framerate;    /**< 1 = fixed frame rate                */
    int  const_bitrate;           /**< 1 = CBR                             */
} nop_onvif_venc_t;

/** Encoder option ranges for one configuration (from GetOptions).
 *  have_* is 1 only when the camera actually returned that field — mapping
 *  must not invent Min/Max. */
typedef struct nop_onvif_venc_opts {
    int have_quality, quality_min, quality_max;
    int have_bitrate, bitrate_min, bitrate_max;
    int have_gov,     gov_min, gov_max;
    int have_fps,     fps_min, fps_max;
    int res_count;
    int res_w[NOP_ONVIF_VENC_MAX_RES];
    int res_h[NOP_ONVIF_VENC_MAX_RES];
} nop_onvif_venc_opts_t;

/** GetVideoEncoderConfigurations into @p out (capped). @return count or <0. */
int nop_onvif_media2_get_vencs(nop_onvif_device_t *device,
                               nop_onvif_venc_t *out, int max);

/** GetVideoEncoderConfigurationOptions for @p config_token.
 *  If @p encoding is non-empty, pick the Options node matching it.
 *  @return 0 or <0. */
int nop_onvif_media2_get_venc_options(nop_onvif_device_t *device,
                                      const char *config_token,
                                      const char *encoding,
                                      nop_onvif_venc_opts_t *out);

/** SetVideoEncoderConfiguration from a full config. @return 0 or <0. */
int nop_onvif_media2_set_venc(nop_onvif_device_t *device, const nop_onvif_venc_t *cfg);

/* ======================================================================== */
/* §9/§8 Analytics — Line / Field / CellMotion / ObjectDetection rules      */
/* ======================================================================== */

/** Max polygon/polyline vertices carried per analytics rule. */
#define NOP_ONVIF_RULE_MAX_PTS 24

/**
 * One analytics rule, flattened to plain C. Geometry is ONVIF-normalized
 * [-1,1]; the adapter marshals it to/from the ONVIF rule's ElementItem XML.
 * class_filter is a comma-joined class list (e.g. "Human,Vehicle").
 */
typedef struct nop_onvif_rule {
    char  name[64];          /**< rule Name                                */
    char  type[24];          /**< "LineDetector"/"FieldDetector"/
                                  "CellMotionDetector"/"ObjectDetection"    */
    char  direction[16];     /**< line direction ("Left"/"Right"/"Any")    */
    char  class_filter[128]; /**< comma-joined trigger classes             */
    int   enabled;           /**< 1=on；GetRules 无 Enabled 栏位时默认 1     */
    int   point_count;
    float x[NOP_ONVIF_RULE_MAX_PTS]; /**< ONVIF 归一化 [-1,1]              */
    float y[NOP_ONVIF_RULE_MAX_PTS];
} nop_onvif_rule_t;

/** Resolve the profile's VideoAnalyticsConfiguration token. @return 0 or <0. */
int nop_onvif_analytics_config_token(nop_onvif_device_t *device, char *out, int size);

/* ---- §10 Multi video source — per-source token resolution -------------- */

/** Upper bound on video sources per physical device (NVRs are well under this). */
#define NOP_ONVIF_MAX_SOURCES 16

/** The set of ONVIF tokens that belong to one physical video source, grouped
 *  from GetProfiles by VideoSourceConfiguration.SourceToken. */
typedef struct nop_onvif_source_tokens {
    char source_token[100];   /**< VideoSourceToken (physical input; the key)  */
    char profile[100];        /**< 该源 PTZ/控制用 Profile：优先子码流上带 PTZ 的 */
    char vsc_token[100];      /**< VideoSourceConfiguration token (OSD/Mask)   */
    char analytics_cfg[100];  /**< 该源子码流 VideoAnalyticsConfiguration token */
    char main_venc[100];      /**< VideoEncoderConfiguration token, main (max res) */
    char sub_venc[100];       /**< VideoEncoderConfiguration token, sub         */
} nop_onvif_source_tokens_t;

/** Resolve the token set for @p source_token (""/NULL = the first source).
 *  优先走连接时已在 handle 上的 profiles，不再打 GetProfiles。@return 0 or negative. */
int nop_onvif_resolve_source(nop_onvif_device_t *device, const char *source_token,
                             nop_onvif_source_tokens_t *out);
/** 读连接时缓存的源 token（不再打 GetProfiles）。空 source_token=首源。0=ok。 */
int nop_onvif_device_cached_source(nop_onvif_device_t *device, const char *source_token,
                                   nop_onvif_source_tokens_t *out);
/** 连接时缓存的源个数。 */
int nop_onvif_device_cached_nsrc(nop_onvif_device_t *device);
/** 按序号读缓存源。0=ok。 */
int nop_onvif_device_cached_source_at(nop_onvif_device_t *device, int index,
                                      nop_onvif_source_tokens_t *out);

/** 连接时 / 上次 GET 刷新的 OSD 列表。@p key 为 VideoSourceToken 或 VSC。
 *  @return 条数（>=0）或 -1（尚未缓存）。 */
int nop_onvif_device_cached_osds(nop_onvif_device_t *device, const char *key,
                                 nop_onvif_osd_t *out, int max);
int nop_onvif_device_cached_vencs(nop_onvif_device_t *device, const char *source_token,
                                  nop_onvif_venc_t *out, int max);
int nop_onvif_device_cached_masks(nop_onvif_device_t *device, const char *key,
                                  nop_onvif_mask_t *out, int max);

/** SET 成功后回写缓存（避免下次 SET 再 GetOSDs/GetVencs/GetMasks）。 */
int nop_onvif_device_osd_cache_put(nop_onvif_device_t *device, const char *key,
                                   const nop_onvif_osd_t *osd);
int nop_onvif_device_osd_cache_del(nop_onvif_device_t *device, const char *key,
                                   const char *osd_token);
int nop_onvif_device_osd_cache_replace(nop_onvif_device_t *device, const char *key,
                                       const nop_onvif_osd_t *osds, int n);
int nop_onvif_device_venc_cache_put(nop_onvif_device_t *device, const nop_onvif_venc_t *venc);
int nop_onvif_device_venc_cache_ingest(nop_onvif_device_t *device,
                                       const nop_onvif_venc_t *vencs, int n);
int nop_onvif_device_mask_cache_replace(nop_onvif_device_t *device, const char *key,
                                        const nop_onvif_mask_t *masks, int n);

/** List distinct VideoSourceTokens the device exposes (one channel per source).
 *  @return count (>=0) or negative. */
int nop_onvif_list_sources(nop_onvif_device_t *device, char tokens[][100], int max);

/**
 * GetRules, keeping only those whose ONVIF Type contains @p type_substr
 * (e.g. "LineDetector"; NULL = all). @return count (>=0) or negative.
 */
int nop_onvif_analytics_get_rules(nop_onvif_device_t *device, const char *config_token,
                                  const char *type_substr,
                                  nop_onvif_rule_t *out, int max);

/** CreateRules with one rule. */
int nop_onvif_analytics_create_rule(nop_onvif_device_t *device, const char *config_token,
                                    const nop_onvif_rule_t *rule);
/** ModifyRules with one rule (matched by name). */
int nop_onvif_analytics_modify_rule(nop_onvif_device_t *device, const char *config_token,
                                    const nop_onvif_rule_t *rule);
/** DeleteRules by name. */
int nop_onvif_analytics_delete_rule(nop_onvif_device_t *device, const char *config_token,
                                    const char *name);

/**
 * Analytics AI capabilities distilled from GetSupportedRules (rule presence +
 * maxInstances) and GetRuleOptions (Segments/Field point ranges, ClassFilter /
 * Direction StringLists), per requirement/nightow_onvif_RuleDescription.md.
 * Class/direction lists are comma-joined ONVIF names ("Human,Vehicle" /
 * "Left,Right,Any"); a *_max_instances / *_max_* of 0 means the device did not
 * advertise a limit. Fields left 0/empty when the option is absent.
 */
typedef struct nop_onvif_ai_caps {
    int  line_present;          /**< tt:LineDetector advertised            */
    int  line_max_instances;    /**< LineDetector @maxInstances            */
    int  line_max_points;       /**< Segments IntRange Max                 */
    char line_classes[128];     /**< LineDetector ClassFilter StringList   */
    char line_directions[64];   /**< Direction StringList (Left,Right,Any) */
    int  field_present;         /**< tt:FieldDetector advertised           */
    int  field_max_instances;   /**< FieldDetector @maxInstances           */
    int  field_max_vertices;    /**< Field IntRange Max                    */
    char field_classes[128];    /**< FieldDetector ClassFilter StringList  */
    int  object_present;        /**< tt:ObjectDetection advertised         */
    int  object_max_instances;  /**< ObjectDetection @maxInstances         */
    char object_classes[128];   /**< ObjectDetection ClassFilter StringList*/
    int  motion_present;        /**< tt:CellMotionDetector advertised      */
} nop_onvif_ai_caps_t;

/** GetSupportedRules + GetRuleOptions -> @p out. @return 0 or negative. */
int nop_onvif_analytics_get_ai_caps(nop_onvif_device_t *device,
                                    const char *config_token,
                                    nop_onvif_ai_caps_t *out);

/**
 * Per-camera capabilities for X_NightOwl_getDeviceCapabilities, distilled from
 * the Media2 profile ConfigurationSet flags (ptz/mic/speaker/analytics/metadata)
 * and the PTZ Node + PTZ service capabilities (preset/home/patrol/hdTrack +
 * pan/tilt/zoom motion). Booleans are 0/1; *_max_* are 0 when not advertised.
 */
typedef struct nop_onvif_dev_caps {
    int has_ptz;          /**< profile has a PTZConfiguration            */
    int has_mic;          /**< AudioSource (mic)                         */
    int has_speaker;      /**< AudioOutput (speaker)                     */
    int has_analytics;    /**< Analytics (sensor)                        */
    int has_metadata;     /**< Metadata stream                           */
    int ptz_pan;          /**< continuous pan supported                  */
    int ptz_tilt;         /**< continuous tilt supported                 */
    int ptz_zoom;         /**< continuous zoom supported                 */
    int ptz_preset;       /**< MaximumNumberOfPresets > 0                */
    int ptz_max_presets;  /**< MaximumNumberOfPresets                    */
    int ptz_home;         /**< HomeSupported                             */
    int ptz_patrol;       /**< SupportedPresetTour present               */
    int ptz_max_tours;    /**< MaximumNumberOfPresetTours                */
    int ptz_hdtrack;      /**< PTZ Capabilities MoveAndTrack advertised  */
    int ptz_focus;        /**< Imaging Move supported (focus lens)       */
} nop_onvif_dev_caps_t;

/** Capabilities of ONE video source: OR of that source's Media2 profile
 *  ConfigurationSet flags + PTZ node/service caps. @p source_token ""/NULL =
 *  first source. @return 0 or negative. */
int nop_onvif_get_device_caps(nop_onvif_device_t *device,
                              const char *source_token,
                              nop_onvif_dev_caps_t *out);

/** 连接时缓存的 PTZ/成像/音视频能力。空 source_token=首源。0=ok。 */
int nop_onvif_device_cached_caps(nop_onvif_device_t *device, const char *source_token,
                                 nop_onvif_dev_caps_t *out);
int nop_onvif_device_caps_cache_put(nop_onvif_device_t *device, const char *source_token,
                                    const nop_onvif_dev_caps_t *caps);
/** 连接时缓存的 AI 能力（按源 token 或 analytics_cfg）。0=ok。 */
int nop_onvif_device_cached_ai(nop_onvif_device_t *device, const char *source_or_cfg,
                               nop_onvif_ai_caps_t *out);
int nop_onvif_device_ai_cache_put(nop_onvif_device_t *device, const char *source_or_cfg,
                                  const nop_onvif_ai_caps_t *ai);

/* ---- §8 Motion — CellMotion detector (ActiveCells bitmap) --------------- */

/** Max cells (bits) an ActiveCells bitmap carries across the ABI. */
#define NOP_ONVIF_CELLS_MAX_BITS 4096

/**
 * CellMotion rule, flattened. @c active is a row-major bit-per-cell grid of
 * @c columns x @c rows (bit index = row*columns + col, MSB-first per byte),
 * mirroring the ONVIF ActiveCells base64. Caller presets columns/rows.
 */
typedef struct nop_onvif_cellmotion {
    int columns;
    int rows;
    int sensitivity;   /**< 0..100 */
    int min_count;
    unsigned char active[NOP_ONVIF_CELLS_MAX_BITS / 8];
} nop_onvif_cellmotion_t;

/**
 * GetRules(CellMotionDetector) -> @p io (columns/rows preset by caller; fills
 * sensitivity/min_count/active). @return 1 if a rule was found, 0 if none, <0 on
 * error. When 0, @p io keeps its preset dims with an all-clear bitmap.
 */
int nop_onvif_analytics_get_cellmotion(nop_onvif_device_t *device, const char *config_token,
                                       nop_onvif_cellmotion_t *io);

/**
 * Write ActiveCells / MinCount onto the existing CellMotionDetector via
 * ModifyRules (keep Name and other SimpleItems). CreateRules only if none
 * exists. Never DeleteRules.
 */
int nop_onvif_analytics_set_cellmotion(nop_onvif_device_t *device, const char *config_token,
                                       const nop_onvif_cellmotion_t *in);

/* ======================================================================== */
/* §1 Events — structured PullMessages                                      */
/* ======================================================================== */

/** One pulled notification. `topic` gives the rule kind (tns1:RuleEngine/...);
 *  `source_vsct` is the Source's VideoSourceConfigurationToken (§10: routes the
 *  event to the NVR channel bound to that source); `class_types` is the Data's
 *  ClassTypes (Human/Vehicle/... — the object class, needed because the
 *  ObjectDetection topic itself carries no class). Empty when absent. */
typedef struct nop_onvif_event_msg {
    char topic[256];
    char source_vsct[100];   /**< Source: VideoSourceConfigurationToken */
    char class_types[64];    /**< Data: ClassTypes (space/comma separated) */
    char direction[16];      /**< Data: Direction (Left/Right) for line-cross */
} nop_onvif_event_msg_t;

/**
 * PullMessages on an already-created pull-point (nop_onvif_events_create_pullpoint),
 * copying each notification's Topic into @p out (capped at @p max). Blocks up to
 * @p timeout_s. @return message count (>=0) or negative on error.
 */
int nop_onvif_events_pull_msgs(nop_onvif_device_t *device, int timeout_s, int max,
                               nop_onvif_event_msg_t *out);

/* ======================================================================== */
/* Media capability flags (getDeviceCapabilities mapping)                    */
/* ======================================================================== */

/** Per-device media capability flags, derived from Media2 GetProfiles'
 *  ConfigurationSet flags (ProfileCapabilities.ConfigurationsSupported):
 *   - mic         = any profile carries AudioSource
 *   - audio_out   = any profile carries AudioOutput (speaker declaration)
 *   - audio_dec   = any profile carries AudioDecoder (backchannel decode path)
 *   - ptz         = any profile carries PTZ
 *   - analytics   = any profile carries Analytics (→ sensor)
 *  speaker(2-way) is (audio_out && audio_dec): AudioOutput 声明 + backchannel。 */
typedef struct nop_onvif_media_caps {
    int mic;
    int audio_out;
    int audio_dec;
    int ptz;
    int analytics;
} nop_onvif_media_caps_t;

/** Fill @p out from Media2 GetProfiles ConfigurationSet flags. @return 0 or <0. */
int nop_onvif_get_media_caps(nop_onvif_device_t *device, nop_onvif_media_caps_t *out);

/** Analytics sensor capabilities, derived from **GetSupportedRules ∪
 *  GetSupportedAnalyticsModules** (NOPMappingONVIF.md):
 *   - CellMotionDetector/CellMotionEngine    → motion(pixelChange)
 *   - ObjectDetection/ObjectInField/…Engine  → objectDetection(+ human/vehicle/animal/face) */
typedef struct nop_onvif_analytics_caps {
    int motion;
    int objdet;
    int obj_human, obj_vehicle, obj_animal, obj_face;
    /* ruledDetection(AI_getChannelAICapabilities):越线/区域入侵 */
    int line_cross;       /* GetSupportedRules 含 tt:LineDetector */
    int line_max;         /* LineDetector maxInstances(maxLineCount) */
    int line_max_points;  /* GetRuleOptions:每条线点数(缺省 2) */
    int field_intrusion;  /* GetSupportedRules 含 tt:FieldDetector */
    int field_max;        /* FieldDetector maxInstances(maxFieldCount) */
    int field_max_verts;  /* GetRuleOptions:多边形顶点上限 */
} nop_onvif_analytics_caps_t;

/** Resolve the analytics config token (media2, else media1 va_cfg), then combine
 *  GetSupportedRules + GetSupportedAnalyticsModules (+ GetRuleOptions for ClassFilter
 *  classes / line-field limits) into @p out. @return 0 or <0. */
int nop_onvif_analytics_get_supported(nop_onvif_device_t *device, nop_onvif_analytics_caps_t *out);

/** GetNetworkInterfaces → 一只网口 HwAddress。want_ip 非空时优先匹配该 IPv4；
 *  否则取首个已启用口。成功写 @p out 返 0。 */
int nop_onvif_get_network_mac(nop_onvif_device_t *device, const char *want_ip,
                              char *out, int cap);

#ifdef __cplusplus
}
#endif

#endif /* NOP_SDK_ONVIF_EXT_H */
