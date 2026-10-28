<script>
  import { onMount, onDestroy } from 'svelte';
  import { getConfig, setConfig, reboot } from '../api.js';
  import { tokenize, join, getOpt, setOpt } from '../lib/args.js';
  import { generateToken, linkWithToken, getToken } from '../lib/auth.js';
  import { copyText } from '../lib/clipboard.js';
  import { followMessages, resetMessages } from '../lib/messages.js';
  import Result from '../components/Result.svelte';

  const FIELDS = [
    { key: 'host_name', label: 'Имя хоста', hint: 'как устройство видно в сети' },
    { key: 'ap_ssid', label: 'Имя точки доступа', hint: 'сеть, которую плата создаёт без Wi-Fi' },
    { key: 'ap_pwd', label: 'Пароль точки доступа', type: 'password' },
    { key: 'dhcp_tmout', label: 'Таймаут DHCP, с', numeric: true },
    { key: 'telnet_buffer', label: 'Буфер логов, байт', numeric: true },
    { key: 'telnet_block', label: 'Блок передачи, байт', numeric: true },
    { key: 'rel_api', label: 'Список обновлений (API)' },
    { key: 'release_url', label: 'Страница релизов' },
    { key: 'ui_url', label: 'Адрес интерфейса (ui_url)' },
  ];

  const LOG_TARGETS = ['all', 'slimproto', 'stream', 'decode', 'output', 'ir', 'display', 'bt', 'wifi', 'config', 'http'];
  const LOG_LEVELS = ['info', 'debug', 'sdebug'];

  let form = {};
  let original = {};
  let autoexec1 = '';
  let logTarget = '';
  let logLevel = '';
  let showPwd = false;
  let msg = '';
  let error = '';
  let saving = false;
  let busy = false;
  let releaseMessages = null;
  let stopTimer = null;
  let copied = false;
  let copiedTimer = null;

  const norm = (v) => String(v ?? '');
  const changedKeys = () => Object.keys(form).filter((k) => norm(form[k]) !== norm(original[k]));
  const dirty = (key) => norm(form[key]) !== norm(original[key]);

  // The firmware treats telnet_enable as a mode string, not a boolean:
  // anything with Y/X means telnet, D means telnet plus a mirror on the serial port,
  // everything else (including the empty default) is off.
  const normalizeTelnet = (v) => {
    const s = norm(v).toUpperCase();
    if (s.includes('D')) return 'D';
    if (s.includes('Y') || s.includes('X')) return 'Y';
    return 'N';
  };

  $: logValue = logTarget ? `${logTarget}=${logLevel || 'info'}` : '';
  // reference `form` directly: a call to changedKeys() would hide the dependency
  // and the save button would stay disabled until something else changed
  $: formDirty = Object.keys(form).filter((k) => norm(form[k]) !== norm(original[k])).length;
  $: changedCount = formDirty + (logValue !== (getOpt(tokenize(autoexec1), '-d') || '') ? 1 : 0);

  async function load() {
    error = '';
    msg = '';
    try {
      const cfg = await getConfig();
      const c = cfg?.config ?? {};
      autoexec1 = c.autoexec1?.value ?? '';

      const level = getOpt(tokenize(autoexec1), '-d') || '';
      const [target = '', lvl = ''] = level.split('=');
      logTarget = target;
      logLevel = lvl || (target ? 'info' : '');

      form = {
        stats: c.stats?.value ?? 'n',
        telnet_enable: normalizeTelnet(c.telnet_enable?.value),
        web_auth: c.web_auth?.value ?? '',
        web_token: c.web_token?.value ?? '',
        web_allow_ip: c.web_allow_ip?.value ?? '',
      };
      for (const f of FIELDS) form[f.key] = c[f.key]?.value ?? '';
      original = { ...form };
    } catch (e) {
      error = e?.message ?? String(e);
    }
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
      const entries = {};
      for (const key of changedKeys()) entries[key] = form[key];

      if (logValue !== (getOpt(tokenize(autoexec1), '-d') || '')) {
        entries.autoexec1 = join(setOpt(tokenize(autoexec1), '-d', logValue));
      }

      if (!Object.keys(entries).length) {
        msg = 'Изменений нет.';
        return;
      }

      await setConfig(entries);
      msg = `Сохранено ключей: ${Object.keys(entries).length}.`;
      if (reloadOrReboot) await reboot();
      else await load();
    } catch (e) {
      error = e?.message ?? String(e);
    } finally {
      saving = false;
    }
  }

  // Copy the "open with key" link. navigator.clipboard is unavailable on the plain
  // HTTP page served by the device, so the helper falls back to execCommand and
  // reports whether it worked; show that as a short check mark on the button.
  async function copyLink() {
    const ok = await copyText(linkWithToken(form.web_token));
    if (copiedTimer) clearTimeout(copiedTimer);
    copied = ok;
    copiedTimer = setTimeout(() => {
      copied = false;
      copiedTimer = null;
    }, 1600);
    if (!ok) error = 'Браузер не дал скопировать — выделите ссылку вручную.';
  }

  const toggle = (key) => {
    form[key] = norm(form[key]).toLowerCase().startsWith('y') ? 'N' : 'Y';
  };
  const isOn = (key) => norm(form[key]).toLowerCase().startsWith('y');

  onMount(load);

  onDestroy(() => {
    if (stopTimer) clearTimeout(stopTimer);
    if (copiedTimer) clearTimeout(copiedTimer);
    if (releaseMessages) releaseMessages();
  });
