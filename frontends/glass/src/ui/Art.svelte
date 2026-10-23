<script lang="ts">
  // Album art. The service does not stream art yet (TODO/open-backend.md,
  // step 6), so until GET /art lands this is a tile coloured from the title.
  let { title = '', artist = '', url = '', size = 160 }: { title?: string; artist?: string; url?: string; size?: number } = $props()
  function hueOf(s: string): number { let h = 0; for (const c of s) h = (h * 31 + c.charCodeAt(0)) >>> 0; return h % 360 }
  const h = $derived(hueOf(title + artist))
  const initials = $derived((title || '♪').trim().slice(0, 1).toUpperCase())
  const http = $derived(/^https?:\/\//.test(url))
</script>

<div class="art" style="--s:{size}px; --h:{h}">
  {#if http}
    <img src={url} alt="" />
  {:else}
    <span>{initials}</span>
  {/if}
</div>

<style>
  .art { width: var(--s); height: var(--s); flex: none; border-radius: calc(var(--s) * .14); overflow: hidden;
         display: grid; place-items: center; color: #fff; font-weight: 800; font-size: calc(var(--s) * .42);
         background: linear-gradient(145deg, oklch(62% .17 var(--h)), oklch(38% .14 calc(var(--h) + 60)));
         box-shadow: inset 0 1px 0 rgba(255,255,255,.3), 0 14px 34px rgba(0,0,0,.28); transition: background 1s var(--ease); }
  img { width: 100%; height: 100%; object-fit: cover; }
</style>
