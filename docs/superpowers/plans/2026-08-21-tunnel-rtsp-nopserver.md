# Tunnel liveView/playback → NopRtspServer (standard RTSP) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the hand-rolled `nvr_rtsp_live` RTP/RTSP server (tunnel liveView + playback on :8554) with the OnvifClientLibrary's standard `NopRtspServer`, so RTP timestamping/pacing/RTP-Info/RTCP are library-correct — fixing "发送快于播放/冻结" over the P2P tunnel — with **all library stubs replaced by real per-slot data (no hardcoded codec/resolution)**.

**Architecture:** `nvr_rtsp_live.c` becomes a thin wrapper over `nop::NopRtspServer` plus a 4-slot dynamic pool (`NOP_RTSP_MAX_STREAMS=4`). Each app liveView/playback request allocates a slot; `startLiveStream/startPlayback` return `rtsp://iotc-tunnel:8554/live<N>`; the puller feeds `pushVideo(slot,...)`/`pushAudio(...)`. The library's `rtsp_parse_live_url` is rewritten to map `liveN → slot N-1` using **real** codec/resolution/audio read from the `CLiveVideo`/`CLiveAudio` instance (via new getters). Playback pushes disk frames on a paced thread.

**Tech Stack:** C/C++, NA51090 aarch64 cross-build (`build_arm`), OnvifClientLibrary (`SDK/OnvifClientLibrary`), telnet+NFS+nandwrite flashing.

**Spec:** this plan (derived from the live debugging session; doc refs: `NT12-SDK/nop_doc/camera/RemoteClients/APP_client/APP实时预览和音视频参数调整.md`, `APP_client_Agent.md` — "音视频流是通过 tunnel 里跑 RTSP").

## Global Constraints

- **All stubs → real data**: `rtsp_parse_live_url` must derive codec/width/height/framerate/audio from the actual per-slot `CLiveVideo`/`CLiveAudio` instance; **no hardcoded H264/1080p/720p**. Reject `liveN` whose slot is not inited (return FALSE).
- **≤4 concurrent live/playback slots** (`NOP_RTSP_MAX_STREAMS=4`, `MAX_LIVE_VIDEO_NUMS`). Dynamic pool; free on stop/teardown.
- **URL scheme**: `rtsp://iotc-tunnel:8554/live<N>` (N=1..4). Host must remain `iotc-tunnel` (agent P2PTunnel maps it). profile.txt `p2pProtocols` already lists `iotc-tunnel:8554`.
- **Codec map**: NVR `RSDK_CODEC_H264` → `NOP_VIDEO_CODEC_H264(1)`; `RSDK_CODEC_H265` → `NOP_VIDEO_CODEC_H265(4)`; audio AAC → `NOP_AUDIO_CODEC_AAC(4)`.
- **Preserve the public C API** consumed by `stream_router.c` (weak `nvr_rtsp_live_feed`/`_feed_audio`) and `nvr_cmd_p2p.c` (`nvr_rtsp_live_start`, `nvr_rtsp_live_port`, `nvr_rtsp_live_select*`, `nvr_rtsp_pb_prepare`, `nvr_rtsp_pb_*`) — only internals change.
- Build: `cd build_arm && make nvr_app -j$(nproc)`. Flash: telnet 192.168.9.102 + NFS `/mnt/m` + `flash_a.sh`/`flash_b.sh` + `ROOTFS_MTD` + `reboot -f`; re-enable remote_access after boot.
- Commit only when the user asks; NO Co-Authored-By trailer.

---

### Task 1: CLiveVideo / CLiveAudio real-data getters + slot-reuse reset

**Files:**
- Modify: `SDK/OnvifClientLibrary/media/live_video.h`, `media/live_video.cpp`
- Modify: `SDK/OnvifClientLibrary/media/live_audio.h`, `media/live_audio.cpp`

**Interfaces produced:**
- `int CLiveVideo::getCodec() const; int getWidth() const; int getHeight() const; double getFramerate() const; int getBitrate() const; BOOL isInited() const;`
- `int CLiveAudio::getCodec() const; int getSamplerate() const; int getChannels() const; int getBitrate() const; BOOL isInited() const;`
- Slot reuse: `initCapture` currently `if (m_bInited) return TRUE;` (no-op) → make `freeInstance(idx)` reset `m_bInited=FALSE` so a slot rebind with new codec/res re-inits. (Verify freeInstance already resets; if not, add.)

