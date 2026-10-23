<script lang="ts">
  // One dashboard cell: binds a widget type to its PID (through the catalog
  // and the shared live map) and hands it display-unit values and props.
  import type { Cell } from '../lib/widgets'
  import { catalog, toDisplay, nowPlaying, position, volume, mmss, act, artSrc, type ParamInfo } from '../lib/stores'
  import { octave } from '../lib/octave'
  import ArcGauge from './ArcGauge.svelte'
  import CircularGauge from './CircularGauge.svelte'
  import BarGauge from './BarGauge.svelte'
  import LinearGauge from './LinearGauge.svelte'
  import DigitalReadout from './DigitalReadout.svelte'
  import SparklineGauge from './SparklineGauge.svelte'
  import WarningLight from './WarningLight.svelte'
  import GForceGauge from './GForceGauge.svelte'
  import CompassGauge from './CompassGauge.svelte'
  import Art from '../ui/Art.svelte'
  import Icon from '../ui/Icon.svelte'

  let { cell, live }: { cell: Cell; live: Record<string, number> } = $props()
  const p = $derived(cell.props ?? {})
  const info: ParamInfo | undefined = $derived(cell.paramId ? $catalog.get(cell.paramId) : undefined)
  const raw = $derived(cell.paramId ? live[cell.paramId] : undefined)
  const value = $derived(raw === undefined ? undefined : toDisplay(info, raw))
  // paramUnit props are stored in the PID's native unit
  const conv = (k: string, def: number) => (typeof p[k] === 'number' ? toDisplay(info, p[k] as number) : def)
  const num = (k: string, def: number) => (typeof p[k] === 'number' ? (p[k] as number) : def)
  const bool = (k: string, def: boolean) => (typeof p[k] === 'boolean' ? (p[k] as boolean) : def)
  const str = (k: string, def: string) => (typeof p[k] === 'string' ? (p[k] as string) : def)
  const title = $derived(str('label', '') || str('title', '') || info?.title || cell.paramId || '')
  const unit = $derived(str('unit', '') || info?.unit || '')
  const min = $derived(conv('min', info?.min ?? 0)), max = $derived(conv('max', info?.max ?? 100))
  const decimals = $derived(num('decimals', info?.decimals ?? 0))
</script>

