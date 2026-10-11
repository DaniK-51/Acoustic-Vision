import { app, BrowserWindow, session } from 'electron';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));

const isDev = process.argv.includes('--dev') || !!process.env.VITE_DEV_SERVER_URL;
const devUrl = process.env.VITE_DEV_SERVER_URL ?? 'http://127.0.0.1:5173';

// av-api sends no CORS headers (desktop client contract, not a browser page).
// Inject them here so the renderer can fetch /v1 with webSecurity left ON;
// the API client avoids preflight by sending JSON bodies as text/plain.
function installCorsHeaders() {
  session.defaultSession.webRequest.onHeadersReceived((details, callback) => {
    callback({
      responseHeaders: {
        ...details.responseHeaders,
        'Access-Control-Allow-Origin': ['*'],
        'Access-Control-Allow-Methods': ['GET, POST, PATCH, DELETE, OPTIONS'],
        'Access-Control-Allow-Headers': ['*'],
      },
    });
  });
}

function createWindow() {
  const win = new BrowserWindow({
    width: 1440,
    height: 900,
    title: 'Acoustic-Vision',
    webPreferences: {
      contextIsolation: true,
      nodeIntegration: false,
      sandbox: true,
    },
  });
  if (isDev) {
    win.loadURL(devUrl);
  } else {
    win.loadFile(path.join(__dirname, '..', 'dist', 'index.html'));
  }
}

app.whenReady().then(() => {
  installCorsHeaders();
  createWindow();
  app.on('activate', () => {
    if (BrowserWindow.getAllWindows().length === 0) {
      createWindow();
    }
  });
});

app.on('window-all-closed', () => {
  if (process.platform !== 'darwin') {
    app.quit();
  }
});
