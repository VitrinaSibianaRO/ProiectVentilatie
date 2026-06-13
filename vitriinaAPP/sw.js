// VitriinApp service worker — offline app shell cache
const CACHE = 'vitriinapp-v7';
const ASSETS = [
  './',
  './index.html',
  './styles.css?v=7',
  './js/mqtt-service.js',
  './js/components.jsx',
  './js/Dashboard.jsx',
  './js/Settings.jsx',
  './js/System.jsx',
  './js/Cameras.jsx',
  './js/Tv.jsx',
  './js/App.jsx',
  './manifest.webmanifest',
  './icons/icon-192.png',
  './icons/icon-512.png',
  './icons/apple-touch-icon.png',
];

self.addEventListener('install', (e) => {
  e.waitUntil(caches.open(CACHE).then(c => c.addAll(ASSETS)).then(() => self.skipWaiting()));
});

self.addEventListener('activate', (e) => {
  e.waitUntil(
    caches.keys().then(keys => Promise.all(keys.filter(k => k !== CACHE).map(k => caches.delete(k)))).then(() => self.clients.claim())
  );
});

self.addEventListener('fetch', (e) => {
  const url = new URL(e.request.url);
  // Never cache MQTT/websocket or cross-origin CDN streams; only same-origin GET
  if (e.request.method !== 'GET' || url.origin !== self.location.origin) return;

  e.respondWith(
    caches.match(e.request).then(cached => {
      const network = fetch(e.request).then(res => {
        if (res && res.status === 200) {
          const clone = res.clone();
          caches.open(CACHE).then(c => c.put(e.request, clone));
        }
        return res;
      }).catch(() => cached);
      return cached || network;
    })
  );
});
