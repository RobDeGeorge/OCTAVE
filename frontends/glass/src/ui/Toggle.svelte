<script lang="ts">
  let { on, label = '', onchange }: { on: boolean; label?: string; onchange: (v: boolean) => void } = $props()
</script>

<button class="toggle" class:on role="switch" aria-checked={on} aria-label={label} onclick={() => onchange(!on)}>
  <span class="knob"></span>
</button>

<style>
  .toggle { width: 68px; height: 40px; border-radius: 999px; background: var(--track); position: relative; flex: none;
            transition: background .22s var(--ease); }
  .toggle.on { background: var(--accent); }
  .knob { position: absolute; top: 4px; left: 4px; width: 32px; height: 32px; border-radius: 50%; background: #fff;
          box-shadow: 0 2px 6px rgba(0,0,0,.25); transition: transform .22s var(--ease); }
  .on .knob { transform: translateX(28px); }
</style>
