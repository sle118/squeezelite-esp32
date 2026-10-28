// Single poller for /messages.json.
//
// The firmware hands out its message queue through /messages.json and *drains* it on
// every read, so two independent readers would steal messages from each other. Every
// consumer subscribes here instead, and polling runs only while somebody needs it.

import { writable } from 'svelte/store';

const LIMIT = 40;

export const messages = writable([]);

let timer = null;
let subscribers = 0;
let inFlight = false;

async function poll() {
  if (inFlight) return;
  inFlight = true;
  try {
    const res = await fetch('/messages.json', { cache: 'no-store' });
    if (!res.ok) return;
    const items = await res.json();
    if (!Array.isArray(items) || !items.length) return;
    messages.update((list) => {
      const next = list.slice();
      for (const item of items) {
        next.push({
          type: String(item?.type ?? ''),
          cls: String(item?.class ?? ''),
          text: String(item?.message ?? ''),
        });
      }
      return next.slice(-LIMIT);
    });
  } catch {
    // the board reboots during apply/update, so failures here are expected
  } finally {
    inFlight = false;
  }
}

// Returns a function that stops following. Polling stops when the last one leaves.
export function followMessages(intervalMs = 600) {
  subscribers += 1;
  if (!timer) {
    poll();
    timer = setInterval(poll, intervalMs);
  }
  let active = true;
  return () => {
    if (!active) return;
    active = false;
    subscribers = Math.max(0, subscribers - 1);
    if (subscribers === 0 && timer) {
      clearInterval(timer);
      timer = null;
    }
  };
}

export function resetMessages() {
  messages.set([]);
}

// Some messages carry JSON in their text: the OTA progress is
// {"ota_dsc":"...","ota_pct":N}. Turn anything like that into readable text.
export function describeMessage(item) {
  const raw = String(item?.text ?? '');
  try {
    const parsed = JSON.parse(raw);
    if (parsed && (parsed.ota_dsc !== undefined || parsed.ota_pct !== undefined)) {
      const pct = parsed.ota_pct !== undefined ? ` · ${parsed.ota_pct}%` : '';
      return `${parsed.ota_dsc ?? 'обновление'}${pct}`;
    }
  } catch {
    // plain text message
  }
  return raw;
}

export function toneOf(item) {
  const type = String(item?.type ?? '').toLowerCase();
  if (type.includes('error')) return 'err';
  if (type.includes('warning')) return 'warn';
  return 'info';
}
