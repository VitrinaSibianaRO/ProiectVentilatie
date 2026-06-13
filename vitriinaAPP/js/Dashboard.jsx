// Dashboard — full parity with MAUI DashboardPage:
// hero · lock/reboot banners · system-status card · zone cards · TV telemetry · slave

function Dashboard() {
  const { isConnected, isOnline, ventState, tvState, sendCommand } = useMqtt();
  const [lastUpdate, setLastUpdate] = React.useState(null);
  const [, force] = React.useState(0);

  React.useEffect(() => { if (ventState) setLastUpdate(new Date()); }, [ventState]);
  // tick every 10s to refresh the "ago" label
  React.useEffect(() => { const id = setInterval(() => force(n => n + 1), 10000); return () => clearInterval(id); }, []);

  const agoText = (() => {
    if (!lastUpdate) return 'Niciodată';
    const s = Math.floor((Date.now() - lastUpdate) / 1000);
    if (s < 10) return 'Actualizat acum câteva secunde';
    if (s < 60) return `Actualizat acum ${s}s`;
    if (s < 3600) return `Actualizat acum ${Math.floor(s/60)} min`;
    return `Actualizat acum ${Math.floor(s/3600)}h`;
  })();

  const formatUptime = (sec) => {
    if (sec == null) return '—';
    if (sec < 60) return `${sec}s`;
    if (sec < 3600) return `${Math.floor(sec/60)}m ${sec%60}s`;
    if (sec < 86400) return `${Math.floor(sec/3600)}h ${Math.floor((sec%3600)/60)}m`;
    return `${Math.floor(sec/86400)}d ${Math.floor((sec%86400)/3600)}h`;
  };

  const s = ventState;
  const left = s?.left || {};
  const right = s?.right || {};
  const slave = s?.led ? s : s; // state root carries slave
  const slaveObj = s?.slave;
  const ledI = s?.led?.intensity;
  const rebootWarning = s && s.uptimeSec != null && s.uptimeSec < 120;
  const lock = s?.lock;
  const lockActive = lock && lock.owner;
  const controlEnabled = !(lockActive && lock.owner === 'blynk');

  // thresholds for hot-coloring come from local Settings (not echoed in state)
  const threshT = Number(localStorage.getItem('cfg_threshT')) || 45;

  const toggleOverride = (zone) => {
    const current = zone === 'left' ? left.override : right.override;
    sendCommand({ cmd: 'setOverride', zone, value: current ? 2 : 1 });
  };

  // TV telemetry
  const tv = tvState;
  const tvReach = tv?.reachable;
  const tvTemp = tv?.tempC ?? tv?.temperatureC;
  const tvHours = tv?.hours ?? tv?.usageHours;
  const tvPower = tv?.power;

  return (
    <div className="page-wrap">
      {/* Header */}
      <div className="flex items-center justify-between mb-4 flex-wrap" style={{ gap: 12 }}>
        <div>
          <div className="page-eyebrow">Sistem Ventilație · Vitrină</div>
          <div className="page-title" style={{ marginBottom: 0 }}>Monitor</div>
        </div>
        <span className={`badge ${isOnline ? 'green' : 'red'}`}>
          <PulseBadge on={isOnline} />{isOnline ? 'ESP32 Online' : 'ESP32 Offline'}
        </span>
      </div>

      {/* COMBINED HERO & SYSTEM STATUS CARD */}
      <div className="hero mb-4">
        <div className="hero-grid-bg"></div>
        <div className="hero-glow"></div>
        
        <div className="grid-2" style={{ position: 'relative', gap: 24 }}>
          {/* Left Column: Magistrala & Metrics */}
          <div style={{ display: 'flex', flexDirection: 'column', gap: 22, justifyContent: 'space-between' }}>
            <div style={{ display: 'flex', alignItems: 'center', gap: 18 }}>
              <MiniFan on={left.relay || right.relay} size={66} color="var(--cyan)" />
              <div style={{ display: 'flex', flexDirection: 'column', gap: 5 }}>
                <div className="label-tiny">Status Magistrală</div>
                <div style={{ fontSize: 26, fontWeight: 700, letterSpacing: '0.03em', lineHeight: 1.05, color: (left.relay || right.relay) ? 'var(--cyan)' : 'var(--text-muted)' }}>
                  {(left.relay || right.relay) ? 'VENTILARE ACTIVĂ' : 'STANDBY'}
                </div>
                <div style={{ display: 'flex', gap: 14, marginTop: 2, alignItems: 'center', flexWrap: 'wrap' }}>
                  <span className={`badge ${isConnected ? 'cyan' : 'red'}`} style={{ padding: '2px 8px' }}>{isConnected ? 'BROKER OK' : 'NO BROKER'}</span>
                  {s != null && s.fw != null && <span className="val-mono" style={{ fontSize: 11, color: 'var(--text-muted)' }}>FW #{s.fw}</span>}
                </div>
              </div>
            </div>
            
            <div style={{ display: 'flex', gap: 24 }}>
              <div style={{ display: 'flex', flexDirection: 'column', gap: 3 }}>
                <span className="label-tiny">Uptime</span>
                <span className="val-mono" style={{ fontSize: 16, fontWeight: 500, color: 'var(--cyan)' }}>{formatUptime(s?.uptimeSec)}</span>
              </div>
              <div style={{ display: 'flex', flexDirection: 'column', gap: 3 }}>
                <span className="label-tiny">Heap</span>
                <span className="val-mono" style={{ fontSize: 16, fontWeight: 500, color: s && s.heap < 30000 ? 'var(--red)' : 'var(--cyan)' }}>{s ? `${Math.round((s.heap||0)/1024)}K` : '—'}</span>
              </div>
              <div style={{ display: 'flex', flexDirection: 'column', gap: 3 }}>
                <span className="label-tiny">LED</span>
                <span className="val-mono" style={{ fontSize: 16, fontWeight: 500, color: 'var(--orange)' }}>{ledI != null ? `${ledI}%` : '—'}</span>
              </div>
            </div>
          </div>

          {/* Right Column: System Status */}
          <div style={{ display: 'flex', flexDirection: 'column', gap: 16, justifyContent: 'center', background: 'rgba(0,0,0,0.25)', padding: 18, borderRadius: 'var(--radius)', border: '1px solid rgba(255,255,255,0.04)' }}>
            <div className="flex items-start justify-between" style={{ gap: 12, flexWrap: 'wrap' }}>
              <div style={{ display: 'flex', flexDirection: 'column', gap: 5 }}>
                <span className="label-tiny">System Status</span>
                <div style={{ display: 'flex', alignItems: 'center', gap: 9 }}>
                  <PulseBadge on={isOnline} />
                  <span style={{ fontWeight: 700, fontSize: 15, letterSpacing: '0.04em', color: isOnline ? 'var(--green)' : 'var(--red)', whiteSpace: 'nowrap' }}>
                    {isOnline ? 'ESP32 ONLINE' : 'ESP32 OFFLINE'}
                  </span>
                </div>
                <span className="val-mono" style={{ fontSize: 11, color: 'var(--text-muted)', whiteSpace: 'nowrap', marginTop: 4 }}>{agoText}</span>
                {slaveObj && (
                  <span className="val-mono" style={{ fontSize: 11, color: slaveObj.online ? 'var(--text-muted)' : 'var(--orange)', whiteSpace: 'nowrap' }}>
                    Slave LED: {slaveObj.online ? 'online' : 'offline'}{slaveObj.errors ? ` · ${slaveObj.errors} erori` : ''}
                  </span>
                )}
              </div>
              <button className="btn btn-cyan btn-sm" onClick={() => sendCommand({ cmd: 'refresh' })} disabled={!isConnected}>
                <svg width="13" height="13" viewBox="0 0 16 16" fill="none"><path d="M13.5 8A5.5 5.5 0 1 1 8 2.5" stroke="currentColor" strokeWidth="1.6" strokeLinecap="round"/><path d="M8 1v4l3-2-3-2z" fill="currentColor"/></svg>
                Refresh
              </button>
            </div>
          </div>
        </div>
      </div>

      {rebootWarning && <div className="alert alert-orange mb-4">⚠ ESP32 a repornit recent ({s.uptimeSec}s) — pragurile pot fi la valori implicite.</div>}
      {lockActive && <div className="alert alert-orange mb-4">🔒 Control blocat ({lock.owner === 'blynk' ? 'Blynk' : 'MQTT'} activ)</div>}

      {/* Zone cards */}
      <div className="grid-2 mb-4">
        <ZoneCard label="Zona Stânga" accent="var(--cyan)" data={left} disabled={!isConnected || !controlEnabled} threshT={threshT} onToggleOverride={() => toggleOverride('left')} />
        <ZoneCard label="Zona Dreapta" accent="var(--green)" data={right} disabled={!isConnected || !controlEnabled} threshT={threshT} onToggleOverride={() => toggleOverride('right')} />
      </div>

      {/* TV telemetry strip */}
      <div className="card card-corners" style={{ padding: '12px 16px' }}>
        <div style={{ display: 'grid', gridTemplateColumns: 'auto 1fr 1fr 1fr 1fr', gap: 10, alignItems: 'center' }}>
          <span className="label-tiny" style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
            <svg width="15" height="15" viewBox="0 0 24 24" fill="none"><rect x="2" y="7" width="20" height="13" rx="1.6" stroke="currentColor" strokeWidth="1.6"/><path d="M8 3l4 3 4-3" stroke="currentColor" strokeWidth="1.6" strokeLinecap="round" strokeLinejoin="round"/></svg>
            TV
          </span>
          <TvCell value={tvReach ? `${tvTemp}°C` : '—'} color="var(--orange)" mono />
          <TvCell value={tvReach ? `${tvHours} ore` : '—'} color="var(--cyan)" mono />
          <TvCell value={tvReach ? 'ACCESIBIL' : 'INACCESIBIL'} color={tvReach ? 'var(--green)' : 'var(--red)'} />
          <TvCell value={tvReach ? (tvPower ? 'PORNIT' : 'STANDBY') : '—'} color={tvReach && tvPower ? 'var(--green)' : 'var(--text-muted)'} />
        </div>
      </div>
    </div>
  );
}

function HeroMetric({ label, value, accent = 'var(--cyan)' }) {
  return (
    <div style={{ display: 'flex', flexDirection: 'column', gap: 3, alignItems: 'flex-end' }}>
      <span className="label-tiny">{label}</span>
      <span className="val-mono" style={{ fontSize: 16, fontWeight: 500, color: accent }}>{value}</span>
    </div>
  );
}

function ZoneCard({ label, accent, data, disabled, threshT, onToggleOverride }) {
  const isOn = data.relay;
  const isFail = data.failsafe;
  const isOverride = data.override;
  const relayText = isFail ? 'FAILSAFE' : (isOn ? 'PORNIT' : 'OPRIT');
  const relayColor = isFail ? 'var(--red)' : (isOn ? 'var(--green)' : 'var(--text-muted)');
  const tempHot = data.temp != null && threshT != null && data.temp >= threshT;

  return (
    <div className={`zone-card ${isOn ? 'relay-on' : ''} ${isFail ? 'failsafe' : ''}`}>
      <div className="flex items-center justify-between">
        <div style={{ display: 'flex', alignItems: 'center', gap: 9 }}>
          <span className="section-bar" style={{ background: accent }}></span>
          <span style={{ fontSize: 14, fontWeight: 700, letterSpacing: '0.05em', textTransform: 'uppercase' }}>{label}</span>
        </div>
        <span className={`badge ${isFail ? 'red' : (isOn ? 'green' : 'gray')}`}>{relayText}</span>
      </div>

      {/* Temperature */}
      <div style={{ display: 'flex', alignItems: 'center', gap: 12 }}>
        <Thermometer value={data.temp} min={0} max={60} threshold={threshT} />
        <div>
          <div className="metric-label">Temp</div>
          <div className="metric-value" style={{ color: tempHot ? 'var(--red)' : 'var(--orange)' }}>
            {data.temp != null ? data.temp.toFixed(1) : '--'}<span style={{ fontSize: 13, color: 'var(--text-muted)' }}>°C</span>
          </div>
        </div>
        <div style={{ marginLeft: 'auto', display: 'flex', alignItems: 'center', gap: 12 }}>
          <div style={{ textAlign: 'right' }}>
            <div className="metric-label">Umid</div>
            <div className="metric-value" style={{ color: 'var(--cyan)' }}>
              {data.hum != null ? data.hum.toFixed(0) : '--'}<span style={{ fontSize: 13, color: 'var(--text-muted)' }}>%</span>
            </div>
          </div>
          <Droplet value={data.hum} />
        </div>
      </div>

      {/* Fan + relay */}
      <div style={{ display: 'flex', alignItems: 'center', gap: 12 }}>
        <MiniFan on={isOn && !isFail} size={38} />
        <div>
          <div className="metric-label">Ventilator</div>
          <div style={{ fontSize: 14, fontWeight: 700, color: relayColor, letterSpacing: '0.04em' }}>{relayText}</div>
        </div>
      </div>

      <div className="hr" style={{ margin: '2px 0' }}></div>

      <div className="flex items-center justify-between">
        <div>
          <div className="metric-label" style={{ marginBottom: 4 }}>Override</div>
          <span className={`badge ${isOverride ? 'cyan' : 'gray'}`}>{isOverride ? 'Manual ON' : 'Auto'}</span>
        </div>
        <button className={`btn btn-sm ${isOverride ? 'btn-cyan' : 'btn-orange'}`} onClick={onToggleOverride} disabled={disabled}>
          {isOverride ? 'Dezactivează' : 'Override ON'}
        </button>      </div>

      {data.errs > 0 && <div className="alert alert-red" style={{ padding: '7px 11px', fontSize: 12 }}>⚠ {data.errs} erori senzor</div>}
    </div>
  );
}

function TvCell({ value, color, mono }) {
  return (
    <div style={{ textAlign: 'center' }}>
      <span style={{ fontFamily: mono ? 'var(--font-mono)' : 'var(--font-head)', fontSize: mono ? 16 : 13, fontWeight: 700, color }}>{value}</span>
    </div>
  );
}

Object.assign(window, { Dashboard });
