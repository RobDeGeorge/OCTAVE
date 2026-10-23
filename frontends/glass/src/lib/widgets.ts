// The widget catalog, mirroring frontend/dashboards/WidgetCatalog.qml:
// which types exist, their default span, the PID kinds they accept, and the
// props the editor offers. Specs store paramUnit props in the PID's native
// unit; the renderer converts them with the catalog's displayScale / offset.
export interface EditableProp { key: string; label: string; kind: 'bool' | 'int' | 'real' | 'enum' | 'string'; def: unknown; min?: number; max?: number; options?: string[]; paramUnit?: boolean }
export interface WidgetMeta {
  type: string; displayName: string; glyph: string; defaultColSpan: number; defaultRowSpan: number
  supportedKinds: string[]; labelProps: boolean; rangeProps: boolean; colorProps: string[]; editableProps: EditableProp[]
}
const b = (key: string, label: string, def: boolean): EditableProp => ({ key, label, kind: 'bool', def })
const i = (key: string, label: string, def: number, min: number, max: number): EditableProp => ({ key, label, kind: 'int', def, min, max })
const r = (key: string, label: string, def: number | null, min?: number, max?: number, paramUnit = false): EditableProp => ({ key, label, kind: 'real', def, min, max, paramUnit })

export const widgets: WidgetMeta[] = [
  { type: 'CircularGauge', displayName: 'Circular', glyph: '◯', defaultColSpan: 4, defaultRowSpan: 4, supportedKinds: ['*'], labelProps: true, rangeProps: true, colorProps: ['fillColor', 'needleColor'],
    editableProps: [b('showNeedle', 'Needle', false), b('showTicks', 'Ticks', true), b('showCenterReadout', 'Readout', true), r('redlineStart', 'Redline start', null, undefined, undefined, true),
      i('decimals', 'Decimals', 0, 0, 4), i('majorTickCount', 'Major ticks', 9, 2, 20), i('minorTicksPerMajor', 'Minor per major', 5, 0, 10), r('startAngle', 'Start angle', 135, 0, 360), r('sweepAngle', 'Sweep angle', 270, 30, 360)] },
  { type: 'ArcGauge', displayName: 'Arc', glyph: '◠', defaultColSpan: 6, defaultRowSpan: 3, supportedKinds: ['*'], labelProps: true, rangeProps: true, colorProps: ['fillColor', 'needleColor'],
    editableProps: [b('showNeedle', 'Needle', false), b('showTicks', 'Ticks', true), b('showReadout', 'Readout', true), r('redlineStart', 'Redline start', null, undefined, undefined, true),
      i('decimals', 'Decimals', 0, 0, 4), i('majorTickCount', 'Major ticks', 5, 2, 20), i('minorTicksPerMajor', 'Minor per major', 4, 0, 10)] },
  { type: 'BarGauge', displayName: 'Bar', glyph: '▮', defaultColSpan: 3, defaultRowSpan: 2, supportedKinds: ['percentage', 'temperature', 'numeric', 'pressure', 'voltage'], labelProps: true, rangeProps: true, colorProps: ['fillColor'],
    editableProps: [{ key: 'orientation', label: 'Orientation', kind: 'enum', def: 'horizontal', options: ['horizontal', 'vertical'] }, r('warnAbove', 'Warn above', null, undefined, undefined, true),
      b('showLabel', 'Label', true), b('showValue', 'Value', true), i('decimals', 'Decimals', 0, 0, 4)] },
  { type: 'LinearGauge', displayName: 'Linear', glyph: '↔', defaultColSpan: 4, defaultRowSpan: 2, supportedKinds: ['bidirectional'], labelProps: true, rangeProps: true, colorProps: ['fillColor'],
    editableProps: [b('showTicks', 'Ticks', true), i('decimals', 'Decimals', 0, 0, 4), i('majorTickCount', 'Major ticks', 5, 2, 20), i('minorTicksPerMajor', 'Minor per major', 4, 0, 10)] },
  { type: 'DigitalReadout', displayName: 'Digital', glyph: '88', defaultColSpan: 4, defaultRowSpan: 2, supportedKinds: ['*'], labelProps: true, rangeProps: false, colorProps: ['valueColor'],
    editableProps: [b('showTitle', 'Title', true), b('showUnit', 'Unit', true), i('decimals', 'Decimals', 0, 0, 4), i('padDigits', 'Pad digits', 0, 0, 8), r('valueScale', 'Digit size', 3.5, 0.5, 12)] },
  { type: 'SparklineGauge', displayName: 'Sparkline', glyph: '∿', defaultColSpan: 6, defaultRowSpan: 2, supportedKinds: ['*'], labelProps: true, rangeProps: true, colorProps: ['lineColor'],
    editableProps: [b('autoScale', 'Auto-scale', false), b('fillBelow', 'Area fill', true), b('showHeader', 'Header', true), i('decimals', 'Decimals', 0, 0, 4), i('maxSamples', 'Samples', 60, 10, 300), i('sampleIntervalMs', 'Sample (ms)', 500, 100, 5000)] },
  { type: 'WarningLight', displayName: 'Warning', glyph: '⚠', defaultColSpan: 2, defaultRowSpan: 2, supportedKinds: ['*'], labelProps: false, rangeProps: false, colorProps: ['activeColor'],
    editableProps: [{ key: 'label', label: 'Light text', kind: 'string', def: '' }, r('triggerAbove', 'Trigger above', null, undefined, undefined, true), r('triggerBelow', 'Trigger below', null, undefined, undefined, true), b('pulse', 'Pulse', false)] },
  { type: 'GForceGauge', displayName: 'G-Force', glyph: '◎', defaultColSpan: 4, defaultRowSpan: 4, supportedKinds: [], labelProps: false, rangeProps: false, colorProps: ['dotColor'],
    editableProps: [r('maxG', 'Max G', 1.5, 0.2, 5), b('showTrail', 'Trail', true), b('showValue', 'Readout', true)] },
  { type: 'CompassGauge', displayName: 'Compass', glyph: 'N', defaultColSpan: 4, defaultRowSpan: 4, supportedKinds: [], labelProps: false, rangeProps: false, colorProps: ['northColor'],
    editableProps: [b('showDegrees', 'Degrees', true)] },
  { type: 'NowPlayingWidget', displayName: 'Now Playing', glyph: '♪', defaultColSpan: 6, defaultRowSpan: 2, supportedKinds: [], labelProps: false, rangeProps: false, colorProps: ['progressColor'],
    editableProps: [b('showArt', 'Album art', true), b('showArtist', 'Artist', true), b('showProgress', 'Progress', true)] },
  { type: 'MediaControlsWidget', displayName: 'Media Keys', glyph: '▶', defaultColSpan: 4, defaultRowSpan: 2, supportedKinds: [], labelProps: false, rangeProps: false, colorProps: ['accentColor'], editableProps: [] },
]
export const colorSwatches = ['#00BFFF', '#2ECC71', '#F1C40F', '#E67E22', '#E74C3C', '#FF00AA', '#9C27B0', '#FFFFFF']
export function metaFor(type: string): WidgetMeta | undefined { return widgets.find((w) => w.type === type) }
export function acceptsKind(meta: WidgetMeta, kind: string): boolean { return meta.supportedKinds.includes('*') || meta.supportedKinds.includes(kind) }
/** Props of a type whose values are in the PID's unit. */
export function paramUnitProps(type: string): string[] {
  const m = metaFor(type); if (!m) return []
  return [...(m.rangeProps ? ['min', 'max'] : []), ...m.editableProps.filter((p) => p.paramUnit).map((p) => p.key)]
}
/** The PIDs a self-binding widget reads. */
export function selfBound(type: string): string[] {
  return type === 'GForceGauge' ? ['LATERAL_G', 'LONGITUDINAL_G', 'ACCEL_MAG'] : type === 'CompassGauge' ? ['HEADING'] : []
}

export interface Cell { type: string; paramId?: string; col: number; row: number; colSpan?: number; rowSpan?: number; props?: Record<string, string | number | boolean> }
export interface Spec { schema: 1; id?: string; label: string; gridColumns?: number; gridRows?: number; margins?: number; spacing?: number; cells: Cell[] }
