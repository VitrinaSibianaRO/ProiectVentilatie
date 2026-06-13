// Main App — floating bottom nav (mobile-friendly) + page routing

const TABS = [
  { id: 'dashboard', label: 'Monitor', icon: (a) => (
    <svg viewBox="0 0 24 24" fill="none"><rect x="3" y="3" width="8" height="10" rx="1.5" stroke="currentColor" strokeWidth="1.6"/><rect x="3" y="16" width="8" height="5" rx="1.5" stroke="currentColor" strokeWidth="1.6"/><rect x="14" y="3" width="7" height="5" rx="1.5" stroke="currentColor" strokeWidth="1.6"/><rect x="14" y="11" width="7" height="10" rx="1.5" stroke="currentColor" strokeWidth="1.6"/></svg>
  )},
  { id: 'cameras', label: 'Camere', icon: (a) => (
    <svg viewBox="0 0 24 24" fill="none"><rect x="2" y="6" width="14" height="12" rx="2" stroke="currentColor" strokeWidth="1.6"/><path d="M16 10l6-3v10l-6-3" stroke="currentColor" strokeWidth="1.6" strokeLinejoin="round"/></svg>
  )},
  { id: 'tv', label: 'TV', icon: (a) => (
    <svg viewBox="0 0 24 24" fill="none"><rect x="2" y="7" width="20" height="13" rx="1.6" stroke="currentColor" strokeWidth="1.6"/><path d="M8 3l4 3 4-3" stroke="currentColor" strokeWidth="1.6" strokeLinecap="round" strokeLinejoin="round"/></svg>
  )},
  { id: 'settings', label: 'Setări', icon: (a) => (
    <svg viewBox="0 0 24 24" fill="none"><circle cx="12" cy="12" r="3" stroke="currentColor" strokeWidth="1.6"/><path d="M12 2v3M12 19v3M4.2 4.2l2.1 2.1M17.7 17.7l2.1 2.1M2 12h3M19 12h3M4.2 19.8l2.1-2.1M17.7 6.3l2.1-2.1" stroke="currentColor" strokeWidth="1.6" strokeLinecap="round"/></svg>
  )},
  { id: 'system', label: 'Sistem', icon: (a) => (
    <svg viewBox="0 0 24 24" fill="none"><rect x="4" y="4" width="16" height="16" rx="2" stroke="currentColor" strokeWidth="1.6"/><rect x="9" y="9" width="6" height="6" rx="1" stroke="currentColor" strokeWidth="1.6"/><path d="M9 2v2M15 2v2M9 20v2M15 20v2M2 9h2M2 15h2M20 9h2M20 15h2" stroke="currentColor" strokeWidth="1.6" strokeLinecap="round"/></svg>
  )},
];

// ==========================================
// FIREBASE CONFIGURATION
// INLOCUIESTE ACESTE DATE CU CELE DIN CONSOLA FIREBASE
// ==========================================
const firebaseConfig = {
  apiKey: "AIzaSyANhVdGbN4cs5BprmwLYFsZbD5vJkeb-I8",
  authDomain: "vitrinasibianaautomatizare.firebaseapp.com",
  projectId: "vitrinasibianaautomatizare",
  storageBucket: "vitrinasibianaautomatizare.firebasestorage.app",
  messagingSenderId: "860776455148",
  appId: "1:860776455148:web:ccbe05e159872f8ed5fec9",
  measurementId: "G-5QG4SKKRK4"
};

// Initialize Firebase only once
if (window.firebase && !window.firebase.apps.length) {
  window.firebase.initializeApp(firebaseConfig);
}

function MainApp() {
  const [tab, setTab] = React.useState(() => localStorage.getItem('vitriina_tab') || 'dashboard');
  const switchTab = (id) => { setTab(id); localStorage.setItem('vitriina_tab', id); };

  return (
    <MqttProvider>
      <div className="app-shell">
        <TopBar />
        <div className="page-content">
          {tab === 'dashboard' && <Dashboard />}
          {tab === 'cameras'   && <Cameras />}
          {tab === 'tv'        && <Tv />}
          {tab === 'settings'  && <Settings />}
          {tab === 'system'    && <SystemPage />}
        </div>
        <FloatingNav activeTab={tab} onTabChange={switchTab} />
      </div>
    </MqttProvider>
  );
}

function App() {
  const [user, setUser] = React.useState(null);
  const [authInitialized, setAuthInitialized] = React.useState(false);

  React.useEffect(() => {
    if (!window.firebase) return;
    const unsubscribe = window.firebase.auth().onAuthStateChanged((usr) => {
      setUser(usr);
      setAuthInitialized(true);
    });
    return () => unsubscribe();
  }, []);

  if (!authInitialized) {
    return (
      <div className="app-shell" style={{ display: 'flex', alignItems: 'center', justifyContent: 'center' }}>
        <Spinner />
      </div>
    );
  }

  if (!user) {
    return <Auth onLoginSuccess={() => console.log("Logged in!")} />;
  }

  return <MainApp />;
}

function TopBar() {
  const { isConnected, isOnline } = useMqtt();
  return (
    <nav className="top-nav">
      <div className="brand">
        <div className="brand-mark"></div>
        <div>
          <div className="brand-text">VITRIIN<span>APP</span></div>
        </div>
      </div>
      <div className="nav-right">
        <div className="conn-indicator" style={{ color: isConnected ? 'var(--cyan)' : 'var(--text-muted)' }}>
          <span className={`dot ${isConnected ? 'on' : 'off'}`}></span>
          <span className="tab-label">MQTT</span>
        </div>
        <div className="conn-indicator" style={{ color: isOnline ? 'var(--green)' : 'var(--text-muted)' }}>
          <PulseBadge on={isOnline} />
          <span className="tab-label">ESP32</span>
        </div>
      </div>
    </nav>
  );
}

function FloatingNav({ activeTab, onTabChange }) {
  return (
    <nav className="floating-nav">
      {TABS.map(t => (
        <button key={t.id} className={`fnav-tab ${activeTab === t.id ? 'active' : ''}`} onClick={() => onTabChange(t.id)}>
          {t.icon(activeTab === t.id)}
          <span className="fnav-label">{t.label}</span>
        </button>
      ))}
    </nav>
  );
}

const root = ReactDOM.createRoot(document.getElementById('root'));
root.render(<App />);
