<script>
  import { onMount } from 'svelte';
  import { getConfig, setConfig, reboot } from '../api.js';
  import { tokenize, join, getOpt, setOpt, hasFlag, setFlag } from '../lib/args.js';

  const OUTPUTS = [
    { id: 'bt', label: 'Bluetooth' },
    { id: 'I2S', label: 'I2S DAC (аналоговый выход)' },
    { id: 'SPDIF', label: 'SPDIF' },
  ];

  const LOGS = ['', 'all=info', 'all=debug', 'slimproto=info', 'output=info', 'wifi=info'];

  let original = '';
  let output = 'bt';
  let outputKnown = true;
  let buffers = '';
  let log = '';
  let resample = false;
  let btName = '';
  let preview = '';
  let msg = '';
  let error = '';
  let saving = false;

  async function load() {
    error = '';
    msg = '';
    try {
      const cfg = await getConfig();
      original = cfg?.config?.autoexec1?.value ?? '';
      btName = cfg?.config?.a2dp_sink_name?.value ?? '';

      const tokens = tokenize(original);
      const o = getOpt(tokens, '-o');
      outputKnown = OUTPUTS.some((x) => x.id.toLowerCase() === o.toLowerCase());
      output = outputKnown ? OUTPUTS.find((x) => x.id.toLowerCase() === o.toLowerCase()).id : o;
      buffers = getOpt(tokens, '-b');
      log = getOpt(tokens, '-d');
      resample = hasFlag(tokens, '-R');
      build();
    } catch (e) {
      error = e?.message ?? String(e);
    }
  }

  function build() {
    let t = tokenize(original);
    t = setOpt(t, '-o', output === 'custom' ? '' : output);
    t = setOpt(t, '-b', buffers.trim());
    t = setOpt(t, '-d', log);
    t = setFlag(t, '-R', resample);
    preview = join(t);
    return preview;
  }

  async function save() {
    saving = true;
    msg = '';
    error = '';
    try {
      const cmd = build();
      await setConfig({ autoexec1: cmd, a2dp_sink_name: btName.trim() });
      original = cmd;
      msg = 'Сохранено. Перезагрузите плеер, чтобы применить настройки.';
    } catch (e) {
      error = e?.message ?? String(e);
    } finally {
      saving = false;
    }
  }

  onMount(load);
</script>

<section class="card">
  <h2>Звук</h2>
  <p class="muted small">
    Выход, буферы и уровень логирования. Настройки записываются в строку запуска и в ключ
    <code>a2dp_sink_name</code>; после сохранения нужна перезагрузка плеера.
  </p>

  <div class="form">
    <label>
      Выход <span class="muted">(-o)</span>
      <select bind:value={output} onchange={build}>
        {#each OUTPUTS as o}
          <option value={o.id}>{o.label}</option>
        {/each}
        {#if !outputKnown}
          <option value="custom">{output || 'не задан'}</option>
        {/if}
      </select>
    </label>

    <label>
      Имя Bluetooth-колонки <span class="muted">(a2dp_sink_name)</span>
      <input bind:value={btName} placeholder="HT-S700RF" />
    </label>

    <label>
      Буферы, КБ <span class="muted">(-b поток:выход)</span>
      <input bind:value={buffers} oninput={build} placeholder="500:1000" />
    </label>

    <label>
      Логирование <span class="muted">(-d)</span>
      <select bind:value={log} onchange={build}>
        {#each LOGS as l}
          <option value={l}>{l || 'выключено'}</option>
        {/each}
      </select>
    </label>
  </div>

  <label class="check">
    <input type="checkbox" bind:checked={resample} onchange={build} />
    Ресемплинг <code>-R</code> (нужен, если источник не 44,1 кГц)
  </label>

  <p class="muted small">Итоговая строка:</p>
  <pre class="code">{preview || '—'}</pre>

  {#if error}
    <div class="banner error">{error}</div>
  {/if}
  {#if msg}
    <div class="banner ok">{msg}</div>
  {/if}

  <div class="actions">
    <button class="primary" onclick={save} disabled={saving}>{saving ? 'Сохраняю…' : 'Сохранить'}</button>
    <button onclick={() => reboot()}>Перезагрузить</button>
    <button onclick={load}>Обновить</button>
  </div>
</section>
