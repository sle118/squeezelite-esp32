<script>
  import { onMount, onDestroy } from 'svelte';
  import { getConfig, setConfig, reboot } from '../api.js';
  import { followMessages, resetMessages } from '../lib/messages.js';
  import Result from '../components/Result.svelte';

  const HINTS = {
    autoexec1: 'Командная строка плеера (её формируют вкладка «Звук» и встроенный интерфейс)',
    autoexec: 'Выполнять autoexec при старте',
    a2dp_sink_name: 'Имя Bluetooth-колонки для вывода звука',
    a2dp_spin: 'PIN Bluetooth-колонки (обычно 1234 или 0000)',
    ui_url: 'Адрес внешнего веб-интерфейса (страница /app)',
    rel_api: 'API для проверки обновлений',
    release_url: 'Страница релизов',
    stats: 'Печатать статистику буферов BT в лог (Y/N)',
    telnet_enable: 'Telnet-сервер (Y/N)',
    telnet_buffer: 'Размер буфера логов для telnet',
    set_GPIO: 'Назначение GPIO (amp, green, jack …)',
    dac_config: 'Параметры ЦАП/кодека',
    model_config: 'Выбранный пресет платы',
    host_name: 'Имя устройства в сети',
  };

  let rows = [];
  let changed = {};
  let filter = '';
  let msg = '';
  let error = '';
  let saving = false;
  let busy = false;
  let releaseMessages = null;
  let stopTimer = null;

  async function load() {
    error = '';
    changed = {};
    try {
      const cfg = await getConfig();
      rows = Object.entries(cfg?.config ?? {})
        .map(([key, item]) => ({
          key,
          type: item?.type ?? 33,
          value: item?.value ?? '',
          original: item?.value ?? '',
        }))
        // alphabetical: keys that belong together (a2dp_*, display_*, telnet_*)
        // end up next to each other regardless of the order the device reports
        .sort((a, b) => a.key.localeCompare(b.key));
    } catch (e) {
      error = e?.message ?? String(e);
    }
  }

  function edit(key, value) {
    const row = rows.find((r) => r.key === key);
    if (!row) return;
    row.value = value;
    if (String(value) === String(row.original)) delete changed[key];
    else changed[key] = { type: row.type, value };
  }

  // Watch the board's own messages while an action runs (Save Success, Rebooting, ...).
  function watchBoard(holdMs) {
    resetMessages();
    busy = true;
    if (!releaseMessages) releaseMessages = followMessages();
    if (stopTimer) clearTimeout(stopTimer);
    stopTimer = setTimeout(() => {
      busy = false;
      stopTimer = null;
      if (releaseMessages) {
        releaseMessages();
        releaseMessages = null;
      }
    }, holdMs);
  }

  async function save(reloadOrReboot = false) {
    saving = true;
    msg = '';
    error = '';
    watchBoard(reloadOrReboot ? 12000 : 4000);
    try {
      if (!Object.keys(changed).length) {
        msg = 'Изменений нет.';
        return;
      }
      await setConfig(changed);
      msg = `Сохранено ключей: ${Object.keys(changed).length}.`;
      if (reloadOrReboot) await reboot();
      else await load();
    } catch (e) {
      error = e?.message ?? String(e);
    } finally {
      saving = false;
    }
  }

  $: visible = rows.filter(
    (r) => !filter || r.key.toLowerCase().includes(filter.toLowerCase()) || String(r.value).toLowerCase().includes(filter.toLowerCase())
  );
  $: changedCount = Object.keys(changed).length;

  onMount(load);

  onDestroy(() => {
    if (stopTimer) clearTimeout(stopTimer);
    if (releaseMessages) releaseMessages();
  });
</script>

<section class="card">
  <h2>NVS</h2>
  <div class="toolbar">
    <input placeholder="Фильтр по ключу или значению" bind:value={filter} />
    <span class="muted small">{visible.length} из {rows.length} · изменено {changedCount}</span>
  </div>

  {#if error}
    <div class="banner error">{error}</div>
  {/if}
  {#if msg}
    <div class="banner ok">{msg}</div>
  {/if}

  <div class="nvs">
    {#each visible as row (row.key)}
      <div class="nvs-row" class:changed={row.value !== row.original}>
        <div class="nvs-key">
          <code>{row.key}</code>{#if row.value !== row.original}<span class="badge">изменено</span>{/if}
          {#if HINTS[row.key]}<span class="muted small hint">{HINTS[row.key]}</span>{/if}
        </div>
        <input class:dirty={row.value !== row.original} value={row.value} oninput={(e) => edit(row.key, e.currentTarget.value)} />
      </div>
    {/each}
  </div>

  <div class="actions">
    <button class="primary" onclick={() => save(false)} disabled={saving || !changedCount}>
      {saving ? 'Сохраняю…' : changedCount ? `Сохранить (${changedCount})` : 'Сохранить'}
    </button>
    <button onclick={() => save(true)} disabled={saving || !changedCount}>
      Сохранить и перезагрузить
    </button>
    <button onclick={load}>Обновить</button>
  </div>

  <Result running={busy} />
</section>
