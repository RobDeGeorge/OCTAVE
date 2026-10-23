// Compiled with --noEmit by `npm test`: the facade must accept the right
// calls and reject the wrong ones (the @ts-expect-error lines fail the
// build if they stop being errors).
import { OctaveClient, obj, type OBDManager, type Objects } from '../src/index.js'

const client = new OctaveClient()
const vol = obj(client, 'volumeController')
const ok1: Promise<void> = vol.call('applyVolume', 40)
const ok2: Promise<number> = vol.get('percent')
const ok3: Promise<boolean> = vol.set('muted', true)
// @ts-expect-error applyVolume takes a number
vol.call('applyVolume', 'loud')
// @ts-expect-error no such method
vol.call('explode')
// @ts-expect-error no such property
vol.get('loudness')

const obd = obj(client, 'obdManager')
const ok4: Promise<OBDManager['properties']['connectionState']> = obd.get('connectionState')
const ok5: Promise<void> = obd.call('setParameterDemand', 'me', ['RPM'])
obd.signal('dtcClearResult', (success, message) => { const s: boolean = success; const m: string = message; void s; void m })
// @ts-expect-error the second argument is a string list
obd.call('setParameterDemand', 'me', 'RPM')

const trips = obj(client, 'tripRecorder')
trips.call('requestSeries', 1, 'RPM')
trips.call('requestSeries', 1, 'RPM', 360, 0, 10)
// @ts-expect-error no overload with three numbers and no param id
trips.call('requestSeries', 1, 2, 3)

const names: (keyof Objects)[] = ['nowPlaying', 'settingsManager']
void names; void ok1; void ok2; void ok3; void ok4; void ok5
