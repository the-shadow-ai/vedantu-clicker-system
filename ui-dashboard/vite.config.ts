import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'

export default defineConfig({
  plugins: [react()],
  server: {
    port: 5173,
    // Allow connections from the network (useful for remote testing)
    host: '0.0.0.0',
  },
})
