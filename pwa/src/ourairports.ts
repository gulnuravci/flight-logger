// ---------------------------------------------------------------------------
// OurAirports data loader  (GA-175)
//
// Fetches the free, CORS-enabled OurAirports CSV dataset to find the nearest
// airport to a given GPS position and return its traffic pattern geometry.
//
// Data sources (public domain, no API key needed):
//   https://ourairports.com/data/
//   Hosted CDN: https://davidmegginson.github.io/ourairports-data/
//
// Both CSVs are cached in module scope for the page session — after the first
// fetch (~2–4s) every subsequent lookup is instant.
// ---------------------------------------------------------------------------

import { computePattern, distNm } from './patternGeometry'
import type { TrafficPattern } from './patternGeometry'

const AIRPORTS_URL = 'https://davidmegginson.github.io/ourairports-data/airports.csv'
const RUNWAYS_URL  = 'https://davidmegginson.github.io/ourairports-data/runways.csv'

// ─── Types ────────────────────────────────────────────────────────────────────

interface Airport {
  ident: string
  type:  string
  name:  string
  lat:   number
  lon:   number
  elev:  number   // ft MSL
}

interface Runway {
  airportIdent: string
  lengthFt:     number
  leIdent: string;  leLat: number;  leLon: number;  leHdg: number
  heIdent: string;  heLat: number;  heLon: number;  heHdg: number
}

// ─── Module-level cache ───────────────────────────────────────────────────────
// All three caches persist for the lifetime of the page tab (cleared on hard
// refresh).  After the first ~2–4 s fetch, every lookup is instant.

let cachedAirports: Airport[]                   | null = null
let cachedRunways:  Runway[]                    | null = null
const patternCache = new Map<string, TrafficPattern>()  // keyed by airport ident

// ─── CSV parsing ─────────────────────────────────────────────────────────────

