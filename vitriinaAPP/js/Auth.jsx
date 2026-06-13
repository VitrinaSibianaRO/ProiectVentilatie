// Firebase Authentication Component

const { useState } = React;

function Auth({ onLoginSuccess }) {
  const [email, setEmail] = useState('');
  const [password, setPassword] = useState('');
  const [error, setError] = useState('');
  const [loading, setLoading] = useState(false);

  const handleLogin = async (e) => {
    e.preventDefault();
    setError('');
    setLoading(true);

    try {
      const auth = window.firebase.auth();
      await auth.signInWithEmailAndPassword(email, password);
      if (onLoginSuccess) {
        onLoginSuccess();
      }
    } catch (err) {
      console.error(err);
      if (err.code === 'auth/user-not-found' || err.code === 'auth/wrong-password' || err.code === 'auth/invalid-credential') {
        setError('Email sau parolă incorectă.');
      } else {
        setError('Eroare la autentificare: ' + err.message);
      }
    } finally {
      setLoading(false);
    }
  };

  return (
    <div className="app-shell" style={{ display: 'flex', alignItems: 'center', justifyContent: 'center', padding: 20 }}>
      <div className="scanlines"></div>
      
      <div className="card card-corners" style={{ maxWidth: 400, width: '100%', padding: '40px 30px', position: 'relative', zIndex: 10 }}>
        
        <div style={{ textAlign: 'center', marginBottom: 40 }}>
          <div className="brand-mark" style={{ margin: '0 auto 15px', width: 40, height: 40 }}></div>
          <div className="brand-text" style={{ fontSize: 32 }}>VITRIIN<span>APP</span></div>
          <div style={{ color: 'var(--text-muted)', marginTop: 8, fontSize: 14, letterSpacing: '0.1em', textTransform: 'uppercase' }}>Sistem Securizat</div>
        </div>

        <form onSubmit={handleLogin} style={{ display: 'flex', flexDirection: 'column', gap: 20 }}>
          <div className="form-group">
            <label className="form-label">Email Operare</label>
            <input 
              type="email" 
              className="form-input mono-input" 
              placeholder="admin@vitriina.ro"
              value={email}
              onChange={(e) => setEmail(e.target.value)}
              required
            />
          </div>

          <div className="form-group">
            <label className="form-label">Cod Acces</label>
            <input 
              type="password" 
              className="form-input mono-input" 
              placeholder="••••••••"
              value={password}
              onChange={(e) => setPassword(e.target.value)}
              required
            />
          </div>

          {error && (
            <div className="alert alert-red" style={{ fontSize: 13, marginTop: 10 }}>
              ⚠ {error}
            </div>
          )}

          <button 
            type="submit" 
            className="btn btn-solid" 
            style={{ marginTop: 10, padding: '14px', fontSize: 16 }}
            disabled={loading}
          >
            {loading ? <span className="spinner" style={{ width: 20, height: 20, borderWidth: 2, borderColor: '#000', borderTopColor: 'transparent', margin: '0 auto' }}></span> : 'Inițializare Conexiune'}
          </button>
        </form>
      </div>
    </div>
  );
}

Object.assign(window, { Auth });
