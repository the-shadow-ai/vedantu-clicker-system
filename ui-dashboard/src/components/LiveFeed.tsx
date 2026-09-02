import type { ClickRecord } from '../types'
import { ClickCard } from './ClickCard'

interface Props {
  events: ClickRecord[]
  onClear: () => void
}

export function LiveFeed({ events, onClear }: Props) {
  return (
    <div className="live-feed">
      <div className="feed-header">
        <div style={{ display: 'flex', alignItems: 'center', gap: 10 }}>
          <h2 className="feed-title" style={{ fontSize: '0.70rem' }}>Live Click Events</h2>
          <span className="feed-count">{events.length} event{events.length !== 1 ? 's' : ''}</span>
        </div>
        {events.length > 0 && (
          <button className="btn-clear" onClick={onClear}>Clear</button>
        )}
      </div>

      <div className="feed-grid">
        {events.length === 0 ? (
          <div className="empty-state">
            <div className="empty-state-icon">📡</div>
            <p>Waiting for clicker events…</p>
            <p style={{ marginTop: 6, fontSize: '0.75rem' }}>Make sure the dongle is connected and the session is active.</p>
          </div>
        ) : (
          events.map(ev => (
            <ClickCard key={ev.serial} record={ev} />
          ))
        )}
      </div>
    </div>
  )
}
