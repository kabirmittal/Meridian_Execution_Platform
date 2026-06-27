// ── Core domain types ─────────────────────────────────────────────────────────

export type RuntimeMode = 'simulation' | 'shadow' | 'production'
export type EffectType  = 'notify' | 'store' | 'suppress' | 'forward' | 'none'
export type ProcessorType =
  | 'threshold' | 'anomaly' | 'rate_limit'
  | 'aggregator' | 'log_filter' | 'heartbeat'

// ── Metric types ──────────────────────────────────────────────────────────────

export interface MetricPoint {
  timestamp_ms: number
  value:        number
}

export interface MetricSeries {
  metric_name: string
  source:      string
  points:      MetricPoint[]
}

// ── Pipeline types ────────────────────────────────────────────────────────────

export interface PipelineStageInfo {
  label:            string
  processor_type:   string
  events_processed: number
  errors:           number
}

export interface PipelineInfo {
  id:               string
  name:             string
  mode:             RuntimeMode
  stage_count:      number
  stages:           PipelineStageInfo[]
  events_processed: number
  running:          boolean
  created_at:       number
}

export interface PipelineCreate {
  name:   string
  mode:   RuntimeMode
  stages: {
    label:          string
    processor_type: ProcessorType
    config:         Record<string, unknown>
  }[]
}

// ── Effect types ──────────────────────────────────────────────────────────────

export interface EffectRecord {
  effect_type:   EffectType
  pipeline_id:   string
  pipeline_name: string
  details:       Record<string, unknown>
  timestamp_ms:  number
}

// ── WebSocket frames ──────────────────────────────────────────────────────────

export interface WsMetricFrame {
  type:         'metric'
  source:       string
  metric_name:  string
  value:        number
  unit:         string
  timestamp_ms: number
}

export interface WsConnectedFrame {
  type:    'connected'
  message: string
  clients: number
}

export interface WsBatchFrame {
  type:         'batch_ingest'
  count:        number
  timestamp_ms: number
}

export type WsFrame = WsMetricFrame | WsConnectedFrame | WsBatchFrame

// ── Health ────────────────────────────────────────────────────────────────────

export interface HealthResponse {
  status:            string
  version:           string
  uptime_s:          number
  pipelines:         number
  events_processed:  number
}
