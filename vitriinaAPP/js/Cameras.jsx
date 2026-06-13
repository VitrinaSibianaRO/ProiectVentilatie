// Cameras — IMOU/RTSP camera config, add/edit/delete, stored in localStorage

const CAM_KEY = 'vitriina_cameras';
const CRED_KEY = 'vitriina_imou_creds';
const loadCams = () => { try { return JSON.parse(localStorage.getItem(CAM_KEY)||'[]'); } catch(e){ return []; } };
const saveCams = (c) => localStorage.setItem(CAM_KEY, JSON.stringify(c));
const loadCreds = () => { try { return JSON.parse(localStorage.getItem(CRED_KEY)||'{}'); } catch(e){ return {}; } };

function Cameras() {
  const [cameras, setCameras] = React.useState(loadCams);
  const [editCam, setEditCam] = React.useState(null);
  const [deleteCam, setDeleteCam] = React.useState(null);
  const [creds, setCreds] = React.useState(loadCreds);
  const [credSaved, setCredSaved] = React.useState(false);
  const [stream, setStream] = React.useState(null);

  const persist = (c) => { setCameras(c); saveCams(c); };

  const handleSave = (cam) => {
    let updated;
    if (cam.id && cameras.find(c => c.id === cam.id)) {
      updated = cameras.map(c => c.id === cam.id ? { ...cam } : c);
    } else {
      updated = [...cameras, { ...cam, id: Date.now().toString(36), order: cameras.length }];
    }
    persist(updated); setEditCam(null);
  };
  const handleDelete = () => { if (deleteCam) { persist(cameras.filter(c => c.id !== deleteCam.id)); setDeleteCam(null); } };
  const toggle = (id) => persist(cameras.map(c => c.id === id ? { ...c, isEnabled: !c.isEnabled } : c));
  const move = (id, dir) => {
    const i = cameras.findIndex(c => c.id === id);
    if ((dir<0 && i===0) || (dir>0 && i===cameras.length-1)) return;
    const a = [...cameras]; [a[i],a[i+dir]] = [a[i+dir],a[i]]; persist(a);
  };
  const saveCreds = () => { localStorage.setItem(CRED_KEY, JSON.stringify(creds)); setCredSaved(true); setTimeout(()=>setCredSaved(false),2400); };

  const enabled = cameras.filter(c => c.isEnabled !== false).length;

  return (
    <div className="page-wrap">
      <div className="flex items-center justify-between flex-wrap" style={{ gap: 12, marginBottom: 18 }}>
        <div>
          <div className="page-eyebrow">Supraveghere</div>
          <div className="page-title" style={{ marginBottom: 0 }}>Camere</div>
        </div>
        <button className="btn btn-solid" onClick={() => setEditCam({ name:'', localIp:'', rtspPort:554, rtspUsername:'admin', imouDeviceId:'', imouChannelId:1, isEnabled:true, preferredScope:'Auto' })}>
          + Adaugă
        </button>
      </div>

      <div className="mb-4"><Collapsible title="Credențiale IMOU Cloud" color="blue" defaultOpen={false}>
        <div style={{ display: 'flex', flexDirection: 'column', gap: 14 }}>
          <div className="alert alert-cyan" style={{ fontSize: 12 }}>Stocate local în browser. Folosite pentru autentificare la IMOU OpenAPI.</div>
          <div className="grid-2" style={{ gap: 14 }}>
            <div className="form-group"><label className="form-label">App ID</label>
              <input className="form-input" value={creds.appId||''} onChange={e => setCreds(c => ({...c, appId:e.target.value}))} placeholder="lc..." /></div>
            <div className="form-group"><label className="form-label">App Secret</label>
              <input className="form-input" type="password" value={creds.appSecret||''} onChange={e => setCreds(c => ({...c, appSecret:e.target.value}))} placeholder="••••••••" /></div>
            <div className="form-group"><label className="form-label">Regiune</label>
              <select className="form-input" value={creds.region||'eu'} onChange={e => setCreds(c => ({...c, region:e.target.value}))}>
                <option value="eu">EU — openapi.easy4ip.com</option><option value="us">US</option><option value="ap">Asia Pacific</option>
              </select></div>
          </div>
          <div><button className="btn btn-solid btn-sm" onClick={saveCreds}>{credSaved ? '✓ Salvat' : 'Salvează Credențiale'}</button></div>
        </div>
      </Collapsible></div>

      <div className="mb-4"><Collapsible title="Camere Configurate" color="cyan" defaultOpen={false}
        badge={{ text: `${cameras.length} · ${enabled} active`, color: 'gray' }}>
        {cameras.length === 0 && (
          <div style={{ textAlign: 'center', padding: '36px 20px', color: 'var(--text-muted)' }}>
            <svg width="44" height="44" viewBox="0 0 24 24" fill="none" style={{ margin: '0 auto 12px', opacity: 0.5 }}>
              <rect x="2" y="6" width="14" height="12" rx="2" stroke="var(--cyan)" strokeWidth="1.5"/><path d="M16 10l6-3v10l-6-3" stroke="var(--cyan)" strokeWidth="1.5" strokeLinejoin="round"/></svg>
            <div style={{ fontWeight: 700, fontSize: 15, marginBottom: 4, color: 'var(--text)' }}>Nicio cameră configurată</div>
            <div style={{ fontSize: 13 }}>Adaugă o cameră IMOU sau RTSP pentru a o vizualiza.</div>
          </div>
        )}

        <div className="grid-2" style={{ gap: 14 }}>
          {cameras.map((cam, i) => (
            <CameraCard key={cam.id} cam={cam} idx={i} total={cameras.length}
              onEdit={() => setEditCam({...cam})} onDelete={() => setDeleteCam(cam)} onToggle={() => toggle(cam.id)}
              onUp={() => move(cam.id,-1)} onDown={() => move(cam.id,1)} onView={() => setStream(cam)} />
          ))}
        </div>
      </Collapsible></div>

      {editCam && <CameraEditDialog cam={editCam} onSave={handleSave} onClose={() => setEditCam(null)} />}
      <ConfirmDialog open={!!deleteCam} title="Șterge Cameră" message={`Ștergi camera "${deleteCam?.name}"?`}
        confirmLabel="Șterge" confirmClass="btn-red" onConfirm={handleDelete} onCancel={() => setDeleteCam(null)} />
      {stream && <StreamViewer cam={stream} onClose={() => setStream(null)} />}
    </div>
  );
}

