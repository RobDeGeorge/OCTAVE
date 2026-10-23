<script lang="ts">
  // Pick the music folder on the head unit: mediaManager.browse_music_folders
  // lists the roots, then each folder's sub-folders and audio files.
  import { octave } from '../lib/octave'
  import { act, toast } from '../lib/stores'
  import Modal from './Modal.svelte'
  import Icon from './Icon.svelte'
  let { onclose, current = '' }: { onclose: () => void; current?: string } = $props()
  interface Roots { roots: { name: string; path: string }[] }
  interface Folder { path: string; parent: string; dirs: string[]; audioFiles: string[]; truncated: boolean; error?: string }
  let roots = $state<{ name: string; path: string }[]>([])
  let folder = $state<Folder | null>(null)
  let error = $state('')
  async function open(path: string) {
    error = ''
    const r = await octave.call<Folder & Roots>('mediaManager', 'browse_music_folders', path).catch((e) => ({ error: e.message } as Folder & Roots))
    if (r.error) { error = r.error; return }
    if (path === '') { roots = r.roots ?? []; folder = null } else folder = r as Folder
  }
  $effect(() => { open(current || '') })
  const sep = (p: string) => (p.endsWith('/') ? p : p + '/')
</script>

<Modal title="Music folder" {onclose} wide>
  {#if folder}
    <div class="crumb">
      <button class="btn icon" aria-label="Roots" onclick={() => open('')}><Icon name="home" /></button>
      <button class="btn icon" aria-label="Up" disabled={!folder.parent} onclick={() => open(folder!.parent)}><Icon name="up" /></button>
      <span class="num path">{folder.path}</span>
    </div>
    <p class="small muted">{folder.audioFiles.length} audio file{folder.audioFiles.length === 1 ? '' : 's'} here{folder.truncated ? ' (list cut short)' : ''}</p>
    <div class="list">
      {#each folder.dirs as d}
        <button class="dir" onclick={() => open(sep(folder!.path) + d)}><Icon name="folder" size={20} />{d}</button>
      {/each}
      {#if folder.dirs.length === 0}<p class="muted small">No folders inside.</p>{/if}
    </div>
    <button class="btn primary" onclick={async () => { await act(octave.call('settingsManager', 'save_media_folder', folder!.path)); toast('Library folder set'); onclose() }}>
      <Icon name="check" />Use this folder
    </button>
  {:else}
    <p class="small muted">Start from one of these.</p>
    <div class="list">
      {#each roots as r}
        <button class="dir" onclick={() => open(r.path)}><Icon name="folder" size={20} /><span>{r.name}</span><span class="small num muted">{r.path}</span></button>
      {/each}
    </div>
  {/if}
  {#if error}<p class="bad small">{error}</p>{/if}
</Modal>

<style>
  .crumb { display: flex; align-items: center; gap: 8px; }
  .path { overflow-wrap: anywhere; }
  .list { display: flex; flex-direction: column; max-height: 50vh; overflow-y: auto; }
  .dir { display: flex; align-items: center; gap: 12px; min-height: 52px; padding: 0 12px; border-radius: 12px; text-align: left; }
  .dir:hover { background: var(--control); }
  .bad { color: var(--bad); }
</style>
