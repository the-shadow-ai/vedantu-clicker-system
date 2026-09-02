# Vedantu Clicker System — Codebase Reference & PRD

> **Repository:** [github.com/the-shadow-ai/vedantu-clicker-system](https://github.com/the-shadow-ai/vedantu-clicker-system)
> **Language:** C++20 (core), TypeScript/React (dashboard)
> **Platform:** Windows 10+ (x64)
> **Build:** CMake + vcpkg + MSVC

---

## 1. What This Product Does

The Vedantu Clicker System is a **classroom response system** for Vedantu live teaching sessions. It enables:

- A teacher to ask a quiz question during a live class
- Up to hundreds of students in the room to answer by pressing **A/B/C/D** on a **SunVote RF clicker** (a physical handheld device)
- The system to **instantly record each click** and **POST the response to Vedantu's backend API** in real time
- The teacher's operator PC to **display all incoming responses** on a large smartboard screen as they arrive

The system runs entirely offline-capable: if the network drops, clicks are stored **encrypted on disk** and replayed when connectivity returns.

---

## 2. System Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│                        Physical Layer                               │
│   Student Clickers (SunVote RF) ──RF──► USB Dongle (on operator PC)│
└───────────────────────────────────────┬─────────────────────────────┘
                                        │  HID / COM port
                                        ▼
┌───────────────────────────────────────────────────────────────────┐
│               SunVote SDK Layer  (sdk_embed/)                     │
│   QTWSCmdApp.exe  ─── Qt WebSocket server on ws://127.0.0.1:9527 │
│   QTZT_SDK_V4.dll, Qt5*.dll, hidapi.dll, libcurl.dll, zlib.dll   │
│   (All binaries embedded inside VedantuClickerSystem.exe as       │
│    RCDATA and extracted to %TEMP%\vedantu_svs\ at startup)        │
└───────────────────────────────────────┬───────────────────────────┘
                                        │  WebSocket JSON events
                                        ▼
┌───────────────────────────────────────────────────────────────────┐
│                    Application Core (src/)                        │
│                                                                   │
│  ClickerClient ──push──► LockFreeQueue<ClickEvent>               │
│       │                          │                                │
│       │                   EventDispatcher ──HTTPS POST──► API     │
│       │                          │                                │
│       └──────────────► UiState ──► MainWindow (ImGui + D3D11)    │
│                                                                   │
│  SessionManager  (validates Session ID against scheduling API)    │
│  OfflineQueue    (AES-256-GCM encrypted disk buffer)             │
│  Logger          (async spdlog: 6 rotating log files)            │
└───────────────────────────────────────────────────────────────────┘
                                        │
                                        ▼
┌───────────────────────────────────────────────────────────────────┐
│              Monitoring Dashboard  (ui-dashboard/)                │
│   React + TypeScript  ──WebSocket──► Real-time click feed        │
│   (Optional: runs separately, connects to a local WS relay)      │
└───────────────────────────────────────────────────────────────────┘
```

### Data Flow (one click, end to end)

1. Student presses **B** on their clicker
2. RF signal → USB dongle → `QTWSCmdApp.exe` (SunVote SDK)
3. `QTWSCmdApp.exe` emits a JSON WebSocket message on `ws://127.0.0.1:9527`
4. `ClickerClient` receives it, parses the event, stamps a **millisecond timestamp**
5. **Two things happen simultaneously:**
   - `UiState::addEvent()` — UI table is updated within ~1 ms (sub-frame)
   - `LockFreeQueue::push()` — event is enqueued for the API dispatcher
6. `EventDispatcher::drainLoop()` wakes (zero-sleep via `condition_variable`)
7. A **detached HTTP thread** calls `HttpClient::post()` (libcurl, keep-alive TLS)
8. Vedantu API receives the click and records the student's answer
9. On failure → exponential back-off (30 ms base, ×2, max 30 s) then offline queue

---

## 3. Repository Structure

```
vedantu-clicker-system/
├── src/                    C++ application source (all headers-only except domain/)
│   ├── main.cpp            Entry point & wiring
│   ├── app/                Configuration
│   ├── domain/             Core data types
│   ├── hid/                Hardware interface (clickers + SDK)
│   ├── logging/            Structured async logging
│   ├── network/            HTTP, WebSocket, session, event dispatch
│   ├── queue/              Lock-free MPSC ring buffer
│   ├── security/           HMAC signing, nonce generation, credential store
│   ├── storage/            AES-256-GCM encrypted offline queue
│   └── ui/                 ImGui/D3D11 desktop window + shared state
├── sdk_embed/              SunVote SDK binaries (embedded in EXE via resource.rc)
├── ui-dashboard/           React + TypeScript monitoring web app
├── resource.rc             Windows resource file (icon + SDK RCDATA embeds)
├── vedantu.ico             App icon shown in taskbar and Explorer
├── CMakeLists.txt          Build system
├── vcpkg.json              C++ dependency manifest
├── build.ps1               Developer quick-build script
├── setup_deps.ps1          First-time vcpkg bootstrap
├── .env.example            Configuration template
└── .gitignore
```

---

## 4. File-by-File Reference

---

### `src/main.cpp` — Entry Point & Wiring

**What it does:** `WinMain` (no console window). Creates every object in dependency order, wires the callbacks between layers, runs the UI message loop, and shuts down cleanly.

**Startup sequence:**
| Step | What happens |
|------|-------------|
| 0 | `CurlGlobal` — `curl_global_init()` once (RAII) |
| 1 | `Config` — loads `.env` from exe directory, validates required keys |
| 2 | `Logger::init()` — 6 rotating async log files |
| 3 | Create `LockFreeQueue`, `OfflineQueue`, `HttpClient`, `SessionManager`, `UiState` |
| 4 | `EventDispatcher::start()` — drain thread + flush thread begin |
| 5 | `SdkExtractor` — extracts SDK binaries from RCDATA to `%TEMP%\vedantu_svs\` |
| 6 | `WSCmdAppManager::start()` — launches `QTWSCmdApp.exe` from temp dir |
| 7 | `ClickerClient::start()` — connects WebSocket to `QTWSCmdApp.exe` |
| 8 | `MainWindow::create()` + `runLoop()` — D3D11 render loop (blocks until close) |
| 9 | Graceful shutdown: dispatcher → clicker → WSCmdApp → logger |

**Why `WinMain`?** The project uses `WIN32` subsystem so no console window appears on teacher/operator machines. `MessageBoxA` is only used for **fatal startup errors** (bad config, missing SDK) — never for runtime events.

---

### `src/app/Config.hpp` — Configuration Loader

**What it does:** Parses a `.env` file (KEY=VALUE format) next to the executable. Provides `get()`, `require()`, `getInt()`, `getBool()` accessors. Keys can be overridden by environment variables at runtime.

**Why it exists:** All deployment settings (API URLs, secrets, log paths) are externalized from the binary. Teachers/operators never recompile — they edit `.env`. The `.env` file is gitignored so secrets are never committed.

**Key config keys (see `.env.example`):**

| Key | Purpose |
|-----|---------|
| `API_BASE_URL` | Vedantu backend base URL |
| `API_POST_ENDPOINT` | Click event POST endpoint path |
| `SESSION_BASE_URL` | Scheduling API base URL |
| `SESSION_ENDPOINT` | Session validation endpoint |
| `SESSION_SECRET` | Shared secret for session API authentication |
| `WSCMDAPP_PATH` | Auto-set at runtime to the extracted SDK path |
| `HMAC_ENABLED` | Whether to sign API requests with HMAC-SHA256 |
| `OFFLINE_AES_KEY` | 64-char hex key for encrypting the offline queue |
| `LOG_DIR` | Where rotating log files are written |
| `LOG_LEVEL` | `debug` / `info` / `warn` / `error` |

---

### `src/domain/ClickEvent.hpp` + `ClickEvent.cpp` — Core Data Type

**What it does:** Defines `ClickEvent` — the single value that flows through the entire system from the moment a clicker button is pressed to the HTTP POST.

```cpp
struct ClickEvent {
    uint64_t serial_no;    // Monotonically increasing ID — prevents duplicates
    std::string device_sn; // SunVote device serial number
    std::string device_id; // Human-readable display ID
    int value;             // 1=A, 2=B, 3=C, 4=D, 5=E, 6=F
    int64_t timestamp_ms;  // System clock ms — used for session-gate filtering
    std::string nonce;     // UUID v4 — deduplicate on the API side
};
```

**Why `serial_no`?** The drain loop and retry threads run concurrently. `serial_no` lets log messages be traced end-to-end: entry → UI → queue → HTTP → API.

---

### `src/hid/` — Hardware Interface Layer

#### `SdkExtractor.hpp`
**What it does:** On construction, extracts all 21 SunVote SDK binaries embedded in the EXE's RCDATA section to `%TEMP%\vedantu_svs\`. On destruction, kills `QTWSCmdApp.exe` and deletes the temp directory.

**Why embed instead of ship a separate folder?**
The entire app is **one executable** (`VedantuClickerSystem.exe`). Operators can copy it to a USB drive and run it on any classroom PC without installation or SDK setup. No `.dll hell`, no `%PATH%` issues.

**How it extracts:** Uses `FindResourceW` + `LoadResource` + `LockResource` — pure Win32 API, no shell commands, no console windows.

#### `WSCmdAppManager.hpp`
**What it does:** Launches `QTWSCmdApp.exe` (from the extracted temp path) as a background process using `CreateProcessA`. Monitors its health and restarts if it crashes. On `stop()`, terminates the process.

**Why a separate process?** The SunVote SDK (`QTZT_SDK_V4.dll`) is a closed-source Qt5 library. It runs inside `QTWSCmdApp.exe` and exposes a **WebSocket JSON API**. We cannot statically link it into our C++20 codebase — the architecture boundary is the WebSocket connection.

#### `ClickerClient.hpp`
**What it does:** Connects to `ws://127.0.0.1:9527` (QTWSCmdApp's WebSocket server) using **ixwebsocket**. Parses incoming JSON messages and routes them:

| JSON event | Action |
|-----------|--------|
| `keyboardOnlineOne` / `keypadOnlineOne` | Clicker registered (pairing mode) **or** late-join/sleep-wake detected in answering mode → debounced `startChoices` re-send |
| `keyboardOfflineOne` | Clicker disconnected — logged |
| `keyboardPressOne` / `keypadPressOne` | **Answer click** — fires the `on_click` callback |
| `keyboardDisconnected` | Dongle unplugged — `dongle_connected = false` |
| `keyboardConnected` | Dongle re-inserted — `dongle_connected = true` |

**Late-join/sleep-wake fix:** When a clicker powers on _after_ the quiz is already running, WSCmdApp doesn't know to include it in the active session. `ClickerClient` detects the `keyboardOnlineOne` event in answering mode and sends a debounced `startChoices` command (max once per 600 ms) to refresh WSCmdApp's slot table.

#### `RegisteredClicker.hpp`
Simple data struct: `{ sn, key_id, model, version }` — populated during pairing and displayed in the registration panel.

#### `resource.h`
Integer constants (`IDR_SDK_WSCMDAPP = 201`, etc.) that match the IDs in `resource.rc`. Shared between `resource.rc` (Windows RC compiler) and `SdkExtractor.hpp` (C++ code). This file is the only connection between the two.

---

### `src/logging/` — Structured Async Logging

#### `Logger.hpp`
**What it does:** Initialises 6 named **spdlog** async loggers. Each logger writes to:
- A **rotating file** (`logs/<NAME>.log`, max 10 MB × 5 files)
- **Stdout** (colour-coded, for developer terminals)

| Logger | File | Used for |
|--------|------|---------|
| `HID_EVENTS` | `logs/HID_EVENTS.log` | Clicker connect/disconnect, click events received |
| `SESSION` | `logs/SESSION.log` | Session validation results, app lifecycle |
| `API_CALLS` | `logs/API_CALLS.log` | Every HTTP POST attempt, status code, latency |
| `SECURITY` | `logs/SECURITY.log` | HMAC signing, nonce generation |
| `PERFORMANCE` | `logs/PERFORMANCE.log` | Queue stats, offline flush, per-event latency |
| `ERRORS` | `logs/ERRORS.log` | All warnings and errors across all layers |

**Why async?** Logging on the click-receive path (the WebSocket callback thread) must never block. spdlog's async mode writes to an 8192-slot queue on a background thread — zero UI or network impact.

#### `LatencyLogger.hpp`
**What it does:** Records the complete lifecycle of every click event as a **CSV row**:
```
serial_no, device_sn, value, entry_ts_ms, ui_render_ts_ms, api_done_ts_ms, http_status
```
Flushed to `logs/LATENCY.csv` for post-session analysis. Measures:
- **Entry delay** (clicker press → OS timestamp)
- **UI render delay** (entry → first frame painted)
- **API round-trip** (entry → 2xx response)

---

### `src/network/` — Network Layer

#### `HttpClient.hpp`
**What it does:** A **thread-local libcurl wrapper** that provides sub-10 ms POST latency at classroom scale (300+ simultaneous clicks).

Key optimisations:
| Technique | Why |
|-----------|-----|
| Thread-local `CURL*` handle | Zero lock contention; each HTTP worker thread gets its own persistent TLS connection |
| `CURLOPT_TCP_NODELAY` | Disables Nagle's algorithm — prevents 40 ms buffering of small POSTs |
| `CURLOPT_TCP_KEEPALIVE` | Reuses TCP/TLS connection across all requests from a thread |
| `CURL_HTTP_VERSION_2TLS` | HTTP/2 multiplexing — one TLS session handles multiple concurrent POSTs |
| `Expect:` header suppressed | Removes 100-Continue round-trip on every POST |
| POST timeout: 4 s | Fast failure detection → fast retry |
| Connect timeout: 1.5 s | Dead server fails in 1.5 s, not 3 s |
| `CURLOPT_LOW_SPEED_LIMIT` | Stalled connection detected in 3 s, not after full timeout |

#### `SessionManager.hpp`
**What it does:** Validates a Session ID against Vedantu's scheduling API via a `GET` request. Caches the result (`SessionInfo`: title, presenter, start/end times). Thread-safe via `std::mutex`.

**Why session validation?** The system must only accept clicks during an active scheduled session. A quiz started at 10:00 AM should not accept clicks from 9:59 AM or 10:30 AM. The `session_start_ms_` timestamp in `EventDispatcher` enforces this at the queue level.

#### `EventDispatcher.hpp`
**What it does:** The central event routing engine. Runs two background threads:

1. **Drain thread** (sole consumer of `LockFreeQueue`) — woken by `notifyPush()` with zero polling delay. For each valid event: validates session, stamps generation, spawns a detached HTTP worker thread.
2. **Flush thread** — every 5 s, replays `OfflineQueue` events if session is valid.

**Generation counter (Bug 3 fix):** Every `VALIDATE` press increments an atomic `generation_` counter. Each HTTP retry thread captures the generation at dispatch time and **aborts** if the counter has advanced — preventing a stale pre-quiz click from overwriting a current-quiz answer on the server.

**Queue purge on validate:** When a session is validated, `purgeQueue()` drains all pending ring-buffer slots before dispatching any event. This closes the race window where a click made 50 ms before VALIDATE could slip through the timestamp gate.

#### `CurlGlobal.hpp`
RAII wrapper: calls `curl_global_init(CURL_GLOBAL_ALL)` on construction and `curl_global_cleanup()` on destruction. Must be constructed exactly once in `main()` before any `HttpClient`.

---

### `src/queue/LockFreeQueue.hpp` — Lock-Free Ring Buffer

**What it does:** A **cache-line padded MPSC** (multi-producer, single-consumer) lock-free ring buffer, capacity 65,536 slots (power of two).

**Why lock-free?** The `on_click` callback fires on the WebSocket receive thread. If a mutex was used, a slow API response could block incoming click events. With a lock-free queue, `push()` completes in ~10 ns regardless of what the drain thread is doing.

**MPSC safety:** Multiple producers (clicker callback threads) can call `push()` concurrently using `compare_exchange_weak` on the head index. **Only one thread** (the drain loop) calls `pop()` — this is enforced by the `EventDispatcher` design.

**False-sharing prevention:** Each slot is `alignas(64)` — placed on its own cache line so concurrent producers don't invalidate each other's cache lines.

---

### `src/security/` — Security Layer

#### `HmacSigner.hpp`
**What it does:** Computes **HMAC-SHA256** of an API request body using a shared secret, returning a lowercase hex string. Added as an `X-Signature` header when `HMAC_ENABLED=true` in `.env`.

Also contains `NonceGenerator` — generates **UUID v4** using `OpenSSL RAND_bytes` (cryptographic quality). Used in `ClickEvent.nonce` to deduplicate retried events on the API server.

#### `CredentialStore.hpp`
**What it does:** Retrieves secrets in priority order:
1. **Environment variable** (highest priority — CI/CD pipelines)
2. **Windows Credential Manager** (for per-machine secrets without a `.env` file)

Wraps `CredReadW` from `wincred.h`. Never stores secrets in memory longer than needed.

---

### `src/storage/OfflineQueue.hpp` — Encrypted Offline Buffer

**What it does:** An **AES-256-GCM** encrypted append-only file (`%APPDATA%/VotingSystem/offline_queue.dat`). When the API is unreachable and all retries are exhausted, click events are written here. The `EventDispatcher` flush thread replays them every 5 seconds when connectivity returns.

**Wire format per record:**
```
[4 bytes: ciphertext length] [12 bytes: IV] [16 bytes: GCM tag] [N bytes: ciphertext]
```

Each record is independently encrypted with a fresh random IV — if one record is corrupted, others are still readable.

**Performance:** The `ofstream` is held open for the lifetime of the queue. No open/close per write — avoids a kernel round-trip on every event.

**On session validate:** `clear()` deletes the file — prevents pre-session clicks from replaying into the new quiz.

---

### `src/ui/` — User Interface Layer

#### `UiState.hpp` — Shared State (Thread-Safe)
**What it does:** The single shared data structure between the clicker thread and the UI render thread. All mutable fields are either `std::atomic` or guarded by a `std::mutex`.

Key fields:
| Field | Type | Purpose |
|-------|------|---------|
| `dongle_connected` | `atomic<bool>` | Green/red chip in header |
| `session_valid` | `atomic<bool>` | Session chip + enables dispatching |
| `validation_error` | `atomic<bool>` | Inline red error banner (no popup) |
| `pairing_active` | `atomic<bool>` | Controls Pair/Stop button state |
| `rows` | `vector<EventRow>` (mutex) | Live click event table |
| `registered_clickers` | `vector<RegisteredClicker>` (mutex) | Pairing panel list |
| `hwnd_to_wake` | `atomic<uintptr_t>` | HWND — clicker thread posts `WM_APP` to wake render loop |

**Why `PostMessage` instead of a flag?** The render loop sleeps in `MsgWaitForMultipleObjects`. When a click arrives, `addEvent()` posts a `WM_APP+1` message to the window — this wakes the loop in <1 ms without busy-spinning.

#### `MainWindow.hpp` — ImGui + D3D11 Window
**What it does:** The complete desktop UI. Uses **Dear ImGui** over **Direct3D 11** for GPU-accelerated rendering at the native refresh rate.

Sized at **70% of physical screen** on launch; operator can maximize to fill a 75"/85" smartboard. All layout dimensions are **percentage-based** — no fixed pixel values.

**UI sections:**
| Section | Description |
|---------|-------------|
| Header | Orange gradient banner — "VEDANTU CLICKER SYSTEM" + Dongle and Session status chips |
| Toolbar | "Pair Clickers" / "Stop Pairing" button (right-aligned) |
| Session Panel | Session ID input + PASTE + VALIDATE; info chips after validation; inline red error banner on failure |
| Event Table | Live scrolling table: No. / Device ID / Value / Entry Time — newest row highlighted in orange tint |
| Registration Panel | Floating modal (50%×60%) — clicker list with SN, slot, model, firmware |

**Fonts:** Loads `Poppins-Regular.ttf` from exe dir → Windows Fonts → Segoe UI → ImGui built-in fallback. Four sizes: 36/48/28/40 px for large-screen readability.

**Render loop:** `Present(0, 0)` (vsync OFF) with `MsgWaitForMultipleObjects(..., 1, QS_ALLINPUT)` — renders only when there is input or a message, sleeping ≤1 ms otherwise.

---

### `sdk_embed/` — Embedded SunVote SDK Binaries

All 21 files here are compiled directly **into** `VedantuClickerSystem.exe` as Windows RCDATA resources by the resource compiler (`rc.exe`). They are extracted to `%TEMP%\vedantu_svs\` at startup by `SdkExtractor`.

| File | Role |
|------|------|
| `QTWSCmdApp.exe` | SunVote SDK host process — manages USB dongle, RF receiver, exposes WebSocket API |
| `QTZT_SDK_V4.dll` | Core SunVote protocol library (closed source) |
| `Qt5Core.dll` | Qt5 runtime — core |
| `Qt5Network.dll` | Qt5 runtime — TCP/networking |
| `Qt5WebSockets.dll` | Qt5 runtime — WebSocket server |
| `hidapi.dll` | USB HID communication with the dongle |
| `libcurl.dll` | HTTP client (Qt's internal, not our libcurl) |
| `log4cplusU.dll` | Logging for QTWSCmdApp |
| `quazip1-qt5.dll` | Zip support for Qt |
| `zlib.dll` | Compression |
| `SDK_V4_resources.rcc` | Qt resource bundle (icons, QML) for QTWSCmdApp |
| `config.ini` | QTWSCmdApp configuration (role=server, jsonBase=1, port=9527) |
| `log.properties` | log4cplus configuration for QTWSCmdApp |
| `MSVCP140.dll` + variants | MSVC C++ runtime redistributables (7 DLLs) — ensures QTWSCmdApp runs on any Win10 machine without Visual C++ Redistributable install |
| `VCRUNTIME140.dll` | MSVC runtime |

---

### `resource.rc` — Windows Resource File

Declares what gets embedded into the EXE:
1. `IDI_APPLICATION ICON "vedantu.ico"` — app icon (taskbar, Explorer, title bar)
2. `VS_VERSION_INFO VERSIONINFO` — file metadata visible in Windows → Properties
3. Resource IDs 201–221: each `sdk_embed/` file mapped to an integer ID matching `src/hid/resource.h`

---

### `ui-dashboard/` — React Monitoring Dashboard

An optional **web-based monitoring dashboard** built with React + TypeScript + Vite. Connects to a local WebSocket relay and displays incoming click events in real time for remote monitoring (e.g., by a session coordinator on a separate machine).

| Path | Role |
|------|------|
| `src/App.tsx` | Root component — layout and state |
| `src/components/Header.tsx` | Session info bar |
| `src/components/LiveFeed.tsx` | Scrolling click event feed |
| `src/components/ClickCard.tsx` | Individual click event card |
| `src/components/SessionPanel.tsx` | Session metadata display |
| `src/components/StatsBar.tsx` | Live A/B/C/D/E/F answer distribution bars |
| `src/hooks/useClickerSocket.ts` | WebSocket connection + event deserialization |
| `src/types.ts` | TypeScript interfaces matching `ClickEvent` |
| `vite.config.ts` | Vite dev server config |
| `package.json` | Node dependencies (`react`, `vite`, `typescript`) |

**To run:** `cd ui-dashboard && npm install && npm run dev`

---

### `CMakeLists.txt` — Build System

Configures the project for CMake + MSVC:

| Section | What it does |
|---------|-------------|
| `find_package` | Resolves vcpkg-installed C++ dependencies |
| `file(GLOB_RECURSE SOURCES)` | Collects all `.cpp` and `.hpp` under `src/` |
| `add_executable(... WIN32 ... resource.rc)` | Builds GUI executable with resource compilation |
| `MSVC_RUNTIME_LIBRARY "MultiThreaded"` | Static runtime (`/MT`) — no MSVC runtime install needed for our EXE |
| `target_link_libraries` | Links: nlohmann_json, spdlog, OpenSSL, libcurl, ixwebsocket, imgui + Win32 DLLs |
| `if(SUNVOTE_SDK_DIR)` | Optional: copies SunVote SDK files into `sdk_embed/` from a developer's SDK installation |
| `POST_BUILD` | Copies the EXE to `AppRelease/` after every build |

---

### `vcpkg.json` — C++ Dependency Manifest

| Dependency | Version | Used for |
|-----------|---------|---------|
| `nlohmann-json` | ≥3.11 | JSON parsing of WebSocket events and API responses |
| `spdlog` | ≥1.12 | Async structured logging (6 categories) |
| `openssl` | ≥3.0 | AES-256-GCM (offline queue), HMAC-SHA256, RAND_bytes |
| `curl` | ≥8.0 | HTTPS POST with keep-alive, HTTP/2, TLS |
| `ixwebsocket` | ≥11.0 | WebSocket client connecting to QTWSCmdApp |
| `imgui` | ≥1.90 | Immediate-mode GUI rendered over D3D11 |

All packages are compiled with `x64-windows-static` triplet — statically linked into `VedantuClickerSystem.exe`. The user does **not** need any C++ runtime or library installed.

---

### `build.ps1` — Build Script

Run: `.\build.ps1`

Performs:
1. **vcpkg check** — clones and bootstraps if missing
2. **`.env` creation** — copies `.env.example` if no `.env` exists
3. **CMake configure** — generates Visual Studio project in `build/`
4. **CMake build** — compiles Release binary

Output: `build/Release/VedantuClickerSystem.exe` + `AppRelease/VedantuClickerSystem.exe`

### `setup_deps.ps1` — First-Time Setup

Checks and installs prerequisites (Git, CMake, vcpkg). Run once on a fresh developer machine before `build.ps1`.

### `.env.example` — Configuration Template

Documents every configuration key with comments. Copy to `.env` and fill in real values. **Never commit `.env`** — it contains API secrets.

---

## 5. Deployment

The deployable unit is the **`AppRelease/` folder** (or `VedantuClickerApp/`):

```
VedantuClickerSystem.exe   ← the entire app (SDK binaries embedded)
.env                       ← API credentials (edit before first run)
logs/                      ← created at runtime
```

**To run on a classroom PC:**
1. Copy `VedantuClickerSystem.exe` and `.env` to the PC
2. Plug in the SunVote USB dongle
3. Double-click `VedantuClickerSystem.exe`
4. Enter Session ID → click VALIDATE
5. Click "Pair Clickers" → power on student clickers
6. Start the quiz — clicks appear in the table in real time

**Requirements:** Windows 10+ x64. No .NET, no Visual C++ Redistributable, no admin rights. The MSVC runtime DLLs are bundled inside the EXE.

---

## 6. Logging Reference

All logs are in `logs/` next to the executable. Rotate at 10 MB, keep 5 files.

| File | When to read it |
|------|----------------|
| `SESSION.log` | App start, session validation result, shutdown |
| `HID_EVENTS.log` | Clicker connect/disconnect, pairing, click events |
| `API_CALLS.log` | Every POST attempt — status code, latency, retries |
| `PERFORMANCE.log` | Queue stats, offline flush, per-event timing |
| `SECURITY.log` | HMAC activity, nonce generation |
| `ERRORS.log` | All warnings and errors — start here when debugging |
| `LATENCY.csv` | End-to-end latency per click (entry→UI→API) |

---

## 7. Key Design Decisions

| Decision | Why |
|---------|-----|
| Single EXE, no installer | Classroom PCs may not have admin rights. USB-drop deployment. |
| SDK embedded as RCDATA | Prevents "missing DLL" errors; ensures exact SDK version is always used |
| Lock-free MPSC queue | Click callback thread must never block on API latency |
| SPSC drain thread | Prevents two threads calling `queue_.pop()` simultaneously (data race) |
| Generation counter on re-validate | Prevents stale retry threads from overwriting current-quiz answers |
| Ring buffer purge on validate | Closes the timestamp race window for pre-quiz clicks |
| AES-256-GCM offline queue | Click data is student answer data — encrypt at rest |
| Thread-local curl handles | One persistent TLS session per HTTP worker — eliminates handshake cost |
| Inline error banner (no MessageBox) | `MessageBoxA` steals focus and freezes the render loop |
| ImGui + D3D11 | GPU-rendered UI at native refresh rate; scales to 4K smartboards |
| Percentage-based layout | UI fills any screen size — laptop to 85" smartboard — without code changes |

---

*Document generated from source code — commit `8ae3358` on `main`.*
