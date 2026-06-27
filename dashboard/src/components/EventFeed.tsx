import { useEffect, useRef } from 'react'
import { Bell, Database, EyeOff, ArrowRight, type LucideIcon } from 'lucide-react'
import type { EffectRecord } from '../types/api'

const ICONS: Record<string, LucideIcon> = {
  notify:   Bell,
  store:    Database,
  suppress: EyeOff,
  forward:  ArrowRight,
}

const COLOURS: Record<string, string> = {
  notify:   'text-amber-400 bg-amber-400/10',
  store:    'text-sky-400 bg-sky-400/10',
  suppress: 'text-gray-400 bg-gray-700',
  forward:  'text-purple-400 bg-purple-400/10',
}

interface Props { effects: EffectRecord[]; maxRows?: number }

export function EventFeed({ effects, maxRows = 50 }: Props) {
  const bottomRef = useRef<HTMLDivElement>(null)

  useEffect(() => {
    bottomRef.current?.scrollIntoView({ behavior: 'smooth' })
  }, [effects])

  const visible = effects.slice(-maxRows)

  return (
    <div className="rounded-xl border border-gray-800 bg-gray-900 p-4 flex flex-col gap-2 h-full">
      <h3 className="text-sm font-semibold text-gray-300 mb-1">Live Effect Feed</h3>
      {visible.length === 0 ? (
        <p className="text-sm text-gray-600 mt-4 text-center">No effects yet — waiting for pipeline activity…</p>
      ) : (
        <div className="overflow-y-auto flex flex-col gap-1.5 max-h-72 pr-1">
          {visible.map((e, i) => {
            const Icon   = ICONS[e.effect_type] ?? Bell
            const colour = COLOURS[e.effect_type] ?? 'text-gray-400 bg-gray-700'
            const ts     = new Date(e.timestamp_ms).toLocaleTimeString()
            const title  = String((e.details as any)?.title ?? e.effect_type)
            return (
              <div key={i} className="flex items-start gap-2 rounded-lg bg-gray-800 px-3 py-2">
                <span className={`mt-0.5 rounded p-1 ${colour}`}>
                  <Icon size={12} />
                </span>
                <div className="flex-1 min-w-0">
                  <p className="text-xs font-medium text-gray-200 truncate">{title}</p>
                  <p className="text-[10px] text-gray-500">{e.pipeline_name} · {ts}</p>
                </div>
              </div>
            )
          })}
          <div ref={bottomRef} />
        </div>
      )}
    </div>
  )
}
