// System — collapsible sections (closed by default): status, heap, commands, about

function SystemPage() {
  const { isConnected, isOnline, ventState, sendCommand } = useMqtt();
  const [confirm, setConfirm] = React.useState(null);
  const [toast, setToast] = React.useState(null);
  const s = ventState;

  const formatUptime = (sec) => {
    if (sec == null) return '—';
    if (sec < 60) return `${sec}s`;
    if (sec < 3600) return `${Math.floor(sec/60)}m ${sec%60}s`;
    if (sec < 86400) return `${Math.floor(sec/3600)}h ${Math.floor((sec%3600)/60)}m`;
    return `${Math.floor(sec/86400)}d ${Math.floor((sec%86400)/3600)}h`;
  };

  const heapKb = s ? Math.round((s.heap||0)/1024) : 0;
  const heapPct = s ? Math.min(1, (s.heap||0)/200000) : 0;
  const heapCrit = s && s.heap < 30000;
  const slaveObj = s?.slave;

  const doConfirm = async () => {
    if (!confirm) return;
    await sendCommand(confirm.cmd);
    setToast({ msg: confirm.toast, color: 'var(--green)' });
    setTimeout(() => setToast(null), 2600);
    setConfirm(null);
  };

  return (
    <div className="page-wrap" style={{ maxWidth: 720 }}>
      <div className="page-eyebrow">Stare & Mentenanță</div>
      <div className="page-title">Sistem</div>

      {toast && <div className="alert alert-green mb-4">{toast.msg}</div>}

      <div style={{ display: 'flex', flexDirection: 'column', gap: 14 }}>

        {/* Status conexiune */}
        <Collapsible title="Status Conexiune" color="cyan" defaultOpen={false}
          badge={{ text: isOnline ? 'ONLINE' : 'OFFLINE', color: isOnline ? 'green' : 'red' }}>
          <div className="grid-2" style={{ gap: 18 }}>
            <div>
              <div className="flex items-center justify-between mb-3">
                <span className="label-tiny">Broker MQTT</span>
                <span className={`badge ${isConnected ? 'green' : 'red'}`}><PulseBadge on={isConnected} />{isConnected ? 'Conectat' : 'Deconectat'}</span>
              </div>
              <div style={{ display: 'flex', flexDirection: 'column', gap: 9 }}>
                <Info label="Host" value="hivemq.cloud" mono />
                <Info label="Transport" value="WSS · TLS" />
                <Info label="Port" value="8884" mono />
              </div>
            </div>
            <div>
              <div className="flex items-center justify-between mb-3">
                <span className="label-tiny">ESP32 Master</span>
                <span className={`badge ${isOnline ? 'green' : 'red'}`}><PulseBadge on={isOnline} />{isOnline ? 'Online' : 'Offline'}</span>
              </div>
              <div style={{ display: 'flex', flexDirection: 'column', gap: 9 }}>
                <Info label="Firmware" value={s ? `Build #${s.fw}` : '—'} mono />
                <Info label="Uptime" value={formatUptime(s?.uptimeSec)} mono />
                <Info label="Erori S / D" value={s ? `${s.left?.errs||0} / ${s.right?.errs||0}` : '—'} mono accent={s && ((s.left?.errs||0)+(s.right?.errs||0))>0} />
                <Info label="Slave LED" value={slaveObj ? (slaveObj.online ? 'online' : 'offline') : '—'} accent={slaveObj && !slaveObj.online} />
              </div>
            </div>
          </div>
        </Collapsible>

        {/* Heap */}
        <Collapsible title="Memorie Heap ESP32" color="blue" defaultOpen={false}
          badge={s ? { text: `${heapKb} KB`, color: heapCrit ? 'red' : 'green' } : null}>
          {s ? (
            <div>
              <div className="progress-bar"><div className={`progress-fill ${heapCrit ? 'critical' : ''}`} style={{ width: `${heapPct*100}%` }}></div></div>
              {heapCrit && <div className="alert alert-red mt-3" style={{ fontSize: 12 }}>⚠ Heap critic — risc de instabilitate.</div>}
            </div>
          ) : <div className="label-tiny">Fără date — așteaptă state de la ESP32.</div>}
        </Collapsible>

        {/* Commands */}
        <Collapsible title="Comenzi Sistem" color="orange" defaultOpen={false}>
          <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 10 }}>
            <button className="btn btn-cyan" onClick={() => { sendCommand({ cmd:'refresh' }); setToast({ msg:'✓ Cerere refresh trimisă', color:'var(--green)' }); setTimeout(()=>setToast(null),2200); }} disabled={!isConnected}>
              ⟳ Refresh Stare
            </button>
            <button className="btn btn-orange" disabled={!isConnected}
              onClick={() => setConfirm({ label:'Restart Master', msg:'ESP32 Master va reporni — sistemul e indisponibil ~5s.', cmd:{cmd:'reboot'}, toast:'✓ Comandă reboot trimisă' })}>
              ↺ Reboot Master
            </button>
            <button className="btn btn-orange" disabled={!isConnected}
              onClick={() => setConfirm({ label:'Restart Slave', msg:'Unitatea Slave (control LED) va reporni.', cmd:{cmd:'rebootSlave'}, toast:'✓ Reboot Slave trimis' })}>
              ↺ Reboot Slave
            </button>
            <button className="btn btn-red" disabled={!isConnected}
              onClick={() => setConfirm({ label:'Reset Defaults', msg:'Pragurile revin la valorile implicite (T≥45°C, H≥60%, 300s).', cmd:{cmd:'reset'}, toast:'✓ Reset trimis' })}>
              ⟳ Reset Defaults
            </button>
          </div>
        </Collapsible>

        {/* About */}
        <Collapsible title="Despre Aplicație" color="cyan" defaultOpen={false}>
          <div style={{ display: 'flex', flexDirection: 'column', gap: 9 }}>
            <Info label="Aplicație" value="VitriinApp Web" />
            <Info label="Versiune" value="2.0.0" mono />
            <Info label="Protocol" value="MQTT 5 over WebSocket" />
          </div>
        </Collapsible>
      </div>

      <ConfirmDialog open={!!confirm} title={confirm?.label || ''} message={confirm?.msg || ''}
        confirmLabel="Confirmă" confirmClass="btn-red" onConfirm={doConfirm} onCancel={() => setConfirm(null)} />
    </div>
  );
}

function Info({ label, value, mono, accent }) {
  return (
    <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', fontSize: 13 }}>
      <span style={{ color: 'var(--text-muted)' }}>{label}</span>
      <span style={{ fontFamily: mono ? 'var(--font-mono)' : 'inherit', color: accent ? 'var(--red)' : 'var(--text)', fontWeight: 500 }}>{value}</span>
    </div>
  );
}

Object.assign(window, { SystemPage });
