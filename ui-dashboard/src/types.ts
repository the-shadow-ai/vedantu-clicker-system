// ── WebSocket message types from C++ backend ────────────────
export interface ClickMsg {
  type: 'click'
  sn: string
  value: number
  serial: number
  entry_ts: number   // ms since epoch, captured at C++ click-receive
}

export interface ApiDoneMsg {
  type: 'api_done'
  serial: number
  status: number
  latency_ms: number  // total round-trip from entry_ts to HTTP response
}

export interface StatusMsg {
  type: 'status'
  dongle: boolean
  session_valid: boolean
  session_title: string
}

export type WsMsg = ClickMsg | ApiDoneMsg | StatusMsg

// ── Merged event record (UI state) ──────────────────────────
export interface ClickRecord {
  serial: number
  sn: string
  value: number
  entry_ts: number         // C++ capture timestamp (ms)
  latency_ms: number | null // null until api_done arrives
  api_status: number | null
}
