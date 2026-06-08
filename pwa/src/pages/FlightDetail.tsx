import { useEffect, useState, useMemo } from 'react'
import { useParams, Link } from 'react-router-dom'
import {
  ResponsiveContainer, LineChart, Line, XAxis, YAxis,
  Tooltip, CartesianGrid,
} from 'recharts'
import { Card, Skeleton, Tabs, Text, Title } from '@mantine/core'
import { fetchFlight, type FlightRow } from '../api'
import { analyzeFlight, type FlightAnalysis } from '../analysis'
import MapView from '../components/MapView'
import SummaryView from '../components/SummaryView'

// Downsample to at most `maxPoints` evenly-spaced rows
function downsample(data: FlightRow[], maxPoints = 500): FlightRow[] {
  if (data.length <= maxPoints) return data
  const step = Math.ceil(data.length / maxPoints)
  return data.filter((_, i) => i % step === 0)
}

// Compact form for chart axis ticks: "45s", "1m23s"
function formatElapsed(s: number): string {
  if (s < 60) return `${Math.floor(s)}s`
  return `${Math.floor(s / 60)}m${Math.floor(s % 60).toString().padStart(2, '0')}s`
}

// Human-readable form for the header: "45s", "8m 02s"
function formatDuration(s: number): string {
  const m = Math.floor(s / 60)
  const sec = Math.floor(s % 60)
  if (m === 0) return `${sec}s`
  return `${m}m ${sec.toString().padStart(2, '0')}s`
}

interface ChartCardProps {
  title: string
  data: FlightRow[]
  lines: { key: keyof FlightRow; color: string; label: string }[]
  unit: string
}

function ChartCard({ title, data, lines, unit }: ChartCardProps) {
  return (
    <Card bg="dark.8" radius="md" p="md">
      <Text size="sm" fw={500} c="dimmed" mb="xs">{title}</Text>
      <ResponsiveContainer width="100%" height={180}>
        <LineChart data={data} margin={{ top: 4, right: 4, left: -20, bottom: 0 }}>
          <CartesianGrid strokeDasharray="3 3" stroke="#1e293b" />
          <XAxis
            dataKey="t"
            tickFormatter={formatElapsed}
            tick={{ fill: '#64748b', fontSize: 11 }}
            axisLine={false}
            tickLine={false}
          />
          <YAxis
            tick={{ fill: '#64748b', fontSize: 11 }}
            axisLine={false}
            tickLine={false}
            unit={unit}
          />
          <Tooltip
            contentStyle={{ background: '#0f172a', border: '1px solid #1e293b', borderRadius: 8 }}
            labelStyle={{ color: '#94a3b8', fontSize: 11 }}
            labelFormatter={v => formatElapsed(Number(v))}
          />
          {lines.map(l => (
            <Line
              key={l.key as string}
              type="monotone"
              dataKey={l.key as string}
              stroke={l.color}
              dot={false}
              strokeWidth={1.5}
              name={l.label}
            />
          ))}
        </LineChart>
      </ResponsiveContainer>
    </Card>
  )
}

export default function FlightDetail() {
  const { id } = useParams<{ id: string }>()
  const [data, setData] = useState<FlightRow[]>([])
  const [loading, setLoading] = useState(true)
  const [error, setError] = useState(false)
  const [tab, setTab] = useState('map')
  const [highlightRow, setHighlightRow] = useState<FlightRow | null>(null)

  function viewOnMap(row: FlightRow) {
    setHighlightRow(row)
    setTab('map')
  }

  // Run analysis whenever data loads — memoised so it only recomputes on data change
  const analysis: FlightAnalysis | null = useMemo(
    () => (data.length > 0 ? analyzeFlight(data) : null),
    [data]
  )

  useEffect(() => {
    if (!id) return
    const controller = new AbortController()
    fetchFlight(Number(id), controller.signal)
      .then(rows => setData(downsample(rows)))
      .catch(err => { if (err.name !== 'AbortError') setError(true) })
      .finally(() => setLoading(false))
    return () => controller.abort()
  }, [id])

  // Compute duration from the loaded data's first/last timestamp
  const flightDuration = data.length >= 2
    ? data[data.length - 1].t - data[0].t
    : null

  return (
    <div className="min-h-screen bg-slate-950 text-white">
      {/* Header */}
      <div className="px-4 pt-12 pb-4 border-b border-slate-800">
        <div className="flex items-center gap-4 mb-4">
          <Link to="/" className="text-blue-400 text-sm no-underline">← Back</Link>
          <div>
            <Title order={2} size="h3" fw={600}>
              Flight {String(id).padStart(3, '0')}
            </Title>
            {!loading && !error && flightDuration != null && (
              <Text size="xs" c="dimmed" mt={2}>
                {formatDuration(flightDuration)}
              </Text>
            )}
          </div>
        </div>

        <Tabs value={tab} onChange={v => setTab(v ?? 'map')}>
          <Tabs.List>
            <Tabs.Tab value="map">Map</Tabs.Tab>
            <Tabs.Tab value="charts">Charts</Tabs.Tab>
            <Tabs.Tab value="summary">Summary</Tabs.Tab>
          </Tabs.List>
        </Tabs>
      </div>

      {/* Content */}
      <div className="px-4 py-6 space-y-4">
        {loading && (
          <div className="flex flex-col gap-4">
            {Array.from({ length: 4 }).map((_, i) => (
              <Skeleton key={i} height={208} radius="md" />
            ))}
          </div>
        )}

        {error && (
          <div className="py-12 text-center">
            <Text c="dimmed" size="sm">Failed to load flight data.</Text>
          </div>
        )}

        {!loading && !error && tab === 'map' && (
          <MapView data={data} highlightRow={highlightRow} turns={analysis?.turns} />
        )}

        {!loading && !error && tab === 'summary' && analysis && (
          <SummaryView analysis={analysis} onViewOnMap={viewOnMap} />
        )}

        {!loading && !error && tab === 'charts' && (
          <>
            <ChartCard
              title="Altitude"
              data={data}
              lines={[{ key: 'alt', color: '#38bdf8', label: 'Alt' }]}
              unit="m"
            />
            <ChartCard
              title="Accelerometer"
              data={data}
              lines={[
                { key: 'ax', color: '#f43f5e', label: 'X' },
                { key: 'ay', color: '#a3e635', label: 'Y' },
                { key: 'az', color: '#fb923c', label: 'Z' },
              ]}
              unit="g"
            />
            <ChartCard
              title="Gyroscope"
              data={data}
              lines={[
                { key: 'gx', color: '#c084fc', label: 'X' },
                { key: 'gy', color: '#34d399', label: 'Y' },
                { key: 'gz', color: '#fbbf24', label: 'Z' },
              ]}
              unit="°/s"
            />
            <ChartCard
              title="Temperature"
              data={data}
              lines={[{ key: 'temp', color: '#fb923c', label: 'Temp' }]}
              unit="°C"
            />
          </>
        )}
      </div>
    </div>
  )
}
