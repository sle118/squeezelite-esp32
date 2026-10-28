// Minimal client for the Squeezelite-ESP32 web API.
// Everything is same-origin: the shell page is served by the device itself.

import { getToken, authRequired } from './lib/auth.js';

const TIMEOUT = 5000;

export class ApiError extends Error {
  constructor(message, status) {
    super(message);
    this.status = status;
  }
}

async function request(path, { method = 'GET', body, timeout = TIMEOUT } = {}) {
  const ctrl = new AbortController();
  const timer = setTimeout(() => ctrl.abort(), timeout);
  const headers = {};
  if (body) headers['Content-Type'] = 'application/json';
  const token = getToken();
  if (token) headers.Authorization = `Bearer ${token}`;

  try {
    const res = await fetch(path, {
      method,
      body,
      signal: ctrl.signal,
      cache: 'no-store',
      headers,
    });
    if (res.status === 401) {
      authRequired.set(true);
      throw new ApiError('требуется ключ доступа', 401);
    }
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

export const getStatus = (timeout) => request('/status.json', { timeout });
export const getConfig = (timeout) => request('/config.json', { timeout });
export const getCommands = () => request('/commands.json');
export const getScan = () => request('/scan.json');
export const scan = () => request('/scan.json', { timeout: 20000 });
export const connect = (ssid, pwd) =>
  request('/connect.json', { method: 'POST', body: JSON.stringify({ timestamp: Date.now(), ssid, pwd }), timeout: 15000 });
export const reboot = () => request('/reboot.json', { method: 'POST' });

// Write NVS entries: { key: "value" } or { key: { type, value } } -> POST /config.json
export async function setConfig(entries) {
  const config = {};
  for (const [key, val] of Object.entries(entries)) {
    const entry = val && typeof val === 'object' ? val : { type: 33, value: val };
    // type 33 == NVS_TYPE_STR; anything else is numeric in this firmware
    config[key] = {
      type: entry.type ?? 33,
      value: (entry.type ?? 33) === 33 ? String(entry.value) : Number(entry.value),
    };
  }
  return request('/config.json', {
    method: 'POST',
    body: JSON.stringify({ config }),
    timeout: 15000,
  });
}

export const post = (path, payload) =>
  request(path, { method: 'POST', body: JSON.stringify(payload) });
