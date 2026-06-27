/**
 * Typed API client for the Meridian orchestrator REST endpoints.
 */

const BASE = import.meta.env.VITE_API_URL ?? '/api'

async function json<T>(path: string, init?: RequestInit): Promise<T> {
  const res = await fetch(`${BASE}${path}`, {
    headers: { 'Content-Type': 'application/json', ...init?.headers },
    ...init,
  })
  if (!res.ok) {
    const msg = await res.text().catch(() => res.statusText)
    throw new Error(`API ${res.status}: ${msg}`)
  }
  return res.json() as Promise<T>
}

import type {
  HealthResponse, MetricSeries, PipelineInfo,
  PipelineCreate, EffectRecord,
} from '../types/api'

// ── Health ────────────────────────────────────────────────────────────────────
export const fetchHealth    = (): Promise<HealthResponse>   => json('/health')

// ── Metrics ───────────────────────────────────────────────────────────────────
export const listMetrics    = (): Promise<string[]>          => json('/metrics')
export const fetchMetric    = (key: string, limit = 120): Promise<MetricSeries> =>
  json(`/metrics/${encodeURIComponent(key)}?limit=${limit}`)
export const fetchEffects   = (limit = 50): Promise<EffectRecord[]> =>
  json(`/metrics/effects/recent?limit=${limit}`)

export const ingestMetric   = (body: {
  source: string; metric_name: string; value: number; unit?: string
}): Promise<{ status: string }> =>
  json('/metrics/ingest', { method: 'POST', body: JSON.stringify(body) })

// ── Pipelines ─────────────────────────────────────────────────────────────────
export const listPipelines  = (): Promise<PipelineInfo[]>   => json('/pipelines')
export const getPipeline    = (id: string): Promise<PipelineInfo> => json(`/pipelines/${id}`)
export const createPipeline = (body: PipelineCreate): Promise<PipelineInfo> =>
  json('/pipelines', { method: 'POST', body: JSON.stringify(body) })
export const deletePipeline = (id: string): Promise<void> =>
  json(`/pipelines/${id}`, { method: 'DELETE' })
