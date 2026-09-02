# Vedantu Clicker System — UI Dashboard

A React + TypeScript + Vite monitoring dashboard that connects to the
Vedantu Clicker System backend over WebSocket and displays live click
event telemetry.

## Prerequisites

- Node.js ≥ 18
- npm ≥ 9

## Setup

```bash
# Install dependencies
cd ui-dashboard
npm install

# Start development server
npm run dev
```

The dashboard will open at **http://localhost:5173** by default.

## Configuration

The dashboard connects to the C++ backend's WebSocket server.  
The port is controlled by `UI_WS_PORT` in the root `.env` (default: `9099`).

If you need to change the WebSocket URL, edit the `WS_URL` constant in
`src/hooks/useClickerSocket.ts`.

## Build for production

```bash
npm run build
# Output goes to dist/
```

## Project structure

```
ui-dashboard/
├── src/
│   ├── components/
│   │   ├── ClickCard.tsx       # Individual click event card
│   │   ├── Header.tsx          # App header with status indicators
│   │   ├── LiveFeed.tsx        # Scrolling live event list
│   │   ├── SessionPanel.tsx    # Session info and controls
│   │   └── StatsBar.tsx        # Aggregate statistics bar
│   ├── hooks/
│   │   └── useClickerSocket.ts # WebSocket connection hook
│   ├── App.tsx
│   ├── index.css
│   ├── main.tsx
│   └── types.ts                # Shared TypeScript types
├── index.html
├── package.json
├── tsconfig.json
└── vite.config.ts
```
