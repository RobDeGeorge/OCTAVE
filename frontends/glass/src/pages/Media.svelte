<script lang="ts">
  // Media: what is playing, the library (sorted), playlists.
  import { octave } from '../lib/octave'
  import { nowPlaying, act, toast } from '../lib/stores'
  import NowPlayingCard from '../ui/NowPlayingCard.svelte'
  import Modal from '../ui/Modal.svelte'
  import Icon from '../ui/Icon.svelte'
  import Art from '../ui/Art.svelte'
  import { mediaUrl, ticket } from '../lib/media'

  interface Song { file: string; title: string; artist: string; album: string; duration: string }

  let playlists = $state<string[]>([])
  let current = $state('')
  let songs = $state<Song[]>([])
  let loading = $state(false)
  let sort = $state<'title' | 'artist' | 'album'>('title')
  let ascending = $state(true)
  let currentFile = $state('')
  let picked = $state<Song | null>(null)   // song menu
  let newList = $state(false)
  let newName = $state('')
  let confirmDeleteList = $state(false)
  let confirmDeleteSong = $state<Song | null>(null)
  let movable = $state<string[]>([])

  async function loadLists() {
    playlists = await octave.call<string[]>('mediaManager', 'get_playlist_names')
    current = await octave.call<string>('mediaManager', 'get_current_playlist_name')
  }
  async function loadSongs() {
    loading = true
    try {
      const files = await octave.call<string[]>('mediaManager', 'sort_media_files', sort, ascending)
      currentFile = await octave.call<string>('mediaManager', 'get_current_file')
      songs = await Promise.all(files.map(async (file) => {
        const [title, artist, album, duration] = await Promise.all([
          octave.call<string>('mediaManager', 'get_display_name', file),
          octave.call<string>('mediaManager', 'get_band', file),
          octave.call<string>('mediaManager', 'get_album', file),
          octave.call<string>('mediaManager', 'get_formatted_duration', file),
        ])
        return { file, title: title || file, artist, album, duration }
      }))
    } catch (e) { toast((e as Error).message, 'bad') }
    loading = false
  }
  async function refresh() { await loadLists(); await loadSongs() }

  $effect(() => {
    const status = octave.status
    const unsubs = [
      status.subscribe((s) => { if (s === 'online') refresh() }),
      octave.signal('mediaManager', 'mediaListChanged', () => loadSongs()),
      octave.signal('mediaManager', 'playlistsChanged', () => loadLists()),
      octave.signal('mediaManager', 'currentPlaylistChanged', () => refresh()),
      octave.signal('mediaManager', 'currentMediaChanged', (f) => { currentFile = String(f) }),
    ]
    return () => unsubs.forEach((u) => u())
  })

  function setSort(col: typeof sort) {
    if (sort === col) ascending = !ascending
    else { sort = col; ascending = true }
    loadSongs()
  }
  async function openMenu(s: Song) {
    picked = s
    movable = await octave.call<string[]>('mediaManager', 'get_movable_playlist_names')
  }
  async function createList() {
    const name = newName.trim()
    if (!name) return
    const ok = await act(octave.call<boolean>('mediaManager', 'create_playlist', name))
    if (ok) { toast(`Created ${name}`); newList = false; newName = '' }
    else toast('That playlist exists already', 'bad')
  }
  const deletable = $derived(current && current !== 'All Music' && current !== 'Unsorted')
</script>

