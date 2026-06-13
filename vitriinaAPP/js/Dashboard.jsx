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
      <div className="hero mb-4" style={{ padding: '16px' }}>
        <div className="hero-grid-bg"></div>
        <div className="hero-glow"></div>
        
        <div style={{ position: 'relative', display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 12 }}>
          {/* Left Column: Magistrala & Metrics */}
          <div style={{ display: 'flex', flexDirection: 'column', gap: 16, justifyContent: 'space-between' }}>
            <div style={{ display: 'flex', flexDirection: 'column', gap: 8 }}>
              <div style={{ display: 'flex', alignItems: 'center', gap: 10 }}>
                <MiniFan on={left.relay || right.relay} size={42} color="var(--cyan)" />
                <div style={{ display: 'flex', flexDirection: 'column', gap: 2 }}>
                  <div className="label-tiny" style={{ fontSize: 8 }}>Magistrală</div>
                  <div style={{ fontSize: 16, fontWeight: 700, letterSpacing: '0.02em', lineHeight: 1.1, color: (left.relay || right.relay) ? 'var(--cyan)' : 'var(--text-muted)' }}>
                    {(left.relay || right.relay) ? 'ACTIVĂ' : 'STANDBY'}
                  </div>
                </div>
              </div>
              <div style={{ display: 'flex', gap: 6, alignItems: 'center', flexWrap: 'wrap' }}>
                <span className={`badge ${isConnected ? 'cyan' : 'red'}`} style={{ padding: '2px 6px', fontSize: 9 }}>{isConnected ? 'BROKER OK' : 'NO BROKER'}</span>
                {s != null && s.fw != null && <span className="val-mono" style={{ fontSize: 10, color: 'var(--text-muted)' }}>FW #{s.fw}</span>}
              </div>
            </div>
            
            <div style={{ display: 'flex', flexWrap: 'wrap', gap: 12 }}>
              <div style={{ display: 'flex', flexDirection: 'column', gap: 2 }}>
                <span className="label-tiny" style={{ fontSize: 8 }}>Uptime</span>
                <span className="val-mono" style={{ fontSize: 13, fontWeight: 500, color: 'var(--cyan)' }}>{formatUptime(s?.uptimeSec)}</span>
              </div>
              <div style={{ display: 'flex', flexDirection: 'column', gap: 2 }}>
                <span className="label-tiny" style={{ fontSize: 8 }}>Heap</span>
                <span className="val-mono" style={{ fontSize: 13, fontWeight: 500, color: s && s.heap < 30000 ? 'var(--red)' : 'var(--cyan)' }}>{s ? `${Math.round((s.heap||0)/1024)}K` : '—'}</span>
              </div>
            </div>
          </div>

          {/* Right Column: System Status */}
          <div style={{ display: 'flex', flexDirection: 'column', gap: 12, justifyContent: 'space-between', background: 'rgba(0,0,0,0.3)', padding: 12, borderRadius: 'var(--radius)', border: '1px solid rgba(255,255,255,0.06)' }}>
            <div style={{ display: 'flex', flexDirection: 'column', gap: 4 }}>
              <span className="label-tiny" style={{ fontSize: 8 }}>System Status</span>
              <div style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
                <PulseBadge on={isOnline} />
                <span style={{ fontWeight: 700, fontSize: 13, letterSpacing: '0.02em', color: isOnline ? 'var(--green)' : 'var(--red)', whiteSpace: 'nowrap' }}>
                  {isOnline ? 'ONLINE' : 'OFFLINE'}
                </span>
              </div>
              <span className="val-mono" style={{ fontSize: 10, color: 'var(--text-muted)', marginTop: 2, lineHeight: 1.2 }}>{agoText}</span>
              {slaveObj && (
                <span className="val-mono" style={{ fontSize: 10, color: slaveObj.online ? 'var(--text-muted)' : 'var(--orange)', marginTop: 2 }}>
                  Slave LED: {slaveObj.online ? 'online' : 'offline'}{slaveObj.errors ? ` · ${slaveObj.errors} err` : ''}
                </span>
              )}
            </div>
            
            <button className="btn btn-cyan btn-sm" style={{ padding: '6px 8px', fontSize: 11, width: '100%', justifyContent: 'center' }} onClick={() => sendCommand({ cmd: 'refresh' })} disabled={!isConnected}>
              <svg width="12" height="12" viewBox="0 0 16 16" fill="none"><path d="M13.5 8A5.5 5.5 0 1 1 8 2.5" stroke="currentColor" strokeWidth="1.6" strokeLinecap="round"/><path d="M8 1v4l3-2-3-2z" fill="currentColor"/></svg>
              Refresh
            </button>
          </div>
        </div>
      </div>

      {rebootWarning && <div className="alert alert-orange mb-4">⚠ ESP32 a repornit recent ({s.uptimeSec}s) — pragurile pot fi la valori implicite.</div>}
      {lockActive && <div className="alert alert-orange mb-4">🔒 Control blocat ({lock.owner === 'blynk' ? 'Blynk' : 'MQTT'} activ)</div>}

      {/* Zone cards */}
      <div className="card card-corners mb-4" style={{ padding: 0, overflow: 'hidden' }}>
        <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr' }}>
          <div style={{ borderRight: '1px solid rgba(255,255,255,0.08)', padding: '12px 10px' }} className={`${left.relay ? 'relay-on' : ''} ${left.failsafe ? 'failsafe' : ''}`}>
            <ZonePanel label="Stânga" accent="var(--cyan)" data={left} disabled={!isConnected || !controlEnabled} threshT={threshT} onToggleOverride={() => toggleOverride('left')} />
          </div>
          <div style={{ padding: '12px 10px' }} className={`${right.relay ? 'relay-on' : ''} ${right.failsafe ? 'failsafe' : ''}`}>
            <ZonePanel label="Dreapta" accent="var(--green)" data={right} disabled={!isConnected || !controlEnabled} threshT={threshT} onToggleOverride={() => toggleOverride('right')} />
          </div>
        </div>
      </div>

      {/* TV telemetry strip */}
      <div className="card card-corners mb-4" style={{ padding: '12px 16px' }}>
        <div style={{ display: 'flex', flexWrap: 'wrap', gap: 14, alignItems: 'center', justifyContent: 'space-between' }}>
          <span className="label-tiny" style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
            <svg width="15" height="15" viewBox="0 0 24 24" fill="none"><rect x="2" y="7" width="20" height="13" rx="1.6" stroke="currentColor" strokeWidth="1.6"/><path d="M8 3l4 3 4-3" stroke="currentColor" strokeWidth="1.6" strokeLinecap="round" strokeLinejoin="round"/></svg>
            TV
          </span>
          <div style={{ display: 'flex', flexWrap: 'wrap', gap: 18, flex: 1, justifyContent: 'space-between', paddingLeft: 12 }}>
            <TvCell value={tvReach ? `${tvTemp}°C` : '—'} color="var(--orange)" mono />
            <TvCell value={tvReach ? `${tvHours} ore` : '—'} color="var(--cyan)" mono />
            <TvCell value={tvReach ? 'ACCESIBIL' : 'INACCESIBIL'} color={tvReach ? 'var(--green)' : 'var(--red)'} />
            <TvCell value={tvReach ? (tvPower ? 'PORNIT' : 'STANDBY') : '—'} color={tvReach && tvPower ? 'var(--green)' : 'var(--text-muted)'} />
          </div>
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

function ZonePanel({ label, accent, data, disabled, threshT, onToggleOverride }) {
  const isOn = data.relay;
  const isFail = data.failsafe;
  const isOverride = data.override;
  const relayText = isFail ? 'FAILSAFE' : (isOn ? 'PORNIT' : 'OPRIT');
  const relayColor = isFail ? 'var(--red)' : (isOn ? 'var(--green)' : 'var(--text-muted)');
  const tempHot = data.temp != null && threshT != null && data.temp >= threshT;

  return (
    <div style={{ display: 'flex', flexDirection: 'column', gap: 10 }}>
      <div className="flex items-center justify-between" style={{ flexWrap: 'wrap', gap: 6 }}>
        <div style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
          <span className="section-bar" style={{ background: accent }}></span>
          <span style={{ fontSize: 13, fontWeight: 700, letterSpacing: '0.05em', textTransform: 'uppercase' }}>{label}</span>
        </div>
        <span className={`badge ${isFail ? 'red' : (isOn ? 'green' : 'gray')}`} style={{ padding: '2px 6px', fontSize: 9 }}>{relayText}</span>
      </div>

      {/* Temperature & Humidity forced on 1 row */}
      <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', gap: 4 }}>
        <div style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
          <Thermometer value={data.temp} min={0} max={60} threshold={threshT} height={70} />
          <div>
            <div className="metric-label" style={{ fontSize: 8 }}>Temp</div>
            <div className="metric-value" style={{ color: tempHot ? 'var(--red)' : 'var(--orange)', fontSize: 16 }}>
              {data.temp != null ? data.temp.toFixed(1) : '--'}<span style={{ fontSize: 11, color: 'var(--text-muted)' }}>°C</span>
            </div>
          </div>
        </div>
        
        <div style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
          <div style={{ textAlign: 'right' }}>
            <div className="metric-label" style={{ fontSize: 8 }}>Umid</div>
            <div className="metric-value" style={{ color: 'var(--cyan)', fontSize: 16 }}>
              {data.hum != null ? data.hum.toFixed(0) : '--'}<span style={{ fontSize: 11, color: 'var(--text-muted)' }}>%</span>
            </div>
          </div>
          <Droplet value={data.hum} height={58} />
        </div>
      </div>

      {/* Fan + relay */}
      <div style={{ display: 'flex', alignItems: 'center', gap: 10 }}>
        <MiniFan on={isOn && !isFail} size={28} />
        <div>
          <div className="metric-label" style={{ fontSize: 8 }}>Ventilator</div>
          <div style={{ fontSize: 12, fontWeight: 700, color: relayColor, letterSpacing: '0.04em' }}>{relayText}</div>
        </div>
      </div>

      <div className="hr" style={{ margin: 0, opacity: 0.5 }}></div>

      <div className="flex items-center justify-between" style={{ flexWrap: 'wrap', gap: 6 }}>
        <div>
          <div className="metric-label" style={{ fontSize: 8, marginBottom: 2 }}>Override</div>
          <span className={`badge ${isOverride ? 'cyan' : 'gray'}`} style={{ padding: '2px 6px', fontSize: 9 }}>{isOverride ? 'Manual' : 'Auto'}</span>
        </div>
        <button className={`btn btn-sm ${isOverride ? 'btn-cyan' : 'btn-orange'}`} style={{ padding: '4px 8px', fontSize: 10 }} onClick={onToggleOverride} disabled={disabled}>
          {isOverride ? 'Dezact.' : 'Override'}
        </button>
      </div>

      {data.errs > 0 && <div className="alert alert-red" style={{ padding: '4px 8px', fontSize: 11 }}>⚠ {data.errs} err</div>}
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
