// Minimal client for the Squeezelite-ESP32 web API.
// Everything is same-origin: the shell page is served by the device itself.

const TIMEOUT = 5000;

export class ApiError extends Error {}

async function request(path, { method = 'GET', body, timeout = TIMEOUT } = {}) {
  const ctrl = new AbortController();
  const timer = setTimeout(() => ctrl.abort(), timeout);
  try {
    const res = await fetch(path, {
      method,
      body,
      signal: ctrl.signal,
      cache: 'no-store',
      headers: body ? { 'Content-Type': 'application/json' } : undefined,
    });
    if (!res.ok) throw new ApiError(`${path}: HTTP ${res.status}`);
    const text = await res.text();
    if (!text) return null;
    try {
      return JSON.parse(text);
    } catch {
      return text;
    }
  } finally {
    clearTimeout(timer);
  }
}

export const getStatus = () => request('/status.json');
export const getConfig = () => request('/config.json');
export const getCommands = () => request('/commands.json');
export const getScan = () => request('/scan.json');
export const reboot = () => request('/reboot.json', { method: 'POST' });

export const post = (path, payload) =>
  request(path, { method: 'POST', body: JSON.stringify(payload) });