</script>

<section class="card">
  <h2>Система</h2>
  <p class="muted small">
    Настройки самой платы: имя в сети, точка доступа, логирование, доступ, каналы обновлений.
  </p>

  {#if error}
    <div class="banner error">{error}</div>
  {/if}
  {#if msg}
    <div class="banner ok">{msg}</div>
  {/if}

  <h3>Идентификация в сети</h3>
  <div class="form">
    {#each FIELDS.slice(0, 4) as f (f.key)}
      <label>
        {f.label}
        {#if f.key === 'ap_pwd'}
          <span class="with-eye">
            <input type={showPwd ? 'text' : 'password'} class:dirty={dirty(f.key)} bind:value={form[f.key]} />
            <button class="eye" type="button" onclick={() => (showPwd = !showPwd)} title={showPwd ? 'скрыть' : 'показать'}>
              <svg viewBox="0 0 24 24" width="16" height="16" aria-hidden="true">
                <path d="M2 12s3.6-6 10-6 10 6 10 6-3.6 6-10 6-10-6-10-6z" fill="none" stroke="currentColor" stroke-width="1.7" />
                <circle cx="12" cy="12" r="3" fill="none" stroke="currentColor" stroke-width="1.7" />
                {#if !showPwd}<line x1="4" y1="20" x2="20" y2="4" stroke="currentColor" stroke-width="1.7" />{/if}
              </svg>
            </button>
          </span>
        {:else}
          <input type={f.numeric ? 'number' : 'text'} class:dirty={dirty(f.key)} bind:value={form[f.key]} />
        {/if}
        {#if f.hint}<span class="muted small hint">{f.hint}</span>{/if}
      </label>
    {/each}
  </div>

  <h3>Логирование</h3>
  <div class="form">
    <label>
      Категория <span class="muted">(-d до «=»)</span>
      <select bind:value={logTarget}>
        <option value="">— выключено —</option>
        {#each LOG_TARGETS as t}<option value={t}>{t}</option>{/each}
      </select>
    </label>

    <label>
      Уровень <span class="muted">(после «=»)</span>
      <select bind:value={logLevel} disabled={!logTarget}>
        {#each LOG_LEVELS as l}<option value={l}>{l}</option>{/each}
      </select>
    </label>

    <label>
      Буфер логов, байт
      <input type="number" class:dirty={dirty('telnet_buffer')} bind:value={form.telnet_buffer} />
    </label>

    <label>
      Блок передачи, байт
      <input type="number" class:dirty={dirty('telnet_block')} bind:value={form.telnet_block} />
    </label>
  </div>

  <p class="muted small">
    Итог: <code>{logValue || 'логи выключены'}</code> — пишется в строку запуска, применяется после перезагрузки.
  </p>

  <div class="form">
    <label>
      Консоль <span class="muted">(telnet_enable)</span>
      <select class:dirty={dirty('telnet_enable')} bind:value={form.telnet_enable}>
        <option value="N">выключено</option>
        <option value="Y">telnet</option>
        <option value="D">telnet и серийный порт</option>
      </select>
      <span class="muted small hint">
        Одно подключение за раз. «И серийный порт» дублирует вывод логов на UART.
        Применяется после перезагрузки.
      </span>
    </label>
  </div>

  <label class="check">
    <input type="checkbox" checked={isOn('stats')} onchange={() => toggle('stats')} />
    Статистика буферов Bluetooth в лог
  </label>

  <h3>Обновления</h3>
  <div class="form stacked">
    {#each FIELDS.slice(6) as f (f.key)}
      <label>
        {f.label}
        <input class:dirty={dirty(f.key)} bind:value={form[f.key]} />
      </label>
    {/each}
  </div>
  <p class="muted small">Список релизов и установка — на вкладке <a href="#firmware">Прошивка</a>.</p>

  <h3>Доступ</h3>
  <div class="form">
    <label>
      Ключ доступа <span class="muted">(web_token)</span>
      <span class="with-actions">
        <input class:dirty={dirty('web_token')} bind:value={form.web_token} placeholder="пусто = без защиты" />
        <button class="icon-btn" type="button" title="сгенерировать новый ключ" onclick={() => (form.web_token = generateToken())}>
          <svg viewBox="0 0 24 24" width="15" height="15" aria-hidden="true">
            <path d="M20 12a8 8 0 1 1-2.3-5.6" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" />
            <path d="M20 4v4h-4" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round" />
          </svg>
        </button>
        <button
          class="icon-btn"
          type="button"
          title={copied ? 'скопировано' : 'скопировать ссылку с ключом'}
          disabled={!form.web_token}
          onclick={copyLink}
        >
          {#if copied}
            <svg viewBox="0 0 24 24" width="15" height="15" aria-hidden="true">
              <path d="M5 13l4 4L19 7" fill="none" stroke="currentColor" stroke-width="1.9" stroke-linecap="round" stroke-linejoin="round" />
            </svg>
          {:else}
            <svg viewBox="0 0 24 24" width="15" height="15" aria-hidden="true">
              <rect x="9" y="9" width="11" height="12" rx="2" fill="none" stroke="currentColor" stroke-width="1.7" />
              <path d="M15 6.5V5a2 2 0 0 0-2-2H6a2 2 0 0 0-2 2v9a2 2 0 0 0 2 2h1.5" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" />
            </svg>
          {/if}
        </button>
      </span>
    </label>
    <label>
      Режим <span class="muted">(web_auth)</span>
      <select class:dirty={dirty('web_auth')} bind:value={form.web_auth}>
        <option value="">выключено</option>
        <option value="write">только изменения (POST)</option>
        <option value="all">всё, включая чтение</option>
      </select>
    </label>
    <label>
      Без ключа пускать с IP <span class="muted">(web_allow_ip)</span>
      <input class:dirty={dirty('web_allow_ip')} bind:value={form.web_allow_ip} placeholder="192.168.1.*, 10.0.0.5" />
      <span class="muted small hint">через запятую; звёздочка в конце — любой адрес подсети</span>
    </label>
  </div>
  {#if getToken()}
    <p class="muted small">Ключ в этом браузере: <code>{getToken().slice(0, 6)}…</code></p>
  {/if}
  <p class="muted small">
    Ключ защищает только веб-интерфейс; консоль (UART/telnet) остаётся доступной — это аварийный вход.
  </p>

  <h3>Обслуживание</h3>
  <div class="actions">
    <a class="button" href="/config.json" download="squeezelite-config.json">Скачать конфиг</a>
    <a class="button" href="#nvs">Открыть NVS</a>
    <button onclick={load}>Обновить</button>
  </div>

  <div class="actions">
    <button class="primary" onclick={() => save(false)} disabled={saving || !changedCount}>
      {saving ? 'Сохраняю…' : changedCount ? `Сохранить (${changedCount})` : 'Сохранить'}
    </button>
    <button onclick={() => save(true)} disabled={saving || !changedCount}>
      Сохранить и перезагрузить
    </button>
  </div>

  <Result running={busy} />
</section>
