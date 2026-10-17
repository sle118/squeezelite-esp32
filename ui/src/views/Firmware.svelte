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
      const res = await fetch(relApi, { cache: 'no-store' });
      if (!res.ok) throw new Error(`HTTP ${res.status}`);
      const data = await res.json();
      releases = (data ?? [])
        .map((r) => {
          const bin = (r.assets ?? []).find((a) => a.name.endsWith('.bin'));
          return {
            tag: r.tag_name,
            name: r.name,
            published: r.published_at,
            url: bin?.browser_download_url ?? '',
            size: bin?.size ?? 0,
          };
        })
        .filter((r) => r.url);
    } catch (e) {
      error = e?.message ?? String(e);
    } finally {
      loading = false;
    }
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
            const messages = await res.json();
            const ota = (messages ?? []).filter((m) => m && m.ota_pct !== undefined).pop();
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

  {#if !releases.length && !loading && !error}
    <p class="muted small">
      Список ещё не загружен — нажмите «Получить список».
    </p>
  {/if}

  {#if releases.length}
    <div class="releases">
      {#each releases as r}
        <div class="release">
          <div>
            <strong>{r.name || r.tag}</strong>
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
  {/if}
</section>
