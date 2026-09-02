import { useClickerSocket } from './hooks/useClickerSocket'
import { Header } from './components/Header'
import { SessionPanel } from './components/SessionPanel'
import { LiveFeed } from './components/LiveFeed'
import { StatsBar } from './components/StatsBar'

export default function App() {
  const {
    events, wsStatus, dongleOk, sessionOk, sessionTitle,
    clearEvents, sendValidate,
  } = useClickerSocket()

  return (
    <div className="app-shell">
      <Header wsStatus={wsStatus} dongleOk={dongleOk} sessionOk={sessionOk} />

      <main className="main-content">
        <StatsBar events={events} dongleOk={dongleOk} />

        <div className="row-2col">
          <SessionPanel
            wsStatus={wsStatus}
            sessionOk={sessionOk}
            sessionTitle={sessionTitle}
            onValidate={sendValidate}
          />
          <LiveFeed events={events} onClear={clearEvents} />
        </div>
      </main>
    </div>
  )
}
