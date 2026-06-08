// ---------------------------------------------------------------------------
// Traffic pattern geometry  (GA-175)
//
// Pure dead-reckoning math — no API calls, no React imports.
// Given a runway's threshold / departure-end positions and heading,
// computes the five legs of a standard rectangular traffic pattern.
// ---------------------------------------------------------------------------

export interface PatternLeg {
  name: 'upwind' | 'crosswind' | 'downwind' | 'base' | 'final'
  positions: [number, number][]   // [lat, lon] pairs
}

export interface TrafficPattern {
  airportIcao: string
  airportName: string
  runway: string              // e.g. "24", "13"
  direction: 'left' | 'right'
  patternAltMsl: number       // feet MSL
  legs: PatternLeg[]
}

// ─── Maths ───────────────────────────────────────────────────────────────────

const R_NM = 3440.065  // Earth radius in nautical miles

function toRad(deg: number): number { return (deg * Math.PI) / 180 }
function toDeg(rad: number): number { return (rad * 180) / Math.PI }

/**
 * Dead-reckoning: given a position, bearing (°true), and distance (nm),
 * return the destination [lat, lon].
 */
export function deadReckon(
  lat: number, lon: number,
  bearingDeg: number,
  distNm: number,
): [number, number] {
  const d  = distNm / R_NM
  const θ  = toRad(bearingDeg)
  const φ1 = toRad(lat)
  const λ1 = toRad(lon)
  const φ2 = Math.asin(
    Math.sin(φ1) * Math.cos(d) + Math.cos(φ1) * Math.sin(d) * Math.cos(θ),
  )
  const λ2 =
    λ1 +
    Math.atan2(
      Math.sin(θ) * Math.sin(d) * Math.cos(φ1),
      Math.cos(d) - Math.sin(φ1) * Math.sin(φ2),
    )
  return [toDeg(φ2), toDeg(λ2)]
}

/** Great-circle distance in nautical miles between two lat/lon points. */
export function distNm(
  lat1: number, lon1: number,
  lat2: number, lon2: number,
): number {
  const φ1 = toRad(lat1), φ2 = toRad(lat2)
  const Δφ = toRad(lat2 - lat1), Δλ = toRad(lon2 - lon1)
  const a =
    Math.sin(Δφ / 2) ** 2 +
    Math.cos(φ1) * Math.cos(φ2) * Math.sin(Δλ / 2) ** 2
  return 2 * R_NM * Math.asin(Math.sqrt(a))
}

// ─── Pattern construction ─────────────────────────────────────────────────────

// Standard FAA spacings (nm)
const PATTERN_WIDTH  = 0.5   // lateral offset from runway centreline
const UPWIND_EXT     = 0.4   // upwind extension past the departure end
const BASE_OFFSET    = 0.5   // how far past abeam-threshold before the base turn

/**
 * Compute a standard rectangular traffic pattern.
 *
 *  thrLat/thrLon — landing threshold (the end you cross when landing)
 *  depLat/depLon — departure end     (the far end of the runway)
 *  headingDeg    — runway heading (the magnetic/true direction you land on)
 *  direction     — left or right traffic
 *
 * Pattern orientation for LEFT traffic, runway heading θ:
 *   Upwind     θ           (depart heading)
 *   Crosswind  θ − 90°     (left turn off upwind)
 *   Downwind   θ + 180°    (opposite direction, offset left)
 *   Base       θ + 90°     (left turn off downwind)
 *   Final      θ           (back to runway heading)
 */
export function computePattern(
  thrLat: number,  thrLon: number,
  depLat: number,  depLon: number,
  headingDeg: number,
  direction: 'left' | 'right',
  patternAltMsl: number,
  airportIcao: string,
  airportName: string,
  runway: string,
): TrafficPattern {
  const sign = direction === 'left' ? -1 : 1   // left = subtract 90°

  // Key bearings
  const perpBrg  = (headingDeg + sign * 90 + 360) % 360  // toward the downwind side
  const downBrg  = (headingDeg + 180) % 360               // opposite of final/approach
  const baseBrg  = (downBrg   + sign * 90 + 360) % 360   // 90° turn off downwind

  // ── Five corner points ───────────────────────────────────────────────────
  // 1. End of upwind (past dep end, still on centreline)
  const upEnd = deadReckon(depLat, depLon, headingDeg, UPWIND_EXT)

  // 2. End of crosswind (offset to downwind side)
  const crossEnd = deadReckon(upEnd[0], upEnd[1], perpBrg, PATTERN_WIDTH)

  // 3. Point on downwind abeam the landing threshold
  const abeam = deadReckon(thrLat, thrLon, perpBrg, PATTERN_WIDTH)

  // 4. Base turn (past abeam, still heading downwind)
  const baseTurn = deadReckon(abeam[0], abeam[1], downBrg, BASE_OFFSET)

  // 5. Final start (turn base → final, should land on extended centreline)
  const finStart = deadReckon(baseTurn[0], baseTurn[1], baseBrg, PATTERN_WIDTH)

  return {
    airportIcao,
    airportName,
    runway,
    direction,
    patternAltMsl,
    legs: [
      { name: 'upwind',    positions: [[depLat, depLon], upEnd] },
      { name: 'crosswind', positions: [upEnd, crossEnd] },
      { name: 'downwind',  positions: [crossEnd, abeam, baseTurn] },
      { name: 'base',      positions: [baseTurn, finStart] },
      { name: 'final',     positions: [finStart, [thrLat, thrLon]] },
    ],
  }
}
