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
int nop_onvif_media2_get_masks(nop_onvif_device_t *device,
                               nop_onvif_mask_t *out, int max);

/**
 * CreateMask from a polygon. @p config_token binds the mask to a video source
 * configuration (see nop_onvif_media2_video_source_token). @p type is the ONVIF
 * mask type ("Color"/"Pixelated"/"Blurred"; "" -> "Color").
 */
int nop_onvif_media2_create_mask(nop_onvif_device_t *device,
                                 const char *config_token,
                                 const float *xs, const float *ys, int npoints,
                                 int enabled, const char *type);

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

/** GetOSDs into @p out (capped at @p max). @return count (>=0) or negative. */
int nop_onvif_media2_get_osds(nop_onvif_device_t *device,
                              nop_onvif_osd_t *out, int max);

/** SetOSD (update an existing OSD identified by @p osd->token). */
int nop_onvif_media2_set_osd(nop_onvif_device_t *device, const nop_onvif_osd_t *osd);

/** CreateOSD (token empty; @p osd->config_token required). */
int nop_onvif_media2_create_osd(nop_onvif_device_t *device, const nop_onvif_osd_t *osd);

/** DeleteOSD by token. */
int nop_onvif_media2_delete_osd(nop_onvif_device_t *device, const char *token);

/* ======================================================================== */
/* §3 Media — Media2 VideoEncoder configuration                             */
/* ======================================================================== */

/** Max resolutions carried in the encoder options. */
#define NOP_ONVIF_VENC_MAX_RES 24

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

/** Encoder option ranges for one configuration (from GetOptions). */
typedef struct nop_onvif_venc_opts {
    int quality_min, quality_max;
    int bitrate_min, bitrate_max;
    int gov_min, gov_max;
    int fps_min, fps_max;
    int res_count;
    int res_w[NOP_ONVIF_VENC_MAX_RES];
    int res_h[NOP_ONVIF_VENC_MAX_RES];
} nop_onvif_venc_opts_t;

/** GetVideoEncoderConfigurations into @p out (capped). @return count or <0. */
int nop_onvif_media2_get_vencs(nop_onvif_device_t *device,
                               nop_onvif_venc_t *out, int max);

/** GetVideoEncoderConfigurationOptions for @p config_token. @return 0 or <0. */
int nop_onvif_media2_get_venc_options(nop_onvif_device_t *device,
                                      const char *config_token,
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
    int   point_count;
    float x[NOP_ONVIF_RULE_MAX_PTS];
    float y[NOP_ONVIF_RULE_MAX_PTS];
} nop_onvif_rule_t;

/** Resolve the profile's VideoAnalyticsConfiguration token. @return 0 or <0. */
int nop_onvif_analytics_config_token(nop_onvif_device_t *device, char *out, int size);

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

/** CreateRules with one CellMotionDetector rule (caller clears old rules first). */
int nop_onvif_analytics_set_cellmotion(nop_onvif_device_t *device, const char *config_token,
                                       const nop_onvif_cellmotion_t *in);

/* ======================================================================== */
/* §1 Events — structured PullMessages                                      */
/* ======================================================================== */

/** One pulled notification: its ONVIF topic (e.g. tns1:RuleEngine/...). */
typedef struct nop_onvif_event_msg {
    char topic[256];
} nop_onvif_event_msg_t;

/**
 * PullMessages on an already-created pull-point (nop_onvif_events_create_pullpoint),
 * copying each notification's Topic into @p out (capped at @p max). Blocks up to
 * @p timeout_s. @return message count (>=0) or negative on error.
 */
int nop_onvif_events_pull_msgs(nop_onvif_device_t *device, int timeout_s, int max,
                               nop_onvif_event_msg_t *out);

#ifdef __cplusplus
}
#endif

#endif /* NOP_SDK_ONVIF_EXT_H */
