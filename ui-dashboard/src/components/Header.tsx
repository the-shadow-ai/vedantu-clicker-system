import type { WsStatus } from '../hooks/useClickerSocket'

interface Props {
  wsStatus: WsStatus
  dongleOk: boolean
  sessionOk: boolean
}

export function Header({ wsStatus, dongleOk, sessionOk }: Props) {
  return (
    <header className="header">
      <h1 className="header-title">Vedantu Clicker Dashboard</h1>
      <div className="header-chips">
        <span className={`chip ${dongleOk ? 'ok' : 'err'}`}>
          <span className="chip-dot" />
          Dongle: {dongleOk ? 'CONNECTED' : 'DISCONNECTED'}
        </span>
        <span className={`chip ${sessionOk ? 'ok' : 'warn'}`}>
          <span className="chip-dot" />
          Session: {sessionOk ? 'ACTIVE' : 'PENDING'}
        </span>
        <span className={`chip ${wsStatus === 'connected' ? 'ok' : wsStatus === 'connecting' ? 'warn' : 'err'}`}>
          <span className="chip-dot" />
          UI: {wsStatus.toUpperCase()}
        </span>
      </div>
    </header>
  )
}
