import { useEffect, useRef, useState } from 'react'
import { MeridianWS } from '../services/websocket'
import type { WsFrame, WsMetricFrame } from '../types/api'

const WS_URL = (import.meta.env.VITE_WS_URL as string | undefined)
  ?? `ws://${window.location.hostname}:8000/ws`

export type WsStatus = 'connecting' | 'connected' | 'disconnected'

export function useWebSocket() {
  const wsRef  = useRef<MeridianWS | null>(null)
  const [status, setStatus] = useState<WsStatus>('disconnected')
  const [lastFrame, setLastFrame] = useState<WsFrame | null>(null)

  useEffect(() => {
    const ws = new MeridianWS(WS_URL)
    wsRef.current = ws
    const offFrame  = ws.onFrame(f  => setLastFrame(f))
    const offStatus = ws.onStatus(s => setStatus(s))
    ws.connect()
    return () => {
      offFrame(); offStatus()
      ws.disconnect()
    }
  }, [])

  return { status, lastFrame }
}

/** Returns a rolling buffer of N metric points for a given metric_name */
export function useMetricStream(metricName: string, maxPoints = 60) {
  const { lastFrame } = useWebSocket()
  const [points, setPoints] = useState<{ ts: number; value: number }[]>([])

  useEffect(() => {
    if (!lastFrame || lastFrame.type !== 'metric') return
    const f = lastFrame as WsMetricFrame
    if (f.metric_name !== metricName) return
    setPoints(prev => {
      const next = [...prev, { ts: f.timestamp_ms, value: f.value }]
      return next.slice(-maxPoints)
    })
  }, [lastFrame, metricName, maxPoints])

  return points
}
