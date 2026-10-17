<script>
  import { onMount, onDestroy } from 'svelte';
  import { getStatus } from '../api.js';
  import { describeBt } from '../lib/bt.js';

  let status = null;
  let error = '';
  let lastUpdate = null;
  let timer = null;

  async function poll() {
    try {
      status = await getStatus();
      error = '';
      lastUpdate = new Date();
    } catch (e) {
      error = e?.message ?? String(e);
    }
  }

  onMount(() => {
    poll();
    timer = setInterval(poll, 2000);
  });

  onDestroy(() => clearInterval(timer));

  function fmtUptime(sec) {
    const s = Number(sec) || 0;
    const d = Math.floor(s / 86400);
    const h = Math.floor((s % 86400) / 3600);
    const m = Math.floor((s % 3600) / 60);
    if (d) return `${d} д ${h} ч`;
    if (h) return `${h} ч ${m} мин`;
    return `${m} мин`;
  }

  $: bt = status ? describeBt(status.bt_status, status.bt_sub_status) : null;
  $: rssi = status?.rssi ?? null;
  $: rssiLevel = rssi === null ? 0 : rssi >= -55 ? 4 : rssi >= -65 ? 3 : rssi >= -75 ? 2 : 1;
</script>

{#if error}
  <div class="banner error">Нет связи с устройством: {error}</div>
{/if}

<div class="grid">
  <section class="card">
    <h2>Устройство</h2>
    <dl>
      <dt>Плата</dt>
      <dd>{status?.project_name ?? '—'}</dd>
      <dt>Платформа</dt>
      <dd>{status?.platform_name ?? '—'}</dd>
      <dt>Прошивка</dt>
      <dd><code>{status?.version ?? '—'}</code></dd>
      <dt>Режим</dt>
      <dd>{status?.recovery ? 'recovery' : 'обычный'}</dd>
      {#if status?.depth}
        <dt>Разрядность ядра</dt>
        <dd>{status.depth} бит</dd>
      {/if}
      {#if status?.uptime !== undefined}
        <dt>Время работы</dt>
        <dd>{fmtUptime(status.uptime)}</dd>
      {/if}
    </dl>
  </section>

  {#if status?.heap_internal !== undefined || status?.heap_external !== undefined}
    <section class="card">
      <h2>Ресурсы</h2>
      <dl>
        {#if status?.heap_internal !== undefined}
          <dt>Внутренняя память</dt>
          <dd>{Math.round(status.heap_internal / 1024)} КБ</dd>
        {/if}
        {#if status?.heap_external !== undefined}
          <dt>PSRAM</dt>
          <dd>{Math.round(status.heap_external / 1024)} КБ</dd>
        {/if}
      </dl>
    </section>
  {/if}

  <section class="card">
    <h2>Сеть</h2>
    <dl>
      <dt>Интерфейс</dt>
      <dd>{status?.if ?? '—'}</dd>
      <dt>SSID</dt>
      <dd>{status?.ssid ?? '—'}</dd>
      <dt>IP</dt>
      <dd><code>{status?.ip ?? '—'}</code></dd>
      <dt>Сигнал</dt>
      <dd>
        <span class="rssi" data-level={rssiLevel}>
          {#each [1, 2, 3, 4] as b}<i class:on={b <= rssiLevel}></i>{/each}
        </span>
        {rssi !== null ? `${rssi} dBm` : '—'}
      </dd>
    </dl>
  </section>

  <section class="card">
    <h2>Сервер</h2>
    <dl>
      <dt>Адрес</dt>
      <dd><code>{status?.lms_ip ?? '—'}</code></dd>
      <dt>HTTP / CLI</dt>
      <dd>{status?.lms_port ?? '—'} / {status?.lms_cport ?? '—'}</dd>
      <dt>Переподключений</dt>
      <dd>{status?.disconnect_count ?? 0}</dd>
    </dl>
  </section>

  <section class="card">
    <h2>Bluetooth</h2>
    {#if bt}
      <p class="state" data-tone={bt.tone}>
        <span class="pill">{bt.label}</span>
        {#if bt.media}<span class="muted">· {bt.media}</span>{/if}
      </p>
      <dl>
        <dt>Состояние A2DP</dt>
        <dd>{status.bt_status}</dd>
        <dt>Медиа</dt>
        <dd>{status.bt_sub_status}</dd>
      </dl>
    {:else}
      <p class="muted">нет данных</p>
    {/if}
  </section>
</div>

<p class="muted small">
  Обновлено: {lastUpdate ? lastUpdate.toLocaleTimeString() : '—'} · опрос каждые 2 с
</p>
