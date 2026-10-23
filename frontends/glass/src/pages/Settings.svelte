<script lang="ts">
  // Settings: every setting, grouped like the bundled UI's pages. Rows come
  // from settings-spec.ts; whatever describe() lists with a save_* slot that
  // the spec does not name is offered under "Everything else".
  import { octave, type Json, type Description } from '../lib/octave'
  import { groups, themeNames, snake, type Item } from '../lib/settings-spec'
  import SettingRow from '../ui/SettingRow.svelte'
  import ShiftLightEditor from '../ui/ShiftLightEditor.svelte'
  import HomeParams from '../ui/HomeParams.svelte'
  import GestureMap from '../ui/GestureMap.svelte'
  import Esp32 from '../ui/Esp32.svelte'
  import Imu from '../ui/Imu.svelte'
  import DeviceBits from '../ui/DeviceBits.svelte'
  import ThemePicker from '../ui/ThemePicker.svelte'
  import Presets from '../ui/Presets.svelte'
  import Icon from '../ui/Icon.svelte'

  let { arg = '' }: { arg?: string } = $props()
  let groupId = $state('display')
  $effect(() => { if (arg && (groups.some((g) => g.id === arg) || arg === 'other')) groupId = arg })
  const group = $derived(groups.find((g) => g.id === groupId))

  // One subscription to every property the spec shows
  const specProps = groups.flatMap((g) => g.sections.flatMap((s) => s.items.map((i) => i.prop)))
  const values = octave.props<Record<string, Json>>('settingsManager', specProps, {})

  const fonts = octave.prop<{ family: string; name: string }[]>('fontCatalog', 'fonts', [])
  const customThemes = octave.prop<{ name?: string }[] | string[]>('settingsManager', 'customThemes', [])
  function optionsFor(item: Item) {
    if (item.options === 'themes') {
      const custom = ($customThemes as (string | { name?: string })[]).map((t) => (typeof t === 'string' ? t : t.name ?? '')).filter(Boolean)
      return [...themeNames, ...custom].map((t) => ({ value: t, label: t.replace(/([a-z])([A-Z])/g, '$1 $2') }))
    }
    if (item.options === 'fonts') return $fonts.map((f) => ({ value: f.name, label: f.family }))
    return item.options ?? []
  }

  // Everything else: properties with a save slot the spec does not cover
  let rest = $state<Item[]>([])
  const restValues = $derived(octave.props<Record<string, Json>>('settingsManager', rest.map((i) => i.prop), {}))
  $effect(() => {
    const u = octave.status.subscribe(async (s) => {
      if (s !== 'online') return
      const d: Description = await octave.describe('settingsManager')
      const known = new Set(specProps)
      const slots = new Set(d.methods.map((m) => m.name))
      rest = d.properties.filter((p) => !known.has(p.name) && slots.has('save_' + snake(p.name)) && ['bool', 'int', 'float', 'double', 'QString'].includes(p.type))
        .map((p) => ({ prop: p.name, slot: 'save_' + snake(p.name), label: p.name.replace(/([a-z])([A-Z])/g, '$1 $2').replace(/^./, (c) => c.toUpperCase()),
          kind: p.type === 'bool' ? 'toggle' : p.type === 'QString' ? 'text' : 'number', float: p.type === 'float' || p.type === 'double' }))
    })
    return u
  })
</script>

<div class="page">
  <div class="page-head"><h1>Settings</h1></div>
  <div class="layout">
    <nav class="glass groups" aria-label="Settings groups">
      {#each groups as g}
        <button class="g" class:on={groupId === g.id} onclick={() => (groupId = g.id)}><Icon name={g.icon} size={22} />{g.title}</button>
      {/each}
      {#if rest.length}<button class="g" class:on={groupId === 'other'} onclick={() => (groupId = 'other')}><Icon name="list" size={22} />Everything else</button>{/if}
    </nav>

    <div class="sections">
      {#if group}
        {#each group.sections as s}
          <section class="glass panel">
            <h2>{s.title}</h2>
            {#if s.special === 'homeParams'}
              {#each s.items as item}<SettingRow {item} value={$values[item.prop]} options={optionsFor(item)} />{/each}
              <div class="sub"><HomeParams /></div>
            {:else}
              {#if s.special === 'presets'}<Presets values={$values} />{/if}
              {#each s.items as item}<SettingRow {item} value={$values[item.prop]} options={optionsFor(item)} />{/each}
              {#if s.special === 'themes'}<div class="sub"><ThemePicker /></div>
              {:else if s.special === 'shiftlight'}<ShiftLightEditor />
              {:else if s.special === 'gestures'}<GestureMap />
              {:else if s.special === 'esp32'}<Esp32 />
              {:else if s.special === 'imu'}<Imu />
              {:else if s.special === 'network' || s.special === 'power' || s.special === 'about'}<DeviceBits which={s.special} />
              {/if}
            {/if}
          </section>
        {/each}
      {:else}
        <section class="glass panel">
          <h2>Everything else</h2>
          <p class="muted small">Settings the app exposes that have no row of their own yet, by name.</p>
          {#each rest as item}<SettingRow {item} value={$restValues[item.prop]} />{/each}
        </section>
      {/if}
    </div>
  </div>
</div>

<style>
  .layout { display: grid; grid-template-columns: 220px 1fr; gap: var(--gap); align-items: start; }
  .groups { display: flex; flex-direction: column; gap: 4px; padding: 10px; position: sticky; top: 0; }
  .g { display: flex; align-items: center; gap: 12px; min-height: 56px; padding: 0 16px; border-radius: 16px; font-weight: 600; color: var(--ink-2); text-align: left; }
  .g.on { background: var(--control-on); color: var(--control-on-ink); }
  .sections { display: flex; flex-direction: column; gap: var(--gap); min-width: 0; }
  section h2 { margin-bottom: 8px; }
  .sub { margin-top: 12px; }
  @media (max-width: 760px) {
    .layout { grid-template-columns: 1fr; }
    .groups { flex-direction: row; overflow-x: auto; position: static; }
    .g { flex: none; min-height: 48px; }
  }
</style>
