import { useState, useEffect, useCallback } from 'react'
import { listPipelines, createPipeline, deletePipeline } from '../services/api'
import type { PipelineInfo, PipelineCreate } from '../types/api'

export function usePipelines() {
  const [pipelines, setPipelines] = useState<PipelineInfo[]>([])
  const [loading,   setLoading]   = useState(false)
  const [error,     setError]     = useState<string | null>(null)

  const refresh = useCallback(async () => {
    setLoading(true)
    try {
      setPipelines(await listPipelines())
      setError(null)
    } catch (e) {
      setError(String(e))
    } finally {
      setLoading(false)
    }
  }, [])

  useEffect(() => { refresh() }, [refresh])

  const create = useCallback(async (spec: PipelineCreate) => {
    const p = await createPipeline(spec)
    setPipelines(prev => [...prev, p])
    return p
  }, [])

  const remove = useCallback(async (id: string) => {
    await deletePipeline(id)
    setPipelines(prev => prev.filter(p => p.id !== id))
  }, [])

  return { pipelines, loading, error, refresh, create, remove }
}
