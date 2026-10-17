<script>
  import { onMount } from 'svelte';
  import Status from './views/Status.svelte';
  import Player from './views/Player.svelte';
  import Nvs from './views/Nvs.svelte';
  import Audio from './views/Audio.svelte';
  import Placeholder from './views/Placeholder.svelte';

  const tabs = [
    { id: 'status', label: 'Статус' },
    { id: 'player', label: 'Плеер' },
    { id: 'nvs', label: 'NVS' },
    { id: 'audio', label: 'Звук' },
    { id: 'firmware', label: 'Прошивка' },
  ];

  const fromHash = () => {
    const id = (typeof location !== 'undefined' ? location.hash : '').replace(/^#/, '');
    return tabs.some((t) => t.id === id) ? id : 'status';
  };

  let tab = fromHash();

  function select(id) {
    tab = id;
    if (typeof history !== 'undefined') history.replaceState(null, '', `#${id}`);
  }

  onMount(() => {
    const onHash = () => (tab = fromHash());
    window.addEventListener('hashchange', onHash);
    return () => window.removeEventListener('hashchange', onHash);
  });
</script>

<header class="topbar">
  <div class="brand">
    <span class="dot"></span>
    <strong>Squeezelite-ESP32</strong>
    <span class="muted" id="host">{typeof location !== 'undefined' ? location.host : ''}</span>
  </div>
  <nav>
    {#each tabs as t}
      <button class:active={tab === t.id} onclick={() => select(t.id)}>{t.label}</button>
    {/each}
  </nav>
</header>

<main>
  {#if tab === 'status'}
    <Status />
  {:else if tab === 'player'}
    <Player />
  {:else if tab === 'nvs'}
    <Nvs />
  {:else if tab === 'audio'}
    <Audio />
  {:else}
    <Placeholder name={tabs.find((t) => t.id === tab)?.label ?? ''} />
  {/if}
</main>

<footer>
  <span class="muted">Squeezelite-ESP32 web UI · ранняя версия · <a href="/">встроенный интерфейс</a></span>
</footer>
