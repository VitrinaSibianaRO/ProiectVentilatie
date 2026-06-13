// TV — LG signage display control (telemetry + quick control + advanced + config)

const TV_INPUTS  = ['HDMI 1', 'HDMI 2 / OPS', 'DisplayPort'];
const TV_INPUT_CODES = [0x90, 0x91, 0xC0];
const TV_PICTURE = ['Vivid', 'Standard', 'Cinema', 'Sports', 'Game', 'Photos'];
const TV_ENERGY  = ['Dezactivat', 'Minim', 'Mediu', 'Maxim', 'Auto', 'Ecran oprit'];

function Tv() {
  const { isConnected, tvState, sendCommand } = useMqtt();
  const s = tvState;

  // Local control state (synced from telemetry)
  const [volume, setVolume]     = React.useState(0);
  const [backlight, setBacklight] = React.useState(100);
  const [muted, setMuted]       = React.useState(false);
  const [inputIdx, setInputIdx] = React.useState(0);
  const [pictIdx, setPictIdx]   = React.useState(0);
  const [energyIdx, setEnergyIdx] = React.useState(0);
  const [noSignalOff, setNoSignalOff] = React.useState(false);
  const applying = React.useRef(false);

  const [tvIp, setTvIp]   = React.useState(() => localStorage.getItem('tv_ip') || '');
  const [tvMac, setTvMac] = React.useState(() => localStorage.getItem('tv_mac') || '');
  const [cfgSaved, setCfgSaved] = React.useState(false);
  const [toast, setToast] = React.useState(null);

  // Apply incoming telemetry
  React.useEffect(() => {
    if (!s) return;
    applying.current = true;
    setVolume(s.volume ?? 0);
    setBacklight(s.backlight ?? 100);
    setMuted(!!s.muted);
    setPictIdx(s.pictureMode ?? 0);
    setEnergyIdx(s.energySaving ?? 0);
    setNoSignalOff(!!s.noSignalOff);
    const codeMap = { 0x90: 0, 0x91: 1, 0xC0: 2 };
    setInputIdx(codeMap[s.inputId] ?? 0);
    setTimeout(() => { applying.current = false; }, 0);
  }, [s]);

  const flash = (msg) => { setToast(msg); setTimeout(() => setToast(null), 2000); };

  const cmd = (action, value) => {
    const payload = value !== undefined ? { cmd: 'setTv', action, value } : { cmd: 'setTv', action };
    sendCommand(payload);
  };

  const powerOn  = () => { cmd('power_on');  flash('✓ Pornire trimisă'); };
  const powerOff = () => { cmd('power_off'); flash('✓ Oprire trimisă'); };
  const commitVolume    = (v) => { setVolume(v); cmd('volume', v); };
  const commitBacklight = (v) => { setBacklight(v); cmd('backlight', v); };
  const toggleMute = (v) => { setMuted(v); cmd('mute', v ? 1 : 0); };
  const setInput = (i) => { setInputIdx(i); cmd('input', TV_INPUT_CODES[i]); };
  const cyclePict = (d) => { const v = (pictIdx + d + TV_PICTURE.length) % TV_PICTURE.length; setPictIdx(v); cmd('pictureMode', v); };
  const cycleEnergy = (d) => { const v = (energyIdx + d + TV_ENERGY.length) % TV_ENERGY.length; setEnergyIdx(v); cmd('energySaving', v); };
  const toggleNoSig = (v) => { setNoSignalOff(v); cmd('noSignalOff', v ? 1 : 0); };

  const saveCfg = () => {
    if (!tvIp.trim() || !tvMac.trim()) return;
    localStorage.setItem('tv_ip', tvIp.trim());
    localStorage.setItem('tv_mac', tvMac.trim());
    sendCommand({ cmd: 'setTvConfig', ip: tvIp.trim(), mac: tvMac.trim() });
    setCfgSaved(true); setTimeout(() => setCfgSaved(false), 2400);
  };

  const reachable = s?.reachable;
  const power = s?.power;
  const hasSignal = s?.signal ?? s?.hasSignal;
  const tempC = s?.tempC ?? s?.temperatureC;
  const overheating = tempC != null && tempC > 60;
  const statusText = s == null ? 'Neconfigurat' : (reachable ? (power ? 'Pornit' : 'Standby') : 'Inaccesibil');

  return (
    <div className="page-wrap" style={{ maxWidth: 720 }}>
      <div className="page-eyebrow">Display Control</div>
      <div className="page-title">LG 75XS4P Signage</div>

      {toast && <div className="alert alert-green mb-4">{toast}</div>}

      <div style={{ display: 'flex', flexDirection: 'column', gap: 14 }}>

      {/* Telemetry */}
      <Collapsible title="Telemetrie & Status" color="cyan" defaultOpen={false}
        badge={{ text: statusText, color: power ? 'green' : 'gray' }}>
        <div className="grid-2" style={{ gap: 16 }}>
          <TvStat label="Rețea TCP" value={reachable ? 'ACCESIBIL' : 'OFFLINE'} color={reachable ? 'var(--green)' : 'var(--orange)'} />
          <TvStat label="Semnal HDMI" value={hasSignal ? 'ACTIV' : 'NO SIGNAL'} color={hasSignal ? 'var(--green)' : 'var(--orange)'} />
          <div>
            <div className="metric-label">Temp. Panou</div>
            <div className="val-mono" style={{ fontSize: 18, color: overheating ? 'var(--red)' : 'var(--orange)' }}>{tempC != null ? `${tempC}°C` : '--'}</div>
            {overheating && <div style={{ fontSize: 10, color: 'var(--red)', fontWeight: 600 }}>⚠ DEPĂȘIRE TERMICĂ</div>}
          </div>
          <TvStat label="Ore Utilizare" value={s?.hours != null || s?.usageHours != null ? `${s.hours ?? s.usageHours} ore` : '--'} color="var(--cyan)" />
          <TvStat label="Serial" value={s?.serial || '--'} color="var(--text-muted)" small />
          <TvStat label="SW Version" value={s?.swVersion || '--'} color="var(--text-muted)" small />
        </div>
      </Collapsible>

      {/* Quick control */}
      <Collapsible title="Control Rapid" color="orange" defaultOpen={false}>
        <div style={{ display: 'flex', flexDirection: 'column', gap: 16 }}>
          <div className="grid-2" style={{ gap: 10 }}>
            <button className="btn btn-green btn-lg" onClick={powerOn} disabled={!isConnected}>⏻ PORNIT</button>
            <button className="btn btn-orange btn-lg" onClick={powerOff} disabled={!isConnected}>⏼ OPRIT</button>
          </div>

          <div className="form-group">
            <label className="form-label">Volum</label>
            <RangeSlider value={volume} onChange={commitVolume} min={0} max={100} step={1} unit="%" disabled={!isConnected} />
          </div>

          <div className="flex items-center justify-between">
            <span style={{ fontSize: 14 }}>Silențios (Mute)</span>
            <Toggle checked={muted} onChange={toggleMute} disabled={!isConnected} color="var(--orange)" />
          </div>

          <div className="form-group">
            <label className="form-label">Intrare Semnal</label>
            <div style={{ display: 'flex', gap: 8, flexWrap: 'wrap' }}>
              {TV_INPUTS.map((name, i) => (
                <button key={i} className={`btn btn-sm ${inputIdx === i ? 'btn-solid' : 'btn-cyan'}`} onClick={() => setInput(i)} disabled={!isConnected}>
                  {name}
                </button>
              ))}
            </div>
          </div>
        </div>
      </Collapsible>

      {/* Advanced */}
      <Collapsible title="Setări Avansate" color="blue" defaultOpen={false}>
        <div style={{ display: 'flex', flexDirection: 'column', gap: 16 }}>
          <div className="form-group">
            <label className="form-label">Backlight</label>
            <RangeSlider value={backlight} onChange={commitBacklight} min={0} max={100} step={1} unit="%" disabled={!isConnected} />
          </div>

          <div className="form-group">
            <label className="form-label">Mod Imagine</label>
            <div className="mode-selector">
              <button className="btn btn-cyan" style={{ padding: '10px 0' }} onClick={() => cyclePict(-1)} disabled={!isConnected}>‹</button>
              <div className="mode-name">{TV_PICTURE[pictIdx]}</div>
              <button className="btn btn-cyan" style={{ padding: '10px 0' }} onClick={() => cyclePict(1)} disabled={!isConnected}>›</button>
            </div>
          </div>

          <div className="form-group">
            <label className="form-label">Economie Energie</label>
            <div className="mode-selector">
              <button className="btn btn-cyan" style={{ padding: '10px 0' }} onClick={() => cycleEnergy(-1)} disabled={!isConnected}>‹</button>
              <div className="mode-name">{TV_ENERGY[energyIdx]}</div>
              <button className="btn btn-cyan" style={{ padding: '10px 0' }} onClick={() => cycleEnergy(1)} disabled={!isConnected}>›</button>
            </div>
          </div>

          <div className="flex items-center justify-between">
            <span style={{ fontSize: 14 }}>Oprire la lipsă semnal (15 min)</span>
            <Toggle checked={noSignalOff} onChange={toggleNoSig} disabled={!isConnected} color="var(--orange)" />
          </div>
        </div>
      </Collapsible>

      {/* Config */}
      <Collapsible title="Configurare Dispozitiv" color="blue" defaultOpen={false}>
        <div style={{ display: 'flex', flexDirection: 'column', gap: 14 }}>
          <div className="form-group">
            <label className="form-label">Adresă IP TV</label>
            <input className="form-input mono-input" value={tvIp} onChange={e => setTvIp(e.target.value)} placeholder="192.168.1.100" />
          </div>
          <div className="form-group">
            <label className="form-label">Adresă MAC TV (pentru Wake-on-LAN)</label>
            <input className="form-input mono-input" value={tvMac} onChange={e => setTvMac(e.target.value)} placeholder="AA:BB:CC:DD:EE:FF" />
          </div>
          <span className="form-hint">IP static sau rezervare DHCP necesară în router.</span>
          <button className="btn btn-solid" onClick={saveCfg} disabled={!tvIp.trim() || !tvMac.trim()}>
            {cfgSaved ? '✓ Salvat' : 'Salvează Configurare'}
          </button>
        </div>
      </Collapsible>

      </div>
    </div>
  );
}

function TvStat({ label, value, color, small }) {
  return (
    <div>
      <div className="metric-label">{label}</div>
      <div className="val-mono" style={{ fontSize: small ? 12 : 15, color, fontWeight: 500, wordBreak: 'break-all' }}>{value}</div>
    </div>
  );
}

Object.assign(window, { Tv });
