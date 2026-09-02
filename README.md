# Vedantu Clicker System

A high-performance **Windows desktop application** that captures button presses
from SunVote RF clickers, validates them against a live session, and posts each
event to the Vedantu backend API in real time.

Designed for **75"/85" touch-screen displays** in classrooms: large scalable
fonts, full adaptive layout, and hold-to-paste Session ID entry.

---

## Architecture Overview

```
SunVote RF Clicker(s)
        │  (radio, < 5 ms RF latency)
   USB Dongle (base station)
        │
  QTWSCmdApp.exe  ◄─── spawned & monitored by WSCmdAppManager
  (SunVote SDK)         (crash-restart in < 3 s)
        │  WebSocket  ws://127.0.0.1:9002
   ClickerClient  (IXWebSocket, receive callback never blocks)
        │
        │  on_click() — stamped with entry_ts_ms here
        │
        ├──────────────────────────────────────────────────►  UiState.addEvent()
        │  PATH A — UI fast-path (sub-10 ms, bypasses dispatcher)        │
        │                                                          MainWindow render
        │                                                          (ImGui / D3D11)
        │
        │  PATH B — API path
        ▼
   LockFreeQueue<ClickEvent>  (MPSC wait-free ring, 65536-slot capacity)
        │  notifyPush() wakes drain thread instantly via condvar
        ▼
   EventDispatcher — drain thread (sole pop() caller, SPSC contract)
        │  session gate: events before validation are silently discarded
        │
        │  per-event: std::thread([ev]{ postWithRetry(ev); }).detach()
        ▼
   HttpClient  ─── thread-local CURL handle (keep-alive, TCP_NODELAY)
        │  HTTP/2 over pre-warmed TLS session
        ▼
   Vedantu API  POST /hybrid/print-clicker-stats
        │
        │  on HTTP 2xx → latencyLog().recordApiDone()
        │               → UiState.updateEventLatency()  ← O(1) via serial_index_
        │  on failure  → exponential backoff + jitter   → OfflineQueue (AES-256-GCM)
        │
        ▼
   OfflineQueue — flush loop every 5 s (replays when session is valid)


  ── Startup sequence ────────────────────────────────────────────────────────

  WinMain
    │  1. CurlGlobal init (curl_global_init — process-wide, RAII)
    │  2. Config (.env) — all required keys validated at start
    │  3. Logger + LatencyLogger (async spdlog, rotating files)
    │  4. EventDispatcher.start()  — drain & flush threads ready
    │  5. SdkExtractor → extract SDK to %TEMP%\vedantu_svs\
    │  6. WSCmdAppManager.start()  — spawn QTWSCmdApp.exe hidden
    │  7. sleep 1.5 s              — wait for WS server to bind
    │  8. ClickerClient.start()    — connect WebSocket, start heartbeat
    │  9. MainWindow created → SW_SHOWNORMAL (operator may maximize manually)
    │
    │  [User holds Session ID field ≥ 800 ms → paste from clipboard]
    │  [User clicks VALIDATE]
    │
    │  10. SessionManager.validate()  — GET scheduling API (timeout 10 s)
    │  11. On success → http.warmup() ← on detached thread
    │                  (pre-establishes TCP+TLS to API node)
    │                  ← first real click POST reuses warm keep-alive handle
```

### Layer summary

