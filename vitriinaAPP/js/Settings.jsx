// Settings — thresholds + LED control with Prev/Next pattern selector

const LED_PATTERNS = [
  { id: 0,  name: 'CONSTANTĂ' },
  { id: 1,  name: 'RESPIRAȚIE' },
  { id: 2,  name: 'TRIUNGHI' },
  { id: 3,  name: 'FIERĂSTRĂU' },
  { id: 4,  name: 'STROBE' },
  { id: 5,  name: 'BĂTAIE INIMĂ' },
  { id: 6,  name: 'LUMÂNARE' },
  { id: 7,  name: 'FULGERE' },
  { id: 8,  name: 'MORSE' },
  { id: 9,  name: 'RĂSĂRIT' },
  { id: 10, name: 'APUS' },
  { id: 11, name: 'ALEATOR' },
];

const P = {
  threshT: 'cfg_threshT', threshH: 'cfg_threshH', interval: 'cfg_interval', hystT: 'cfg_hystT', hystH: 'cfg_hystH',
  ledI: 'led_intensity', schedEn: 'led_schedEn', onH: 'led_onH', onM: 'led_onM', offH: 'led_offH', offM: 'led_offM', maxI: 'led_maxI', mode: 'led_mode', morse: 'led_morse',
};
const gp = (k, d) => { const v = localStorage.getItem(k); return v !== null ? (typeof d === 'boolean' ? v === 'true' : typeof d === 'number' ? Number(v) : v) : d; };
const sp = (k, v) => localStorage.setItem(k, String(v));

