<script>
  import { onMount, onDestroy } from 'svelte';
  import { getConfig, getStatus, setConfig } from '../api.js';

  let relApi = '';
  let releasesUrl = '';
  let version = '';
  let releases = [];
  let loading = false;
  let error = '';
  let installing = '';
  let progress = null;
  let timer = null;
  let showAll = false;
  let onlyMine = true;

  // сколько релизов запрашивать у GitHub и сколько показывать сразу
  const REQUEST_LIMIT = 20;
  const VISIBLE_LIMIT = 8;

  const versionNumber = (v) => {
    const m = String(v ?? '').match(/(\d+)\s*$/);
    return m ? Number(m[1]) : 0;
  };

  async function loadMeta() {
    error = '';
    try {
      const [cfg, st] = await Promise.all([getConfig(), getStatus()]);
      relApi = cfg?.config?.rel_api?.value ?? '';
      releasesUrl = cfg?.config?.release_url?.value ?? '';
      version = st?.version ?? '';
    } catch (e) {
      error = e?.message ?? String(e);
    }
  }

  async function loadReleases() {
    loading = true;
    error = '';
    try {
      if (!relApi) throw new Error('В настройках не задан адрес списка обновлений (rel_api)');
      // ask for a bounded page: the API returns 30 by default and the JSON grows
      // quickly, so be explicit about how much is enough for a picker
      const url = relApi.includes('?') ? `${relApi}&per_page=${REQUEST_LIMIT}` : `${relApi}?per_page=${REQUEST_LIMIT}`;
      const res = await fetch(url, { cache: 'no-store' });
      if (!res.ok) throw new Error(`HTTP ${res.status}`);
      const data = await res.json();
      releases = (data ?? [])
        .map((r) => {
          const bin = (r.assets ?? []).find((a) => a.name.endsWith('.bin'));
          return {
            tag: r.tag_name,
            platform: String(r.tag_name ?? '').split('.')[0],
            depth: String(r.tag_name ?? '').split('.')[1] ?? '',
            name: r.name,
            published: r.published_at,
            url: bin?.browser_download_url ?? '',
            size: bin?.size ?? 0,
          };
        })
        .filter((r) => r.url);
      showAll = false;
    } catch (e) {
      error = e?.message ?? String(e);
    } finally {
      loading = false;
    }
  }

  // The firmware posts its progress as a message whose text is itself JSON:
  // {"ota_dsc":"...","ota_pct":N}. /messages.json wraps it as
  // { message, type, class, sent_time, current_time } and drains the queue,
  // so every message has to be parsed here before it is lost.
  function readProgress(messages) {
    let latest = null;
    for (const item of messages ?? []) {
      if (!item?.message) continue;
      try {
        const parsed = JSON.parse(item.message);
        if (parsed && parsed.ota_pct !== undefined) latest = parsed;
      } catch {
        /* not a JSON message - ignore */
      }
    }
    return latest;
  }

  async function install(release) {
    installing = release.tag;
    progress = { pct: 0, dsc: 'передаю команду на обновление' };
    try {
      await setConfig({ fwurl: release.url });
      if (!timer) {
        timer = setInterval(async () => {
          try {
            const res = await fetch('/messages.json', { cache: 'no-store' });
            if (!res.ok) return;
            const ota = readProgress(await res.json());
            if (ota) progress = { pct: ota.ota_pct, dsc: ota.ota_dsc };
          } catch {
            /* the device reboots during the update, so failures are expected */
          }
        }, 1000);
      }
    } catch (e) {
      error = e?.message ?? String(e);
      installing = '';
      progress = null;
    }
  }

  $: platform = String(version ?? '').replace(/-\d+$/, '');
  $: filtered = releases.filter((r) => !onlyMine || r.platform === platform);
  $: visible = showAll ? filtered : filtered.slice(0, VISIBLE_LIMIT);

  const kb = (n) => (n ? `${(n / 1024).toFixed(0)} КБ` : '');
  const date = (s) => (s ? new Date(s).toLocaleDateString() : '');

  onMount(async () => {
    await loadMeta();
  });

  onDestroy(() => {
    if (timer) clearInterval(timer);
  });
</script>

<section class="card">
  <h2>Прошивка</h2>
  <p class="muted small">
    Текущая версия: <code>{version || '—'}</code>. Во время установки плата перезагрузится,
    соединение на несколько секунд пропадёт.
  </p>
  <p class="muted small">
    Список загружается только по кнопке: GitHub отдаёт неавторизованным клиентам
    ограниченное число запросов в час, и тратить их на каждое открытие страницы
    не стоит.
  </p>

  {#if error}
    <div class="banner error">{error}</div>
  {/if}

  {#if progress}
    <div class="banner ok">
      Обновление: {progress.pct ?? 0}% — {progress.dsc ?? ''}
      <div class="bar"><i style="width:{progress.pct ?? 0}%"></i></div>
    </div>
  {/if}

  <div class="actions">
    <button onclick={loadReleases} disabled={loading}>
      {loading ? 'Загружаю…' : releases.length ? 'Обновить список' : 'Получить список'}
    </button>
    {#if releasesUrl}<a class="muted small" href={releasesUrl} target="_blank" rel="noreferrer">страница релизов</a>{/if}
  </div>

  {#if releases.length}
    <label class="check">
      <input type="checkbox" bind:checked={onlyMine} />
      Только сборки для этой платы (<code>{platform || '—'}</code>)
    </label>
  {/if}

  {#if !releases.length && !loading && !error}
    <p class="muted small">
      Список ещё не загружен — нажмите «Получить список».
    </p>
  {/if}

  {#if releases.length}
    <div class="releases">
      {#each visible as r}
        <div class="release">
          <div>
            <strong>{r.name || r.tag}</strong>
            {#if r.platform !== platform}<span class="badge">{r.platform}</span>{/if}
            <span class="muted small">{date(r.published)} · {kb(r.size)}</span>
          </div>
          <div class="actions">
            {#if versionNumber(r.tag) > versionNumber(version)}
              <span class="badge">новее</span>
            {/if}
            <button class="primary" onclick={() => install(r)} disabled={installing === r.tag}>
              {installing === r.tag ? 'Устанавливаю…' : 'Установить'}
            </button>
          </div>
        </div>
      {/each}
    </div>
    {#if filtered.length > VISIBLE_LIMIT && !showAll}
      <div class="actions">
        <button onclick={() => (showAll = true)}>Показать все ({filtered.length})</button>
      </div>
    {/if}
  {/if}
</section>