| Module | Location | Responsibility |
|--------|----------|----------------|
| `Config` | `src/app/` | Parses `.env`, validates required keys, typed accessors (`get`, `require`, `getInt`, `getBool`) |
| `ClickEvent` | `src/domain/` | Immutable value type for one button press; static atomic serial counter |
| `ClickerClient` | `src/hid/` | WebSocket client to QTWSCmdApp.exe — `startChoices` sent once, `lessMode=2` |
| `SdkExtractor` | `src/hid/` | Unpacks embedded RCDATA SDK binaries to `%TEMP%\vedantu_svs\` |
| `WSCmdAppManager` | `src/hid/` | Spawns QTWSCmdApp.exe hidden, watchdog-restarts on crash in < 3 s |
| `Logger` | `src/logging/` | Six named async spdlog loggers (rotating files, configurable level/size) |
| `LatencyLogger` | `src/logging/` | Per-event CSV writer: entry → UI render → API done |
| `CurlGlobal` | `src/network/` | RAII libcurl process-wide init / cleanup |
| `HttpClient` | `src/network/` | Thread-local keep-alive HTTPS (libcurl); TCP_NODELAY; HTTP/2; TLS 1.2+ |
| `SessionManager` | `src/network/` | Session ID validation (GET) against scheduling API; mutex-guarded cache |
| `EventDispatcher` | `src/network/` | SPSC drain thread → per-event detached HTTP threads; offline flush every 5 s |
| `LockFreeQueue` | `src/queue/` | Wait-free MPSC ring buffer (power-of-2 capacity, 65536 slots default) |
| `CredentialStore` | `src/security/` | Windows Credential Manager + env-var fallback lookup |
| `HmacSigner` | `src/security/` | HMAC-SHA256 request signing + UUID v4 nonce (optional) |
| `OfflineQueue` | `src/storage/` | AES-256-GCM encrypted disk queue; flush callback for replay |
| `MainWindow` | `src/ui/` | Win32 + D3D11 + ImGui render loop; adaptive layout; hold-to-paste |
| `UiState` | `src/ui/` | Thread-safe shared state; 10,000-row deque + O(1) serial index |

---

## Prerequisites

| Tool | Version | Notes |
|------|---------|-------|
| Windows | 10 or 11 (x64) | Required — Win32 / D3D11 API |
| Visual Studio | 2022 (MSVC v143) | C++ Desktop workload required |
| CMake | ≥ 3.25 | Must be on PATH |
| Git | any recent | Must be on PATH |
| vcpkg | auto-installed | Run `setup_deps.ps1` to bootstrap |
| SunVote SDK | v5.1.0.43 WSapp | Only needed to re-stage `sdk_embed/` |

### vcpkg packages (auto-installed via `vcpkg.json`)

- `nlohmann-json` — JSON parsing
- `spdlog` — async rotating file logging
- `openssl` — TLS, HMAC-SHA256, AES-256-GCM
- `curl[ssl]` — HTTPS client (libcurl)
- `ixwebsocket` — WebSocket client
- `imgui[dx11-binding,win32-binding]` — immediate-mode GUI

---

## Getting Started

### 1. Clone the repository

```powershell
git clone <repo-url> S6
cd S6
```

### 2. Run the one-time setup script

```powershell
.\setup_deps.ps1
```

This will:
1. Install / bootstrap vcpkg at `C:\vcpkg` (customizable — see below)
2. Copy `.env.example` → `.env`
3. Configure CMake with the vcpkg toolchain
4. Build the Release binary

> **First build** downloads all vcpkg packages and may take **10–30 minutes**
> depending on your internet connection.

### 3. Configure `.env`

Open `.env` (created in step 2) and fill in the required values:

```ini
SESSION_SECRET=<your-scheduling-api-secret>
OFFLINE_AES_KEY=<64-hex-chars-random-key>
```

To generate a random `OFFLINE_AES_KEY`:
```powershell
-join ((1..32) | ForEach-Object { '{0:x2}' -f (Get-Random -Maximum 256) })
```

All other values have working defaults for the Vedantu production environment.

### 4. Run the application

```powershell
.\build\Release\VedantuClickerSystem.exe
```

Or use the staged copy in `AppRelease\`:

```powershell
.\AppRelease\VedantuClickerSystem.exe
```

Connect the SunVote USB dongle before launching. The app opens in a **normal
(restored) window** — press the maximize button or Windows snap to fill a
75"/85" touch screen. The layout adapts to any resolution automatically.

---

## Build Options

### Standard build (most developers)

```powershell
.\build.ps1
```

### Custom vcpkg location

```powershell
.\build.ps1 -VcpkgRoot "D:\tools\vcpkg"
# or
$env:VCPKG_ROOT = "D:\tools\vcpkg"; .\build.ps1
```

### Re-staging SunVote SDK binaries

Only needed if you received a new SDK version. Set the path to the folder
containing `QTWSCmdApp.exe`:

```powershell
.\build.ps1 -SunvoteSdkDir "C:\SunVote SDK\WSapp for win-5.1.0.43"
# or
$env:SUNVOTE_SDK_DIR = "C:\SunVote SDK\WSapp for win-5.1.0.43"; .\build.ps1
```

The binaries will be copied to `sdk_embed/` and embedded in the exe by the
resource compiler. Commit the updated `sdk_embed/` to the repository so other
developers don't need the SDK installer.

### Debug build

```powershell
cmake --build build --config Debug
```

---

## Project Structure

```
S6/
├── src/                        # C++ source — all headers-only except ClickEvent.cpp
│   ├── app/
│   │   └── Config.hpp          # .env parser: get(), require(), getInt(), getBool()
│   ├── domain/
│   │   ├── ClickEvent.hpp      # Core event value type (serial_no, device_sn, value, timestamp_ms)
│   │   └── ClickEvent.cpp      # Static atomic serial counter definition
│   ├── hid/
│   │   ├── ClickerClient.hpp   # WebSocket client to QTWSCmdApp.exe
│   │   ├── RegisteredClicker.hpp
│   │   ├── SdkExtractor.hpp    # Unpacks SDK from embedded RCDATA resources
│   │   ├── WSCmdAppManager.hpp # Launches and watchdogs QTWSCmdApp.exe
│   │   └── resource.h          # RCDATA ID constants for embedded SDK files
│   ├── logging/
│   │   ├── LatencyLogger.hpp   # Per-event CSV latency tracker (entry/ui/api stages)
│   │   └── Logger.hpp          # Named async spdlog loggers (HID, SESSION, API, PERF, ERRORS)
│   ├── network/
│   │   ├── CurlGlobal.hpp      # RAII libcurl global init/cleanup
│   │   ├── EventDispatcher.hpp # SPSC drain → detached HTTP threads; offline flush loop
│   │   ├── HttpClient.hpp      # Thread-local keep-alive HTTPS wrapper (libcurl)
│   │   └── SessionManager.hpp  # Session ID validation (GET) and cached SessionInfo
│   ├── queue/
│   │   └── LockFreeQueue.hpp   # Wait-free MPSC ring buffer (65536 slots, power-of-2)
│   ├── security/
│   │   ├── CredentialStore.hpp # Windows Credential Manager helper
│   │   └── HmacSigner.hpp      # HMAC-SHA256 + UUID v4 nonce generator
│   ├── storage/
│   │   └── OfflineQueue.hpp    # AES-256-GCM encrypted disk queue with flush callback
│   ├── ui/
│   │   ├── MainWindow.hpp      # Win32 + D3D11 + ImGui window; adaptive layout; hold-to-paste
│   │   └── UiState.hpp         # Thread-safe shared state: 10,000-row event log + O(1) index
│   └── main.cpp                # Entry point (WinMain): init sequence, layer wiring
│
├── ui-dashboard/               # React + TypeScript monitoring dashboard
│   ├── src/
│   │   ├── components/         # UI components
│   │   ├── hooks/              # WebSocket hook
│   │   ├── types.ts
│   │   └── ...
│   ├── package.json
│   ├── vite.config.ts
│   └── README.md               # Dashboard-specific setup guide
│
├── sdk_embed/                  # Pre-staged SunVote SDK binaries (committed to repo)
│   ├── QTWSCmdApp.exe          # SunVote WebSocket bridge
│   ├── QTZT_SDK_V4.dll
│   ├── Qt5*.dll
│   └── ...                     # All DLLs + config.ini embedded into the exe
│
├── AppRelease/                 # Post-build staged release (git-ignored)
│   └── VedantuClickerSystem.exe
│
├── logs/                       # Runtime log output (git-ignored)
│
├── resource.rc                 # Embeds sdk_embed/ binaries as RCDATA
├── vedantu.ico                 # Application icon
├── CMakeLists.txt              # Build system
├── vcpkg.json                  # vcpkg manifest (dependency list)
├── .env.example                # Configuration template — copy to .env
├── .env                        # Local config (git-ignored — never commit)
├── .gitignore
├── build.ps1                   # Quick build script
├── setup_deps.ps1              # First-time full setup script
└── make_ico.ps1                # Helper: convert PNG → .ico
```

---

## Configuration Reference

All configuration is loaded from `.env` (next to the executable at runtime).

| Key | Required | Default | Description |
|-----|----------|---------|-------------|
| `API_BASE_URL` | ✅ | — | Vedantu OTM node base URL |
| `API_POST_ENDPOINT` | ✅ | — | Click event POST path |
| `API_MAX_RETRIES` | | `10` | Max retry attempts per event |
| `API_RETRY_BASE_MS` | | `100` | Base retry delay (doubles each attempt) |
| `SESSION_BASE_URL` | ✅ | — | Scheduling service base URL |
| `SESSION_ENDPOINT` | ✅ | — | Session fetch endpoint path |
| `SESSION_SECRET` | ✅ | — | **Set this** — scheduling API secret |
| `WSCMDAPP_PATH` | ✅ | — | Path to QTWSCmdApp.exe (overridden at runtime by SdkExtractor) |
| `WS_HOST` | | `127.0.0.1` | QTWSCmdApp WebSocket host |
| `WS_PORT` | | `9002` | QTWSCmdApp WebSocket port |
| `BASE_ID` | | `1` | Serial number offset for click event numbering |
| `HMAC_ENABLED` | | `false` | Enable HMAC-SHA256 request signing |
| `TLS_CERT_PINNING` | | `false` | Enable TLS certificate pinning |
| `TLS_PINNED_CERT_HASH` | | `` | Pinned public key hash (SHA-256) |
| `OFFLINE_QUEUE_PATH` | | `%APPDATA%/VedantuClickerSystem/offline_queue.dat` | Encrypted offline queue file |
| `OFFLINE_AES_KEY` | | — | **Set this** — 64 hex chars (32-byte AES-256 key) |
| `UI_WS_PORT` | | `9099` | Port for the React dashboard WebSocket |
| `LOG_LEVEL` | | `info` | `trace`/`debug`/`info`/`warn`/`error` |
| `LOG_DIR` | | `./logs` | Log output directory |
| `LOG_MAX_SIZE_MB` | | `10` | Max size per log file before rotation |
| `LOG_MAX_FILES` | | `5` | Number of rotating log files to keep |
| `DEBUG_MODE` | | `false` | Enable extra debug output |

---

## UI Reference

### Main Window

The application opens in a **normal (restored) window** at startup.
The operator can maximize it manually using the title-bar maximize button or
Windows Snap — the layout fills the new size automatically at any resolution.

| Area | Description |
|------|-------------|
| **Header** | Gradient banner; title; **Dongle** and **Session** status chips |
| **Pair Clickers / Stop Pairing** | Toggle clicker pairing mode (top-right button) |
| **Session Configuration** | Session ID input + VALIDATE button |
| **Hold-to-paste** | Press-and-hold the Session ID field ≥ 800 ms to paste from clipboard — a growing orange progress bar shows the hold progress |
| **LIVE CLICK EVENTS table** | 4 columns: No. · Device ID · Value · Entry Time |

### Event Table Columns

| Column | Source field | Notes |
|--------|-------------|-------|
| **No.** | `serial_no` | Sequential counter; up to **10,000** rows retained in memory |
| **Device ID** | `device_sn` | SunVote clicker serial number |
| **Value** | `value` | Button pressed: 1=A 2=B 3=C 4=D 5=E 6=F |
| **Entry Time** | `timestamp_ms` | Local time formatted as `HH:MM:SS.mmm` |

Latency columns (UI Delay, API Delay) are **not displayed** but are still
recorded to the CSV latency log and updated via `UiState.updateEventLatency()`.

### Clicker Pairing Panel

Press **Pair Clickers** to open the pairing modal. Power on each clicker —
the panel lists registered clickers as they appear. Press **Clear List** to
reset. Press **Stop Pairing** or the X to close.

---

## API Integration

### Click Event POST

**Endpoint:** `POST {API_BASE_URL}{API_POST_ENDPOINT}`
**Content-Type:** `application/json`

```json
{
  "sessionId": "abc123",
  "deviceID":  "A1B2C3",
  "eventNum":  2
}
```

`eventNum` maps to the button pressed: `1=A`, `2=B`, `3=C`, `4=D`, `5=E`, `6=F`.

**Retry policy:** exponential backoff with ±20% jitter, up to `API_MAX_RETRIES`.
Failed events are persisted to the AES-256-GCM encrypted offline queue and
replayed every 5 seconds once connectivity is restored.

### Session Validation GET

**Endpoint:** `GET {SESSION_BASE_URL}{SESSION_ENDPOINT}?sessionId=<id>&secret=<secret>`

Expected response:
```json
{
  "title":     "Physics Lecture",
  "presenter": "John Doe",
  "startTime": 1700000000000,
  "endTime":   1700003600000
}
```

Click events are **discarded** until a session is validated. Events that arrived
before validation are not replayed (pre-session events in the offline queue are
also purged on the first validated event).

---

## Latency & Performance

The system minimises the round-trip from **button press → API confirmed** while
keeping the UI update completely independent of network latency.

### End-to-end timeline (typical classroom, good connectivity)

| Stage | Target | Mechanism |
|-------|--------|-----------|
| RF → USB dongle | < 5 ms | SunVote hardware |
| Dongle → QTWSCmdApp (WS) | < 2 ms | Loopback WebSocket |
| QTWSCmdApp → ClickerClient callback | < 1 ms | IXWebSocket receive thread |
| Callback → UI row visible | **< 10 ms** | `UiState.addEvent()` direct path; `PostMessage` wakes render loop |
| Callback → queue push + notify | **< 1 µs** | Wait-free MPSC push + condvar notify |
| Drain thread wakeup | **< 200 µs** | `cv_.wait(pred)` + `notify_one()` — zero polling |
| HTTP POST (warm TLS, keep-alive) | **30–80 ms** | Thread-local CURL handle, HTTP/2, TCP_NODELAY |
| API confirmed → latency update | **O(1)** | `serial_index_` unordered_map lookup — no linear scan |

### Key design decisions

**1. Dual-path on click event**
Every `ClickEvent` takes two simultaneous paths:
- **Path A (UI):** directly into `UiState.addEvent()` → `PostMessage` wakes the render loop. The UI shows the event in < 10 ms regardless of network state.
- **Path B (API):** into the SPSC queue → drain thread → detached HTTP thread. Network latency never delays the UI update.

**2. O(1) API latency update**
`UiState` maintains a `serial_index_` (`unordered_map<uint64_t, size_t>`) alongside
the event deque. `updateEventLatency()` looks up any of the 10,000 rows in O(1)
instead of scanning the entire list on every HTTP response.

**3. TLS pre-warm on session validation**
As soon as `SessionManager.validate()` succeeds, a detached background thread
calls `HttpClient::warmup()` to establish the TCP+TLS connection. The first
click POST reuses the warm keep-alive handle.

**4. Thread-local CURL handles (zero lock contention)**
Each HTTP worker thread gets its own `CURL*` handle with a persistent TCP/TLS
session. There is no shared mutex on the HTTP layer.

**5. TCP_NODELAY + HTTP/2**
Nagle's algorithm is disabled (`TCP_NODELAY=1`). Small POST payloads (≈ 80 bytes
JSON) are sent immediately. HTTP/2 multiplexes streams over a single TLS session.

**6. SPSC + condvar (zero polling)**
The drain thread blocks on `cv_.wait(pred)` and is woken in < 200 µs by
`notifyPush()`. No sleep polling loop.

**7. lessMode=2 (clicker LCD never restarts)**
`startChoices` is sent **once** on dongle connect. Re-sending it resets the
WSCmdApp question session (~2–3 s blackout). `lessMode=2` keeps the session
alive indefinitely.

### Tunable parameters

```ini
API_MAX_RETRIES=10        # Max retry attempts before persisting to offline queue
API_RETRY_BASE_MS=100     # Starting backoff delay (doubles each attempt ±20% jitter)
```

---

## Logs

Logs are written to `logs/` (relative to the executable):

| File | Content |
|------|---------|
| `HID_EVENTS.log` | Clicker connect/disconnect, raw click events |
| `SESSION.log` | Session validation requests and results |
| `API_CALLS.log` | HTTP post attempts and responses per event |
| `PERFORMANCE.log` | Per-request latency measurements |
| `ERRORS.log` | Warnings and errors from all layers |

Latency data is also written to `logs/latency_YYYY-MM-DD.csv` with columns:
`Serial No`, `Stage`, `Device ID`, `Click Value`, `Wall Time`, `Epoch ms`,
`Delta from Entry ms`, `HTTP Status`.

---

## UI Dashboard

See [`ui-dashboard/README.md`](ui-dashboard/README.md) for setup and usage.

```powershell
cd ui-dashboard
npm install
npm run dev
```

---

## Troubleshooting

### "Configuration file not found"

Copy `.env.example` to `.env` and fill in the required values:

```powershell
Copy-Item .env.example .env
```

### `OFFLINE_AES_KEY` error at startup

The key must be exactly 64 hex characters (32 bytes). Generate one:
```powershell
-join ((1..32) | ForEach-Object { '{0:x2}' -f (Get-Random -Maximum 256) })
```

### Dongle shows "DISCONNECTED" in the UI

1. Unplug and replug the SunVote USB dongle.
2. Check that `QTWSCmdApp.exe` is not blocked by antivirus — whitelist `%TEMP%\vedantu_svs\`.
3. Check `logs\HID_EVENTS.log` for WebSocket connection errors.

### Session validation fails

1. Verify `SESSION_SECRET` in `.env` matches the scheduling API secret.
2. Check network connectivity to `SESSION_BASE_URL`.
3. Check `logs\SESSION.log` for the full HTTP error response.

### API events not posting

1. Verify `API_BASE_URL` and `API_POST_ENDPOINT` in `.env`.
2. Check `logs\API_CALLS.log` for HTTP status codes.
3. Events that fail all retries are saved to the offline queue and replayed
   automatically every 5 seconds once the API is reachable.

### Build fails at CMake configure

Ensure vcpkg is bootstrapped and the toolchain file path is correct:

```powershell
.\setup_deps.ps1 -VcpkgRoot "C:\vcpkg"
```

---

## Contributing

1. **Never commit `.env`** — it contains secrets.
2. **Never commit `AppRelease/`** or `build/` — they are generated artefacts.
3. `sdk_embed/` **should** be committed — it contains the pre-staged SDK
   binaries that other developers need to build without the SDK installer.
4. Keep each layer in its own subdirectory under `src/`.
5. All public APIs must be thread-safe; document which mutex guards each member.
6. Follow the existing latency design: the WebSocket receive callback must never
   sleep or block; the UI render thread must never do network I/O.

---

## License

Copyright © 2025 Vedantu Innovations Pvt. Ltd. All rights reserved.