function Settings() {
  const { isConnected, sendCommand } = useMqtt();

  const [tempThresh, setTempThresh] = React.useState(() => gp(P.threshT, 45));
  const [humThresh, setHumThresh]   = React.useState(() => gp(P.threshH, 60));
  const [interval, setIntervalV]    = React.useState(() => gp(P.interval, 300));
  const [hystT, setHystT]           = React.useState(() => gp(P.hystT, 2));
  const [hystH, setHystH]           = React.useState(() => gp(P.hystH, 5));

  const [ledI, setLedI]         = React.useState(() => gp(P.ledI, 0));
  const [schedEn, setSchedEn]   = React.useState(() => gp(P.schedEn, false));
  const [onH, setOnH]           = React.useState(() => gp(P.onH, 18));
  const [onM, setOnM]           = React.useState(() => gp(P.onM, 0));
  const [offH, setOffH]         = React.useState(() => gp(P.offH, 23));
  const [offM, setOffM]         = React.useState(() => gp(P.offM, 30));
  const [maxI, setMaxI]         = React.useState(() => gp(P.maxI, 80));
  const [mode, setMode]         = React.useState(() => gp(P.mode, 0));
  const [morse, setMorse]       = React.useState(() => gp(P.morse, 'SOS'));
  const [followTv, setFollowTv] = React.useState(() => gp('led_followTv', false));

  const [pp, setPp] = React.useState(() => ({
    breathMin: gp('led_p1_1',10), breathDur: gp('led_p1_2',4),
    triMin: gp('led_p2_1',10), triDur: gp('led_p2_2',4),
    sawMin: gp('led_p3_1',10), sawDur: gp('led_p3_2',4), sawDir: gp('led_p3_3',0),
    strDuty: gp('led_p4_1',50), strFreq: gp('led_p4_2',20),
    hbBpm: gp('led_p5_1',65), hbInt: gp('led_p5_2',80),
    candleVar: gp('led_p6_1',50),
    lightFreq: gp('led_p7_1',5), lightBase: gp('led_p7_2',20),
    morseDit: gp('led_p8_1',200),
    sunriseDur: gp('led_p9_1',30), sunriseFin: gp('led_p9_2',100),
    sunsetDur: gp('led_p10_1',30), sunsetStart: gp('led_p10_2',100),
    rndMin: gp('led_p11_1',10), rndMax: gp('led_p11_2',100), rndSpd: gp('led_p11_3',50),
  }));

  const [dirty, setDirty] = React.useState({ thresh: false, led: false, sched: false, mode: false, morse: false, follow: false });
  const [status, setStatus] = React.useState(null);
  const [resetDlg, setResetDlg] = React.useState(false);

  const hasChanges = Object.values(dirty).some(Boolean);
  const md = (k) => setDirty(d => ({ ...d, [k]: true }));
  const setPpf = (k, v, pk) => { setPp(p => ({ ...p, [k]: v })); if (pk) sp(pk, v); md('mode'); };

  const modeParams = (m) => {
    switch(m) {
      case 1: return { p1: pp.breathMin, p2: pp.breathDur };
      case 2: return { p1: pp.triMin, p2: pp.triDur };
      case 3: return { p1: pp.sawMin, p2: pp.sawDur, p3: pp.sawDir };
      case 4: return { p1: pp.strDuty, p2: pp.strFreq };
      case 5: return { p1: pp.hbBpm, p2: pp.hbInt };
      case 6: return { p1: pp.candleVar };
      case 7: return { p1: pp.lightFreq, p2: pp.lightBase };
      case 8: return { p1: pp.morseDit, text: morse };
      case 9: return { p1: pp.sunriseDur, p2: pp.sunriseFin };
      case 10: return { p1: pp.sunsetDur, p2: pp.sunsetStart };
      case 11: return { p1: pp.rndMin, p2: pp.rndMax, p3: pp.rndSpd };
      default: return {};
    }
  };

  const save = async () => {
    setStatus({ msg: 'Se trimite...', color: 'var(--orange)' });
    try {
      if (dirty.thresh) {
        sp(P.threshT, tempThresh); sp(P.threshH, humThresh); sp(P.interval, interval); sp(P.hystT, hystT); sp(P.hystH, hystH);
        await sendCommand({ cmd: 'setConfig', threshT: tempThresh, threshH: humThresh, interval, hystT, hystH });
      }
      if (dirty.sched) {
        sp(P.onH,onH); sp(P.onM,onM); sp(P.offH,offH); sp(P.offM,offM); sp(P.maxI,maxI); sp(P.schedEn,schedEn);
        await sendCommand({ cmd: 'setLedSchedule', onH, onM, offH, offM, maxI, enabled: schedEn });
      }
      if (dirty.led) { sp(P.ledI, ledI); await sendCommand({ cmd: 'setLed', percent: ledI }); }
      if (dirty.mode) {
        sp(P.mode, mode);
        const { text, ...params } = modeParams(mode);
        await sendCommand({ cmd: 'setLedMode', mode, ...params });
      }
      if (dirty.morse || (dirty.mode && mode === 8)) {
        sp(P.morse, morse);
        await sendCommand({ cmd: 'setLedMorseText', text: morse });
      }
      if (dirty.follow) {
        sp('led_followTv', followTv);
        await sendCommand({ cmd: 'setFollowTvBrightness', enabled: followTv ? 1 : 0 });
      }
      setDirty({ thresh:false, led:false, sched:false, mode:false, morse:false, follow:false });
      setStatus({ msg: '✓ Salvat cu succes', color: 'var(--green)' });
    } catch(e) { setStatus({ msg: `✗ ${e.message}`, color: 'var(--red)' }); }
  };

  const doReset = async () => {
    await sendCommand({ cmd: 'reset' });
    [P.threshT,P.threshH,P.interval,P.hystT,P.hystH].forEach(k => localStorage.removeItem(k));
    setTempThresh(45); setHumThresh(60); setIntervalV(300); setHystT(2); setHystH(5);
    setDirty(d => ({ ...d, thresh:false }));
    setStatus({ msg: '✓ Reset trimis', color: 'var(--green)' });
    setResetDlg(false);
  };

  const cycleMode = (dir) => {
    setMode(m => { const next = (m + dir + 12) % 12; sp(P.mode, next); return next; });
    md('mode');
  };

  return (
    <div className="page-wrap" style={{ maxWidth: 720 }}>
      <div className="page-eyebrow">Configurare</div>
      <div className="page-title">Setări</div>

      {/* Save bar */}
      <div className="card mb-4" style={{ padding: '12px 16px', position: 'sticky', top: 0, zIndex: 20 }}>
        <div className="flex items-center justify-between" style={{ gap: 10, flexWrap: 'wrap' }}>
          <span style={{ fontSize: 13, fontWeight: 600, color: status ? status.color : (hasChanges ? 'var(--orange)' : 'var(--text-muted)') }}>
            {status ? status.msg : (hasChanges ? '● Modificări nesalvate' : 'Fără modificări')}
          </span>
          <div style={{ display: 'flex', gap: 8 }}>
            <button className="btn btn-red btn-sm" onClick={() => setResetDlg(true)} disabled={!isConnected}>Reset</button>
            <button className="btn btn-solid btn-sm" onClick={save} disabled={!hasChanges || !isConnected}>Salvează</button>
          </div>
        </div>
      </div>

      <div style={{ display: 'flex', flexDirection: 'column', gap: 14 }}>
        {/* Thresholds */}
        <Collapsible title="Praguri Automatizare" color="orange" defaultOpen={false}>
          <div className="grid-2" style={{ gap: 16 }}>
            <div className="form-group">
              <label className="form-label">Prag Temperatură</label>
              <RangeSlider value={tempThresh} onChange={v => { setTempThresh(v); md('thresh'); }} min={20} max={80} step={0.5} unit="°C" color="var(--orange)" />
            </div>
            <div className="form-group">
              <label className="form-label">Prag Umiditate</label>
              <RangeSlider value={humThresh} onChange={v => { setHumThresh(v); md('thresh'); }} min={20} max={100} step={1} unit="%" color="var(--blue)" />
            </div>
            <div className="form-group">
              <label className="form-label">Histerezis Temp</label>
              <RangeSlider value={hystT} onChange={v => { setHystT(v); md('thresh'); }} min={0.5} max={10} step={0.5} unit="°C" />
            </div>
            <div className="form-group">
              <label className="form-label">Histerezis Umid</label>
              <RangeSlider value={hystH} onChange={v => { setHystH(v); md('thresh'); }} min={1} max={20} step={1} unit="%" />
            </div>
            <div className="form-group" style={{ gridColumn: '1 / -1' }}>
              <label className="form-label">Interval Citire Senzori</label>
              <div className="flex items-center flex-wrap" style={{ gap: 8 }}>
                {[10,60,300,900,3600].map(v => (
                  <button key={v} className={`btn btn-sm ${interval===v ? 'btn-solid' : 'btn-cyan'}`}
                    onClick={() => { setIntervalV(v); md('thresh'); }}>
                    {v<60?`${v}s`:v<3600?`${v/60}m`:`${v/3600}h`}
                  </button>
                ))}
                <NumberInput value={interval} onChange={v => { setIntervalV(v); md('thresh'); }} min={5} max={7200} unit="s" />
              </div>
            </div>
          </div>
        </Collapsible>

        {/* LED */}
        <Collapsible title="Control LED" color="cyan" defaultOpen={false}>
          <div style={{ display: 'flex', flexDirection: 'column', gap: 18 }}>
            <div className="form-group">
              <label className="form-label">Intensitate Curentă</label>
              <RangeSlider value={ledI} onChange={v => { setLedI(v); sp(P.ledI, v); md('led'); }} min={0} max={100} step={1} unit="%" disabled={followTv} />
              {followTv && <span className="form-hint">Dezactivat — LED-ul urmărește luminozitatea TV.</span>}
            </div>

            <div className="flex items-center justify-between">
              <div>
                <span style={{ fontSize: 14, fontWeight: 600 }}>Urmărește luminozitatea TV</span>
                <div className="form-hint">LED-ul copiază backlight-ul televizorului.</div>
              </div>
              <Toggle checked={followTv} onChange={v => { setFollowTv(v); md('follow'); }} color="var(--orange)" />
            </div>

            <div className="hr"></div>
            <div className="section-head" style={{ marginBottom: 0 }}>
              <span className="section-bar" style={{ background: 'var(--green)' }}></span>
              <h3 style={{ fontSize: 13 }}>Programare Automată</h3>
            </div>
            <label className="toggle-wrap">
              <Toggle checked={schedEn} onChange={v => { setSchedEn(v); md('sched'); }} color="var(--green)" />
              <span style={{ fontSize: 14 }}>Activează programare orară</span>
            </label>
            <div className="grid-2" style={{ gap: 16, opacity: schedEn ? 1 : 0.4 }}>
              <div className="form-group">
                <label className="form-label">Pornire</label>
                <TimeInput value={{ hours:onH, minutes:onM }} onChange={({hours,minutes}) => { setOnH(hours); setOnM(minutes); md('sched'); }} disabled={!schedEn} />
              </div>
              <div className="form-group">
                <label className="form-label">Oprire</label>
                <TimeInput value={{ hours:offH, minutes:offM }} onChange={({hours,minutes}) => { setOffH(hours); setOffM(minutes); md('sched'); }} disabled={!schedEn} />
              </div>
              <div className="form-group" style={{ gridColumn: '1 / -1' }}>
                <label className="form-label">Intensitate Maximă în Program</label>
                <RangeSlider value={maxI} onChange={v => { setMaxI(v); md('sched'); }} min={0} max={100} step={1} unit="%" disabled={!schedEn} color="var(--green)" />
              </div>
            </div>

            <div className="hr"></div>
            <div className="section-head" style={{ marginBottom: 0 }}>
              <span className="section-bar" style={{ background: 'var(--purple)' }}></span>
              <h3 style={{ fontSize: 13 }}>Mod / Pattern LED</h3>
            </div>

            {/* Prev/Next mode selector */}
            <div className="mode-selector">
              <button className="btn btn-cyan" style={{ padding: '10px 0' }} onClick={() => cycleMode(-1)}>‹</button>
              <div style={{ textAlign: 'center' }}>
                <div className="mode-name">{LED_PATTERNS[mode].name}</div>
                <div className="label-tiny" style={{ marginTop: 2 }}>{mode + 1} / 12</div>
              </div>
              <button className="btn btn-cyan" style={{ padding: '10px 0' }} onClick={() => cycleMode(1)}>›</button>
            </div>

            <LedParams mode={mode} pp={pp} setPpf={setPpf} morse={morse} setMorse={(v) => { setMorse(v); sp(P.morse, v); md('morse'); }} />
          </div>
        </Collapsible>

        {/* Account / Firebase */}
        <Collapsible title="Cont Securizat" color="red" defaultOpen={false}>
          <div style={{ display: 'flex', flexDirection: 'column', gap: 10 }}>
            <p style={{ color: 'var(--text-muted)', fontSize: 14 }}>Ești conectat ca administrator autorizat.</p>
            <button 
              className="btn btn-red" 
              style={{ padding: '12px', fontSize: 15 }}
              onClick={() => {
                if (window.firebase) {
                  window.firebase.auth().signOut().catch(err => console.error(err));
                }
              }}
            >
              Deconectare / Logout
            </button>
          </div>
        </Collapsible>
      </div>

      <ConfirmDialog open={resetDlg} title="Reset Valori Implicite"
        message="Se restaurează pragurile default (T≥45°C, H≥60%, Interval=300s) și se șterg override-urile. Continui?"
        confirmLabel="Da, Reset" confirmClass="btn-red" onConfirm={doReset} onCancel={() => setResetDlg(false)} />
    </div>
  );
}

