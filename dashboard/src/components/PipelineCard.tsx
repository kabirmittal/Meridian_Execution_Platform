import { Trash2, Layers, Activity } from 'lucide-react'
import { StatusBadge } from './StatusBadge'
import type { PipelineInfo } from '../types/api'

interface Props {
  pipeline: PipelineInfo
  onDelete: (id: string) => void
}

export function PipelineCard({ pipeline, onDelete }: Props) {
  return (
    <div className="rounded-xl border border-gray-800 bg-gray-900 p-4 flex flex-col gap-3">
      <div className="flex items-start justify-between">
        <div>
          <h3 className="font-semibold text-gray-100">{pipeline.name}</h3>
          <p className="text-xs text-gray-500 font-mono mt-0.5">#{pipeline.id}</p>
        </div>
        <button
          onClick={() => onDelete(pipeline.id)}
          className="text-gray-600 hover:text-red-400 transition-colors"
          aria-label="Delete pipeline"
        >
          <Trash2 size={14} />
        </button>
      </div>

      <div className="flex items-center gap-4 text-xs text-gray-400">
        <StatusBadge status={pipeline.mode} pulse={pipeline.running} />
        <span className="flex items-center gap-1">
          <Layers size={12} /> {pipeline.stage_count} stage{pipeline.stage_count !== 1 ? 's' : ''}
        </span>
        <span className="flex items-center gap-1">
          <Activity size={12} /> {pipeline.events_processed.toLocaleString()} events
        </span>
      </div>

      {pipeline.stages.length > 0 && (
        <div className="flex flex-col gap-1">
          {pipeline.stages.map((s, i) => (
            <div key={i} className="flex items-center justify-between rounded bg-gray-800 px-3 py-1.5">
              <span className="text-xs text-gray-300">{s.label}</span>
              <span className="text-[10px] text-gray-500 font-mono">{s.processor_type}</span>
            </div>
          ))}
        </div>
      )}
    </div>
  )
}
