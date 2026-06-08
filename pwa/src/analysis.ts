// ---------------------------------------------------------------------------
// Flight Analysis Engine  (GA-172)
//
// Pure TypeScript — no React imports, fully unit-testable.
// Takes raw FlightRow[] and returns structured performance scores.
// ---------------------------------------------------------------------------

import type { FlightRow } from './api'

// ─── Types ──────────────────────────────────────────────────────────────────

export type Phase = 'ground' | 'climb' | 'cruise' | 'descent'
export type Grade = 'pass' | 'warn' | 'fail'

export interface Turn {
  index: number            // 1-based turn number
  startT: number           // elapsed seconds
  endT: number
  direction: 'left' | 'right'
  avgYawRate_dps: number   // average |gz| during turn
  coordinationPct: number  // % of rows where |ay| < threshold
  worstAy: number          // peak |ay| — the worst slip/skid moment
  worstRow: FlightRow      // the actual row — used to highlight on map
}

export interface FlightAnalysis {
  phases: Phase[]               // one entry per input row — for chart coloring
  altKeeping_ft: number         // std dev of altitude during cruise (feet)
  altKeepingGrade: Grade        // vs FAA PTS ±100 ft standard
  coordinationPct: number       // weighted % coordinated across all turns
  coordinationGrade: Grade
  smoothness: number            // 0–10 scale
  smoothnessGrade: Grade
  turns: Turn[]
  worstMoment: FlightRow | null // most uncoordinated row across all turns
  cruiseDuration_s: number
}

// ─── Thresholds ─────────────────────────────────────────────────────────────

const YAW_THRESHOLD   = 1.8   // °/s  — |gz| above this = in a turn
const TURN_DEBOUNCE   = 15    // rows — gz must stay low this long to end a turn
const MIN_TURN_DUR    = 3.0   // s    — ignore turns shorter than this
const COORD_THRESHOLD = 0.05  // g    — |ay| below this = ball centered
const CLIMB_RATE      = 0.4   // m/s  — sustained altitude rate for climb
const DESCENT_RATE    = -0.4  // m/s  — sustained altitude rate for descent
const GROUND_MARGIN   = 30    // m    — above baseline altitude = still ground

// Exported so UI components grade turns consistently without hardcoding numbers
export const COORD_PASS = 90  // % — at or above this = green / pass
export const COORD_WARN = 70  // % — at or above this = yellow / warn, below = red / fail

// ─── Utilities ───────────────────────────────────────────────────────────────

function mean(arr: number[]): number {
  return arr.reduce((s, v) => s + v, 0) / arr.length
}

function stdDev(arr: number[]): number {
  const m = mean(arr)
  return Math.sqrt(mean(arr.map(v => (v - m) ** 2)))
}

// ─── Phase Detection ─────────────────────────────────────────────────────────

function detectPhases(data: FlightRow[]): Phase[] {
  const SMOOTH = 15  // rows on each side for the altitude-rate window (~1.5s)

  // Baseline altitude = median of the first 20 rows (pre-takeoff)
  const first20 = data.slice(0, Math.min(20, data.length)).map(r => r.alt)
  const sorted  = [...first20].sort((a, b) => a - b)
  const baselineAlt = sorted[Math.floor(sorted.length / 2)]

  return data.map((row, i) => {
    // Ground: within GROUND_MARGIN of baseline and vertical accel ≈ 1g
    if (row.alt < baselineAlt + GROUND_MARGIN && Math.abs(row.az - 1.0) < 0.2) {
      return 'ground'
    }

    // Altitude rate from a smoothed window
    const i0      = Math.max(0, i - SMOOTH)
    const i1      = Math.min(data.length - 1, i + SMOOTH)
    const dt      = data[i1].t - data[i0].t
    const altRate = dt > 0 ? (data[i1].alt - data[i0].alt) / dt : 0

    if (altRate >  CLIMB_RATE)   return 'climb'
    if (altRate <  DESCENT_RATE) return 'descent'
    return 'cruise'
  })
}

// ─── Turn Detection ───────────────────────────────────────────────────────────

