import { useState } from 'react'
import { LayoutDashboard, Layers, Github, ExternalLink } from 'lucide-react'
import { Dashboard } from './views/Dashboard'
import { Pipelines } from './views/Pipelines'

type Tab = 'dashboard' | 'pipelines'

const NAV: { id: Tab; label: string; Icon: React.FC<any> }[] = [
  { id: 'dashboard', label: 'Dashboard', Icon: LayoutDashboard },
  { id: 'pipelines', label: 'Pipelines', Icon: Layers },
]

export default function App() {
  const [tab, setTab] = useState<Tab>('dashboard')

  return (
    <div className="min-h-screen bg-gray-950 text-gray-100 flex flex-col">
      {/* Top nav */}
      <header className="border-b border-gray-800 px-6 py-3 flex items-center gap-6">
        <div className="flex items-center gap-2">
          {/* Logo mark */}
          <svg width="22" height="22" viewBox="0 0 22 22" fill="none">
            <circle cx="11" cy="11" r="10" stroke="#6366f1" strokeWidth="1.5" />
            <path d="M6 11h10M11 6v10" stroke="#6366f1" strokeWidth="1.5" strokeLinecap="round" />
            <circle cx="11" cy="11" r="2" fill="#6366f1" />
          </svg>
          <span className="font-bold text-white tracking-tight">Meridian</span>
          <span className="text-xs text-gray-500 border border-gray-700 rounded px-1 py-0.5 ml-1">v1.0</span>
        </div>

        <nav className="flex gap-1">
          {NAV.map(({ id, label, Icon }) => (
            <button
              key={id}
              onClick={() => setTab(id)}
              className={`flex items-center gap-1.5 rounded-lg px-3 py-1.5 text-sm transition-colors
                ${tab === id
                  ? 'bg-gray-800 text-white'
                  : 'text-gray-400 hover:text-gray-200 hover:bg-gray-800/50'
                }`}
            >
              <Icon size={14} />
              {label}
            </button>
          ))}
        </nav>

        <div className="ml-auto flex items-center gap-3">
          <a
            href="https://github.com"
            target="_blank"
            rel="noopener noreferrer"
            className="flex items-center gap-1.5 text-xs text-gray-500 hover:text-gray-300 transition-colors"
          >
            <Github size={14} />
            Source
            <ExternalLink size={10} />
          </a>
        </div>
      </header>

      {/* Page content */}
      <main className="flex-1 px-6 py-6 max-w-7xl mx-auto w-full">
        {tab === 'dashboard' && <Dashboard />}
        {tab === 'pipelines' && <Pipelines />}
      </main>
    </div>
  )
}
