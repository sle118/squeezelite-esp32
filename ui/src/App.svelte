<script>
  import Status from './views/Status.svelte';
  import Placeholder from './views/Placeholder.svelte';

  const tabs = [
    { id: 'status', label: 'Статус' },
    { id: 'audio', label: 'Звук' },
    { id: 'hardware', label: 'Железо' },
    { id: 'nvs', label: 'NVS' },
    { id: 'firmware', label: 'Прошивка' },
  ];

  let tab = 'status';
</script>

<header class="topbar">
  <div class="brand">
    <span class="dot"></span>
    <strong>Squeezelite-ESP32</strong>
    <span class="muted" id="host">{typeof location !== 'undefined' ? location.host : ''}</span>
  </div>
  <nav>
    {#each tabs as t}
      <button class:active={tab === t.id} onclick={() => (tab = t.id)}>{t.label}</button>
    {/each}
  </nav>
</header>

<main>
  {#if tab === 'status'}
    <Status />
  {:else}
    <Placeholder name={tabs.find((t) => t.id === tab)?.label ?? ''} />
  {/if}
</main>

<footer>
  <span class="muted">Squeezelite-ESP32 web UI · ранняя версия</span>
</footer>