function CameraCard({ cam, idx, total, onEdit, onDelete, onToggle, onUp, onDown, onView }) {
  const en = cam.isEnabled !== false;
  return (
    <div className="camera-card" style={{ opacity: en ? 1 : 0.55 }}>
      <div className="camera-preview" onClick={onView} style={{ cursor: 'pointer' }}>
        <div style={{ textAlign: 'center' }}>
          <svg width="40" height="40" viewBox="0 0 24 24" fill="none" style={{ opacity: 0.55 }}>
            <rect x="2" y="6" width="14" height="12" rx="2" stroke="var(--cyan)" strokeWidth="1.4"/><path d="M16 10l6-3v10l-6-3" stroke="var(--cyan)" strokeWidth="1.4" strokeLinejoin="round"/></svg>
          <div className="label-tiny" style={{ marginTop: 6 }}>Apasă pentru stream</div>
        </div>
        <div style={{ position: 'absolute', top: 9, right: 9 }}>
          <span className={`badge ${en ? 'green' : 'gray'}`} style={{ fontSize: 10 }}>{en ? 'Activă' : 'Off'}</span>
        </div>
        <div style={{ position: 'absolute', top: 9, left: 9 }} className="label-tiny mono">CH.{cam.imouChannelId||1}</div>
      </div>
      <div className="camera-info">
        <div style={{ fontWeight: 700, fontSize: 15, marginBottom: 3 }}>{cam.name || 'Cameră'}</div>
        <div className="val-mono" style={{ fontSize: 11, color: 'var(--text-muted)' }}>
          {cam.localIp ? `${cam.localIp}:${cam.rtspPort||554}` : 'IP neconfigurat'}
        </div>
        {cam.imouDeviceId && <div className="val-mono" style={{ fontSize: 11, color: 'var(--text-muted)', marginTop: 2 }}>IMOU · {cam.imouDeviceId}</div>}
        <div className="hr" style={{ margin: '11px 0' }}></div>
        <div style={{ display: 'flex', gap: 6, flexWrap: 'wrap' }}>
          <button className="btn btn-cyan btn-sm" onClick={onEdit}>Editează</button>
          <button className="btn btn-cyan btn-sm" onClick={onToggle}>{en ? 'Off' : 'On'}</button>
          {idx>0 && <button className="btn btn-cyan btn-sm" onClick={onUp}>↑</button>}
          {idx<total-1 && <button className="btn btn-cyan btn-sm" onClick={onDown}>↓</button>}
          <button className="btn btn-red btn-sm" onClick={onDelete}>✕</button>
        </div>
      </div>
    </div>
  );
}

