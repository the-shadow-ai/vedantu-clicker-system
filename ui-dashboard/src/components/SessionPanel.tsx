import { useState, useRef } from 'react'
import type { WsStatus } from '../hooks/useClickerSocket'

interface Props {
  wsStatus: WsStatus
  sessionOk: boolean
  sessionTitle: string
  onValidate: (sessionId: string) => void
}

export function SessionPanel({ wsStatus, sessionOk, sessionTitle, onValidate }: Props) {
  const [sid, setSid] = useState('')
  const [loading, setLoading] = useState(false)
  const [result, setResult] = useState<string | null>(null)
  const [pasteError, setPasteError] = useState(false)
  const inputRef = useRef<HTMLInputElement>(null)

  async function handleValidate() {
    if (!sid.trim()) return
    setLoading(true)
    setResult(null)
    try {
      onValidate(sid.trim())
      setResult('Validation request sent to backend.')
    } finally {
      setLoading(false)
    }
  }

  // Touch-friendly paste: reads from clipboard API
  async function handlePaste() {
    setPasteError(false)
    try {
      let text = ''
      if (navigator.clipboard && navigator.clipboard.readText) {
        text = await navigator.clipboard.readText()
      } else {
        // Fallback: focus input and fire a paste command
        inputRef.current?.focus()
        document.execCommand('paste')
        return
      }
      if (text) {
        setSid(text.trim())
        inputRef.current?.focus()
      }
    } catch {
      setPasteError(true)
      // Fallback: try to focus and let the user long-press paste
      inputRef.current?.focus()
    }
  }

  function handleClear() {
    setSid('')
    setResult(null)
    inputRef.current?.focus()
  }

  const wsLabel =
    wsStatus === 'connected' ? 'Connected to C++ backend' :
    wsStatus === 'connecting' ? 'Connecting...' :
    'Backend disconnected — retrying...'

  return (
    <aside className="session-panel">
      <div className="panel-heading">Session Configuration</div>

      {/* WS Connection notice */}
      <div className={`ws-notice ${wsStatus}`}>{wsLabel}</div>

      {/* Session validate form */}
      <div className="session-form">
        <label className="form-label" htmlFor="session-id-input">Session ID</label>

        {/* Input + paste/clear row */}
        <div style={{ display: 'flex', gap: 8 }}>
          <input
            ref={inputRef}
            id="session-id-input"
            className="form-input"
            type="text"
            inputMode="text"
            placeholder="Enter or paste Session ID…"
            value={sid}
            onChange={e => setSid(e.target.value)}
            onKeyDown={e => e.key === 'Enter' && handleValidate()}
            autoComplete="off"
            autoCorrect="off"
            autoCapitalize="none"
            spellCheck={false}
            style={{ flex: 1, minWidth: 0 }}
          />
          {/* Touch-friendly paste button */}
          <button
            className="btn-paste"
            onClick={handlePaste}
            title="Paste from clipboard"
            type="button"
            aria-label="Paste session ID"
          >
            📋
          </button>
          {/* Clear button — only shown when there's text */}
          {sid && (
            <button
              className="btn-paste btn-paste-clear"
              onClick={handleClear}
              title="Clear"
              type="button"
              aria-label="Clear session ID"
            >
              ✕
            </button>
          )}
        </div>

        {pasteError && (
          <p style={{ fontSize: '0.72rem', color: 'var(--warn)', lineHeight: 1.4 }}>
            ⚠ Clipboard access denied. Long-press the field and tap Paste.
          </p>
        )}

        <button
          className="btn-validate"
          onClick={handleValidate}
          disabled={loading || !sid.trim()}
        >
          {loading ? 'Validating…' : 'Validate Session'}
        </button>
        {result && (
          <p style={{ fontSize: '0.72rem', color: 'var(--text-muted)', lineHeight: 1.4 }}>
            {result}
          </p>
        )}
      </div>

      {/* Session details */}
      {sessionOk && (
        <div className="session-info">
          <div className="session-info-row">
            <span className="session-info-key">Status</span>
            <span className="session-info-val copyable" style={{ color: 'var(--ok)' }}>● ACTIVE</span>
          </div>
          {sessionTitle && (
            <div className="session-info-row">
              <span className="session-info-key">Title</span>
              <span className="session-info-val copyable">{sessionTitle}</span>
            </div>
          )}
        </div>
      )}

      {/* How-to note */}
      <div style={{ marginTop: 'auto', fontSize: '0.68rem', color: 'var(--text-muted)', lineHeight: 1.7 }}>
        <p style={{ fontWeight: 700, marginBottom: 4 }}>Latency Guide</p>
        <p style={{ display: 'flex', gap: 6, alignItems: 'center' }}>
          <span style={{ color: 'var(--lat-fast)', fontWeight: 700 }}>● &lt;100ms</span> Fast
        </p>
        <p style={{ display: 'flex', gap: 6, alignItems: 'center' }}>
          <span style={{ color: 'var(--lat-mid)', fontWeight: 700 }}>● 100–300ms</span> Acceptable
        </p>
        <p style={{ display: 'flex', gap: 6, alignItems: 'center' }}>
          <span style={{ color: 'var(--lat-slow)', fontWeight: 700 }}>● &gt;300ms</span> Slow
        </p>
        <p style={{ marginTop: 8, fontStyle: 'italic' }}>
          Latency = C++ entry → API HTTP response.<br />
          All text is selectable &amp; copyable.
        </p>
      </div>
    </aside>
  )
}
