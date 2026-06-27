import type { WsStatus } from '../hooks/useWebSocket'
import type { RuntimeMode } from '../types/api'

interface Props { status: WsStatus | RuntimeMode | string; pulse?: boolean }

const COLOURS: Record<string, string> = {
  connected:    'bg-emerald-500',
  ok:           'bg-emerald-500',
  production:   'bg-emerald-500',
  disconnected: 'bg-red-500',
  connecting:   'bg-amber-400',
  shadow:       'bg-purple-500',
  simulation:   'bg-sky-500',
  running:      'bg-emerald-500',
  stopped:      'bg-gray-500',
}

export function StatusBadge({ status, pulse = false }: Props) {
  const colour = COLOURS[status] ?? 'bg-gray-400'
  return (
    <span className="inline-flex items-center gap-1.5 text-xs font-medium capitalize">
      <span className={`relative flex h-2 w-2 ${pulse ? 'animate-pulse' : ''}`}>
        <span className={`${colour} inline-flex h-full w-full rounded-full`} />
      </span>
      {status}
    </span>
  )
}
