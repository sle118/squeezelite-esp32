import { writable, get } from 'svelte/store';

const KEY = 'squeezelite_theme';

function stored() {
  try {
    return localStorage.getItem(KEY) || 'system';
  } catch {
    return 'system';
  }
}

export const theme = writable(stored());

function prefersDark() {
  return typeof matchMedia !== 'undefined' && matchMedia('(prefers-color-scheme: dark)').matches;
}

export function applyTheme(value) {
  const dark = value === 'dark' || (value === 'system' && prefersDark());
  if (typeof document !== 'undefined') document.documentElement.dataset.theme = dark ? 'dark' : 'light';
}

export function setTheme(value) {
  try {
    localStorage.setItem(KEY, value);
  } catch {
    /* ignore */
  }
  theme.set(value);
  applyTheme(value);
}

export function initTheme() {
  // ?theme=light|dark forces a theme without touching the stored preference
  try {
    const forced = new URLSearchParams(location.search).get('theme');
    if (forced === 'light' || forced === 'dark') {
      applyTheme(forced);
      return;
    }
  } catch {
    /* ignore */
  }

  applyTheme(get(theme));
  if (typeof matchMedia === 'undefined') return;
  const media = matchMedia('(prefers-color-scheme: dark)');
  const onChange = () => {
    if (get(theme) === 'system') applyTheme('system');
  };
  media.addEventListener ? media.addEventListener('change', onChange) : media.addListener(onChange);
}
