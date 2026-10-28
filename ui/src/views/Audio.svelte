<script>
  import { onMount, onDestroy } from 'svelte';
  import { getConfig, setConfig, reboot } from '../api.js';
  import { tokenize, join, getOpt, setOpt, hasFlag, setFlag } from '../lib/args.js';
  import { followMessages, resetMessages } from '../lib/messages.js';
  import Result from '../components/Result.svelte';

  const OUTPUTS = [
    { id: 'bt', label: 'Bluetooth' },
    { id: 'I2S', label: 'I2S / ЦАП' },
    { id: 'SPDIF', label: 'SPDIF' },
  ];

  const DAC_PRESETS = [
    { label: 'ESP32-A1S v2.2 (ES8388)', value: 'model=ES8388,bck=27,ws=25,do=26,sda=33,scl=32,i2c=16' },
    { label: 'ES8388 (bck=5, sda=18, scl=23)', value: 'model=ES8388,bck=5,ws=25,do=26,sda=18,scl=23,i2c=16' },
    { label: 'AC101 (LyraT, ранние A1S)', value: 'model=AC101,bck=27,ws=26,do=25,di=35,sda=33,scl=32' },
    { label: 'TAS57xx', value: 'model=TAS57xx,bck=33,ws=25,do=32,sda=27,scl=26,mute=14:0' },
    { label: 'Только I2S, без управления кодеком', value: 'model=I2S,bck=27,ws=25,do=26' },
  ];

  let autoexec1 = '';
  let form = {};
  let original = {};
  let preset = '';
  let originalOutput = '';
  let showAll = false;
  let msg = '';
  let error = '';
  let saving = false;
  let busy = false;
  let releaseMessages = null;
  let stopTimer = null;

  const norm = (v) => String(v ?? '');
  const changedKeys = () => Object.keys(form).filter((k) => !k.startsWith('__') && norm(form[k]) !== norm(original[k]));
  const dirty = (key) => norm(form[key]) !== norm(original[key]);

  $: tokens = tokenize(autoexec1);
  $: output = getOpt(tokens, '-o') || 'bt';
  $: resample = hasFlag(tokens, '-R');
  $: sinkEnabled = norm(form.enable_bt_sink).toLowerCase().startsWith('y');
  $: showBt = output.toLowerCase() === 'bt' || showAll;
  $: showDac = output.toLowerCase() === 'i2s' || showAll;
  $: showSpdif = output.toLowerCase() === 'spdif' || showAll;
  $: command = buildCommand(autoexec1, form.buffers, output, resample, form.player_name, form.close_timeout, form.server);
  $: metaDirty = output !== originalOutput;
  // the dependency on `form` must be visible here, otherwise Svelte never
  // re-runs this when a field changes (a function call hides the dependency)
  $: formDirty = Object.keys(form).filter(
    (k) => !k.startsWith('__') && norm(form[k]) !== norm(original[k])
  ).length;
  $: changedCount = formDirty + (metaDirty ? 1 : 0);

  function clockOf(value) {
    const out = { clock: '', ws: '', data: '' };
    for (const part of String(value ?? '').split(',')) {
      const [key, val] = part.split('=');
      if (key === 'clock') out.clock = val ?? '';
      if (key === 'wordselect') out.ws = val ?? '';
      if (key === 'data') out.data = val ?? '';
    }
    return out;
  }

  async function load() {
    error = '';
    msg = '';
    try {
      const cfg = await getConfig();
      const c = cfg?.config ?? {};
      autoexec1 = c.autoexec1?.value ?? '';
      const spdif = clockOf(c.spdif_config?.value);

      form = {
        buffers: getOpt(tokenize(autoexec1), '-b'),
        player_name: getOpt(tokenize(autoexec1), '-n'),
        server: getOpt(tokenize(autoexec1), '-s'),
        close_timeout: getOpt(tokenize(autoexec1), '-C'),
        dac_config: c.dac_config?.value ?? '',
        a2dp_sink_name: c.a2dp_sink_name?.value ?? '',
        a2dp_spin: c.a2dp_spin?.value ?? '',
        a2dp_dev_name: c.a2dp_dev_name?.value ?? '',
        bt_idle_off: c.bt_idle_off?.value ?? '0',
        enable_bt_sink: c.enable_bt_sink?.value ?? 'n',
        bt_sink_pin: c.bt_sink_pin?.value ?? '',
        bt_sink_volume: c.bt_sink_volume?.value ?? '',
        spdif_clock: spdif.clock,
        spdif_ws: spdif.ws,
        spdif_data: spdif.data,
      };
      form.__spdif = c.spdif_config?.value ?? '';
      original = { ...form };
      originalOutput = getOpt(tokenize(autoexec1), '-o') || 'bt';
      preset = DAC_PRESETS.find((p) => p.value === form.dac_config)?.value ?? '';
    } catch (e) {
      error = e?.message ?? String(e);
    }
  }

  // parameters are explicit so Svelte re-runs this whenever any of them changes
  function buildCommand(line, buffers, out, resampleOn, playerName, closeTimeout, server) {
    let t = tokenize(line);
    t = setOpt(t, '-o', out);
    t = setOpt(t, '-s', norm(server).trim());
    t = setOpt(t, '-n', norm(playerName).trim());
    t = setOpt(t, '-b', norm(buffers).trim());
    t = setOpt(t, '-C', norm(closeTimeout).trim());
    t = setFlag(t, '-R', resampleOn);
    return join(t);
  }

  function toggle(key) {
    form[key] = norm(form[key]).toLowerCase().startsWith('y') ? 'N' : 'Y';
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
      // player_name / close_timeout live inside autoexec1, not as NVS keys
      const pseudo = new Set(['player_name', 'close_timeout', 'server']);
      for (const key of changedKeys()) {
        if (!pseudo.has(key)) entries[key] = form[key];
      }

      const cmd = buildCommand(autoexec1, form.buffers, output, resample, form.player_name, form.close_timeout, form.server);
      if (cmd !== autoexec1) entries.autoexec1 = cmd;

      const spdif = [];
      if (form.spdif_clock) spdif.push(`clock=${form.spdif_clock}`);
      if (form.spdif_ws) spdif.push(`wordselect=${form.spdif_ws}`);
      if (form.spdif_data) spdif.push(`data=${form.spdif_data}`);
      const spdifValue = spdif.join(',');
      if (spdifValue !== norm(form.__spdif)) entries.spdif_config = spdifValue;

      if (!Object.keys(entries).length) {
        msg = 'Изменений нет.';
        return;
      }

      await setConfig(entries);
      msg = 'Сохранено.';
      if (reloadOrReboot) await reboot();
      else await load();
    } catch (e) {
      error = e?.message ?? String(e);
    } finally {
      saving = false;
    }
  }

  onMount(load);

  onDestroy(() => {
    if (stopTimer) clearTimeout(stopTimer);
    if (releaseMessages) releaseMessages();
  });
