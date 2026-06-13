// Shared UI components + cybertech visual controls — all exported to window

const { useState, useEffect, useContext, createContext, useRef, useCallback, useMemo } = React;

// ── MQTT Context ─────────────────────────────────────────────────────────────
const MqttContext = createContext(null);

function MqttProvider({ children }) {
  const [isConnected, setIsConnected] = useState(false);
  const [isOnline, setIsOnline]       = useState(false);
  const [ventState, setVentState]     = useState(window.mqttService.lastState);
  const [tvState, setTvState]         = useState(window.mqttService.lastTvState);
  const [events, setEvents]           = useState([]);

  useEffect(() => {
    const svc = window.mqttService;
    const offConn   = svc.on('onConnectionChanged',  (v) => setIsConnected(v));
    const offOnline = svc.on('onOnlineStatusChanged', (s) => setIsOnline(s === 'online'));
    const offState  = svc.on('onStateReceived',       (s) => setVentState({ ...s }));
    const offTv     = svc.on('onTvStateReceived',     (s) => setTvState({ ...s }));
    const offEvent  = svc.on('onEventReceived', (raw) => {
      try { const ev = JSON.parse(raw); setEvents(prev => [{ ...ev, _ts: new Date() }, ...prev].slice(0, 60)); } catch(e) {}
    });
    svc.connect();
    setIsConnected(svc.isConnected);
    return () => { offConn(); offOnline(); offState(); offTv(); offEvent(); };
  }, []);

  const sendCommand = useCallback((cmd) => window.mqttService.sendCommand(cmd), []);

  return (
    <MqttContext.Provider value={{ isConnected, isOnline, ventState, tvState, events, sendCommand }}>
      {children}
    </MqttContext.Provider>
  );
}
function useMqtt() { return useContext(MqttContext); }

// ── Toggle ───────────────────────────────────────────────────────────────────
function Toggle({ checked, onChange, disabled, color }) {
  return (
    <label className="toggle" onClick={(e) => e.stopPropagation()} style={color ? { '--accent-color': color } : {}}>
      <input type="checkbox" checked={!!checked} onChange={e => onChange && onChange(e.target.checked)} disabled={disabled} />
      <span className="toggle-track"></span>
      <span className="toggle-thumb"></span>
    </label>
  );
}

// ── RangeSlider (slider + directly-editable value) ───────────────────────────
function RangeSlider({ value, onChange, min = 0, max = 100, step = 1, unit = '', disabled, color }) {
  const [text, setText] = useState(String(value));
  const [editing, setEditing] = useState(false);

  // Keep the text in sync with external value unless the user is mid-edit
  useEffect(() => { if (!editing) setText(String(value)); }, [value, editing]);

  const clamp = (n) => Math.min(max, Math.max(min, n));

  const commit = (raw) => {
    setEditing(false);
    let n = parseFloat(String(raw).replace(',', '.'));
    if (isNaN(n)) { setText(String(value)); return; }
    n = clamp(Math.round(n / step) * step);
    n = parseFloat(n.toFixed(4));
    setText(String(n));
    if (n !== value) onChange && onChange(n);
  };

  return (
    <div className="range-wrap" style={color ? { '--accent-color': color } : {}}>
      <input type="range" min={min} max={max} step={step} value={value}
        onChange={e => onChange && onChange(Number(e.target.value))} disabled={disabled} />
      <div className="range-value-edit">
        <input
          type="text" inputMode="decimal" className="range-value-input"
          value={editing ? text : `${value}`}
          disabled={disabled}
          onFocus={(e) => { setEditing(true); setText(String(value)); e.target.select(); }}
          onChange={(e) => setText(e.target.value.replace(/[^0-9.,\-]/g, ''))}
          onBlur={(e) => commit(e.target.value)}
          onKeyDown={(e) => {
            if (e.key === 'Enter') { e.currentTarget.blur(); }
            else if (e.key === 'Escape') { setText(String(value)); setEditing(false); e.currentTarget.blur(); }
            else if (e.key === 'ArrowUp') { e.preventDefault(); onChange && onChange(clamp(value + step)); }
            else if (e.key === 'ArrowDown') { e.preventDefault(); onChange && onChange(clamp(value - step)); }
          }}
        />
        {unit && <span className="range-unit">{unit}</span>}
      </div>
    </div>
  );
}

// ── PulseBadge ───────────────────────────────────────────────────────────────
function PulseBadge({ on }) {
  return (
    <span className={`pulse-badge ${on ? 'on' : 'off'}`}>
      {on && <span className="ring"></span>}
      <span className="core"></span>
    </span>
  );
}

