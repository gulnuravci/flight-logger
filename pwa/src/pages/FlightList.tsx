import { useEffect, useState } from 'react'
import { Link } from 'react-router-dom'
import { Badge, Skeleton, Text, Title, UnstyledButton } from '@mantine/core'
import { fetchFlights, type FlightMeta } from '../api'

function formatDuration(s: number): string {
  const m = Math.floor(s / 60)
  const sec = Math.floor(s % 60)
  if (m === 0) return `${sec}s`
  return `${m}m ${sec.toString().padStart(2, '0')}s`
}

export default function FlightList() {
  const [flights, setFlights] = useState<FlightMeta[]>([])
  const [loading, setLoading] = useState(true)
  const [error, setError] = useState(false)

  function loadFlights(signal?: AbortSignal) {
    setError(false)
    setLoading(true)
    fetchFlights(signal)
      .then(setFlights)
      .catch(err => { if (err.name !== 'AbortError') setError(true) })
      .finally(() => setLoading(false))
  }

  useEffect(() => {
    const controller = new AbortController()
    loadFlights(controller.signal)
    return () => controller.abort()
  }, [])  // eslint-disable-line react-hooks/exhaustive-deps

  return (
    <div className="min-h-screen bg-slate-950 text-white">
      {/* Header */}
      <div className="px-4 pt-12 pb-6 border-b border-slate-800">
        <div className="flex items-center gap-3 mb-3">
          <span className="text-2xl">✈</span>
          <Title order={1} size="h3" fw={600}>Flight Logger</Title>
        </div>

        {loading && (
          <Badge variant="dot" color="gray">Connecting…</Badge>
        )}
        {!loading && error && (
          <div className="flex items-center gap-2">
            <Badge variant="dot" color="red">Can't reach Pico</Badge>
            <UnstyledButton onClick={() => loadFlights()} className="text-sm text-blue-400 underline">
              Retry
            </UnstyledButton>
          </div>
        )}
        {!loading && !error && (
          <Badge variant="dot" color="green">Connected to Pico</Badge>
        )}
      </div>

      {/* Flight list */}
      <div className="divide-y divide-slate-800">
        {loading && (
          <div className="px-4 py-4 space-y-4">
            {Array.from({ length: 3 }).map((_, i) => (
              <Skeleton key={i} height={56} radius="md" />
            ))}
          </div>
        )}

        {!loading && !error && flights.length === 0 && (
          <div className="px-4 py-12 text-center">
            <Text c="dimmed" size="sm">No flights recorded yet.</Text>
          </div>
        )}

        {!loading && error && (
          <div className="px-4 py-12 text-center">
            <Text c="dimmed" size="sm">
              Make sure your Pico is on and connected to this WiFi network.
            </Text>
          </div>
        )}

        {!loading && !error && flights.map(flight => (
          <Link
            key={flight.id}
            to={`/flight/${flight.id}`}
            className="flex items-center justify-between px-4 py-5 hover:bg-slate-900 transition-colors no-underline"
            style={{ color: 'inherit' }}
          >
            <div>
              <Text fw={500}>Flight {String(flight.id).padStart(3, '0')}</Text>
              <Text size="sm" c="dimmed" mt={2}>
                {flight.duration_s != null
                  ? formatDuration(flight.duration_s)
                  : `${flight.rows.toLocaleString()} points`}
              </Text>
            </div>
            <Text c="dimmed" size="xl">›</Text>
          </Link>
        ))}
      </div>
    </div>
  )
}