function CameraEditDialog({ cam, onSave, onClose }) {
  const [f, setF] = React.useState({ ...cam });
  const set = (k,v) => setF(p => ({ ...p, [k]: v }));
  return (
    <div className="modal-backdrop">
      <div className="card card-corners" style={{ width: '100%', maxWidth: 520, maxHeight: '88vh', overflow: 'auto', padding: 24 }}>
        <div className="flex items-center justify-between" style={{ marginBottom: 20 }}>
          <div style={{ fontWeight: 700, fontSize: 17, textTransform: 'uppercase', letterSpacing: '0.03em' }}>{cam.id ? 'Editează Cameră' : 'Cameră Nouă'}</div>
          <button className="btn btn-cyan btn-sm" onClick={onClose}>✕</button>
        </div>
        <div style={{ display: 'flex', flexDirection: 'column', gap: 14 }}>
          <div className="form-group"><label className="form-label">Nume</label>
            <input className="form-input" value={f.name||''} onChange={e => set('name', e.target.value)} placeholder="ex: Intrare Vitrină" /></div>
          <div className="grid-2" style={{ gap: 14 }}>
            <div className="form-group"><label className="form-label">IP Local</label>
              <input className="form-input mono-input" value={f.localIp||''} onChange={e => set('localIp', e.target.value)} placeholder="192.168.1.100" /></div>
            <div className="form-group"><label className="form-label">Port RTSP</label>
              <input className="form-input mono-input" type="number" value={f.rtspPort||554} onChange={e => set('rtspPort', Number(e.target.value))} /></div>
            <div className="form-group"><label className="form-label">User RTSP</label>
              <input className="form-input" value={f.rtspUsername||''} onChange={e => set('rtspUsername', e.target.value)} placeholder="admin" /></div>
            <div className="form-group"><label className="form-label">Parolă RTSP</label>
              <input className="form-input" type="password" value={f.rtspPassword||''} onChange={e => set('rtspPassword', e.target.value)} /></div>
          </div>
          <div className="hr"></div>
          <div className="label-tiny">IMOU Cloud (opțional)</div>
          <div className="grid-2" style={{ gap: 14 }}>
            <div className="form-group"><label className="form-label">Device ID</label>
              <input className="form-input mono-input" value={f.imouDeviceId||''} onChange={e => set('imouDeviceId', e.target.value)} /></div>
            <div className="form-group"><label className="form-label">Canal</label>
              <input className="form-input mono-input" type="number" value={f.imouChannelId||1} min={1} max={16} onChange={e => set('imouChannelId', Number(e.target.value))} /></div>
          </div>
          <div className="form-group"><label className="form-label">Sursă Preferată</label>
            <select className="form-input" value={f.preferredScope||'Auto'} onChange={e => set('preferredScope', e.target.value)}>
              <option value="Auto">Auto</option><option value="Local">Local (RTSP)</option><option value="Cloud">Cloud (IMOU)</option>
            </select></div>
          <label className="toggle-wrap"><Toggle checked={f.isEnabled !== false} onChange={v => set('isEnabled', v)} /><span style={{ fontSize: 14 }}>Activă</span></label>
          <div style={{ display: 'flex', gap: 10, justifyContent: 'flex-end', marginTop: 6 }}>
            <button className="btn btn-cyan" onClick={onClose}>Anulează</button>
            <button className="btn btn-solid" onClick={() => onSave(f)} disabled={!f.name}>Salvează</button>
          </div>
        </div>
      </div>
    </div>
  );
}

function StreamViewer({ cam, onClose }) {
  return (
    <div className="modal-backdrop" style={{ flexDirection: 'column' }}>
      <div style={{ width: '92%', maxWidth: 820 }}>
        <div className="flex items-center justify-between" style={{ marginBottom: 12 }}>
          <div style={{ fontWeight: 700, fontSize: 16, letterSpacing: '0.03em' }}>{cam.name}</div>
          <button className="btn btn-cyan btn-sm" onClick={onClose}>✕ Închide</button>
        </div>
        <div style={{ background: 'radial-gradient(ellipse at center, #06182e, #02060e)', border: '1px solid var(--border-cyan)', borderRadius: 'var(--radius-lg)', aspectRatio: '16/9', display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'center', gap: 14, color: 'var(--text-muted)', padding: 20 }}>
          <svg width="48" height="48" viewBox="0 0 24 24" fill="none"><rect x="2" y="6" width="14" height="12" rx="2" stroke="var(--cyan)" strokeWidth="1.4"/><path d="M16 10l6-3v10l-6-3" stroke="var(--cyan)" strokeWidth="1.4" strokeLinejoin="round"/></svg>
          <div style={{ fontWeight: 700, fontSize: 15, color: 'var(--text)' }}>Stream RTSP / IMOU</div>
          <div style={{ fontSize: 13, textAlign: 'center', maxWidth: 420, lineHeight: 1.6 }}>
            Browserele nu pot reda RTSP direct. Pentru stream live într-o aplicație web e necesar un proxy WebRTC (ex. MediaMTX / go2rtc) pe rețeaua locală.
          </div>
          {cam.localIp && <div className="val-mono" style={{ fontSize: 11, color: 'var(--cyan)', background: 'rgba(0,0,0,0.3)', padding: '7px 13px', borderRadius: 'var(--radius)', wordBreak: 'break-all' }}>
            rtsp://{cam.rtspUsername||'admin'}:•••@{cam.localIp}:{cam.rtspPort||554}/cam/realmonitor?channel={cam.imouChannelId||1}&subtype=0
          </div>}
        </div>
      </div>
    </div>
  );
}

Object.assign(window, { Cameras });