</script>

<section class="card">
  <h2>Звук</h2>
  <p class="muted small">
    Меняется только соответствующая опция в строке запуска — остальные настройки остаются нетронутыми.
  </p>

  {#if error}
    <div class="banner error">{error}</div>
  {/if}
  {#if msg}
    <div class="banner ok">{msg}</div>
  {/if}

  <div class="segmented" class:dirty={output !== originalOutput}>
    {#each OUTPUTS as o}
      <button class:active={output.toLowerCase() === o.id.toLowerCase()} onclick={() => (output = o.id)}>
        {o.label}
      </button>
    {/each}
  </div>

  <div class="form">
    <label>
      Имя плеера <span class="muted">(-n)</span>
      <input class:dirty={dirty('player_name')} bind:value={form.player_name} placeholder="Living room" />
    </label>
    <label>
      Сервер <span class="muted">(-s)</span>
      <input class:dirty={dirty('server')} bind:value={form.server} placeholder="192.168.1.10" />
    </label>
    <label>
      Буферы, КБ <span class="muted">(-b поток:выход)</span>
      <input class:dirty={dirty('buffers')} bind:value={form.buffers} placeholder="500:1000" />
    </label>
    <label>
      Закрывать выход через, с <span class="muted">(-C, пусто = никогда)</span>
      <input class:dirty={dirty('close_timeout')} bind:value={form.close_timeout} placeholder="3600" />
    </label>
  </div>

  <label class="check">
    <input type="checkbox" bind:checked={resample} />
    Ресемплинг <code>-R</code> (если источник не 44,1 кГц)
  </label>

  <div class="actions tight">
    <button class="linklike" onclick={() => (showAll = !showAll)}>
      {showAll ? 'Скрыть разделы других выходов' : 'Показать все разделы'}
    </button>
  </div>

  {#if showBt}
    <h3>Bluetooth</h3>
    <div class="form">
      <label>
        Имя колонки <span class="muted">(a2dp_sink_name)</span>
        <input class:dirty={dirty('a2dp_sink_name')} bind:value={form.a2dp_sink_name} placeholder="My Speaker" />
      </label>
      <label>
        PIN <span class="muted">(a2dp_spin)</span>
        <input class:dirty={dirty('a2dp_spin')} bind:value={form.a2dp_spin} placeholder="1234 или 0000" />
      </label>
      <label>
        Имя адаптера <span class="muted">(a2dp_dev_name)</span>
        <input class:dirty={dirty('a2dp_dev_name')} bind:value={form.a2dp_dev_name} placeholder="Squeezelite" />
      </label>
      <label>
        Отпускать колонку через, с <span class="muted">(bt_idle_off, 0 = держать связь)</span>
        <input class:dirty={dirty('bt_idle_off')} bind:value={form.bt_idle_off} placeholder="0" />
        <span class="muted small hint">
          Через сколько секунд тишины разорвать Bluetooth, чтобы колонка ушла в сон.
          Соединение вернётся при следующем треке. Опция <code>-C</code> этого не делает.
        </span>
      </label>
    </div>

    <label class="check">
      <input type="checkbox" checked={sinkEnabled} onchange={() => toggle('enable_bt_sink')} />
      Работать приёмником: телефон → плата
    </label>

    {#if !sinkEnabled}
      <p class="muted small">
        Поля ниже активны только в режиме приёмника: плата принимает звук с телефона, а не
        отправляет его на колонку.
        <button class="linklike" onclick={() => toggle('enable_bt_sink')}>Включить режим приёмника</button>
      </p>
    {/if}

    <div class="form">
      <label>
        PIN приёмника <span class="muted">(bt_sink_pin)</span>
        <input class:dirty={dirty('bt_sink_pin')} bind:value={form.bt_sink_pin} disabled={!sinkEnabled} />
      </label>
      <label>
        Громкость приёмника <span class="muted">(bt_sink_volume, 0–127)</span>
        <input type="number" class:dirty={dirty('bt_sink_volume')} bind:value={form.bt_sink_volume} disabled={!sinkEnabled} />
      </label>
    </div>
  {/if}

  {#if showDac}
    <h3>I2S / ЦАП</h3>
    <div class="form">
      <label>
        Пресет
        <select bind:value={preset} onchange={() => preset && (form.dac_config = preset)}>
          <option value="">— свой вариант —</option>
          {#each DAC_PRESETS as p}
            <option value={p.value}>{p.label}</option>
          {/each}
        </select>
      </label>
      <label>
        Параметры ЦАП <span class="muted">(dac_config)</span>
        <input class:dirty={dirty('dac_config')} bind:value={form.dac_config} placeholder="model=ES8388,bck=27,…" />
      </label>
    </div>
    {#if !norm(form.dac_config).includes('model=')}
      <div class="banner warn">Без <code>model=</code> кодек не настроится — звука на выходе не будет.</div>
    {/if}
  {/if}

  {#if showSpdif}
    <h3>SPDIF</h3>
    <p class="muted small">SPDIF выводится напрямую, ЦАП не используется. Максимум 24 бита.</p>
    <div class="form">
      <label>
        clock / BCK <span class="muted">(gpio)</span>
        <input type="number" class:dirty={dirty('spdif_clock')} bind:value={form.spdif_clock} />
      </label>
      <label>
        wordselect / WS <span class="muted">(gpio)</span>
        <input type="number" class:dirty={dirty('spdif_ws')} bind:value={form.spdif_ws} />
      </label>
      <label>
        data <span class="muted">(gpio, сам выход)</span>
        <input type="number" class:dirty={dirty('spdif_data')} bind:value={form.spdif_data} />
      </label>
    </div>
  {/if}

  <div class="actions">
    <a class="linkline" href="#nvs">открыть autoexec1 в NVS</a>
  </div>

  <p class="muted small">Итоговая строка:</p>
  <pre class="code">{command}</pre>

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
