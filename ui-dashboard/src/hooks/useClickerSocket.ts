import { useCallback, useEffect, useRef, useState } from 'react'
import type { ClickRecord, WsMsg, ClickMsg, ApiDoneMsg, StatusMsg } from '../types'

const WS_URL = 'ws://127.0.0.1:9099'
const MAX_EVENTS = 500

export type WsStatus = 'connecting' | 'connected' | 'disconnected'

interface UseClickerSocketReturn {
  events: ClickRecord[]
  wsStatus: WsStatus
  dongleOk: boolean
  sessionOk: boolean
  sessionTitle: string
  clearEvents: () => void
  sendValidate: (sessionId: string) => void
}

export function useClickerSocket(): UseClickerSocketReturn {
  const [events, setEvents] = useState<ClickRecord[]>([])
  const [wsStatus, setWsStatus] = useState<WsStatus>('connecting')
  const [dongleOk, setDongleOk] = useState(false)
  const [sessionOk, setSessionOk] = useState(false)
  const [sessionTitle, setSessionTitle] = useState('')

  const wsRef = useRef<WebSocket | null>(null)
  const reconnectTimer = useRef<ReturnType<typeof setTimeout> | null>(null)

  // Fast O(1) lookup map: serial → ClickRecord (kept in sync with events array)
  const eventMapRef = useRef<Map<number, ClickRecord>>(new Map())

  const clearEvents = useCallback(() => {
    eventMapRef.current.clear()
    setEvents([])
  }, [])

  // Send a session-validate command over the open WS connection
  const sendValidate = useCallback((sessionId: string) => {
    const ws = wsRef.current
    if (ws && ws.readyState === WebSocket.OPEN) {
      ws.send(JSON.stringify({ type: 'validate', session_id: sessionId }))
    }
  }, [])

  const connect = useCallback(() => {
    if (wsRef.current?.readyState === WebSocket.OPEN) return

    const ws = new WebSocket(WS_URL)
    wsRef.current = ws
    setWsStatus('connecting')

    ws.onopen = () => {
      setWsStatus('connected')
      if (reconnectTimer.current) clearTimeout(reconnectTimer.current)
    }

    ws.onmessage = (e: MessageEvent<string>) => {
      let msg: WsMsg
      try { msg = JSON.parse(e.data) as WsMsg }
      catch { return }

      if (msg.type === 'click') {
        const m = msg as ClickMsg
        const record: ClickRecord = {
          serial: m.serial,
          sn: m.sn,
          value: m.value,
          entry_ts: m.entry_ts,
          latency_ms: null,
          api_status: null,
        }
        // O(1) map insert
        eventMapRef.current.set(record.serial, record)
        // Trim to MAX_EVENTS from the map if needed
        if (eventMapRef.current.size > MAX_EVENTS) {
          // Remove the oldest (last) serial — map preserves insertion order
          const oldestKey = eventMapRef.current.keys().next().value
          if (oldestKey !== undefined) eventMapRef.current.delete(oldestKey)
        }
        // Rebuild display array (newest first) — only on click events
        setEvents([...eventMapRef.current.values()].reverse())
      }

      else if (msg.type === 'api_done') {
        const m = msg as ApiDoneMsg
        // O(1) patch — mutate only the one record in the map
        const existing = eventMapRef.current.get(m.serial)
        if (existing) {
          const updated: ClickRecord = { ...existing, latency_ms: m.latency_ms, api_status: m.status }
          eventMapRef.current.set(m.serial, updated)
          // Rebuild display array — React will reconcile by key so only the
          // changed card re-renders (ClickCard is memoised)
          setEvents([...eventMapRef.current.values()].reverse())
        }
      }

      else if (msg.type === 'status') {
        const m = msg as StatusMsg
        setDongleOk(m.dongle)
        setSessionOk(m.session_valid)
        setSessionTitle(m.session_title ?? '')
      }
    }

    ws.onclose = () => {
      setWsStatus('disconnected')
      reconnectTimer.current = setTimeout(() => connect(), 2000)
    }

    ws.onerror = () => { ws.close() }
  }, [])

  useEffect(() => {
    connect()
    return () => {
      if (reconnectTimer.current) clearTimeout(reconnectTimer.current)
      wsRef.current?.close()
    }
  }, [connect])

  return { events, wsStatus, dongleOk, sessionOk, sessionTitle, clearEvents, sendValidate }
}
