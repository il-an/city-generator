import { useEffect, useMemo, useState } from 'react';

const API_BASE = (import.meta.env.VITE_API_BASE_URL || '').replace(/\/$/, '');

const defaultConfig = {
  population: 100000,
  hospitals: 1,
  schools: 1,
  transport: 'car',
  seed: 0,
  gridSize: 100,
  radiusFraction: 0.8,
  output: 'city_frontend',
};

const numericFields = ['population', 'hospitals', 'schools', 'seed', 'gridSize'];
const decimalFields = ['radiusFraction'];

const formatNumber = (value) => value?.toLocaleString?.() ?? value;

function SummaryGrid({ summary }) {
  if (!summary) return null;

  const entries = [
    ['Grid size', `${summary.gridSize} x ${summary.gridSize}`],
    ['Total buildings', formatNumber(summary.totalBuildings)],
    ['Residential cells', formatNumber(summary.residentialCells)],
    ['Commercial cells', formatNumber(summary.commercialCells)],
    ['Industrial cells', formatNumber(summary.industrialCells)],
    ['Green cells', formatNumber(summary.greenCells)],
    ['Undeveloped cells', formatNumber(summary.undevelopedCells)],
    ['Hospitals', summary.numHospitals],
    ['Schools', summary.numSchools],
    ['Max dist. to school', `${summary.maxDistanceToSchool.toFixed(2)} km`],
    ['Max dist. to hospital', `${summary.maxDistanceToHospital.toFixed(2)} km`],
    ['Max residential height', `${summary.maxResidentialHeight} floors`],
    ['Max commercial height', `${summary.maxCommercialHeight} floors`],
    ['Max industrial height', `${summary.maxIndustrialHeight} floors`],
  ];

  return (
    <dl className="summary-grid">
      {entries.map(([label, value]) => (
        <div key={label} className="summary-item">
          <dt>{label}</dt>
          <dd>{value}</dd>
        </div>
      ))}
    </dl>
  );
}

function CityCard({ city, onSelect, isActive }) {
  return (
    <button className={`city-card ${isActive ? 'city-card--active' : ''}`} onClick={() => onSelect(city)}>
      <div className="city-card__header">
        <span className="city-card__id">{city.id}</span>
        <span className="city-card__stats">{formatNumber(city.summary.totalBuildings)} buildings</span>
      </div>
      <div className="city-card__body">
        <div>
          <p className="muted">Population (target)</p>
          <p className="city-card__value">{formatNumber(city.summary.residentialCells * 1000)}</p>
        </div>
        <div>
          <p className="muted">Hospitals / Schools</p>
          <p className="city-card__value">
            {city.summary.numHospitals} / {city.summary.numSchools}
          </p>
        </div>
      </div>
      <div className="city-card__links">
        <a href={city.summaryURL} target="_blank" rel="noreferrer">Summary JSON</a>
        <a href={city.modelURL} target="_blank" rel="noreferrer">OBJ model</a>
      </div>
    </button>
  );
}

