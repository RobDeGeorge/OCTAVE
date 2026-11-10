<script lang="ts">
  // Line icons on a 24-grid, one path set per name.
  let { name, size = 24 }: { name: string; size?: number } = $props()
  const paths: Record<string, string> = {
    home: 'M3 11 12 3l9 8M5 10v10h14V10',
    music: 'M9 18V5l12-2v13M9 18a3 3 0 1 1-6 0 3 3 0 0 1 6 0Zm12-2a3 3 0 1 1-6 0 3 3 0 0 1 6 0Z',
    gauge: 'M12 21a9 9 0 1 1 9-9M12 12l5-5M12 12v.01',
    grid: 'M4 4h7v7H4zM13 4h7v7h-7zM4 13h7v7H4zM13 13h7v7h-7z',
    route: 'M6 19a2 2 0 1 0 0-4 2 2 0 0 0 0 4Zm12-10a2 2 0 1 0 0-4 2 2 0 0 0 0 4ZM8 17h7a3 3 0 0 0 0-6h-6a3 3 0 0 1 0-6h7',
    sliders: 'M4 6h10M18 6h2M4 12h2M10 12h10M4 18h12M20 18h0M14 4v4M6 10v4M16 16v4',
    more: 'M12 6h.01M12 12h.01M12 18h.01',
    play: 'M7 5v14l12-7z',
    pause: 'M7 5h4v14H7zM13 5h4v14h-4z',
    prev: 'M18 6v12L9 12zM6 6v12',
    next: 'M6 6v12l9-6zM18 6v12',
    shuffle: 'M16 4h4v4M4 20 20 4M4 4l5 5M15 15l5 5M16 20h4v-4',
    volume: 'M4 10v4h3l5 4V6L7 10zM16 9a4 4 0 0 1 0 6M18.5 6.5a7.5 7.5 0 0 1 0 11',
    mute: 'M4 10v4h3l5 4V6L7 10zM16 9l5 6M21 9l-5 6',
    sun: 'M12 3v2M12 19v2M3 12h2M19 12h2M5.6 5.6l1.4 1.4M17 17l1.4 1.4M5.6 18.4 7 17M17 7l1.4-1.4M12 8a4 4 0 1 0 0 8 4 4 0 0 0 0-8Z',
    moon: 'M20 14.5A8 8 0 0 1 9.5 4a8 8 0 1 0 10.5 10.5Z',
    plus: 'M12 5v14M5 12h14',
    x: 'M6 6l12 12M18 6 6 18',
    check: 'M5 12l5 5L20 7',
    trash: 'M4 7h16M9 7V4h6v3M6 7l1 13h10l1-13M10 11v6M14 11v6',
    copy: 'M8 8h12v12H8zM4 16V4h12',
    share: 'M12 3v12M8 7l4-4 4 4M5 13v7h14v-7',
    download: 'M12 3v12M8 11l4 4 4-4M5 19h14',
    back: 'M15 5l-7 7 7 7',
    forward: 'M9 5l7 7-7 7',
    search: 'M10.5 4a6.5 6.5 0 1 0 0 13 6.5 6.5 0 0 0 0-13ZM20 20l-4.5-4.5',
    link: 'M10 14 14 10M8 12l-2 2a3 3 0 0 0 4 4l2-2M16 12l2-2a3 3 0 0 0-4-4l-2 2',
    unlink: 'M10 14 14 10M8 12l-2 2a3 3 0 0 0 4 4l2-2M16 12l2-2a3 3 0 0 0-4-4l-2 2M3 3l18 18',
    bluetooth: 'M7 7l10 10-5 4V3l5 4L7 17',
    phone: 'M8 3h8a2 2 0 0 1 2 2v14a2 2 0 0 1-2 2H8a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2ZM11 18h2',
    edit: 'M4 20h4L19 9l-4-4L4 16zM13 7l4 4',
    refresh: 'M20 12a8 8 0 1 1-2.3-5.7M20 4v5h-5',
    warn: 'M12 4 2.5 20h19zM12 10v4M12 17v.01',
    up: 'M6 15l6-6 6 6',
    down: 'M6 9l6 6 6-6',
    folder: 'M3 7h6l2 2h10v10H3z',
    file: 'M6 3h8l4 4v14H6zM14 3v4h4',
    bolt: 'M13 2 4 14h7l-1 8 9-12h-7z',
    drag: 'M9 6h.01M15 6h.01M9 12h.01M15 12h.01M9 18h.01M15 18h.01',
    car: 'M5 16l1.5-5h11L19 16M4 16h16v3H4zM7 19v1M17 19v1M7.5 16h.01M16.5 16h.01',
    menu: 'M4 7h16M4 12h16M4 17h16',
    stop: 'M6 6h12v12H6z',
    clock: 'M12 3a9 9 0 1 0 0 18 9 9 0 0 0 0-18ZM12 7v5l3 2',
    list: 'M8 6h12M8 12h12M8 18h12M4 6h.01M4 12h.01M4 18h.01',
    activity: 'M3 12h4l3-8 4 16 3-8h4',
    power: 'M12 3v9M6.3 6.3a8 8 0 1 0 11.4 0',
    info: 'M12 3a9 9 0 1 0 0 18 9 9 0 0 0 0-18ZM12 11v5M12 8v.01',
  }
</script>

<svg width={size} height={size} viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2"
     stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
  <path d={paths[name] ?? paths.info} />
</svg>
