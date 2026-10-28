<script>
  import { onMount } from 'svelte';
  import Status from './views/Status.svelte';
  import Nvs from './views/Nvs.svelte';
  import Audio from './views/Audio.svelte';
  import System from './views/System.svelte';
  import Firmware from './views/Firmware.svelte';
  import Placeholder from './views/Placeholder.svelte';
  import { device, startDevicePolling, describeLink } from './lib/device.js';
  import { reboot } from './api.js';
  import { authRequired, setToken } from './lib/auth.js';
  import { theme, setTheme, initTheme } from './lib/theme.js';

  const tabs = [
    { id: 'status', label: 'Статус' },
    { id: 'audio', label: 'Звук' },
    { id: 'screen', label: 'Экран' },
    { id: 'system', label: 'Система' },
    { id: 'firmware', label: 'Прошивка' },
    { id: 'nvs', label: 'NVS' },
  ];

  const fromHash = () => {
    const id = (typeof location !== 'undefined' ? location.hash : '').replace(/^#/, '');
    return tabs.some((t) => t.id === id) ? id : 'status';
  };

  let tab = fromHash();
  let rebooting = false;
  let keyInput = '';
  let confirmReboot = false;
  let menuOpen = false;
  let confirmTimer = null;

  function armReboot() {
    if (confirmReboot) {
      clearTimeout(confirmTimer);
      confirmReboot = false;
      doReboot();
      return;
    }
    confirmReboot = true;
    confirmTimer = setTimeout(() => (confirmReboot = false), 4000);
  }

  function unlock() {
    setToken(keyInput.trim());
    authRequired.set(false);
    if (typeof location !== 'undefined') location.reload();
  }

  function select(id) {
    tab = id;
    if (typeof history !== 'undefined') history.replaceState(null, '', `#${id}`);
  }

  async function doReboot() {
    rebooting = true;
    try {
      await reboot();
    } catch {
      /* the device drops the connection while rebooting */
    } finally {
      setTimeout(() => (rebooting = false), 5000);
    }
  }

  onMount(() => {
    initTheme();
    const stop = startDevicePolling(2000);
    const onHash = () => (tab = fromHash());
    window.addEventListener('hashchange', onHash);
    return () => {
      stop();
      window.removeEventListener('hashchange', onHash);
    };
  });

  $: recovery = $device.status?.recovery === 1;
  $: dot = describeLink($device);
</script>

<header class="topbar">
  <div class="brand">
    <span class="dot {dot.tone}" class:pulse={dot.pulsing} title={dot.text}></span>
    <strong>Squeezelite-ESP32</strong>
    <span class="muted" id="host">{typeof location !== 'undefined' ? location.host : ''}</span>
  </div>

  <nav class:open={menuOpen}>
    {#each tabs as t}
      <button class:active={tab === t.id} onclick={() => select(t.id)}>{t.label}</button>
    {/each}
  </nav>
  <div class="sysbar">
  <button class="burger" class:open={menuOpen} onclick={() => (menuOpen = !menuOpen)} aria-label="меню">
    <svg viewBox="0 0 24 24" width="18" height="18" aria-hidden="true">
      {#if menuOpen}
        <path d="M5 5l14 14M19 5L5 19" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" />
      {:else}
        <path d="M4 7h16M4 12h16M4 17h16" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" />
      {/if}
    </svg>
  </button>
    {#if recovery}
      <span class="badge warn">восстановление</span>
    {/if}
    <div class="theme-switch" role="group" aria-label="тема">
      <button class:active={$theme === 'light'} title="светлая" onclick={() => setTheme('light')}>
        <svg viewBox="0 0 24 24" width="15" height="15" aria-hidden="true">
          <circle cx="12" cy="12" r="4.2" fill="none" stroke="currentColor" stroke-width="1.7" />
          <path d="M12 2.5v2.4M12 19.1v2.4M2.5 12h2.4M19.1 12h2.4M5.2 5.2l1.7 1.7M17.1 17.1l1.7 1.7M18.8 5.2l-1.7 1.7M6.9 17.1l-1.7 1.7" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" />
        </svg>
      </button>
      <button class:active={$theme === 'dark'} title="тёмная" onclick={() => setTheme('dark')}>
        <svg viewBox="0 0 24 24" width="15" height="15" aria-hidden="true">
          <path d="M20 14.5A8.5 8.5 0 0 1 9.5 4a8.5 8.5 0 1 0 10.5 10.5z" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linejoin="round" />
        </svg>
      </button>
      <button class:active={$theme === 'system'} title="как в системе" onclick={() => setTheme('system')}>
        <svg viewBox="0 0 24 24" width="15" height="15" aria-hidden="true">
          <circle cx="12" cy="12" r="8.3" fill="none" stroke="currentColor" stroke-width="1.7" />
          <path d="M12 3.7v16.6a8.3 8.3 0 0 0 0-16.6z" fill="currentColor" />
        </svg>
      </button>
    </div>
    <button class:primary={recovery || confirmReboot} onclick={armReboot} disabled={rebooting}>
      {rebooting ? 'Перезагружаю…' : confirmReboot ? 'Точно перезагрузить?' : recovery ? 'Выйти из восстановления' : 'Перезагрузить'}
    </button>
  </div>
</header>


{#if $authRequired}
  <div class="modal">
    <div class="modal-card">
      <h2>Нужен ключ доступа</h2>
      <p class="muted small">
        Устройство защищено ключом. Вставьте его, чтобы продолжить. Ключ хранится в этом
        браузере и передаётся только плате.
      </p>
      <input bind:value={keyInput} placeholder="ключ доступа" onkeydown={(e) => e.key === 'Enter' && unlock()} />
      <div class="actions">
        <button class="primary" onclick={unlock} disabled={!keyInput.trim()}>Войти</button>
      </div>
    </div>
  </div>
{/if}
<main>
  {#if tab === 'status'}
    <Status />
  {:else if tab === 'audio'}
    <Audio />
  {:else if tab === 'system'}
    <System />
  {:else if tab === 'firmware'}
    <Firmware />
  {:else if tab === 'nvs'}
    <Nvs />
  {:else}
    <Placeholder name={tabs.find((t) => t.id === tab)?.label ?? ''} />
  {/if}
</main>

<footer>
  <span class="muted">Squeezelite-ESP32 web UI · ранняя версия · <a href="/">встроенный интерфейс</a></span>
</footer>
