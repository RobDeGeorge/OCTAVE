#!/usr/bin/env node
// Generates src/protocol.ts from docs/api/protocol.json: the envelope the
// client speaks. One interface per op's arguments and reply (`HelloArgs`,
// `HelloReply`, ...) and `Ops` mapping op names to them, so
// `client.request('hello', {...})` is typed; one interface per event and
// the `ServiceEvent` union; the reply envelope and `ErrorCode`; the object
// description shape (`ObjectDescription`, what `describe` returns); the
// stream record constants and the HTTP route names. Descriptions become
// JSDoc, `since` an @since tag.
//
// `--check` compares with the committed file and exits 1 if it differs
// (npm test), `--stdout` prints instead of writing (tests). The schemas
// use the 2020-12 subset docs/api/README.md ("Protocol spec") names.
import fs from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const here = path.dirname(fileURLToPath(import.meta.url))
const specFile = process.env.OCTAVE_PROTOCOL_JSON ?? path.join(process.env.OCTAVE_API_DIR ?? path.resolve(here, '../../../docs/api'), 'protocol.json')
const outFile = path.resolve(here, '../src/protocol.ts')
const spec = JSON.parse(fs.readFileSync(specFile, 'utf8'))

const pascal = (s) => s.replace(/(^|[^A-Za-z0-9])([a-z0-9])/g, (_, __, c) => c.toUpperCase())
const ident = (s) => /^[A-Za-z_$][\w$]*$/.test(s) ? s : JSON.stringify(s)
const resolve = (ref) => ref.slice(2).split('/').reduce((n, k) => n[k], spec)
const isScalarDef = (s) => !s.properties && !s.allOf && !s.oneOf && !s.anyOf && typeof s.type === 'string' && s.type !== 'object' && s.type !== 'array'

/** A JSON Schema (the spec's subset) as a TypeScript type expression. */
function ts(schema, indent = '') {
  if (schema === undefined || schema === true || Object.keys(schema).length === 0) return 'Json'
  if (Object.keys(schema).every((k) => k === 'description')) return 'Json'
  if (schema.$ref) {
    const def = resolve(schema.$ref)
    return isScalarDef(def) ? ts(def, indent) : pascal(schema.$ref.split('/').pop())
  }
  if ('const' in schema) return JSON.stringify(schema.const)
  if (schema.enum) return schema.enum.map((v) => JSON.stringify(v)).join(' | ')
  if (schema.oneOf || schema.anyOf) return (schema.oneOf ?? schema.anyOf).map((s) => ts(s, indent)).join(' | ')
  if (schema.allOf) return schema.allOf.map((s) => ts(s, indent)).join(' & ')
  const types = Array.isArray(schema.type) ? schema.type : [schema.type]
  return types.map((t) => {
    switch (t) {
      case 'string': return 'string'
      case 'integer': case 'number': return 'number'
      case 'boolean': return 'boolean'
      case 'null': return 'null'
      case 'array': return schema.items ? `${wrap(ts(schema.items, indent))}[]` : 'Json[]'
      case 'object': return object(schema, indent)
      default: return 'Json'
    }
  }).join(' | ')
}
const wrap = (t) => /[ |&]/.test(t) ? `(${t})` : t

function object(schema, indent) {
  const props = schema.properties ?? {}
  const extra = schema.additionalProperties
  if (!Object.keys(props).length) return extra && extra !== true ? `Record<string, ${ts(extra, indent)}>` : 'Record<string, Json>'
  const inner = indent + '  '
  const req = new Set(schema.required ?? [])
  let out = '{\n'
  for (const [name, sub] of Object.entries(props)) {
    out += doc(sub, inner)
    out += `${inner}${ident(name)}${req.has(name) ? '' : '?'}: ${ts(sub, inner)}\n`
  }
  if (extra && extra !== true) out += `${inner}[key: string]: ${ts(extra, inner)}\n`
  return out + indent + '}'
}

