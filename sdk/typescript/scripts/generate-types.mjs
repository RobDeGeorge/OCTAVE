#!/usr/bin/env node
// Generates src/types.ts from docs/api/*.json: one interface per object
// (properties, methods with argument tuples and return types, signal
// argument tuples), enums as string unions, and `Objects` mapping names to
// them. A member's `deprecated` text becomes a JSDoc @deprecated tag (so an
// editor strikes it through and a lint rule can flag it) and `stability:
// experimental` an @experimental tag, on the member and on a whole object.
// `--check` compares with the committed file instead and exits 1 if it
// differs, so stale types fail `npm test` the way tst_apidocs fails ctest;
// `--stdout` prints it (tests).
import fs from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const here = path.dirname(fileURLToPath(import.meta.url))
const apiDir = process.env.OCTAVE_API_DIR ?? path.resolve(here, '../../../docs/api')
const outFile = path.resolve(here, '../src/types.ts')

const scalar = {
  QString: 'string', QUrl: 'string', QByteArray: 'string', bool: 'boolean',
  int: 'number', uint: 'number', long: 'number', qlonglong: 'number', qulonglong: 'number', float: 'number', double: 'number', qreal: 'number',
  QStringList: 'string[]', QVariantList: 'unknown[]', QVariantMap: 'Record<string, unknown>', QVariant: 'unknown',
  'QObject*': 'unknown', 'QAbstractListModel*': 'ModelSummary', void: 'void', '': 'void',
}

/** The JSDoc tags for a member's (or an object's) annotations. */
function tags(member) {
  const t = []
  if (member.deprecated) t.push(`@deprecated ${member.deprecated}`)
  if (member.stability === 'experimental') t.push('@experimental not covered by the protocol version; may change or go in any release')
  return t
}
function docLine(indent, head, member) {
  const lines = [head, ...tags(member)].filter(Boolean)
  if (!lines.length) return ''
  if (lines.length === 1) return `${indent}/** ${lines[0]} */\n`
  return `${indent}/**\n${lines.map((l) => `${indent} * ${l}`).join('\n')}\n${indent} */\n`
}

function tsType(qt, enums, cls) {
  if (qt in scalar) return scalar[qt]
  const m = /^(?:(\w+)::)?(\w+)$/.exec(qt)
  if (m && enums[m[2]]) return `${cls}_${m[2]}`
  return 'unknown'
}

const index = JSON.parse(fs.readFileSync(path.join(apiDir, 'index.json'), 'utf8'))
const objects = Object.keys(index.objects).sort()
let out = `// Generated from docs/api/*.json by scripts/generate-types.mjs; do not edit.\n// Protocol version ${index.protocolVersion}. Regenerate with \`npm run generate\`.\n\n`
out += `/** What \`get\` returns for a list-model property; the rows come from \`rows\` or a model subscription. */\nexport interface ModelSummary { model: true; count: number; roles: string[] }\n\n`
const names = []
for (const name of objects) {
  const d = JSON.parse(fs.readFileSync(path.join(apiDir, `${name}.json`), 'utf8'))
  const cls = d.class
  const enums = d.enums ?? {}
  for (const [en, keys] of Object.entries(enums)) {
    out += `export type ${cls}_${en} = ${keys.map((k) => JSON.stringify(k)).join(' | ')}\n`
  }
  const iface = cls
  names.push([name, iface])
  out += '\n' + docLine('', `${name} (${cls})`, d) + `export interface ${iface} {\n  properties: {\n`
  for (const p of d.properties) {
    const t = p.enum ? p.enum.map((k) => JSON.stringify(k)).join(' | ') : tsType(p.type, enums, cls)
    out += docLine('    ', `${p.type}${p.writable ? ', writable' : ''}${p.constant ? ', constant' : ''}${p.notify ? `, notify ${p.notify}` : ''}`, p)
    out += `    ${p.name}: ${t}\n`
  }
  out += `  }\n  methods: {\n`
  const byName = new Map()
  for (const m of d.methods) { if (!byName.has(m.name)) byName.set(m.name, []); byName.get(m.name).push(m) }
  for (const [mn, overloads] of byName) {
    const args = overloads.map((m) => `[${m.params.map((a) => `${a.name || 'arg'}: ${tsType(a.type, enums, cls)}`).join(', ')}]`)
    const rets = [...new Set(overloads.map((m) => tsType(m.returns ?? '', enums, cls)))]
    out += docLine('    ', '', overloads.find((m) => m.deprecated || m.stability) ?? {})
    out += `    ${mn}: { args: ${[...new Set(args)].join(' | ')}; returns: ${rets.join(' | ')} }\n`
  }
  out += `  }\n  signals: {\n`
  const sigs = new Map()
  for (const s of d.signals) { if (!sigs.has(s.name)) sigs.set(s.name, []); sigs.get(s.name).push(s) }
  for (const [sn, overloads] of sigs) {
    const tuples = [...new Set(overloads.map((s) => `[${s.params.map((a) => `${a.name || 'arg'}: ${tsType(a.type, enums, cls)}`).join(', ')}]`))]
    out += docLine('    ', '', overloads.find((s) => s.deprecated || s.stability) ?? {})
    out += `    ${sn}: ${tuples.join(' | ')}\n`
  }
  out += `  }\n}\n`
}
out += `\n/** Every object the service exposes, by its name in \`hello\`. */\nexport interface Objects {\n${names.map(([n, i]) => `  ${n}: ${i}`).join('\n')}\n}\n`
out += `\nexport const PROTOCOL_VERSION_OF_TYPES = ${index.protocolVersion}\n`

if (process.argv.includes('--stdout')) {
  process.stdout.write(out)
} else if (process.argv.includes('--check')) {
  const have = fs.existsSync(outFile) ? fs.readFileSync(outFile, 'utf8') : ''
  if (have !== out) {
    console.error(`src/types.ts is stale against ${apiDir}: run \`npm run generate\` and commit the result.`)
    process.exit(1)
  }
  console.log('src/types.ts matches docs/api.')
} else {
  fs.writeFileSync(outFile, out)
  console.log(`wrote ${path.relative(process.cwd(), outFile)}: ${objects.length} objects`)
}