function LedParams({ mode, pp, setPpf, morse, setMorse }) {
  const slider = (label, key, pk, min, max, step, unit) => (
    <div className="form-group" key={key}>
      <label className="form-label">{label}</label>
      <RangeSlider value={pp[key]} onChange={v => setPpf(key, v, pk)} min={min} max={max} step={step} unit={unit} />
    </div>
  );
  switch(mode) {
    case 0: return <div className="alert alert-cyan" style={{ fontSize: 13 }}>LED aprins constant la intensitatea setată mai sus.</div>;
    case 1: return <div className="grid-2" style={{ gap: 16 }}>{slider('Intensitate Min','breathMin','led_p1_1',0,100,1,'%')}{slider('Durată Ciclu','breathDur','led_p1_2',1,30,1,'s')}</div>;
    case 2: return <div className="grid-2" style={{ gap: 16 }}>{slider('Intensitate Min','triMin','led_p2_1',0,100,1,'%')}{slider('Durată Ciclu','triDur','led_p2_2',1,30,1,'s')}</div>;
    case 3: return <div className="grid-2" style={{ gap: 16 }}>
      {slider('Intensitate Min','sawMin','led_p3_1',0,100,1,'%')}{slider('Durată Ciclu','sawDur','led_p3_2',1,30,1,'s')}
      <div className="form-group"><label className="form-label">Direcție</label><div className="flex" style={{ gap: 8 }}>
        <button className={`btn btn-sm ${pp.sawDir===0?'btn-solid':'btn-cyan'}`} onClick={() => setPpf('sawDir',0,'led_p3_3')}>Urcare</button>
        <button className={`btn btn-sm ${pp.sawDir===1?'btn-solid':'btn-cyan'}`} onClick={() => setPpf('sawDir',1,'led_p3_3')}>Coborâre</button>
      </div></div></div>;
    case 4: return <div className="grid-2" style={{ gap: 16 }}>
      {slider('Duty Cycle','strDuty','led_p4_1',10,90,1,'%')}{slider('Frecvență','strFreq','led_p4_2',1,100,1,'×0.1Hz')}
      {pp.strFreq/10 > 3 && <div className="alert alert-orange" style={{ gridColumn:'1/-1', fontSize:12 }}>⚠ Frecvențe &gt;3 Hz pot provoca disconfort.</div>}</div>;
    case 5: return <div className="grid-2" style={{ gap: 16 }}>{slider('BPM','hbBpm','led_p5_1',30,200,1,' bpm')}{slider('Intensitate Peak','hbInt','led_p5_2',20,100,1,'%')}</div>;
    case 6: return <div className="grid-2" style={{ gap: 16 }}>{slider('Variație Flicker','candleVar','led_p6_1',5,100,1,'%')}</div>;
    case 7: return <div className="grid-2" style={{ gap: 16 }}>{slider('Frecvență (evt/min)','lightFreq','led_p7_1',1,60,1,'')}{slider('Nivel Baseline','lightBase','led_p7_2',0,80,1,'%')}</div>;
    case 8: return <div style={{ display:'flex', flexDirection:'column', gap:16 }}>
      <div className="form-group"><label className="form-label">Text Mesaj (Morse)</label>
        <input className="form-input mono-input" value={morse} maxLength={51}
          onChange={e => setMorse(e.target.value.toUpperCase().replace(/[^A-Z0-9 ]/g,''))} placeholder="SOS" />
        <span className="form-hint">Doar litere, cifre și spații — codificate automat în Morse.</span>
      </div>
      <div className="grid-2" style={{ gap: 16 }}>{slider('Durată Punct (dit)','morseDit','led_p8_1',50,500,10,'ms')}</div></div>;
    case 9: return <div className="grid-2" style={{ gap: 16 }}>{slider('Durată Răsărit','sunriseDur','led_p9_1',1,120,1,'min')}{slider('Intensitate Finală','sunriseFin','led_p9_2',10,100,1,'%')}</div>;
    case 10: return <div className="grid-2" style={{ gap: 16 }}>{slider('Durată Apus','sunsetDur','led_p10_1',1,120,1,'min')}{slider('Intensitate Start','sunsetStart','led_p10_2',10,100,1,'%')}</div>;
    case 11: return <div className="grid-2" style={{ gap: 16 }}>{slider('Intensitate Min','rndMin','led_p11_1',0,100,1,'%')}{slider('Intensitate Max','rndMax','led_p11_2',0,100,1,'%')}{slider('Viteză Tranziție','rndSpd','led_p11_3',1,100,1,'')}</div>;
    default: return null;
  }
}

Object.assign(window, { Settings });