/** A JSDoc block for a schema's description (and tags), or nothing. */
function doc(schema, indent, tags = []) {
  const lines = []
  if (schema?.description) lines.push(...schema.description.split('\n'))
  if (schema?.default !== undefined) lines.push(`@default ${JSON.stringify(schema.default)}`)
  lines.push(...tags)
  if (!lines.length) return ''
  if (lines.length === 1) return `${indent}/** ${lines[0].replace(/\*\//g, '* /')} */\n`
  return `${indent}/**\n${lines.map((l) => `${indent} * ${l.replace(/\*\//g, '* /')}`).join('\n')}\n${indent} */\n`
}

let out = `// Generated from docs/api/protocol.json by scripts/generate-protocol.mjs; do not edit.\n// Protocol version ${spec.protocolVersion}. Regenerate with \`npm run generate\`.\n\n`
out += `export type Json = null | boolean | number | string | Json[] | { [k: string]: Json }\n\n`
out += `/** The protocol version protocol.json describes (docs/api/README.md, Versioning). */\nexport const PROTOCOL_VERSION = ${spec.protocolVersion}\n\n`

// $defs that are not plain scalars (those are inlined where used)
for (const [name, def] of Object.entries(spec.$defs)) {
  if (isScalarDef(def)) continue
  const type = ts(def)
  out += doc(def, '')
  out += type.startsWith('{') ? `export interface ${pascal(name)} ${type}\n\n` : `export type ${pascal(name)} = ${type}\n\n`
}

// Ops
const opNames = Object.keys(spec.ops)
out += `/** Every op the protocol has. The server's own list is \`describe\` (no object) → \`ops\`. */\nexport const OPS = ${JSON.stringify(opNames)} as const\nexport type Op = typeof OPS[number]\n`
out += `/** The ops an untrusted connection may use (protocol 2). */\nexport const OPEN_OPS = ${JSON.stringify(opNames.filter((n) => spec.ops[n].open))} as const\n\n`
for (const [name, op] of Object.entries(spec.ops)) {
  const tags = op.since ? [`@since protocol ${op.since}`] : []
  out += doc({ description: op.description }, '', tags)
  const args = ts(op.args)
  out += args.startsWith('{') ? `export interface ${pascal(name)}Args ${args}\n` : `export type ${pascal(name)}Args = ${args}\n`
  const reply = ts(op.reply)
  out += doc(op.reply?.description ? { description: op.reply.description } : undefined, '')
  out += reply.startsWith('{') ? `export interface ${pascal(name)}Reply ${reply}\n\n` : `export type ${pascal(name)}Reply = ${reply}\n\n`
}
out += `/** Each op's arguments and reply, for a typed \`request\`. */\nexport interface Ops {\n`
for (const name of opNames) out += `  ${name}: { args: ${pascal(name)}Args; reply: ${pascal(name)}Reply }\n`
out += `}\n\n`

// Envelope
out += `/** What a request looks like on the wire: \`op\`, an optional \`id\` the reply echoes, and the op's arguments. */\nexport type Request<O extends Op = Op> = { op: O; id?: Json } & Ops[O]['args']\n`
out += `export type Reply = OkReply | ErrorReply\n`
out += `/** The \`code\`s a failure reply can carry; every other failure is a message only. */\nexport type ErrorCode = ${Object.keys(spec.errors.codes).map((c) => JSON.stringify(c)).join(' | ')}\n`
out += `export const ERROR_CODES: Record<ErrorCode, { description: string; since?: number; http?: number }> = ${JSON.stringify(spec.errors.codes, null, 2)}\n\n`

// Events
const eventNames = Object.keys(spec.events).filter((k) => k !== 'description')
for (const name of eventNames) {
  const ev = spec.events[name]
  out += doc({ description: ev.description }, '')
  out += `export interface ${pascal(name)}Event ${ts(ev.schema)}\n\n`
}
out += `/** ${spec.events.description} */\nexport type ServiceEvent = ${eventNames.map((n) => `${pascal(n)}Event`).join(' | ')}\n`
out += `export type EventName = ServiceEvent['event']\n\n`

// Streams
out += `/** Media stream record flags (streams.flags). */\n`
for (const [name, f] of Object.entries(spec.streams.flags)) out += `/** ${f.description} */\nexport const F_${name.toUpperCase()} = ${f.bit}\n`
out += `/** Bytes before a record's payload. */\nexport const RECORD_HEADER = ${spec.streams.record.headerSize}\n`
out += `/** The streams the service has, as \`object.name\`, with their format. */\nexport const STREAMS = ${JSON.stringify(Object.fromEntries(Object.entries(spec.streams.names).map(([n, s]) => [n, s.format])))} as const\n`
out += `export type StreamName = keyof typeof STREAMS\n\n`

// HTTP
out += `/** The HTTP routes (GET, trusted: a media ticket or a bearer token). */\nexport const ROUTES = ${JSON.stringify(Object.keys(spec.http.routes))} as const\nexport type Route = typeof ROUTES[number]\n`

if (process.argv.includes('--stdout')) {
  process.stdout.write(out)
} else if (process.argv.includes('--check')) {
  const have = fs.existsSync(outFile) ? fs.readFileSync(outFile, 'utf8') : ''
  if (have !== out) {
    console.error(`src/protocol.ts is stale against ${specFile}: run \`npm run generate\` and commit the result.`)
    process.exit(1)
  }
  console.log('src/protocol.ts matches protocol.json.')
} else {
  fs.writeFileSync(outFile, out)
  console.log(`wrote ${path.relative(process.cwd(), outFile)}: ${opNames.length} ops, ${eventNames.length} events`)
}
