// ---------------------------------------------------------------------------
// Pico API client
//
// MOCK_MODE = true  → synthetic flight data for desktop dev / demo
// MOCK_MODE = false → real data from the Pico over the "FlightLogger" WiFi hotspot
//
// To use real data:
//   1. Hold GP22 LOW while powering the Pico  (button between GP22 and GND)
//   2. Connect your device to the "FlightLogger" WiFi (password: flightlog)
//   3. Flip MOCK_MODE to false below and rebuild the PWA
// ---------------------------------------------------------------------------

const MOCK_MODE = false                       // true = mock data, false = real Pico over WiFi
export const PICO_BASE_URL = 'http://192.168.4.1'  // Pico AP default gateway

// --- Types ------------------------------------------------------------------

export interface FlightMeta {
  id: number
  filename: string
  rows: number
  duration_s?: number  // total elapsed seconds — available once CSV is parsed server-side
}

export interface FlightRow {
  t: number           // elapsed seconds
  alt: number         // altitude m
  ax: number          // accel x g
  ay: number          // accel y g
  az: number          // accel z g
  gx: number          // gyro x °/s
  gy: number          // gyro y °/s
  gz: number          // gyro z °/s
  pressure: number    // hPa
  temp: number        // °C
  lat: number         // decimal degrees
  lon: number         // decimal degrees
  speed_kts: number   // ground speed knots
  heading_deg: number // true heading degrees
}

// --- Mock data --------------------------------------------------------------

// ─── Per-flight config ────────────────────────────────────────────────────────

type Turn = { start: number; end: number; dir: number; ayBad: boolean }
type Route = readonly [number, number, number][]  // [phase, lat, lon]

interface FlightConfig {
  route:   Route
  turns:   Turn[]
  baseAlt: number   // field elevation (m)
  altGain: number   // max cruise altitude gain above field (m)
}

// ─── Flight 001 & 002 — KABE (Lehigh Valley Intl, PA) ────────────────────────
//
// Rectangular counterclockwise training area northwest of KABE.
// Turn 3 is intentionally sloppy — it becomes the "worst moment".
//
//   Leg 1  (0.00–0.10):  NNW out of KABE
//   Turn 1 (0.10–0.17):  left — NNW → W
//   Leg 2  (0.17–0.28):  W
//   Turn 2 (0.28–0.35):  left — W → S
//   Leg 3  (0.35–0.46):  S
//   Turn 3 (0.46–0.53):  left — S → E  (sloppy)
//   Leg 4  (0.53–0.64):  E
//   Turn 4 (0.64–0.71):  left — E → N
//   Return (0.71–1.00):  N back to KABE

const KABE_CONFIG: FlightConfig = {
  baseAlt: 260,   // KABE field elevation ≈ 393 ft = 260 m (rounded for mock data)
  altGain: 800,
  turns: [
    { start: 0.10, end: 0.17, dir: -1, ayBad: false },  // left, coordinated
    { start: 0.28, end: 0.35, dir: -1, ayBad: false },  // left, coordinated
    { start: 0.46, end: 0.53, dir: -1, ayBad: true  },  // left, sloppy ← worst moment
    { start: 0.64, end: 0.71, dir: -1, ayBad: false },  // left, coordinated
  ],
  route: [
    [0.000, 40.6521, -75.4408],  // KABE — takeoff
    [0.100, 40.668,  -75.455 ],  // Turn 1 start  (NNW heading)
    [0.135, 40.674,  -75.470 ],  // Turn 1 arc mid
    [0.170, 40.673,  -75.485 ],  // Turn 1 end    (now W)
    [0.280, 40.671,  -75.520 ],  // Turn 2 start  (W heading)
    [0.315, 40.660,  -75.531 ],  // Turn 2 arc mid
    [0.350, 40.646,  -75.529 ],  // Turn 2 end    (now S)
    [0.460, 40.619,  -75.522 ],  // Turn 3 start  (S heading, sloppy)
    [0.495, 40.609,  -75.508 ],  // Turn 3 arc mid
    [0.530, 40.605,  -75.491 ],  // Turn 3 end    (now E)
    [0.640, 40.605,  -75.456 ],  // Turn 4 start  (E heading)
    [0.675, 40.617,  -75.443 ],  // Turn 4 arc mid
    [0.710, 40.631,  -75.440 ],  // Turn 4 end    (now N)
    [1.000, 40.6521, -75.4408],  // KABE — land
  ],
}