// ── Spinner ──────────────────────────────────────────────────────────────────
function Spinner() { return <div className="spinner"></div>; }

// ── SectionHead ──────────────────────────────────────────────────────────────
function SectionHead({ title, color = 'cyan', right }) {
  const barColor = { cyan: 'var(--cyan)', orange: 'var(--orange)', green: 'var(--green)', blue: 'var(--blue)', red: 'var(--red)' }[color] || 'var(--cyan)';
  return (
    <div className="section-head" style={{ justifyContent: right ? 'space-between' : 'flex-start' }}>
      <div style={{ display: 'flex', alignItems: 'center', gap: 9, flexShrink: 0 }}>
        <span className="section-bar" style={{ background: barColor }}></span>
        <h3>{title}</h3>
      </div>
      {right}
    </div>
  );
}

// ── Thermometer (SVG) ────────────────────────────────────────────────────────
function Thermometer({ value, min = 0, max = 60, threshold, height = 96 }) {
  const pct = value == null ? 0 : Math.max(0, Math.min(1, (value - min) / (max - min)));
  const hot = threshold != null && value != null && value >= threshold;
  const color = value == null ? 'var(--text-dim)' : hot ? 'var(--red)' : value > (min + (max-min)*0.6) ? 'var(--orange)' : 'var(--cyan)';
  const tubeTop = 8, tubeBot = 70, tubeH = tubeBot - tubeTop;
  const fillH = tubeH * pct;
  const fillY = tubeBot - fillH;
  const width = (34 / 96) * height;
  return (
    <svg width={width} height={height} viewBox="0 0 34 96">
      <rect x="12" y="6" width="10" height="66" rx="5" fill="rgba(0,0,0,0.35)" stroke="var(--border-cyan)" strokeWidth="1"/>
      <rect x="13.5" y={fillY} width="7" height={fillH} rx="3.5" fill={color} style={{ transition: 'all 0.5s ease' }}/>
      <circle cx="17" cy="80" r="11" fill={color} stroke="rgba(0,0,0,0.3)" strokeWidth="1" style={{ transition: 'fill 0.5s' }}/>
      <circle cx="17" cy="80" r="5" fill="rgba(255,255,255,0.25)"/>
      {[0,0.25,0.5,0.75,1].map((t,i) => (
        <line key={i} x1="23" y1={tubeBot - tubeH*t} x2="26" y2={tubeBot - tubeH*t} stroke="var(--text-dim)" strokeWidth="1"/>
      ))}
    </svg>
  );
}

// ── Droplet (SVG humidity) ───────────────────────────────────────────────────
function Droplet({ value, height = 84 }) {
  const pct = value == null ? 0 : Math.max(0, Math.min(1, value / 100));
  const clipId = useMemo(() => 'drop' + Math.random().toString(36).slice(2,8), []);
  const fillY = 78 - 60 * pct;
  const width = (62 / 84) * height;
  return (
    <svg width={width} height={height} viewBox="0 0 62 84">
      <defs>
        <clipPath id={clipId}>
          <path d="M31 6 C31 6 52 34 52 52 a21 21 0 0 1 -42 0 C10 34 31 6 31 6 Z"/>
        </clipPath>
      </defs>
      <path d="M31 6 C31 6 52 34 52 52 a21 21 0 0 1 -42 0 C10 34 31 6 31 6 Z"
        fill="rgba(0,0,0,0.3)" stroke="var(--border-cyan)" strokeWidth="1.2"/>
      <g clipPath={`url(#${clipId})`}>
        <rect x="0" y={fillY} width="62" height="84" fill="var(--blue)" opacity="0.85" style={{ transition: 'y 0.6s ease' }}/>
        <rect x="0" y={fillY} width="62" height="4" fill="var(--cyan)" opacity="0.9" style={{ transition: 'y 0.6s ease' }}/>
      </g>
      <path d="M22 30 C24 26 28 22 31 21" stroke="rgba(255,255,255,0.4)" strokeWidth="2" fill="none" strokeLinecap="round"/>
    </svg>
  );
}

// ── MiniFan (SVG, spins when on) ─────────────────────────────────────────────
function MiniFan({ on, size = 44, color }) {
  const c = color || (on ? 'var(--cyan)' : 'var(--text-dim)');
  return (
    <svg width={size} height={size} viewBox="0 0 48 48">
      <circle cx="24" cy="24" r="22" fill="rgba(0,0,0,0.25)" stroke="var(--border-cyan)" strokeWidth="1"/>
      <g className={on ? 'fan-spin' : 'fan-still'} style={{ transformOrigin: '24px 24px' }}>
        {[0,1,2,3].map(i => (
          <path key={i} transform={`rotate(${i*90} 24 24)`}
            d="M24 24 C24 24 26 14 30 12 C34 14 32 22 24 24 Z"
            fill={c} opacity="0.92"/>
        ))}
      </g>
      <circle cx="24" cy="24" r="4" fill={c}/>
      <circle cx="24" cy="24" r="1.8" fill="#001018"/>
    </svg>
  );
}

