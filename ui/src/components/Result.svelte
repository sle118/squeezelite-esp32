<script>
  // Live log of what the board reports while an action runs: saving the config,
  // rebooting, installing firmware. Mirrors the "Result" box of the built-in UI.
  import { messages, describeMessage, toneOf } from '../lib/messages.js';

  export let running = false;
  export let title = 'Результат';

  $: list = $messages.slice(-14);
  $: visible = running || list.length > 0;
</script>

{#if visible}
  <div class="result">
    <div class="result-head">
      <strong>{title}</strong>
      {#if running}
        <span class="result-state"><i class="spinner" aria-hidden="true"></i> выполняю…</span>
      {:else}
        <span class="result-state done">готово</span>
      {/if}
    </div>
    <ul>
      {#each list as item}
        <li class={toneOf(item)}>{describeMessage(item)}</li>
      {/each}
      {#if !list.length}
        <li class="muted">пока ничего не происходило</li>
      {/if}
    </ul>
  </div>
{/if}