// ─── Flight 003 — 1N7 (Blairstown Airport, NJ) ───────────────────────────────
//
// Counterclockwise training rectangle northwest of 1N7 over the Delaware
// Water Gap hills.  Turn 3 (S→E) is the sloppy one.
//
//   Leg 1  (0.00–0.10):  N out of 1N7
//   Turn 1 (0.10–0.17):  left — N → W
//   Leg 2  (0.17–0.28):  W (over Kittatinny Ridge)
//   Turn 2 (0.28–0.35):  left — W → S
//   Leg 3  (0.35–0.46):  S
//   Turn 3 (0.46–0.53):  left — S → E  (sloppy)
//   Leg 4  (0.53–0.64):  E
//   Turn 4 (0.64–0.71):  left — E → N
//   Return (0.71–1.00):  N back to 1N7

const BLAIRSTOWN_CONFIG: FlightConfig = {
  baseAlt: 113,   // 1N7 field elevation = 372 ft = 113 m
  altGain: 580,   // cruise ~1900 ft AGL — typical VFR training altitude
  turns: [
    { start: 0.10, end: 0.17, dir: -1, ayBad: false },  // left, coordinated
    { start: 0.28, end: 0.35, dir: -1, ayBad: false },  // left, coordinated
    { start: 0.46, end: 0.53, dir: -1, ayBad: true  },  // left, sloppy ← worst moment
    { start: 0.64, end: 0.71, dir: -1, ayBad: false },  // left, coordinated
  ],
  route: [
    [0.000, 40.972, -74.999],  // 1N7 — takeoff (N)
    [0.100, 40.996, -74.995],  // Turn 1 start
    [0.135, 41.002, -75.009],  // Turn 1 arc mid
    [0.170, 41.000, -75.022],  // Turn 1 end    (now W)
    [0.280, 40.998, -75.058],  // Turn 2 start
    [0.315, 40.985, -75.067],  // Turn 2 arc mid
    [0.350, 40.970, -75.064],  // Turn 2 end    (now S)
    [0.460, 40.940, -75.056],  // Turn 3 start  (S heading, sloppy)
    [0.495, 40.930, -75.040],  // Turn 3 arc mid
    [0.530, 40.928, -75.023],  // Turn 3 end    (now E)
    [0.640, 40.929, -74.987],  // Turn 4 start
    [0.675, 40.941, -74.977],  // Turn 4 arc mid
    [0.710, 40.956, -74.975],  // Turn 4 end    (now N)
    [1.000, 40.972, -74.999],  // 1N7 — land
  ],
}

/** Linearly interpolate lat/lon between the surrounding route keyframes. */
function interpolateRoute(phase: number, route: Route): { lat: number; lon: number } {
  if (phase <= route[0][0]) return { lat: route[0][1], lon: route[0][2] }
  const last = route[route.length - 1]
  if (phase >= last[0]) return { lat: last[1], lon: last[2] }
  for (let i = 0; i < route.length - 1; i++) {
    const [p0, lat0, lon0] = route[i]
    const [p1, lat1, lon1] = route[i + 1]
    if (phase >= p0 && phase < p1) {
      const t = (phase - p0) / (p1 - p0)
      return { lat: lat0 + t * (lat1 - lat0), lon: lon0 + t * (lon1 - lon0) }
    }
  }
  return { lat: last[1], lon: last[2] }
}

/** True bearing (°) of the route segment containing this phase. */
function routeHeading(phase: number, route: Route): number {
  let seg = route.length - 2
  for (let i = 0; i < route.length - 1; i++) {
    if (phase < route[i + 1][0]) { seg = i; break }
  }
  const [, lat0, lon0] = route[seg]
  const [, lat1, lon1] = route[seg + 1]
  const dLat = lat1 - lat0
  const dLon = (lon1 - lon0) * Math.cos(((lat0 + lat1) / 2) * Math.PI / 180)
  return (Math.atan2(dLon, dLat) * 180 / Math.PI + 360) % 360
}

