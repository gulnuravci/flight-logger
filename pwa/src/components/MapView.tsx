import { useEffect, useState, useMemo, useRef } from 'react'
import { MapContainer, TileLayer, Polyline, CircleMarker, useMap, useMapEvents } from 'react-leaflet'
import 'leaflet/dist/leaflet.css'
import type { FlightRow } from '../api'
import type { Turn } from '../analysis'
import { COORD_PASS, COORD_WARN } from '../analysis'
import type { TrafficPattern } from '../patternGeometry'
import { getPatternsInBounds } from '../ourairports'

// Flies the map to the highlight position whenever it changes
function FlyToHighlight({ row }: { row: FlightRow }) {
  const map = useMap()
  useEffect(() => {
    map.flyTo([row.lat, row.lon], 14, { duration: 1.2 })
  }, [map, row])
  return null
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

function altColor(alt: number, minAlt: number, maxAlt: number): string {
  const t = maxAlt === minAlt ? 0.5 : (alt - minAlt) / (maxAlt - minAlt)
  const hue = Math.round(240 - t * 240)
  return `hsl(${hue}, 85%, 55%)`
}

// Returns both the marker fill hex and the Tailwind text class for a turn's
// coordination score. Single source of truth — thresholds come from analysis.ts.
function turnColors(pct: number): { fill: string; text: string } {
  if (pct >= COORD_PASS) return { fill: '#22c55e', text: 'text-green-400' }
  if (pct >= COORD_WARN) return { fill: '#eab308', text: 'text-yellow-400' }
  return { fill: '#ef4444', text: 'text-red-400' }
}

// Fit map to the full flight path on first render
function FitBounds({ positions }: { positions: [number, number][] }) {
  const map = useMap()
  useEffect(() => {
    if (positions.length > 1) map.fitBounds(positions, { padding: [40, 40] })
  }, [map, positions])
  return null
}

// ---------------------------------------------------------------------------
// PatternLayer — listens to map move/zoom events and fetches airport patterns
// for whatever is currently visible in the viewport.
//
// Defined outside MapView so React doesn't remount it on every parent render.
// Must be rendered inside a MapContainer (uses useMap / useMapEvents).
// ---------------------------------------------------------------------------

function PatternLayer({
  onPatterns,
  onLoading,
}: {
  onPatterns: (p: TrafficPattern[]) => void
  onLoading:  (v: boolean)          => void
}) {
  const timerRef = useRef<ReturnType<typeof setTimeout> | null>(null)
  const map = useMap()

  // Debounced handler — waits 350 ms after the last move/zoom event before
  // querying, so panning doesn't scheduleFetch a fetch on every scroll tick.
  function scheduleFetch() {
    if (timerRef.current) clearTimeout(timerRef.current)
    timerRef.current = setTimeout(async () => {
      const zoom = map.getZoom()
      const b    = map.getBounds()
      onLoading(true)
      try {
        const p = await getPatternsInBounds(
          b.getSouth(), b.getNorth(), b.getWest(), b.getEast(), zoom,
        )
        onPatterns(p)
      } finally {
        onLoading(false)
      }
    }, 350)
  }

  useMapEvents({ moveend: scheduleFetch, zoomend: scheduleFetch })

  // Clear any pending timer on unmount
  useEffect(() => () => { if (timerRef.current) clearTimeout(timerRef.current) }, [])

  return null
}

// ---------------------------------------------------------------------------
// HoverLayer — finds the nearest flight-path point to the cursor and
// notifies the parent so it can render a floating tooltip.
//
// Defined outside MapView so React doesn't remount it on parent re-renders.
// ---------------------------------------------------------------------------

function HoverLayer({
  gpsData,
  onHover,
}: {
  gpsData:  FlightRow[]
  onHover:  (row: FlightRow | null, pt: { x: number; y: number } | null) => void
}) {
  const map = useMap()

  useMapEvents({
    mousemove(e) {
      const cursorPx = map.latLngToContainerPoint(e.latlng)
      let best: FlightRow | null = null
      let bestDist = Infinity

      for (const row of gpsData) {
        const px  = map.latLngToContainerPoint([row.lat, row.lon])
        const d   = Math.hypot(cursorPx.x - px.x, cursorPx.y - px.y)
        if (d < bestDist) { bestDist = d; best = row }
      }

      if (best && bestDist < 28) {
        const pt = map.latLngToContainerPoint([best.lat, best.lon])
        onHover(best, { x: pt.x, y: pt.y })
      } else {
        onHover(null, null)
      }
    },
    mouseout() { onHover(null, null) },
  })

  return null
}

// Small white dot drawn on the map at the hovered position
function HoverDot({ row }: { row: FlightRow }) {
  return (
    <CircleMarker
      center={[row.lat, row.lon]}
      radius={5}
      pathOptions={{ color: '#0f172a', fillColor: '#fff', fillOpacity: 1, weight: 2 }}
    />
  )
}

function fmtT(s: number) {
  if (s < 60) return `${Math.floor(s)}s`
  return `${Math.floor(s / 60)}m ${Math.floor(s % 60).toString().padStart(2, '0')}s`
}

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

interface Props {
  data: FlightRow[]
  highlightRow?: FlightRow | null
  turns?: Turn[]
}

// ---------------------------------------------------------------------------
// Component
// ---------------------------------------------------------------------------

export default function MapView({ data, highlightRow, turns }: Props) {
  const [selected, setSelected]             = useState<FlightRow | null>(null)
  const [selectedTurn, setSelectedTurn]     = useState<Turn | null>(null)
  const [visiblePatterns, setVisiblePatterns] = useState<TrafficPattern[]>([])
  const [patternLoading, setPatternLoading]   = useState(false)
  const [hoveredRow, setHoveredRow]           = useState<FlightRow | null>(null)
  const [hoverPt,    setHoverPt]              = useState<{ x: number; y: number } | null>(null)
  const [hoveredPattern, setHoveredPattern]   = useState<TrafficPattern | null>(null)
  const [patternPt,      setPatternPt]        = useState<{ x: number; y: number } | null>(null)
  const patternHideTimer = useRef<ReturnType<typeof setTimeout> | null>(null)

  function handleHover(row: FlightRow | null, pt: { x: number; y: number } | null) {
    setHoveredRow(row); setHoverPt(pt)
  }

  function showPatternTooltip(p: TrafficPattern, pt: { x: number; y: number }) {
    if (patternHideTimer.current) clearTimeout(patternHideTimer.current)
    setHoveredPattern(p); setPatternPt(pt)
  }

  function scheduleHidePattern() {
    patternHideTimer.current = setTimeout(() => {
      setHoveredPattern(null); setPatternPt(null)
    }, 200)
  }

  // Filter to rows with a real GPS fix
  const gpsData   = useMemo(() => data.filter(r => r.lat !== 0 && r.lon !== 0), [data])
  const positions = useMemo(() => gpsData.map(r => [r.lat, r.lon] as [number, number]), [gpsData])
  const minAlt    = useMemo(() => Math.min(...gpsData.map(r => r.alt)), [gpsData])
  const maxAlt    = useMemo(() => Math.max(...gpsData.map(r => r.alt)), [gpsData])
  const waypoints = useMemo(() => gpsData.filter((_, i) => i % 15 === 0), [gpsData])

  const turnMarkers = useMemo(() => {
    if (!turns?.length || !gpsData.length) return []
    return turns.map(turn => {
      const midT = (turn.startT + turn.endT) / 2
      const row  = gpsData.reduce((best, r) =>
        Math.abs(r.t - midT) < Math.abs(best.t - midT) ? r : best
      )
      return { turn, row }
    }).filter(({ row }) => row.lat !== 0 && row.lon !== 0)
  }, [turns, gpsData])

  if (gpsData.length < 2) {
    return (
      <div className="flex items-center justify-center h-64 text-slate-500 text-sm">
        No GPS data for this flight.
      </div>
    )
  }

  return (
    <div className="relative rounded-xl overflow-hidden">
      <MapContainer
        center={positions[0]}
        zoom={12}
        style={{ height: '62vh', width: '100%' }}
        zoomControl={false}
        attributionControl={false}
      >
        <TileLayer url="https://{s}.basemaps.cartocdn.com/light_all/{z}/{x}/{y}{r}.png" />
        <FitBounds positions={positions} />

        {/* ── Viewport-adaptive traffic pattern overlay ────────────────────
            PatternLayer listens to moveend/zoomend and populates
            visiblePatterns. Rendered before the flight path so the coloured
            route draws on top. */}
        <PatternLayer
          onPatterns={setVisiblePatterns}
          onLoading={setPatternLoading}
        />

        <HoverLayer gpsData={gpsData} onHover={handleHover} />

        {visiblePatterns.flatMap(p =>
          p.legs.map(leg => (
            <Polyline
              key={`${p.airportIcao}-${leg.name}`}
              positions={leg.positions}
              pathOptions={{
                color: '#475569',
                weight: hoveredPattern?.airportIcao === p.airportIcao ? 3 : 2,
                opacity: hoveredPattern?.airportIcao === p.airportIcao ? 1 : 0.75,
                dashArray: '10, 7',
              }}
              eventHandlers={{
                mouseover(e) { showPatternTooltip(p, { x: e.containerPoint.x, y: e.containerPoint.y }) },
                mousemove(e) { showPatternTooltip(p, { x: e.containerPoint.x, y: e.containerPoint.y }) },
                mouseout()   { scheduleHidePattern() },
              }}
            />
          ))
        )}

        {/* ── Altitude-colored flight path ──────────────────────────────── */}
        {gpsData.slice(0, -1).map((row, i) => (
          <Polyline
            key={i}
            positions={[positions[i], positions[i + 1]]}
            pathOptions={{ color: altColor(row.alt, minAlt, maxAlt), weight: 3, opacity: 0.9 }}
          />
        ))}

        {/* Takeoff marker — green */}
        <CircleMarker
          center={positions[0]}
          radius={7}
          pathOptions={{ color: '#fff', fillColor: '#22c55e', fillOpacity: 1, weight: 2 }}
        />

        {/* Landing marker — red */}
        <CircleMarker
          center={positions[positions.length - 1]}
          radius={7}
          pathOptions={{ color: '#fff', fillColor: '#ef4444', fillOpacity: 1, weight: 2 }}
        />

        {/* Turn markers */}
        {turnMarkers.map(({ turn, row }) => (
          <CircleMarker
            key={`turn-${turn.index}`}
            center={[row.lat, row.lon]}
            radius={10}
            pathOptions={{
              color: '#0f172a', fillColor: turnColors(turn.coordinationPct).fill,
              fillOpacity: 1, weight: 2.5,
            }}
            eventHandlers={{ click: () => { setSelectedTurn(turn); setSelected(null) } }}
          />
        ))}

        {/* Worst-moment highlight */}
        {highlightRow && highlightRow.lat !== 0 && (
          <>
            <FlyToHighlight row={highlightRow} />
            <CircleMarker
              center={[highlightRow.lat, highlightRow.lon]}
              radius={18}
              pathOptions={{ color: '#f97316', fillColor: '#f97316', fillOpacity: 0.15, weight: 2 }}
            />
            <CircleMarker
              center={[highlightRow.lat, highlightRow.lon]}
              radius={7}
              pathOptions={{ color: '#fff', fillColor: '#f97316', fillOpacity: 1, weight: 2 }}
            />
          </>
        )}

        {/* Invisible tappable waypoints */}
        {waypoints.map((row, i) => (
          <CircleMarker
            key={`wp-${i}`}
            center={[row.lat, row.lon]}
            radius={12}
            pathOptions={{ color: 'transparent', fillColor: 'transparent', fillOpacity: 0 }}
            eventHandlers={{ click: () => { setSelected(row); setSelectedTurn(null) } }}
          />
        ))}

        {/* Hover dot — white pin at the nearest path point */}
        {hoveredRow && !selected && !selectedTurn && (
          <HoverDot row={hoveredRow} />
        )}
      </MapContainer>

      {/* ── Hover tooltip ────────────────────────────────────────────────────
          Floats near the cursor when hovering the flight path.
          pointer-events:none so it never blocks mouse events. */}
      {hoveredRow && hoverPt && !selected && !selectedTurn && (
        <div
          className="absolute bg-slate-900/95 backdrop-blur rounded-lg px-3 py-2.5 text-xs pointer-events-none z-[1001] border border-slate-700/60"
          style={{
            left: hoverPt.x + 14,
            top:  Math.max(8, hoverPt.y - 80),
          }}
        >
          <p className="text-slate-400 mb-2 font-mono">{fmtT(hoveredRow.t)}</p>
          <div className="grid grid-cols-2 gap-x-5 gap-y-1">
            <div>
              <span className="text-slate-500">Alt </span>
              <span className="text-sky-400 font-semibold">{hoveredRow.alt.toFixed(0)} m</span>
            </div>
            <div>
              <span className="text-slate-500">Spd </span>
              <span className="text-white font-semibold">{hoveredRow.speed_kts.toFixed(0)} kts</span>
            </div>
            <div>
              <span className="text-slate-500">Hdg </span>
              <span className="text-white font-semibold">{hoveredRow.heading_deg.toFixed(0)}°</span>
            </div>
            <div>
              <span className="text-slate-500">Lat G </span>
              <span className={`font-semibold ${Math.abs(hoveredRow.ay) > 0.05 ? 'text-yellow-400' : 'text-green-400'}`}>
                {hoveredRow.ay.toFixed(3)}g
              </span>
            </div>
          </div>
        </div>
      )}

      {/* ── Traffic pattern tooltip ───────────────────────────────────────────
          pointer-events:auto so the AirNav link is clickable.
          onMouseEnter cancels the 200 ms hide timer so the user can move
          their cursor from the polyline onto this card safely. */}
      {hoveredPattern && patternPt && (
        <div
          className="absolute bg-slate-900/97 backdrop-blur rounded-xl px-4 py-3 text-xs z-[1002] border border-slate-700/60 shadow-lg"
          style={{
            left: Math.min(patternPt.x + 14, 620),
            top:  Math.max(8, patternPt.y - 110),
            minWidth: 180,
          }}
          onMouseEnter={() => { if (patternHideTimer.current) clearTimeout(patternHideTimer.current) }}
          onMouseLeave={scheduleHidePattern}
        >
          {/* Header */}
          <p className="font-semibold text-white text-sm leading-tight">
            {hoveredPattern.airportIcao}
          </p>
          <p className="text-slate-400 mb-2 leading-tight">{hoveredPattern.airportName}</p>

          {/* Details */}
          <div className="space-y-1 mb-3">
            <div className="flex justify-between gap-4">
              <span className="text-slate-500">Runway</span>
              <span className="text-slate-200 font-medium">Rwy {hoveredPattern.runway}</span>
            </div>
            <div className="flex justify-between gap-4">
              <span className="text-slate-500">Traffic</span>
              <span className="text-slate-200 font-medium capitalize">{hoveredPattern.direction}</span>
            </div>
            <div className="flex justify-between gap-4">
              <span className="text-slate-500">Pattern alt</span>
              <span className="text-slate-200 font-medium">{hoveredPattern.patternAltMsl.toLocaleString()} ft MSL</span>
            </div>
          </div>

          {/* External link */}
          <a
            href={`https://www.airnav.com/airport/${hoveredPattern.airportIcao}`}
            target="_blank"
            rel="noopener noreferrer"
            className="flex items-center gap-1 text-blue-400 hover:text-blue-300 no-underline transition-colors"
            style={{ pointerEvents: 'auto' }}
          >
            More info on AirNav
            <span className="text-slate-500">↗</span>
          </a>
        </div>
      )}

      {/* ── Pattern info banner ──────────────────────────────────────────────
          Centred at top.  Shows a loading spinner while the CSV fetches, then
          airport info once patterns are visible.  Updates live as you pan/zoom:
          • zoom ≥ 11 → airports in view (up to 8)
          • zoom < 11  → patterns hidden (too zoomed out) */}
      {patternLoading && visiblePatterns.length === 0 && (
        <div className="absolute top-3 left-1/2 -translate-x-1/2 bg-slate-900/85 backdrop-blur rounded-full px-3 py-1.5 text-xs text-slate-400 z-[1000] whitespace-nowrap">
          Loading airport data…
        </div>
      )}
      {!patternLoading && visiblePatterns.length > 0 && (
        <div className="absolute top-3 left-1/2 -translate-x-1/2 bg-slate-900/85 backdrop-blur rounded-full px-3 py-1.5 text-xs z-[1000] whitespace-nowrap flex items-center gap-1.5">
          <span className="w-4 h-px bg-slate-400 inline-block border-t-2 border-dashed border-slate-400" />
          {visiblePatterns.length === 1 ? (
            <>
              <span className="text-slate-200 font-medium">
                {visiblePatterns[0].airportIcao} Rwy {visiblePatterns[0].runway}
              </span>
              <span className="text-slate-500">·</span>
              <span className="text-slate-400 capitalize">{visiblePatterns[0].direction} traffic</span>
              <span className="text-slate-500">·</span>
              <span className="text-slate-400">{visiblePatterns[0].patternAltMsl.toLocaleString()} ft</span>
            </>
          ) : (
            <>
              <span className="text-slate-200 font-medium">
                {visiblePatterns.length} airports
              </span>
              <span className="text-slate-500">·</span>
              <span className="text-slate-400">traffic patterns</span>
            </>
          )}
        </div>
      )}

      {/* Altitude legend */}
      <div className="absolute top-3 right-3 bg-slate-900/80 backdrop-blur rounded-lg px-3 py-2 text-xs space-y-1 z-[1000]">
        <p className="text-slate-400 mb-1">Altitude</p>
        <div className="flex items-center gap-2">
          <span className="w-3 h-3 rounded-full" style={{ background: altColor(maxAlt, minAlt, maxAlt) }} />
          <span className="text-slate-300">{maxAlt.toFixed(0)}m</span>
        </div>
        <div className="flex items-center gap-2">
          <span className="w-3 h-3 rounded-full" style={{ background: altColor(minAlt, minAlt, maxAlt) }} />
          <span className="text-slate-300">{minAlt.toFixed(0)}m</span>
        </div>
      </div>

      {/* Turn legend */}
      {turnMarkers.length > 0 && (
        <div className="absolute top-3 left-3 bg-slate-900/80 backdrop-blur rounded-lg px-3 py-2 text-xs z-[1000]">
          <p className="text-slate-400 mb-1.5">Turns</p>
          <div className="flex flex-col gap-1">
            {[['bg-green-500', '≥ 90%'], ['bg-yellow-500', '70–89%'], ['bg-red-500', '< 70%']].map(([cls, label]) => (
              <div key={cls} className="flex items-center gap-2">
                <span className={`w-2.5 h-2.5 rounded-full ${cls}`} />
                <span className="text-slate-300">{label}</span>
              </div>
            ))}
          </div>
        </div>
      )}

      {/* Tap-a-waypoint bottom sheet */}
      {selected && (
        <div className="absolute bottom-0 left-0 right-0 bg-slate-900/95 backdrop-blur border-t border-slate-700 rounded-t-2xl p-4 z-[1000]">
          <div className="flex justify-between items-center mb-3">
            <span className="text-sm font-medium text-slate-300">t = {selected.t.toFixed(1)}s</span>
            <button onClick={() => setSelected(null)} className="text-slate-400 text-lg leading-none">✕</button>
          </div>
          <div className="grid grid-cols-4 gap-3 text-center">
            <div><p className="text-xs text-slate-500">Alt</p><p className="text-sm font-semibold text-sky-400">{selected.alt.toFixed(0)}m</p></div>
            <div><p className="text-xs text-slate-500">Speed</p><p className="text-sm font-semibold text-white">{selected.speed_kts.toFixed(0)} kts</p></div>
            <div><p className="text-xs text-slate-500">Heading</p><p className="text-sm font-semibold text-white">{selected.heading_deg.toFixed(0)}°</p></div>
            <div><p className="text-xs text-slate-500">Temp</p><p className="text-sm font-semibold text-orange-400">{selected.temp.toFixed(1)}°C</p></div>
          </div>
        </div>
      )}

      {/* Tap-a-turn bottom sheet */}
      {selectedTurn && (
        <div className="absolute bottom-0 left-0 right-0 bg-slate-900/95 backdrop-blur border-t border-slate-700 rounded-t-2xl p-4 z-[1000]">
          <div className="flex justify-between items-center mb-3">
            <div className="flex items-center gap-2">
              <span className="text-base leading-none">{selectedTurn.direction === 'left' ? '←' : '→'}</span>
              <span className="text-sm font-medium text-slate-300">Turn {selectedTurn.index} · {selectedTurn.direction}</span>
            </div>
            <button onClick={() => setSelectedTurn(null)} className="text-slate-400 text-lg leading-none">✕</button>
          </div>
          <div className="grid grid-cols-3 gap-3 text-center">
            <div><p className="text-xs text-slate-500">Coordination</p><p className={`text-sm font-semibold ${turnColors(selectedTurn.coordinationPct).text}`}>{selectedTurn.coordinationPct}%</p></div>
            <div><p className="text-xs text-slate-500">Yaw Rate</p><p className="text-sm font-semibold text-white">{selectedTurn.avgYawRate_dps}°/s</p></div>
            <div><p className="text-xs text-slate-500">Peak Slip</p><p className="text-sm font-semibold text-orange-400">{selectedTurn.worstAy}g</p></div>
          </div>
        </div>
      )}

    </div>
  )
}
