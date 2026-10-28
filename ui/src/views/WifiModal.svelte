<script>
  import { onMount } from 'svelte';
  import { scan, connect } from '../api.js';
  import { device } from '../lib/device.js';

  export let onclose = () => {};

  let networks = [];
  let loading = false;
  let error = '';
  let selected = null;
  let password = '';
  let manualSsid = '';
  let manualPwd = '';
  let connecting = '';
  let message = '';

  const authName = (auth) => (auth === 0 ? 'открытая' : auth >= 4 ? 'WPA2/WPA3' : 'WPA');

  async function load() {
    loading = true;
    error = '';
    try {
      const list = await scan();
      networks = (Array.isArray(list) ? list : []).filter((n) => n?.ssid);
      networks.sort((a, b) => (b.rssi ?? -999) - (a.rssi ?? -999));
    } catch (e) {
      error = e?.message ?? String(e);
    } finally {
      loading = false;
    }
  }

  async function doConnect(ssid, pwd) {
    if (!ssid) return;
    connecting = ssid;
    message = '';
    error = '';
    try {
      await connect(ssid, pwd ?? '');
      message = `Подключаюсь к «${ssid}»… Плата перезагрузит сеть, страница может отвалиться на несколько секунд.`;
    } catch (e) {
      error = e?.message ?? String(e);
    } finally {
      connecting = '';
    }
  }

  function pick(net) {
    selected = net;
    password = '';
    if (!net.auth) doConnect(net.ssid, '');
  }

  const rssiBars = (rssi) => (rssi >= -55 ? 4 : rssi >= -65 ? 3 : rssi >= -75 ? 2 : 1);

  onMount(load);
</script>

<div class="modal" role="dialog" aria-modal="true">
  <div class="modal-card wide">
    <h2>Настройка Wi-Fi</h2>

    {#if $device.status?.ssid}
      <p class="muted small">
        Сейчас подключено к <strong>{$device.status.ssid}</strong>
        ({$device.status.ip ? `IP ${$device.status.ip}` : 'без адреса'}).
      </p>
    {/if}

    {#if error}
      <div class="banner error">{error}</div>
    {/if}
    {#if message}
      <div class="banner ok">{message}</div>
    {/if}

    <div class="actions">
      <button onclick={load} disabled={loading}>{loading ? 'Сканирую…' : 'Обновить список'}</button>
    </div>

    <div class="wifi-list">
      {#each networks as net (net.ssid)}
        <div class="wifi-row" class:active={$device.status?.ssid === net.ssid}>
          <span class="wifi-bars" data-level={rssiBars(net.rssi)}>
            {#each [1, 2, 3, 4] as b}<i class:on={b <= rssiBars(net.rssi)}></i>{/each}
          </span>
          <span class="wifi-name">{net.ssid}</span>
          <span class="muted small">
            {$device.status?.ssid === net.ssid ? 'подключено' : `${net.rssi ?? ''} dBm · ${authName(net.auth)}`}
          </span>
          <button onclick={() => pick(net)} disabled={connecting === net.ssid || $device.status?.ssid === net.ssid}>
            {connecting === net.ssid ? '…' : 'Выбрать'}
          </button>
        </div>
        {#if selected === net}
          <div class="wifi-row password-row">
            <input bind:value={password} type="password" placeholder="пароль сети" />
            <button class="primary" onclick={() => doConnect(net.ssid, password)}>Подключиться</button>
          </div>
        {/if}
      {/each}
      {#if !networks.length && !loading}
        <p class="muted small">Сети не найдены — попробуйте обновить список.</p>
      {/if}
    </div>

    <h3>Вручную</h3>
    <div class="form">
      <label>
        Имя сети (SSID)
        <input bind:value={manualSsid} placeholder="MyWiFi" />
      </label>
      <label>
        Пароль
        <input bind:value={manualPwd} type="password" placeholder="пароль" />
      </label>
    </div>
    <div class="actions">
      <button class="primary" onclick={() => doConnect(manualSsid, manualPwd)} disabled={!manualSsid}>
        Подключиться
      </button>
      <button onclick={onclose}>Закрыть</button>
    </div>
  </div>
</div>
