# Command Coverage Matrix

Tracks implementation of NOP commands against the authoritative spec
`../../nop_api/NOP_APP/openapi.json` (v1.56.0912, ~216 commands). Updated per
batch; "contract" = covered by a ctest assertion.

Legend: ✅ implemented & contract-tested · 🟡 implemented · ⬜ not yet

## Protocol mechanism

| Feature | State | Notes |
|---|---|---|
| Request/response envelope | ✅ | `{func,args}` ↔ `{statusCode,statusMsg,content}` |
| Exact-match routing | ✅ | hash map, mixed `X_NightOwl_*`/`GUI_*`/bare names |
| Capability gating → 501 | ✅ | unlit capability returns 501 |
| Unknown command → 501 | ✅ | |
| Malformed JSON → 400 | ✅ | |
| `batchCmd` recursion | ✅ | `args.cmds[]` → `content.results[]`, no nesting |
| longPolling | 🟡 | event bitmap present; blocking wake pending http_server |

## Commands by capability group

### CAP_DEVICE
| Command | State |
|---|---|
| `getDeviceInfo` | ✅ |
| `X_NightOwl_getDeviceCapabilities` | ✅ |
| `X_NightOwl_getAPIVersion` | ✅ |

### CAP_AUTH
| Command | State |
|---|---|
| `createCredential` | ✅ |
| `deleteCredential` | 🟡 |

### CAP_SYSTEM
| Command | State |
|---|---|
| `reboot` | ✅ |
| `resetDevice` | 🟡 |
| `getName` | 🟡 |
| `setName` | 🟡 |

### CAP_STREAM
| Command | State |
|---|---|
| `getLiveCapabilities` | 🟡 |
| `startLiveStream` | ✅ |
| `stopLiveStream` | 🟡 |

### CAP_PTZ
| Command | State |
|---|---|
| `ptzMove` | 🟡 |
| `ptzGotoPreset` | 🟡 |

### CAP_LIGHT
| Command | State |
|---|---|
| `setChannelLightSwitch` | ✅ |
| `setChannelLightBrightness` | 🟡 |

## Not yet started

CAP_AI, CAP_AUDIO, CAP_STORAGE, CAP_RECORD, CAP_CLOUD, CAP_OTA, CAP_PUSH,
CAP_NETWORK, CAP_BIND, CAP_NOTIFY, CAP_MISC — the remaining ~200 commands,
scheduled across milestones N1–N4 (see 实现流程报告.md). Each lands as a
`src/business/caps/cap_<group>.c` file with handlers + contract tests, requiring
no change to the protocol core.

## Summary

- Implemented: **16** commands across **6** capability groups + full protocol mechanism.
- Contract-tested: 6 commands + all mechanism paths (5 ctest binaries, 100% pass).