function App() {
  const [config, setConfig] = useState(defaultConfig);
  const [cities, setCities] = useState([]);
  const [selectedCity, setSelectedCity] = useState(null);
  const [status, setStatus] = useState({ type: 'idle', message: '' });
  const [isSubmitting, setIsSubmitting] = useState(false);
  const [loadingCities, setLoadingCities] = useState(false);

  const apiURL = (path) => `${API_BASE}${path}`;

  const payload = useMemo(() => {
    const data = {};
    Object.entries(config).forEach(([key, value]) => {
      if (value === '' || value === null || value === undefined) return;
      if (numericFields.includes(key) || decimalFields.includes(key)) {
        data[key] = Number(value);
      } else {
        data[key] = value;
      }
    });
    return data;
  }, [config]);

  useEffect(() => {
    const fetchCities = async () => {
      setLoadingCities(true);
      try {
        const response = await fetch(apiURL('/cities'));
        if (!response.ok) throw new Error(`Failed to load cities (${response.status})`);
        const data = await response.json();
        setCities(data);
        if (!selectedCity && data.length > 0) {
          setSelectedCity(data[data.length - 1]);
        }
      } catch (error) {
        setStatus({ type: 'error', message: error.message });
      } finally {
        setLoadingCities(false);
      }
    };

    fetchCities();
  }, []);

  const handleChange = (event) => {
    const { name, value } = event.target;
    setConfig((prev) => ({ ...prev, [name]: value }));
  };

  const handleSubmit = async (event) => {
    event.preventDefault();
    setIsSubmitting(true);
    setStatus({ type: 'pending', message: 'Generating city…' });

    try {
      const response = await fetch(apiURL('/generate'), {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(payload),
      });

      if (!response.ok) {
        const text = await response.text();
        throw new Error(text || `Generation failed (${response.status})`);
      }

      const result = await response.json();
      setSelectedCity(result);
      setStatus({ type: 'success', message: `City ${result.id} generated.` });
      await refreshCities();
    } catch (error) {
      setStatus({ type: 'error', message: error.message });
    } finally {
      setIsSubmitting(false);
    }
  };

  const refreshCities = async () => {
    setLoadingCities(true);
    try {
      const response = await fetch(apiURL('/cities'));
      if (!response.ok) throw new Error(`Failed to load cities (${response.status})`);
      const data = await response.json();
      setCities(data);
    } catch (error) {
      setStatus({ type: 'error', message: error.message });
    } finally {
      setLoadingCities(false);
    }
  };

  const resetForm = () => {
    setConfig(defaultConfig);
    setStatus({ type: 'idle', message: '' });
  };

  return (
    <div className="page">
      <header className="hero">
        <div>
          <p className="eyebrow">City Generator</p>
          <h1>Drive the procedural generator from the browser</h1>
          <p className="muted">
            Fill in the generation parameters below and submit to create a new city using the Vapor backend.
            Use the city list to download the generated OBJ mesh or JSON summary.
          </p>
        </div>
        <div className="hero__actions">
          <button className="secondary" onClick={refreshCities} disabled={loadingCities}>
            {loadingCities ? 'Refreshing…' : 'Refresh cities'}
          </button>
          <a className="link" href="/health" target="_blank" rel="noreferrer">
            Check API health
          </a>
        </div>
      </header>

      <section className="panel">
        <div className="panel__header">
          <div>
            <p className="eyebrow">Configuration</p>
            <h2>Generation inputs</h2>
          </div>
          <div className="panel__actions">
            <button className="secondary" onClick={resetForm} disabled={isSubmitting}>Reset defaults</button>
            <button type="submit" form="city-form" className="primary" disabled={isSubmitting}>
              {isSubmitting ? 'Generating…' : 'Generate city'}
            </button>
          </div>
        </div>

        {status.message && (
          <div className={`status status--${status.type}`}>
            {status.message}
          </div>
        )}

        <form id="city-form" className="form" onSubmit={handleSubmit}>
          <div className="form__grid">
            <label className="field">
              <span>Population</span>
              <input
                name="population"
                type="number"
                min="0"
                value={config.population}
                onChange={handleChange}
              />
            </label>

            <label className="field">
              <span>Hospitals</span>
              <input
                name="hospitals"
                type="number"
                min="0"
                value={config.hospitals}
                onChange={handleChange}
              />
            </label>

            <label className="field">
              <span>Schools</span>
              <input
                name="schools"
                type="number"
                min="0"
                value={config.schools}
                onChange={handleChange}
              />
            </label>

            <label className="field">
              <span>Transport</span>
              <select name="transport" value={config.transport} onChange={handleChange}>
                <option value="car">Car</option>
                <option value="transit">Transit</option>
                <option value="walk">Walk</option>
              </select>
            </label>

            <label className="field">
              <span>Seed</span>
              <input name="seed" type="number" value={config.seed} onChange={handleChange} />
            </label>

            <label className="field">
              <span>Grid size</span>
              <input
                name="gridSize"
                type="number"
                min="1"
                value={config.gridSize}
                onChange={handleChange}
              />
            </label>

            <label className="field">
              <span>Radius fraction</span>
              <input
                name="radiusFraction"
                type="number"
                min="0"
                max="1"
                step="0.05"
                value={config.radiusFraction}
                onChange={handleChange}
              />
            </label>

            <label className="field">
              <span>Output folder</span>
              <input
                name="output"
                type="text"
                value={config.output}
                onChange={handleChange}
                placeholder="city_2024-01-01"
              />
            </label>
          </div>
        </form>
      </section>

      <section className="panel">
        <div className="panel__header">
          <div>
            <p className="eyebrow">Latest run</p>
            <h2>{selectedCity ? `City ${selectedCity.id}` : 'No city selected yet'}</h2>
            <p className="muted">
              {selectedCity
                ? 'Review the generated summary or download the artefacts using the links below.'
                : 'Submit the form to generate your first city. Generated cities will appear here.'}
            </p>
          </div>
          {selectedCity && (
            <div className="panel__actions">
              <a className="secondary" href={selectedCity.summaryURL} target="_blank" rel="noreferrer">
                Summary JSON
              </a>
              <a className="primary" href={selectedCity.modelURL} target="_blank" rel="noreferrer">
                Download OBJ
              </a>
            </div>
          )}
        </div>

        {selectedCity && <SummaryGrid summary={selectedCity.summary} />}
      </section>

      <section className="panel">
        <div className="panel__header">
          <div>
            <p className="eyebrow">History</p>
            <h2>Generated cities</h2>
            <p className="muted">Select a city to inspect its summary or download its files.</p>
          </div>
          <div className="panel__actions">
            <button className="secondary" onClick={refreshCities} disabled={loadingCities}>
              {loadingCities ? 'Refreshing…' : 'Reload list'}
            </button>
          </div>
        </div>

        {cities.length === 0 && (
          <p className="muted">No cities generated yet. Submit the form above to get started.</p>
        )}

        <div className="city-grid">
          {cities.map((city) => (
            <CityCard
              key={city.id}
              city={city}
              onSelect={setSelectedCity}
              isActive={selectedCity?.id === city.id}
            />
          ))}
        </div>
      </section>
    </div>
  );
}

export default App;
