<script lang="ts">
  // A sheet over the page: confirmations, pickers, small forms.
  import type { Snippet } from 'svelte'
  let { title, onclose, children, wide = false }: { title: string; onclose: () => void; children: Snippet; wide?: boolean } = $props()
  function key(e: KeyboardEvent) { if (e.key === 'Escape') onclose() }
</script>

<svelte:window onkeydown={key} />
<div class="scrim" onclick={onclose} role="presentation">
  <div class="sheet glass strong" class:wide role="dialog" aria-modal="true" aria-label={title} tabindex="-1" onclick={(e) => e.stopPropagation()} onkeydown={(e) => e.stopPropagation()}>
    <div class="head"><h2>{title}</h2><button class="btn icon" aria-label="Close" onclick={onclose}>✕</button></div>
    <div class="body">{@render children()}</div>
  </div>
</div>

<style>
  .scrim { position: fixed; inset: 0; z-index: 30; background: rgba(0,0,0,.35); display: grid; place-items: center; padding: 16px;
           animation: fade .2s var(--ease); }
  .sheet { width: min(560px, 100%); max-height: 90vh; display: flex; flex-direction: column; animation: rise .25s var(--ease); }
  .sheet.wide { width: min(900px, 100%); }
  .head { display: flex; align-items: center; justify-content: space-between; padding: 22px 26px 10px; }
  .body { padding: 10px 26px 26px; overflow-y: auto; display: flex; flex-direction: column; gap: 14px; }
  @keyframes fade { from { opacity: 0; } }
  @keyframes rise { from { transform: translateY(24px); opacity: 0; } }
</style>
