import { writable } from 'svelte/store';

// The device accepts a single shared key (like Zigbee2MQTT's network key).
// It is kept in localStorage and sent as a bearer token on every request;
// the firmware also accepts it as a cookie or a ?token= link.
const STORAGE_KEY = 'squeezelite_web_token';

export const authRequired = writable(false);

export function getToken() {
  try {
    return localStorage.getItem(STORAGE_KEY) || '';
  } catch {
    return '';
  }
}

export function setToken(token) {
  try {
    if (token) localStorage.setItem(STORAGE_KEY, token);
    else localStorage.removeItem(STORAGE_KEY);
  } catch {
    /* private mode, ignore */
  }
}

export function generateToken() {
  const bytes = new Uint8Array(16);
  crypto.getRandomValues(bytes);
  return Array.from(bytes, (b) => b.toString(16).padStart(2, '0')).join('');
}

export function linkWithToken(token) {
  const base = typeof location !== 'undefined' ? location.origin : '';
  return `${base}/?token=${encodeURIComponent(token)}`;
}
