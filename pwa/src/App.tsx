import { BrowserRouter, Routes, Route } from 'react-router-dom'
import FlightList from './pages/FlightList'
import FlightDetail from './pages/FlightDetail'

export default function App() {
  return (
    <BrowserRouter basename={import.meta.env.BASE_URL}>
      <Routes>
        <Route path="/" element={<FlightList />} />
        <Route path="/flight/:id" element={<FlightDetail />} />
      </Routes>
    </BrowserRouter>
  )
}
