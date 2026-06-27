/**
 * Reconnecting WebSocket wrapper with typed message callbacks.
 */
import type { WsFrame } from '../types/api'

type FrameHandler = (frame: WsFrame) => void
type StatusHandler = (status: 'connecting' | 'connected' | 'disconnected') => void

export class MeridianWS {
  private ws:       WebSocket | null = null
  private handlers: FrameHandler[]   = []
  private statusFns: StatusHandler[] = []
  private retryMs   = 2000
  private stopped   = false

  constructor(private readonly url: string) {}

  connect(): void {
    if (this.stopped) return
    this._notify('connecting')
    this.ws = new WebSocket(this.url)

    this.ws.onopen = () => {
      this.retryMs = 2000
      this._notify('connected')
    }

    this.ws.onmessage = (ev) => {
      try {
        const frame = JSON.parse(ev.data as string) as WsFrame
        this.handlers.forEach(h => h(frame))
      } catch {
        // ignore malformed frames
      }
    }

    this.ws.onclose = () => {
      if (!this.stopped) {
        this._notify('disconnected')
        setTimeout(() => this.connect(), this.retryMs)
        this.retryMs = Math.min(this.retryMs * 1.5, 30_000)
      }
    }

    this.ws.onerror = () => this.ws?.close()
  }

  disconnect(): void {
    this.stopped = true
    this.ws?.close()
  }

  onFrame(fn: FrameHandler): () => void {
    this.handlers.push(fn)
    return () => { this.handlers = this.handlers.filter(h => h !== fn) }
  }

  onStatus(fn: StatusHandler): () => void {
    this.statusFns.push(fn)
    return () => { this.statusFns = this.statusFns.filter(h => h !== fn) }
  }

  private _notify(status: Parameters<StatusHandler>[0]) {
    this.statusFns.forEach(fn => fn(status))
  }
}
