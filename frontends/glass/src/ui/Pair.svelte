<script lang="ts">
  // The pair screen: this browser is not on the head unit, so it needs the
  // 6-digit code from Settings > Service > Pair a device.
  import { pair, deviceName } from '../lib/pairing'
  import { octave } from '../lib/octave'
  import Icon from './Icon.svelte'
  let code = $state('')
  let name = $state(deviceName)
  let busy = $state(false)
  let error = $state('')
  const hello = octave.hello
  function key(d: string) { if (code.length < 6) code += d }
  async function go() {
    if (code.length !== 6) return
    busy = true; error = ''
    try { await pair(code, name) } catch (e) { error = (e as Error).message; code = '' }
    busy = false
  }
</script>

<div class="pair">
  <div class="glass strong panel card">
    <h1>Pair with OCTAVE</h1>
    <p class="muted">This browser is not on the head unit, so it has to be let in once. On the car's screen open Settings, Service, Pair a device, and type the code it shows.</p>
    <div class="code num" aria-label="Code">{#each Array(6) as _, i}<span class:on={i < code.length}>{code[i] ?? ''}</span>{/each}</div>
    <div class="pad">
      {#each ['1', '2', '3', '4', '5', '6', '7', '8', '9'] as d}<button class="btn big" onclick={() => key(d)}>{d}</button>{/each}
      <button class="btn big" aria-label="Delete" onclick={() => (code = code.slice(0, -1))}><Icon name="back" /></button>
      <button class="btn big" onclick={() => key('0')}>0</button>
      <button class="btn big primary" disabled={code.length !== 6 || busy} onclick={go}><Icon name="check" /></button>
    </div>
    <input class="field" bind:value={name} aria-label="This device's name" placeholder="Name this device" onkeydown={(e) => e.key === 'Enter' && go()} />
    {#if error}<p class="bad">{error}</p>{/if}
    <p class="small faint">OCTAVE {$hello?.app?.version ?? ''}, protocol {$hello?.protocolVersion ?? ''}. The token stays in this browser; forget it under Settings, Device.</p>
  </div>
</div>

<style>
  .pair { position: fixed; inset: 0; z-index: 15; display: grid; place-items: center; padding: 16px; overflow-y: auto; background: color-mix(in oklab, var(--bg) 82%, transparent); backdrop-filter: blur(18px); }
  .card { width: min(440px, 100%); display: flex; flex-direction: column; gap: 16px; text-align: center; }
  .code { display: flex; justify-content: center; gap: 8px; }
  .code span { width: 44px; height: 56px; border-radius: 12px; background: var(--control); display: grid; place-items: center; font-size: 28px; font-weight: 700; }
  .code span.on { background: var(--control-on); color: var(--control-on-ink); }
  .pad { display: grid; grid-template-columns: repeat(3, 1fr); gap: 10px; }
  .bad { color: var(--bad); }
</style>