function detectTurns(data: FlightRow[], phases: Phase[]): Turn[] {
  const turns: Turn[] = []

  let inTurn    = false
  let turnStart = 0
  let lowCount  = 0

  for (let i = 0; i < data.length; i++) {
    if (phases[i] === 'ground') { inTurn = false; lowCount = 0; continue }

    if (!inTurn) {
      if (Math.abs(data[i].gz) > YAW_THRESHOLD) {
        inTurn    = true
        turnStart = i
        lowCount  = 0
      }
    } else {
      if (Math.abs(data[i].gz) > YAW_THRESHOLD) {
        lowCount = 0  // reset debounce — still turning
      } else {
        lowCount++
        if (lowCount >= TURN_DEBOUNCE) {
          // Turn ended TURN_DEBOUNCE rows ago
          const turnEnd  = i - TURN_DEBOUNCE
          const duration = data[turnEnd].t - data[turnStart].t

          if (duration >= MIN_TURN_DUR) {
            const slice      = data.slice(turnStart, turnEnd + 1)
            const avgGz      = mean(slice.map(r => r.gz))
            const coordCount = slice.filter(r => Math.abs(r.ay) < COORD_THRESHOLD).length
            const worstRow   = slice.reduce((w, r) =>
              Math.abs(r.ay) > Math.abs(w.ay) ? r : w
            )

            turns.push({
              index:            turns.length + 1,
              startT:           data[turnStart].t,
              endT:             data[turnEnd].t,
              direction:        avgGz > 0 ? 'right' : 'left',
              avgYawRate_dps:   parseFloat(Math.abs(avgGz).toFixed(1)),
              coordinationPct:  parseFloat(((coordCount / slice.length) * 100).toFixed(0)),
              worstAy:          parseFloat(Math.abs(worstRow.ay).toFixed(3)),
              worstRow,
            })
          }

          inTurn   = false
          lowCount = 0
        }
      }
    }
  }

  return turns
}

// ─── Main Entry Point ────────────────────────────────────────────────────────

export function analyzeFlight(data: FlightRow[]): FlightAnalysis {
  // Need at least 30 rows (~3 seconds) to say anything meaningful
  if (data.length < 30) {
    return {
      phases: data.map(() => 'ground' as Phase),
      altKeeping_ft: 0, altKeepingGrade: 'fail',
      coordinationPct: 0, coordinationGrade: 'fail',
      smoothness: 0, smoothnessGrade: 'fail',
      turns: [], worstMoment: null, cruiseDuration_s: 0,
    }
  }

  const phases = detectPhases(data)
  const turns  = detectTurns(data, phases)

  // ── Altitude keeping (cruise rows only) ──────────────────────────────────
  const cruiseRows = data.filter((_, i) => phases[i] === 'cruise')

  const cruiseDuration_s = cruiseRows.length > 1
    ? parseFloat((cruiseRows[cruiseRows.length - 1].t - cruiseRows[0].t).toFixed(0))
    : 0

  const altKeeping_ft = cruiseRows.length > 10
    ? parseFloat((stdDev(cruiseRows.map(r => r.alt)) * 3.28084).toFixed(1))
    : 0

  const altKeepingGrade: Grade =
    altKeeping_ft < 100 ? 'pass' :
    altKeeping_ft < 150 ? 'warn' : 'fail'

  // ── Coordination (weighted by turn duration) ─────────────────────────────
  let totalTurnTime = 0
  let coordTime     = 0
  for (const t of turns) {
    const dur = t.endT - t.startT
    totalTurnTime += dur
    coordTime     += dur * (t.coordinationPct / 100)
  }
  const coordinationPct = totalTurnTime > 0
    ? parseFloat(((coordTime / totalTurnTime) * 100).toFixed(0))
    : 100  // no detected turns → default to 100%

  const coordinationGrade: Grade =
    coordinationPct >= 90 ? 'pass' :
    coordinationPct >= 70 ? 'warn' : 'fail'

  // ── Smoothness (total variation of accelerometer, normalized) ────────────
  let totalVariation = 0
  for (let i = 1; i < data.length; i++) {
    totalVariation += Math.abs(data[i].ax - data[i - 1].ax)
    totalVariation += Math.abs(data[i].ay - data[i - 1].ay)
    totalVariation += Math.abs(data[i].az - data[i - 1].az)
  }
  const flightDuration  = data[data.length - 1].t - data[0].t
  const normalizedTV    = totalVariation / (flightDuration || 1)
  // smooth flight ≈ 0.02/s, rough ≈ 0.25/s → map to 10–0
  const smoothness = parseFloat(
    Math.max(0, Math.min(10, 10 - ((normalizedTV - 0.02) / 0.23) * 10)).toFixed(1)
  )
  const smoothnessGrade: Grade =
    smoothness >= 7 ? 'pass' :
    smoothness >= 5 ? 'warn' : 'fail'

  // ── Worst moment (most uncoordinated row across all turns) ───────────────
  const worstMoment = turns.length > 0
    ? turns.reduce((w, t) => t.worstAy > w.worstAy ? t : w).worstRow
    : null

  return {
    phases,
    altKeeping_ft,
    altKeepingGrade,
    coordinationPct,
    coordinationGrade,
    smoothness,
    smoothnessGrade,
    turns,
    worstMoment,
    cruiseDuration_s,
  }
}