- [ ] Step 1: Add the const getters (return the `m_n*` members) to both headers + inline or .cpp.
- [ ] Step 2: Ensure `freeInstance` resets `m_bInited=FALSE` (so re-`initCapture` applies new params). If already, no change.
- [ ] Step 3: Build the OnvifClientLibrary object (host or via `make nvr_app`) to confirm it compiles.
- [ ] Step 4: Commit checkpoint (only if user asked).

**Verification:** `grep getCodec media/live_video.h` present; `make nvr_app` compiles.

---

### Task 2: Rewrite `rtsp_parse_live_url` with real per-slot data

**Files:**
- Modify: `SDK/OnvifClientLibrary/rtsp/rtsp_media.cpp` (`rtsp_parse_live_url`, ~line 2232)

**Interfaces consumed:** Task 1 getters.

**Implementation:** parse `liveN` (N=1..MAX). idx=N-1. `CLiveVideo *v=CLiveVideo::getInstance(idx)`; if `!v || !v->isInited()` return FALSE. Set:
```c
p_rua->media_info.is_live = 1;
p_rua->media_info.has_video = 1;  p_rua->media_info.v_index = idx;
p_rua->media_info.v_info.codec     = v->getCodec();     // real (H264/H265)
p_rua->media_info.v_info.width     = v->getWidth();
p_rua->media_info.v_info.height    = v->getHeight();
p_rua->media_info.v_info.framerate = v->getFramerate();
CLiveAudio *a = CLiveAudio::getInstance(idx);
if (a && a->isInited()) { has_audio=1; a_index=idx; a_info.codec=a->getCodec(); a_info.samplerate=a->getSamplerate(); a_info.channels=a->getChannels(); }
else has_audio = 0;
```
No hardcoded values. Support N up to `CLiveVideo::getStreamNums()`.

- [ ] Step 1: Replace the hardcoded live1/live2 body with the real-data version (loop or parse N).
- [ ] Step 2: Grep the file for other live/SDP stubs (`todo`, hardcoded 1920/1280) on the live path; fix any that affect live SDP (leave file-demux/metadata paths).
- [ ] Step 3: Build; confirm compiles.

**Verification:** code review — no `VIDEO_CODEC_H264;` hardcode in live path; values come from getters.

---

### Task 3: nvr_rtsp_live.c → NopRtspServer wrapper + 4-slot pool (LIVE)

**Files:**
- Rewrite: `components/cloud_tutk/src/nvr_rtsp_live.c` (C, but NopRtspServer is C++ — either compile this TU as C++ or add a small C++ shim `nvr_rtsp_srv.cpp` exposing C functions; prefer a shim to keep other C users unaffected).
- Maybe add: `components/cloud_tutk/src/nvr_rtsp_srv.cpp` + `include/nvr_rtsp_srv.h`
- Modify: `components/cloud_tutk/CMakeLists.txt` (add cpp, link OnvifClientLibrary/nop_rtsp)

**Interfaces produced (preserve existing C API):**
- `int nvr_rtsp_live_start(int port)` → `g_srv.start({port})`; `rtspLibInit` first.
- `int nvr_rtsp_live_port(void)`.
- slot pool: `int slot_of(chn,stream)`; `alloc_slot(chn,stream,want_v,want_a)`; `free_slot(slot)`. Map table `slot→{chn,stream,inited}`.
- `nvr_rtsp_live_feed(chn,stream,data,len,codec,is_key,ts)`: `s=slot_of(chn,stream)`; if `s<0` return. If slot not addStream'd yet AND is_key: resolve real dims (from the assembled keyframe / stream enc_w/h) → `addStream(s, {nopcodec,w,h,fps,bitrate}, audio?)` → mark inited. Then `pushVideo(s, data, len)`. (Reuse `g.live_key` for the "秒开" first frame if the server needs a keyframe.)
- `nvr_rtsp_live_feed_audio(chn,data,len,ts)`: `pushAudio(slot_of(chn,MAIN), data, len, nbsamples)`.
- `nvr_rtsp_live_select_media(chn,stream,want_v,want_a)`: `alloc_slot`.
- `nvr_rtsp_live_select(-1)` / stop: free slot(s).