/** Return the column index for each field name from a CSV header line. */
function colIndices(headerLine: string, ...names: string[]): number[] {
  const cols = headerLine.split(',').map(h => h.replace(/"/g, '').trim())
  return names.map(n => cols.indexOf(n))
}

/** Extract and clean a single cell value. */
function cell(cols: string[], i: number): string {
  return (cols[i] ?? '').replace(/"/g, '').trim()
}

async function loadAirports(): Promise<Airport[]> {
  if (cachedAirports) return cachedAirports

  const text  = await fetch(AIRPORTS_URL).then(r => r.text())
  const lines = text.split('\n')
  const [iIdent, iType, iName, iLat, iLon, iElev] = colIndices(
    lines[0],
    'ident', 'type', 'name', 'latitude_deg', 'longitude_deg', 'elevation_ft',
  )

  const result: Airport[] = []
  for (let i = 1; i < lines.length; i++) {
    const cols = lines[i].split(',')
    const type = cell(cols, iType)
    // Keep only airports (exclude heliports, seaplane bases, etc.)
    if (!type.includes('airport')) continue
    const lat = parseFloat(cell(cols, iLat))
    const lon = parseFloat(cell(cols, iLon))
    if (isNaN(lat) || isNaN(lon)) continue
    result.push({
      ident: cell(cols, iIdent),
      type,
      name:  cell(cols, iName),
      lat, lon,
      elev: parseFloat(cell(cols, iElev)) || 0,
    })
  }
  cachedAirports = result
  return result
}

async function loadRunways(): Promise<Runway[]> {
  if (cachedRunways) return cachedRunways

  const text  = await fetch(RUNWAYS_URL).then(r => r.text())
  const lines = text.split('\n')
  const [
    iApt, iLen,
    iLeId, iLeLat, iLeLon, iLeHdg,
    iHeId, iHeLat, iHeLon, iHeHdg,
  ] = colIndices(
    lines[0],
    'airport_ident', 'length_ft',
    'le_ident', 'le_latitude_deg', 'le_longitude_deg', 'le_heading_degT',
    'he_ident', 'he_latitude_deg', 'he_longitude_deg', 'he_heading_degT',
  )

  const result: Runway[] = []
  for (let i = 1; i < lines.length; i++) {
    const cols = lines[i].split(',')
    result.push({
      airportIdent: cell(cols, iApt),
      lengthFt:     parseFloat(cell(cols, iLen)) || 0,
      leIdent: cell(cols, iLeId),
      leLat:   parseFloat(cell(cols, iLeLat)),
      leLon:   parseFloat(cell(cols, iLeLon)),
      leHdg:   parseFloat(cell(cols, iLeHdg)),
      heIdent: cell(cols, iHeId),
      heLat:   parseFloat(cell(cols, iHeLat)),
      heLon:   parseFloat(cell(cols, iHeLon)),
      heHdg:   parseFloat(cell(cols, iHeHdg)),
    })
  }
  cachedRunways = result
  return result
}

// ─── Main export ─────────────────────────────────────────────────────────────

/**
 * Given a takeoff GPS position (and an optional initial heading to infer the
 * active runway), fetch the nearest airport from OurAirports and return its
 * computed traffic pattern geometry.
 *
 * Returns null if no airport is found within 5 nm or runway data is missing.
 *
 * The first call fetches ~8 MB of CSV data; subsequent calls are instant
 * because the parsed arrays are cached in module scope.
 */
export async function fetchTrafficPattern(
  takeoffLat:       number,
  takeoffLon:       number,
  initialHeadingDeg?: number,
): Promise<TrafficPattern | null> {
  const [airports, runways] = await Promise.all([loadAirports(), loadRunways()])

  // ── 1. Nearest airport within 5 nm ─────────────────────────────────────
  let nearest:     Airport | null = null
  let nearestDist: number         = 999

  for (const apt of airports) {
    const d = distNm(takeoffLat, takeoffLon, apt.lat, apt.lon)
    if (d < nearestDist && d < 5) { nearest = apt; nearestDist = d }
  }
  if (!nearest) return null

  // ── 2. Runways at this airport ─────────────────────────────────────────
  const aptRunways = runways.filter(
    r =>
      r.airportIdent === nearest!.ident &&
      !isNaN(r.leLat) && !isNaN(r.leLon) &&
      !isNaN(r.heLat) && !isNaN(r.heLon),
  )
  if (!aptRunways.length) return null

  // ── 3. Pick the active runway end ──────────────────────────────────────
  //   If we have an initial heading (from the first few GPS rows), pick the
  //   runway end whose true heading is closest — this infers the departure
  //   runway from how the aircraft was actually flying.
  //   Fallback: longest runway, high end.
  let bestRwy  = aptRunways[0]
  let useHeEnd = true

  if (initialHeadingDeg !== undefined) {
    let bestDiff = 999
    for (const rwy of aptRunways) {
      for (const [isHe, hdg] of [
        [true,  rwy.heHdg],
        [false, rwy.leHdg],
      ] as [boolean, number][]) {
        if (isNaN(hdg)) continue
        // Angular difference (0–180°)
        const diff = Math.abs(((hdg - initialHeadingDeg + 540) % 360) - 180)
        if (diff < bestDiff) { bestDiff = diff; bestRwy = rwy; useHeEnd = isHe }
      }
    }
  } else {
    // No heading → pick the longest runway (most likely to be the primary)
    bestRwy  = aptRunways.reduce((a, b) => (a.lengthFt > b.lengthFt ? a : b))
    useHeEnd = true
  }

  const thrLat   = useHeEnd ? bestRwy.heLat   : bestRwy.leLat
  const thrLon   = useHeEnd ? bestRwy.heLon   : bestRwy.leLon
  const depLat   = useHeEnd ? bestRwy.leLat   : bestRwy.heLat
  const depLon   = useHeEnd ? bestRwy.leLon   : bestRwy.heLon
  const rwyHdg   = useHeEnd ? bestRwy.heHdg   : bestRwy.leHdg
  const rwyIdent = useHeEnd ? bestRwy.heIdent : bestRwy.leIdent

  // ── 4. Pattern altitude: 1 000 ft AGL (FAA standard for GA) ──────────
  const patternAlt = Math.round(nearest.elev + 1000)

  return computePattern(
    thrLat, thrLon,
    depLat, depLon,
    rwyHdg,
    'left',          // default: left traffic (correct for ~95 % of US GA airports)
    patternAlt,
    nearest.ident,
    nearest.name,
    rwyIdent,
  )
}

// ─── Viewport-based multi-airport lookup ─────────────────────────────────────

/**
 * Return traffic-pattern geometry for every airport that is currently visible
 * inside the given Leaflet map bounding box.
 *
 * Performance characteristics:
 *  • loadAirports / loadRunways return from the module-level cache after the
 *    first fetch — subsequent calls are synchronous O(n) array scans (~5 ms).
 *  • Pattern geometry is stored in `patternCache` so each airport is computed
 *    at most once per page session.
 *  • zoom < 11  → returns [] immediately (patterns would be sub-pixel specks).
 *  • zoom 11–12 → skips `small_airport` entries to reduce visual clutter.
 *  • Results are capped at `maxAirports` (default 8), sorted by distance from
 *    the viewport centre, so the closest airports are always preferred.
 */
export async function getPatternsInBounds(
  south: number,
  north: number,
  west: number,
  east: number,
  zoom: number,
  maxAirports = 8,
): Promise<TrafficPattern[]> {
  // Below zoom 11 the viewport spans hundreds of miles — too many airports and
  // the pattern rectangles are invisible anyway.
  if (zoom < 11) return []

  const [airports, runways] = await Promise.all([loadAirports(), loadRunways()])

  // ── Filter airports inside the current viewport ─────────────────────────
  const inBounds = airports.filter(apt => {
    if (apt.lat < south || apt.lat > north || apt.lon < west || apt.lon > east) return false
    // At zoom 11–12 only show medium/large airports to keep the map readable
    if (zoom < 13 && apt.type === 'small_airport') return false
    return true
  })

  if (!inBounds.length) return []

  // ── Pick the N closest airports to the viewport centre ─────────────────
  const cLat = (south + north) / 2
  const cLon = (west  + east)  / 2
  const nearest = inBounds
    .map(apt => ({ apt, d: distNm(cLat, cLon, apt.lat, apt.lon) }))
    .sort((a, b) => a.d - b.d)
    .slice(0, maxAirports)
    .map(x => x.apt)

  // ── Build (or serve from cache) a pattern for each airport ─────────────
  const patterns: TrafficPattern[] = []

  for (const apt of nearest) {
    const cached = patternCache.get(apt.ident)
    if (cached) { patterns.push(cached); continue }

    const aptRunways = runways.filter(
      r =>
        r.airportIdent === apt.ident &&
        !isNaN(r.leLat) && !isNaN(r.leLon) &&
        !isNaN(r.heLat) && !isNaN(r.heLon),
    )
    if (!aptRunways.length) continue

    // Default: longest runway, high end, left traffic
    const rwy = aptRunways.reduce((a, b) => (a.lengthFt > b.lengthFt ? a : b))
    if (isNaN(rwy.heHdg)) continue

    const p = computePattern(
      rwy.heLat, rwy.heLon,
      rwy.leLat, rwy.leLon,
      rwy.heHdg,
      'left',
      Math.round(apt.elev + 1000),
      apt.ident,
      apt.name,
      rwy.heIdent,
    )

    patternCache.set(apt.ident, p)
    patterns.push(p)
  }

  return patterns
}