// ── Collapsible ──────────────────────────────────────────────────────────────
function Collapsible({ title, color = 'cyan', badge, defaultOpen = false, children }) {
  const [open, setOpen] = useState(defaultOpen);
  const barColor = { cyan: 'var(--cyan)', orange: 'var(--orange)', green: 'var(--green)', blue: 'var(--blue)', red: 'var(--red)' }[color] || 'var(--cyan)';
  return (
    <div className="card card-corners" style={{ padding: 0, overflow: 'hidden' }}>
      <div className="collapsible-header" onClick={() => setOpen(o => !o)} style={{ padding: '15px 16px' }}>
        <div style={{ display: 'flex', alignItems: 'center', gap: 9, flexShrink: 0 }}>
          <span className="section-bar" style={{ background: barColor, flexShrink: 0 }}></span>
          <span style={{ fontWeight: 700, fontSize: 15, letterSpacing: '0.03em', textTransform: 'uppercase', whiteSpace: 'nowrap' }}>{title}</span>
          {badge && <span className={`badge ${badge.color || 'gray'}`}>{badge.text}</span>}
        </div>
        <svg className={`collapsible-arrow ${open ? 'open' : ''}`} width="16" height="16" viewBox="0 0 16 16" fill="none">
          <path d="M4 6l4 4 4-4" stroke="currentColor" strokeWidth="1.6" strokeLinecap="round" strokeLinejoin="round"/>
        </svg>
      </div>
      {open && <div style={{ padding: '0 16px 18px' }}>{children}</div>}
    </div>
  );
}

// ── ConfirmDialog ────────────────────────────────────────────────────────────
function ConfirmDialog({ open, title, message, onConfirm, onCancel, confirmLabel = 'Confirmă', confirmClass = 'btn-red' }) {
  if (!open) return null;
  return (
    <div className="modal-backdrop">
      <div className="card card-corners" style={{ maxWidth: 420, width: '100%', padding: 24 }}>
        <div style={{ fontWeight: 700, fontSize: 17, marginBottom: 8, letterSpacing: '0.03em', textTransform: 'uppercase' }}>{title}</div>
        <div style={{ color: 'var(--text-muted)', fontSize: 14, marginBottom: 22, lineHeight: 1.5 }}>{message}</div>
        <div style={{ display: 'flex', gap: 10, justifyContent: 'flex-end' }}>
          <button className="btn btn-cyan" onClick={onCancel}>Anulează</button>
          <button className={`btn ${confirmClass}`} onClick={onConfirm}>{confirmLabel}</button>
        </div>
      </div>
    </div>
  );
}

// ── NumberInput ──────────────────────────────────────────────────────────────
function NumberInput({ value, onChange, min, max, step = 1, unit = '', disabled }) {
  return (
    <div style={{ display: 'flex', alignItems: 'center', gap: 7 }}>
      <input type="number" className="form-input mono-input" style={{ width: 96 }}
        value={value} min={min} max={max} step={step}
        onChange={e => onChange && onChange(Number(e.target.value))} disabled={disabled} />
      {unit && <span style={{ fontSize: 13, color: 'var(--text-muted)' }}>{unit}</span>}
    </div>
  );
}

// ── TimeInput ────────────────────────────────────────────────────────────────
function TimeInput({ value, onChange, disabled }) {
  return (
    <div className="time-inputs">
      <input type="number" className="time-input" value={String(value.hours).padStart(2,'0')} min={0} max={23}
        onChange={e => onChange && onChange({ ...value, hours: Math.min(23, Math.max(0, Number(e.target.value))) })} disabled={disabled} />
      <span className="time-sep">:</span>
      <input type="number" className="time-input" value={String(value.minutes).padStart(2,'0')} min={0} max={59}
        onChange={e => onChange && onChange({ ...value, minutes: Math.min(59, Math.max(0, Number(e.target.value))) })} disabled={disabled} />
    </div>
  );
}

Object.assign(window, {
  MqttProvider, useMqtt, MqttContext, Toggle, RangeSlider, PulseBadge, Spinner,
  SectionHead, Thermometer, Droplet, MiniFan, Collapsible, ConfirmDialog, NumberInput, TimeInput,
});
