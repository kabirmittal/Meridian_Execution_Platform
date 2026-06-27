import {
  LineChart, Line, XAxis, YAxis, CartesianGrid,
  Tooltip, ResponsiveContainer, ReferenceLine,
} from 'recharts'

interface DataPoint { ts: number; value: number }

interface Props {
  title:      string
  data:       DataPoint[]
  unit?:      string
  threshold?: number
  color?:     string
  height?:    number
}

function fmtTime(ms: number) {
  return new Date(ms).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit' })
}

export function MetricsChart({
  title, data, unit = '', threshold, color = '#6366f1', height = 200,
}: Props) {
  const latest = data.at(-1)?.value

  return (
    <div className="rounded-xl border border-gray-800 bg-gray-900 p-4">
      <div className="mb-3 flex items-baseline justify-between">
        <h3 className="text-sm font-semibold text-gray-300">{title}</h3>
        {latest !== undefined && (
          <span className="text-2xl font-mono font-bold text-white">
            {latest.toFixed(1)}<span className="text-xs text-gray-500 ml-1">{unit}</span>
          </span>
        )}
      </div>
      {data.length === 0 ? (
        <div className="flex h-[200px] items-center justify-center text-sm text-gray-600">
          Waiting for data…
        </div>
      ) : (
        <ResponsiveContainer width="100%" height={height}>
          <LineChart data={data} margin={{ top: 4, right: 4, bottom: 0, left: -20 }}>
            <CartesianGrid strokeDasharray="3 3" stroke="#1f2937" />
            <XAxis
              dataKey="ts"
              tickFormatter={fmtTime}
              tick={{ fontSize: 10, fill: '#6b7280' }}
              tickLine={false}
              axisLine={false}
            />
            <YAxis
              domain={[0, 100]}
              tick={{ fontSize: 10, fill: '#6b7280' }}
              tickLine={false}
              axisLine={false}
            />
            <Tooltip
              formatter={(v: number) => [`${v.toFixed(2)} ${unit}`, title]}
              labelFormatter={fmtTime}
              contentStyle={{ background: '#111827', border: '1px solid #374151', borderRadius: 8 }}
              itemStyle={{ color: '#e5e7eb' }}
              labelStyle={{ color: '#9ca3af', fontSize: 11 }}
            />
            {threshold !== undefined && (
              <ReferenceLine y={threshold} stroke="#ef4444" strokeDasharray="4 4" />
            )}
            <Line
              type="monotone"
              dataKey="value"
              stroke={color}
              strokeWidth={2}
              dot={false}
              isAnimationActive={false}
            />
          </LineChart>
        </ResponsiveContainer>
      )}
    </div>
  )
}
