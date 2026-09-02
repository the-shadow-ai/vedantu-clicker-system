import { useState, useCallback, memo } from 'react'
import type { ClickRecord } from '../types'

// Value → label mapping (A=1, B=2, C=3, ...)
const VALUE_LABELS: Record<number, string> = {
  1: 'A', 2: 'B', 3: 'C', 4: 'D', 5: 'E', 6: 'F',
}

function latencyClass(ms: number | null): string {
  if (ms === null) return 'pending'
  if (ms < 100) return 'fast'
  if (ms <= 300) return 'mid'
  return 'slow'
}

// Value accent colours (cycle through palette)
const VALUE_COLOURS = [
  '#2563EB', '#7C3AED', '#DB2777', '#D97706',
  '#059669', '#DC2626', '#0891B2',
]
function accentFor(value: number): string {
  return VALUE_COLOURS[(value - 1) % VALUE_COLOURS.length] ?? '#2563EB'
}

interface Props {
  record: ClickRecord
}

function ClickCardInner({ record }: Props) {
  const [copied, setCopied] = useState(false)

  const handleCopy = useCallback(() => {
    const payload = JSON.stringify({
      serial: record.serial,
      device: record.sn,
      value: record.value,
      label: VALUE_LABELS[record.value] ?? String(record.value),
      latency_ms: record.latency_ms,
      api_status: record.api_status,
    }, null, 2)
    navigator.clipboard.writeText(payload).then(() => {
      setCopied(true)
      setTimeout(() => setCopied(false), 1500)
    })
  }, [record])

  const latClass = latencyClass(record.latency_ms)
  const accent = accentFor(record.value)
  const label = VALUE_LABELS[record.value] ?? String(record.value)

  return (
    <div
      className="click-card"
      style={{ /* override accent bar colour */ } as React.CSSProperties}
    >
      {/* Accent bar */}
      <span
        style={{
          position: 'absolute', left: 0, top: 12, bottom: 12,
          width: 3, borderRadius: 2, background: accent,
        }}
      />

      {/* Top row: serial + value badge */}
      <div className="card-row" style={{ paddingLeft: 10 }}>
        <span className="card-serial copyable">#{record.serial}</span>
        <div className="card-value-badge" style={{ background: accent }}>
          {label}
        </div>
      </div>

      {/* Device ID */}
      <div style={{ paddingLeft: 10 }}>
        <div style={{ fontSize: '0.63rem', color: 'var(--text-muted)', fontWeight: 600, textTransform: 'uppercase', letterSpacing: '0.06em', marginBottom: 2 }}>
          Device ID
        </div>
        <span className="card-device">{record.sn}</span>
      </div>


      {/* Latency + copy row */}
      <div className="card-row" style={{ paddingLeft: 10 }}>
        <span className={`latency-pill ${latClass}`}>
          {record.latency_ms !== null
            ? `⚡ ${record.latency_ms}ms`
            : '⏳ waiting…'}
        </span>
        <button
          className={`copy-btn${copied ? ' copied' : ''}`}
          onClick={handleCopy}
          title="Copy event as JSON"
        >
          {copied ? '✓ Copied' : '📋 Copy'}
        </button>
      </div>
    </div>
  )
}

export const ClickCard = memo(ClickCardInner)