{#if cell.type === 'ArcGauge'}
  <ArcGauge {value} {min} {max} {title} {unit} {decimals} redline={conv('redlineStart', NaN)} color={str('fillColor', '')} showReadout={bool('showReadout', true)} showTicks={bool('showTicks', true)} ticks={num('majorTickCount', 5)} />
{:else if cell.type === 'CircularGauge'}
  <CircularGauge {value} {min} {max} {title} {unit} {decimals} redline={conv('redlineStart', NaN)} color={str('fillColor', '')} needleColor={str('needleColor', '')}
    showNeedle={bool('showNeedle', false)} showTicks={bool('showTicks', true)} showReadout={bool('showCenterReadout', true)} majorTicks={num('majorTickCount', 9)}
    minorPerMajor={num('minorTicksPerMajor', 5)} startAngle={num('startAngle', 135)} sweepAngle={num('sweepAngle', 270)} />
{:else if cell.type === 'BarGauge'}
  <BarGauge {value} {min} {max} {title} {unit} {decimals} warnAbove={conv('warnAbove', NaN)} color={str('fillColor', '')} orientation={str('orientation', 'horizontal')} showLabel={bool('showLabel', true)} showValue={bool('showValue', true)} />
{:else if cell.type === 'LinearGauge'}
  <LinearGauge {value} {min} {max} {title} {unit} {decimals} color={str('fillColor', '')} showTicks={bool('showTicks', true)} majorTicks={num('majorTickCount', 5)} />
{:else if cell.type === 'DigitalReadout'}
  <DigitalReadout {value} {title} {unit} {decimals} color={str('valueColor', '')} showTitle={bool('showTitle', true)} showUnit={bool('showUnit', true)} padDigits={num('padDigits', 0)} valueScale={num('valueScale', 3.5)} />
{:else if cell.type === 'SparklineGauge'}
  <SparklineGauge {value} {min} {max} {title} {unit} {decimals} color={str('lineColor', '')} autoScale={bool('autoScale', false)} fillBelow={bool('fillBelow', true)} showHeader={bool('showHeader', true)} maxSamples={num('maxSamples', 60)} sampleIntervalMs={num('sampleIntervalMs', 500)} />
{:else if cell.type === 'WarningLight'}
  <WarningLight {value} label={str('label', '')} {title} color={str('activeColor', '')} triggerAbove={conv('triggerAbove', NaN)} triggerBelow={conv('triggerBelow', NaN)} pulse={bool('pulse', false)} />
{:else if cell.type === 'GForceGauge'}
  <GForceGauge lateral={live.LATERAL_G ?? 0} longitudinal={live.LONGITUDINAL_G ?? 0} maxG={num('maxG', 1.5)} color={str('dotColor', '')} showTrail={bool('showTrail', true)} showValue={bool('showValue', true)} />
{:else if cell.type === 'CompassGauge'}
  <CompassGauge heading={live.HEADING ?? 0} color={str('northColor', '')} showDegrees={bool('showDegrees', true)} />
{:else if cell.type === 'NowPlayingWidget'}
  <div class="np" style:--c={str('progressColor', '') || 'var(--accent)'}>
    {#if bool('showArt', true)}<Art title={$nowPlaying.title} artist={$nowPlaying.artist} url={$artSrc} size={64} />{/if}
    <div class="txt">
      <div class="t">{$nowPlaying.hasTrack ? $nowPlaying.title : 'Nothing playing'}</div>
      {#if bool('showArtist', true)}<div class="a">{$nowPlaying.artist}</div>{/if}
      {#if bool('showProgress', true)}<div class="pr"><div style="width:{$nowPlaying.durationMs ? ($position / $nowPlaying.durationMs) * 100 : 0}%"></div></div>
        <div class="tm num">{mmss($position)} / {mmss($nowPlaying.durationMs)}</div>{/if}
    </div>
  </div>
{:else if cell.type === 'MediaControlsWidget'}
  <div class="mc" style:--c={str('accentColor', '') || 'var(--control-on)'}>
    <button class="btn icon" aria-label="Previous" onclick={() => act(octave.call('nowPlaying', 'previous'))}><Icon name="prev" /></button>
    <button class="btn icon primary" aria-label={$nowPlaying.playing ? 'Pause' : 'Play'} onclick={() => act(octave.call('nowPlaying', 'togglePlay'))}><Icon name={$nowPlaying.playing ? 'pause' : 'play'} /></button>
    <button class="btn icon" aria-label="Next" onclick={() => act(octave.call('nowPlaying', 'next'))}><Icon name="next" /></button>
    <button class="btn icon" class:on={$volume.muted} aria-label="Mute" onclick={() => act(octave.set('volumeController', 'muted', !$volume.muted))}><Icon name={$volume.effectivelyMuted ? 'mute' : 'volume'} /></button>
  </div>
{:else}
  <div class="unknown muted small">{cell.type}<br />needs a newer OCTAVE</div>
{/if}

<style>
  .np { display: flex; gap: 12px; align-items: center; width: 100%; height: 100%; min-width: 0; }
  .txt { min-width: 0; flex: 1; display: flex; flex-direction: column; gap: 3px; }
  .t { font-weight: 700; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
  .a { font-size: 14px; color: var(--ink-2); overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
  .pr { height: 5px; border-radius: 3px; background: var(--track); overflow: hidden; } .pr div { height: 100%; background: var(--c); }
  .tm { font-size: 12px; color: var(--ink-3); }
  .mc { display: flex; gap: 8px; align-items: center; justify-content: center; width: 100%; height: 100%; flex-wrap: wrap; }
  .mc .primary { background: var(--c); }
  .unknown { display: grid; place-items: center; text-align: center; width: 100%; height: 100%; }
</style>
