<script>
  import { onMount } from 'svelte';
  import { getConfig, setConfig, reboot } from '../api.js';
  import { tokenize, join, getOpt, setOpt } from '../lib/args.js';

  let original = '';
  let name = '';
  let server = '';
  let buffers = '';
  let timeout = '';
  let preview = '';
  let msg = '';
  let error = '';
  let saving = false;

  async function load() {
    error = '';
    try {
      const cfg = await getConfig();
      original = cfg?.config?.autoexec1?.value ?? '';
      const tokens = tokenize(original);
      name = getOpt(tokens, '-n');
      server = getOpt(tokens, '-s');
      buffers = getOpt(tokens, '-b');
      timeout = getOpt(tokens, '-C');
      preview = original;
    } catch (e) {
      error = e?.message ?? String(e);
    }
  }

  function build() {
    let t = tokenize(original);
    t = setOpt(t, '-n', name.trim());
    t = setOpt(t, '-s', server.trim());
    t = setOpt(t, '-b', buffers.trim());
    t = setOpt(t, '-C', timeout.trim());
    preview = join(t);
    return preview;
  }

  async function save() {
    saving = true;
    msg = '';
    error = '';
    try {
      const cmd = build();
      await setConfig({ autoexec1: cmd });
      original = cmd;
      msg = 'Сохранено. Перезагрузите плеер, чтобы он перечитал строку запуска.';
    } catch (e) {
      error = e?.message ?? String(e);
    } finally {
      saving = false;
    }
  }

  onMount(load);
</script>

<section class="card">
  <h2>Плеер</h2>
  <p class="muted small">
    Правка строки запуска <code>autoexec1</code> напрямую. В отличие от встроенного интерфейса
    поля не смещаются: меняются только те опции, которые вы действительно правите.
  </p>

  <div class="form">
    <label>
      Имя плеера <span class="muted">(-n)</span>
      <input bind:value={name} oninput={build} placeholder="Колонки в зале" />
    </label>

    <label>
      Сервер <span class="muted">(-s)</span>
      <input bind:value={server} oninput={build} placeholder="192.168.88.111" />
    </label>

    <label>
      Буферы <span class="muted">(-b)</span>
      <input bind:value={buffers} oninput={build} placeholder="500:1000" />
    </label>

    <label>
      Закрывать выход через, с <span class="muted">(-C, пусто = никогда)</span>
      <input bind:value={timeout} oninput={build} placeholder="пусто" />
    </label>
  </div>

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
