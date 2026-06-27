import { useState, useEffect } from 'react'
import { fetchHealth } from '../services/api'
import type { HealthResponse } from '../types/api'

export function useHealth(intervalMs = 5000) {
  const [health, setHealth] = useState<HealthResponse | null>(null)

  useEffect(() => {
    const poll = () => fetchHealth().then(setHealth).catch(() => null)
    poll()
    const id = setInterval(poll, intervalMs)
    return () => clearInterval(id)
  }, [intervalMs])

  return health
}
