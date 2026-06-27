import { useState } from 'react'
import { Plus, RefreshCw } from 'lucide-react'
import { PipelineCard } from '../components/PipelineCard'
import { usePipelines } from '../hooks/usePipelines'
import type { RuntimeMode, ProcessorType } from '../types/api'

export function Pipelines() {
  const { pipelines, loading, error, refresh, create, remove } = usePipelines()
  const [showForm, setShowForm] = useState(false)
  const [name,     setName]     = useState('my-pipeline')
  const [mode,     setMode]     = useState<RuntimeMode>('simulation')
  const [metric,   setMetric]   = useState('cpu.usage')
  const [thresh,   setThresh]   = useState('80')

  const handleCreate = async () => {
    await create({
      name,
      mode,
      stages: [{
        label:          'threshold-detector',
        processor_type: 'threshold' as ProcessorType,
        config:         { metric_name: metric, threshold: parseFloat(thresh) },
      }],
    })
    setShowForm(false)
  }

  return (
    <div className="flex flex-col gap-6">
      <div className="flex items-center justify-between">
        <div>
          <h1 className="text-xl font-bold text-white">Pipelines</h1>
          <p className="text-sm text-gray-500">Create and manage event-processing pipelines</p>
        </div>
        <div className="flex gap-2">
          <button
            onClick={refresh}
            className="flex items-center gap-1.5 rounded-lg border border-gray-700 px-3 py-2 text-sm text-gray-300 hover:bg-gray-800"
          >
            <RefreshCw size={14} /> Refresh
          </button>
          <button
            onClick={() => setShowForm(v => !v)}
            className="flex items-center gap-1.5 rounded-lg bg-indigo-600 px-3 py-2 text-sm text-white hover:bg-indigo-500"
          >
            <Plus size={14} /> New Pipeline
          </button>
        </div>
      </div>

      {/* Quick-create form */}
      {showForm && (
        <div className="rounded-xl border border-gray-700 bg-gray-900 p-5 flex flex-col gap-4">
          <h2 className="font-semibold text-gray-200">Quick Create — Threshold Detector</h2>
          <div className="grid grid-cols-2 gap-4 sm:grid-cols-4">
            {[
              { label: 'Name',       value: name,   set: setName,   type: 'text' },
              { label: 'Metric',     value: metric, set: setMetric, type: 'text' },
              { label: 'Threshold',  value: thresh, set: setThresh, type: 'number' },
            ].map(f => (
              <label key={f.label} className="flex flex-col gap-1">
                <span className="text-xs text-gray-400">{f.label}</span>
                <input
                  type={f.type}
                  value={f.value}
                  onChange={e => f.set(e.target.value)}
                  className="rounded-lg bg-gray-800 border border-gray-700 px-3 py-2 text-sm text-gray-100 focus:outline-none focus:ring-1 focus:ring-indigo-500"
                />
              </label>
            ))}
            <label className="flex flex-col gap-1">
              <span className="text-xs text-gray-400">Mode</span>
              <select
                value={mode}
                onChange={e => setMode(e.target.value as RuntimeMode)}
                className="rounded-lg bg-gray-800 border border-gray-700 px-3 py-2 text-sm text-gray-100 focus:outline-none focus:ring-1 focus:ring-indigo-500"
              >
                <option value="simulation">Simulation</option>
                <option value="shadow">Shadow</option>
                <option value="production">Production</option>
              </select>
            </label>
          </div>
          <div className="flex gap-2">
            <button
              onClick={handleCreate}
              className="rounded-lg bg-indigo-600 px-4 py-2 text-sm text-white hover:bg-indigo-500"
            >
              Create
            </button>
            <button
              onClick={() => setShowForm(false)}
              className="rounded-lg border border-gray-700 px-4 py-2 text-sm text-gray-300 hover:bg-gray-800"
            >
              Cancel
            </button>
          </div>
        </div>
      )}

      {error && (
        <div className="rounded-lg border border-red-900 bg-red-950 p-3 text-sm text-red-300">
          {error}
        </div>
      )}

      {loading && pipelines.length === 0 ? (
        <p className="text-sm text-gray-500">Loading…</p>
      ) : pipelines.length === 0 ? (
        <div className="rounded-xl border border-dashed border-gray-700 p-10 text-center">
          <p className="text-gray-500">No pipelines yet — create one to get started.</p>
        </div>
      ) : (
        <div className="grid grid-cols-1 gap-4 sm:grid-cols-2 xl:grid-cols-3">
          {pipelines.map(p => (
            <PipelineCard key={p.id} pipeline={p} onDelete={remove} />
          ))}
        </div>
      )}
    </div>
  )
}
