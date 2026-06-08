import { Badge, Button, Card, Progress, Text, Title } from '@mantine/core'
import type { FlightAnalysis, Grade, Turn } from '../analysis'
import { COORD_PASS, COORD_WARN } from '../analysis'
import type { FlightRow } from '../api'

// ─── Helpers ─────────────────────────────────────────────────────────────────

function formatTime(s: number): string {
  const m   = Math.floor(s / 60)
  const sec = Math.floor(s % 60)
  return `${m}m ${sec.toString().padStart(2, '0')}s`
}

// Map a Grade to a Mantine color name
const GRADE_COLOR: Record<Grade, string> = {
  pass: 'green',
  warn: 'yellow',
  fail: 'red',
}

const GRADE_LABEL: Record<Grade, string> = {
  pass: 'Pass',
  warn: 'Review',
  fail: 'Fail',
}

// ─── Sub-components ───────────────────────────────────────────────────────────

function MetricCard({
  title, value, grade, detail, progress,
}: {
  title: string
  value: string
  grade: Grade
  detail: string
  progress: number  // 0–1, higher = better
}) {
  const color = GRADE_COLOR[grade]
  return (
    <Card bg="dark.8" radius="md" p="md">
      <div className="flex justify-between items-start mb-1">
        <Text size="sm" fw={500} c="dimmed">{title}</Text>
        <Badge color={color} variant="light" size="sm">{GRADE_LABEL[grade]}</Badge>
      </div>
      <Title order={2} size="h2" fw={700} mt={4} mb="sm">{value}</Title>
      <Progress
        value={Math.max(2, Math.min(100, progress * 100))}
        color={color}
        size="sm"
        radius="xl"
        mb="xs"
      />
      <Text size="xs" c="dimmed">{detail}</Text>
    </Card>
  )
}

function TurnRow({ turn }: { turn: Turn }) {
  const grade: Grade =
    turn.coordinationPct >= COORD_PASS ? 'pass' :
    turn.coordinationPct >= COORD_WARN ? 'warn' : 'fail'

  const color = GRADE_COLOR[grade]
  const arrow = turn.direction === 'left' ? '←' : '→'

  return (
    <div className="flex items-center justify-between py-3 border-b border-slate-800 last:border-0">
      <div className="flex items-center gap-3">
        <Text c="dimmed" ff="monospace">{arrow}</Text>
        <div>
          <Text size="sm">Turn {turn.index}</Text>
          <Text size="xs" c="dimmed" mt={2}>
            {turn.direction} · {turn.avgYawRate_dps}°/s · {formatTime(turn.startT)}
          </Text>
        </div>
      </div>
      <Badge color={color} variant="light">{turn.coordinationPct}%</Badge>
    </div>
  )
}

// ─── Main component ───────────────────────────────────────────────────────────

interface Props {
  analysis: FlightAnalysis
  onViewOnMap: (row: FlightRow) => void
}

export default function SummaryView({ analysis, onViewOnMap }: Props) {
  const {
    altKeeping_ft, altKeepingGrade,
    coordinationPct, coordinationGrade,
    smoothness, smoothnessGrade,
    turns, worstMoment, cruiseDuration_s,
  } = analysis

  // Normalise metrics to 0–1 for progress bars (higher = better)
  const altProgress    = Math.max(0, 1 - altKeeping_ft / 200)
  const coordProgress  = coordinationPct / 100
  const smoothProgress = smoothness / 10

  return (
    <div className="space-y-4">

      <MetricCard
        title="Altitude Keeping"
        value={`±${altKeeping_ft.toFixed(0)} ft`}
        grade={altKeepingGrade}
        detail={`FAA PTS standard: ±100 ft · Cruise: ${cruiseDuration_s}s`}
        progress={altProgress}
      />
      <MetricCard
        title="Coordinated Turns"
        value={`${coordinationPct}%`}
        grade={coordinationGrade}
        detail={`${turns.length} turn${turns.length !== 1 ? 's' : ''} detected · ball centered = |ay| < 0.05g`}
        progress={coordProgress}
      />
      <MetricCard
        title="Control Smoothness"
        value={`${smoothness}/10`}
        grade={smoothnessGrade}
        detail="Based on total accelerometer variation — lower inputs score higher"
        progress={smoothProgress}
      />

      {/* Worst moment */}
      {worstMoment && (
        <Card bg="dark.8" radius="md" p="md">
          <Text size="sm" fw={500} c="dimmed" mb="xs">Worst Moment</Text>
          <div className="flex items-center justify-between">
            <div>
              <Text size="sm">{formatTime(worstMoment.t)} — uncoordinated turn</Text>
              <Text size="xs" c="dimmed" mt={2}>
                lateral G = {Math.abs(worstMoment.ay).toFixed(3)}g · (coordinated &lt; 0.05g)
              </Text>
            </div>
            <Button
              variant="subtle"
              size="xs"
              onClick={() => onViewOnMap(worstMoment)}
            >
              View on map →
            </Button>
          </div>
        </Card>
      )}

      {/* Turn breakdown */}
      {turns.length > 0 && (
        <Card bg="dark.8" radius="md" px="md" pt="md" pb={4}>
          <Text size="sm" fw={500} c="dimmed" mb="xs">Turn Breakdown</Text>
          {turns.map(t => <TurnRow key={t.index} turn={t} />)}
        </Card>
      )}

      {turns.length === 0 && (
        <div className="py-6 text-center">
          <Text c="dimmed" size="sm">No turns detected in this flight.</Text>
        </div>
      )}

    </div>
  )
}
