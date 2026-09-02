import { useMemo } from 'react'
import type { ClickRecord } from '../types'

interface Props {
  events: ClickRecord[]
  dongleOk: boolean
}

export function StatsBar({ events, dongleOk }: Props) {
  const stats = useMemo(() => {
    const resolved = events.filter(e => e.latency_ms !== null)
    const avgLat = resolved.length > 0
      ? Math.round(resolved.reduce((s, e) => s + (e.latency_ms ?? 0), 0) / resolved.length)
      : null
    const minLat = resolved.length > 0
      ? Math.min(...resolved.map(e => e.latency_ms ?? Infinity))
      : null
    const fastCount = resolved.filter(e => (e.latency_ms ?? 0) < 100).length

    return { total: events.length, avgLat, minLat, fastCount }
  }, [events])

  function latColour(ms: number | null): string {
    if (ms === null) return 'var(--text-muted)'
    if (ms < 100) return 'var(--lat-fast)'
    if (ms <= 300) return 'var(--lat-mid)'
    return 'var(--lat-slow)'
  }

  return (
    <div className="stats-bar">
      <div className="stat-card">
        <span className="stat-label">Total Clicks</span>
        <span className="stat-value copyable">{stats.total}</span>
        <span className="stat-sub">since dashboard opened</span>
      </div>
      <div className="stat-card">
        <span className="stat-label">Avg API Latency</span>
        <span
          className="stat-value copyable"
          style={{ color: latColour(stats.avgLat) }}
        >
          {stats.avgLat !== null ? `${stats.avgLat}ms` : '—'}
        </span>
        <span className="stat-sub">entry → HTTP response</span>
      </div>
      <div className="stat-card">
        <span className="stat-label">Min Latency</span>
        <span
          className="stat-value copyable"
          style={{ color: latColour(stats.minLat) }}
        >
          {stats.minLat !== null && stats.minLat !== Infinity ? `${stats.minLat}ms` : '—'}
        </span>
        <span className="stat-sub">best round-trip seen</span>
      </div>
      <div className="stat-card">
        <span className="stat-label">Dongle</span>
        <span
          className="stat-value"
          style={{ color: dongleOk ? 'var(--ok)' : 'var(--err)', fontSize: '1.1rem' }}
        >
          {dongleOk ? '● CONNECTED' : '● DISCONNECTED'}
        </span>
        <span className="stat-sub">
          {stats.fastCount > 0 ? `${stats.fastCount} fast responses (&lt;100ms)` : 'no data yet'}
        </span>
      </div>
    </div>
  )
}
