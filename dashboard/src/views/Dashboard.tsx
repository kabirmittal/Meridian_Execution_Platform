import { useState, useEffect } from 'react'
import { Activity, Zap, Clock, Server } from 'lucide-react'
import { MetricsChart } from '../components/MetricsChart'
import { EventFeed } from '../components/EventFeed'
import { StatusBadge } from '../components/StatusBadge'
import { useWebSocket, useMetricStream } from '../hooks/useWebSocket'
import { useHealth } from '../hooks/useHealth'
import { fetchEffects } from '../services/api'
import type { EffectRecord } from '../types/api'

function StatCard({ label, value, icon: Icon, sub }: {
  label: string; value: string | number; icon: React.FC<any>; sub?: string
}) {
  return (
    <div className="rounded-xl border border-gray-800 bg-gray-900 p-4">
      <div className="flex items-center justify-between mb-2">
        <span className="text-xs text-gray-500 uppercase tracking-wider">{label}</span>
        <Icon size={16} className="text-gray-600" />
      </div>
      <p className="text-2xl font-bold font-mono text-white">{value}</p>
      {sub && <p className="text-xs text-gray-500 mt-1">{sub}</p>}
    </div>
  )
}

export function Dashboard() {
  const { status }   = useWebSocket()
  const health       = useHealth(4000)
  const cpuPoints    = useMetricStream('cpu.usage', 60)
  const memPoints    = useMetricStream('memory.used_pct', 60)
  const latPoints    = useMetricStream('http.latency_p99', 60)
  const errPoints    = useMetricStream('http.error_rate', 60)
  const [effects, setEffects] = useState<EffectRecord[]>([])

  useEffect(() => {
    const load = () => fetchEffects(80).then(setEffects).catch(() => null)
    load()
    const id = setInterval(load, 3000)
    return () => clearInterval(id)
  }, [])

  return (
    <div className="flex flex-col gap-6">
      {/* Status bar */}
      <div className="flex items-center justify-between">
        <div>
          <h1 className="text-xl font-bold text-white">Live Overview</h1>
          <p className="text-sm text-gray-500">Real-time metrics from the synthetic generator</p>
        </div>
        <StatusBadge status={status} pulse={status === 'connected'} />
      </div>

      {/* Stats row */}
      <div className="grid grid-cols-2 gap-4 sm:grid-cols-4">
        <StatCard label="Uptime"    value={health ? `${Math.floor(health.uptime_s)}s` : '—'} icon={Clock}    sub="orchestrator" />
        <StatCard label="Pipelines" value={health?.pipelines ?? 0}    icon={Server}   sub="registered" />
        <StatCard label="Events"    value={(health?.events_processed ?? 0).toLocaleString()} icon={Activity} sub="processed" />
        <StatCard label="Effects"   value={effects.length}            icon={Zap}      sub="captured" />
      </div>

      {/* Charts */}
      <div className="grid grid-cols-1 gap-4 lg:grid-cols-2">
        <MetricsChart title="CPU Usage"       data={cpuPoints} unit="%" threshold={80} color="#6366f1" />
        <MetricsChart title="Memory Used"     data={memPoints} unit="%" threshold={90} color="#8b5cf6" />
        <MetricsChart title="HTTP Latency P99" data={latPoints} unit="ms" threshold={200} color="#06b6d4" />
        <MetricsChart title="Error Rate"      data={errPoints} unit="%" threshold={5}  color="#f59e0b" />
      </div>

      {/* Effect feed */}
      <EventFeed effects={effects} />
    </div>
  )
}