- [ ] Step 1: Add C++ shim (`nvr_rtsp_srv.cpp`) wrapping `nop::NopRtspServer` with the C entry points above; slot table `[4]`.
- [ ] Step 2: In feed, on first is_key per slot, `addStream` with the stream's **real** enc dims (`enc_w/enc_h/fps`; codec→NopVideoCodec). Push every frame via `pushVideo`.
- [ ] Step 3: Remove the hand-rolled RTP/RTSP (send_rtp/send_annexb/build_sdp/accept loop) — or keep the file but gut the server, delegating to the shim.
- [ ] Step 4: CMake: compile cpp, link nop_rtsp/OnvifClientLibrary.
- [ ] Step 5: Build `nvr_app`; fix link errors (NopRtspServer symbols).

**Verification:** `make nvr_app` links; `nm` shows NopRtspServer refs resolved.

---

### Task 4: startLiveStream / stopLiveStream → live<N> URL + slot alloc

**Files:** Modify `app/router/nvr_cmd_p2p.c` (`cmd_startLiveStream`, `cmd_stopLiveStream`).

**Interfaces consumed:** Task 3 `alloc_slot`/`free_slot`/`nvr_rtsp_live_select_media`.

**Implementation:** `startLiveStream`: parse channel+streamType → alloc slot for (chn, sel.stream, want_v, want_a); if none free → error `too_many_streams`; return `{"url":"rtsp://iotc-tunnel:8554/live<slot+1>"}`. `stopLiveStream`: free the slot for that channel (or all if channel omitted).

- [ ] Step 1: Change the URL to `live<slot+1>`; call alloc/free.
- [ ] Step 2: Build.

**Verification:** telnet: `wget POST startLiveStream` returns `.../liveN`.

---

### Task 5: Playback through slot pool + real-time pacing

**Files:** Modify `components/cloud_tutk/src/nvr_rtsp_srv.cpp` (pb path), `app/router/nvr_cmd_p2p.c` (`cmd_startPlayback`), reuse `nvr_rtsp_pb_prepare`.

**Implementation:** playback allocates a slot; a pb thread reads RSDK segments from `start` ts and `pushVideo(slot,...)` **paced** to frame ts deltas (sleep `(ts[n]-ts[n-1])` in real time) since `pushVideo` has no ts and the server timestamps at push. URL `rtsp://iotc-tunnel:8554/live<slot+1>` (same live path) or a distinct mount if needed. Seek → restart pb thread at new ts.

- [ ] Step 1: pb thread: read segment frames, pace by ts delta, pushVideo.
- [ ] Step 2: `startPlayback` returns the slot URL; wire seek/pause.
- [ ] Step 3: Build.

**Verification:** playback plays at ~1× real-time (not fast-forward).

---

### Task 6: Build, flash, real-machine verification

- [ ] Step 1: `make nvr_app`; pack (`pack_ota.sh`); flash inactive slot via telnet; switch `ROOTFS_MTD`; `reboot -f`; re-enable remote_access.
- [ ] Step 2: App liveView (sub then main): confirm **continuous real-time** video (not frozen), audio.
- [ ] Step 3: Telnet log check: NopRtspServer session up, pushVideo flowing, no drops.
- [ ] Step 4: Playback: seek + play at 1×.
- [ ] Step 5: 4 concurrent streams; 5th → graceful `too_many_streams`.

**Verification:** app shows live real-time video + audio over tunnel; playback correct; no freeze.

## Self-Review notes
- Slot reuse: a channel switching main↔sub reuses its slot → must `freeInstance`+`addStream` with new dims (Task 1 Step 2 + Task 3 Step 2).
- Keyframe-first: verify `media_live_put_video` starts a session mid-GOP OK, or push the cached `g.live_key` as the first frame per new SETUP (reuse existing keyframe cache).
- H265 sprop: `getAuxSDPLine` must emit sprop-vps/sps/pps from pushed frames — verify the library extracts them (it parses pushed NALs); if not, that's an added sub-task.
- `pushVideo(idx,data,size)` expects one Annex-B AU per call (same as our per-frame feed) — confirm framing.