function makeMockFlight(_id: number, rows: number, cfg: FlightConfig): FlightRow[] {
  const { route, turns, baseAlt, altGain } = cfg
  const data: FlightRow[] = []

  for (let i = 0; i < rows; i++) {
    const t     = i * 0.1
    const phase = i / (rows - 1)   // 0 → 1

    // Altitude: climb from field, cruise, descend back
    const alt = baseAlt + altGain * Math.sin(Math.PI * phase) + (Math.random() - 0.5) * 10

    // GPS path from route keyframes — straight legs + turn arcs
    const { lat: baseLat, lon: baseLon } = interpolateRoute(phase, route)
    const lat = baseLat + (Math.random() - 0.5) * 0.0004
    const lon = baseLon + (Math.random() - 0.5) * 0.0004

    // Heading derived from current route segment bearing
    const heading = routeHeading(phase, route)

    // Speed: faster in cruise, slower near airport
    const speed = 55 + 50 * Math.sin(Math.PI * phase) + (Math.random() - 0.5) * 5

    // Gyro + lateral accel: simulate discrete turns, near-zero in straight flight
    const activeTurn = turns.find(mt => phase >= mt.start && phase <= mt.end)
    const gz = activeTurn
      ? activeTurn.dir * (3.0 + (Math.random() - 0.5) * 0.4)   // ~±3 °/s in turn
      : (Math.random() - 0.5) * 0.4                              // ~0 in straight flight
    const ay = activeTurn
      ? activeTurn.ayBad
        ? (Math.random() - 0.3) * 0.25   // sloppy: up to ±0.15g lateral
        : (Math.random() - 0.5) * 0.06   // coordinated: ±0.03g
      : (Math.random() - 0.5) * 0.025    // straight: near zero

    data.push({
      t,
      alt:          parseFloat(alt.toFixed(1)),
      ax:           parseFloat(((Math.random() - 0.5) * 0.15).toFixed(3)),
      ay:           parseFloat(ay.toFixed(3)),
      az:           parseFloat((1.0 + (Math.random() - 0.5) * 0.05).toFixed(3)),
      gx:           parseFloat(((Math.random() - 0.5) * 1.0).toFixed(2)),
      gy:           parseFloat(((Math.random() - 0.5) * 1.0).toFixed(2)),
      gz:           parseFloat(gz.toFixed(2)),
      pressure:     parseFloat((1013.25 - alt * 0.12).toFixed(2)),
      temp:         parseFloat((23 - alt * 0.006 + (Math.random() - 0.5)).toFixed(1)),
      lat:          parseFloat(lat.toFixed(6)),
      lon:          parseFloat(lon.toFixed(6)),
      speed_kts:    parseFloat(speed.toFixed(1)),
      heading_deg:  parseFloat(heading.toFixed(1)),
    })
  }
  return data
}

const MOCK_FLIGHTS: FlightMeta[] = [
  { id: 3, filename: 'FLT_003.CSV', rows: 2200, duration_s: 2200 * 0.1 },
  { id: 2, filename: 'FLT_002.CSV', rows: 312,  duration_s: 312  * 0.1 },
  { id: 1, filename: 'FLT_001.CSV', rows: 4821, duration_s: 4821 * 0.1 },
]

// Map each flight id to the config used to generate it
const MOCK_CONFIGS: Record<number, FlightConfig> = {
  1: KABE_CONFIG,
  2: KABE_CONFIG,
  3: BLAIRSTOWN_CONFIG,
}

// --- CSV parser (used when MOCK_MODE = false) --------------------------------
//
// The Pico streams the raw CSV file for /api/flight/<n>.
// Columns: timestamp_ms, ax_g, ay_g, az_g, gx_dps, gy_dps, gz_dps,
//          altitude_m, pressure_hpa, temperature_c
//
// lat/lon/speed/heading are 0 until the GPS module (GA-167) is wired up.

function csvToFlightRows(csv: string): FlightRow[] {
  const lines = csv.trim().split('\n').filter(l => l.trim())
  if (lines.length < 2) return []                    // header only → empty
  const dataLines = lines.slice(1)                   // drop header row
  const firstMs   = parseFloat(dataLines[0].split(',')[0])

  return dataLines.map(line => {
    const c = line.split(',')
    return {
      t:           (parseFloat(c[0]) - firstMs) / 1000,  // ms since boot → elapsed seconds
      ax:          parseFloat(c[1]),
      ay:          parseFloat(c[2]),
      az:          parseFloat(c[3]),
      gx:          parseFloat(c[4]),
      gy:          parseFloat(c[5]),
      gz:          parseFloat(c[6]),
      alt:         parseFloat(c[7]),
      pressure:    parseFloat(c[8]),
      temp:        parseFloat(c[9]),
      lat:         0,   // GPS not yet wired (GA-167)
      lon:         0,
      speed_kts:   0,
      heading_deg: 0,
    }
  })
}

// --- API calls --------------------------------------------------------------

export async function fetchFlights(signal?: AbortSignal): Promise<FlightMeta[]> {
  if (MOCK_MODE) {
    await new Promise(r => setTimeout(r, 400))  // simulate network delay
    return MOCK_FLIGHTS
  }
  const res = await fetch(`${PICO_BASE_URL}/api/flights`, { signal })
  if (!res.ok) throw new Error('Failed to fetch flight list')
  return res.json()
}

export async function fetchFlight(id: number, signal?: AbortSignal): Promise<FlightRow[]> {
  if (MOCK_MODE) {
    await new Promise(r => setTimeout(r, 600))
    const meta   = MOCK_FLIGHTS.find(f => f.id === id)
    const config = MOCK_CONFIGS[id] ?? KABE_CONFIG
    return makeMockFlight(id, meta?.rows ?? 500, config)
  }
  const res = await fetch(`${PICO_BASE_URL}/api/flight/${id}`, { signal })
  if (!res.ok) throw new Error(`Failed to fetch flight ${id}`)
  const csv = await res.text()
  return csvToFlightRows(csv)
}