<div class="page">
  <NowPlayingCard />

  <div class="glass panel library">
    <div class="bar">
      <div class="lists" role="tablist" aria-label="Playlists">
        {#each playlists as p}
          <button class="btn" class:on={p === current} role="tab" aria-selected={p === current}
                  onclick={() => act(octave.call('mediaManager', 'select_playlist', p))}>{p}</button>
        {/each}
        <button class="btn icon" aria-label="New playlist" onclick={() => (newList = true)}><Icon name="plus" /></button>
        {#if deletable}
          <button class="btn icon danger" aria-label="Delete playlist {current}" onclick={() => (confirmDeleteList = true)}><Icon name="trash" /></button>
        {/if}
      </div>
      <div class="seg" aria-label="Sort">
        {#each ['title', 'artist', 'album'] as col}
          <button class:on={sort === col} onclick={() => setSort(col as typeof sort)}>
            {col[0].toUpperCase() + col.slice(1)}{sort === col ? (ascending ? ' ↑' : ' ↓') : ''}
          </button>
        {/each}
      </div>
    </div>

    {#if songs.length === 0}
      <p class="muted empty">{loading ? 'Reading the library…' : 'No songs here. Set the library folder under Settings, Media, or download some.'}</p>
    {/if}
    <ul class="songs">
      {#each songs as s (s.file)}
        <li class:now={s.file === currentFile}>
          <button class="play" onclick={() => act(octave.call('mediaManager', 'play_file', s.file))}>
            <span class="mark">{#if s.file === currentFile}<Icon name={$nowPlaying.playing ? 'pause' : 'play'} size={20} />{/if}</span>
            {#key $ticket}<Art title={s.title} artist={s.artist} url={mediaUrl('/art', { song: s.file })} size={48} />{/key}
            <span class="t">
              <span class="title">{s.title}</span>
              <span class="sub muted small">{[s.artist, s.album].filter(Boolean).join(' – ')}</span>
            </span>
            <span class="dur num muted">{s.duration}</span>
          </button>
          <button class="btn icon" aria-label="More for {s.title}" onclick={() => openMenu(s)}><Icon name="more" /></button>
        </li>
      {/each}
    </ul>
  </div>
</div>

{#if picked}
  <Modal title={picked.title} onclose={() => (picked = null)}>
    <p class="muted">{[picked.artist, picked.album].filter(Boolean).join(' – ')}</p>
    <button class="btn primary" onclick={() => { act(octave.call('mediaManager', 'play_file', picked!.file)); picked = null }}><Icon name="play" />Play</button>
    {#if movable.length}
      <h3>Move to playlist</h3>
      <div class="wrap">
        {#each movable as p}
          <button class="btn" onclick={async () => {
            const ok = await act(octave.call<boolean>('mediaManager', 'move_song_to_playlist', picked!.file, p))
            if (ok) { toast(`Moved to ${p}`); picked = null; loadSongs() } else toast('Could not move it', 'bad')
          }}>{p}</button>
        {/each}
      </div>
    {/if}
    <button class="btn danger" onclick={() => { confirmDeleteSong = picked; picked = null }}><Icon name="trash" />Delete file</button>
  </Modal>
{/if}

{#if newList}
  <Modal title="New playlist" onclose={() => (newList = false)}>
    <input class="field" placeholder="Name" bind:value={newName} onkeydown={(e) => e.key === 'Enter' && createList()} />
    <button class="btn primary" onclick={createList}>Create playlist</button>
  </Modal>
{/if}

{#if confirmDeleteList}
  <Modal title="Delete {current}?" onclose={() => (confirmDeleteList = false)}>
    <p class="muted">The songs go back to Unsorted. This cannot be undone.</p>
    <button class="btn danger on" onclick={async () => {
      const ok = await act(octave.call<boolean>('mediaManager', 'delete_playlist', current))
      confirmDeleteList = false
      toast(ok ? 'Playlist deleted' : 'Could not delete it', ok ? 'ok' : 'bad')
    }}>Delete playlist</button>
  </Modal>
{/if}

{#if confirmDeleteSong}
  <Modal title="Delete {confirmDeleteSong.title}?" onclose={() => (confirmDeleteSong = null)}>
    <p class="muted">The file is removed from the head unit. This cannot be undone.</p>
    <button class="btn danger on" onclick={async () => {
      const ok = await act(octave.call<boolean>('mediaManager', 'delete_song', confirmDeleteSong!.file))
      confirmDeleteSong = null
      toast(ok ? 'Song deleted' : 'Could not delete it', ok ? 'ok' : 'bad')
      loadSongs()
    }}>Delete song</button>
  </Modal>
{/if}

<style>
  .library { display: flex; flex-direction: column; gap: 14px; }
  .bar { display: flex; justify-content: space-between; align-items: center; gap: 14px; flex-wrap: wrap; }
  .lists { display: flex; gap: 8px; flex-wrap: wrap; }
  .empty { padding: 20px 4px; }
  .songs { list-style: none; margin: 0; padding: 0; display: flex; flex-direction: column; }
  li { display: flex; align-items: center; gap: 6px; border-top: 1px solid var(--glass-edge); }
  li.now .title { color: var(--accent); }
  .play { flex: 1; display: flex; align-items: center; gap: 14px; min-height: 68px; text-align: left; min-width: 0; padding: 6px 4px; border-radius: 14px; }
  .play:hover { background: var(--control); }
  .mark { width: 24px; flex: none; display: grid; place-items: center; color: var(--accent); }
  .t { display: flex; flex-direction: column; min-width: 0; flex: 1; }
  .title { font-weight: 600; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
  .sub { overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
  .dur { flex: none; }
  .wrap { display: flex; flex-wrap: wrap; gap: 8px; }
</style>
