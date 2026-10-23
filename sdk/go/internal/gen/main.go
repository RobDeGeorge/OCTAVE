// The generator behind sdk/go: docs/api/protocol.json and the object files
// in, protocol_gen.go and objects_gen.go out. Stdlib only. Run with
// `go generate ./...` in sdk/go (gen.go), or directly:
//
//	go run ./internal/gen [-api ../../docs/api] [-out .]
//
// protocol_gen.go: the protocol version, the op names, one struct per op's
// arguments and reply, one struct per event, the error codes, the record
// flags, the streams and the HTTP routes. objects_gen.go: one type per
// service object with a typed Go method for every method the API lists
// (an overload gets its argument count as a suffix) and a Get<Property>
// for every property. A deprecated member gets a "Deprecated:" paragraph,
// the Go convention that editors and staticcheck honour; an experimental
// member an "Experimental:" one.
package main

import (
	"bytes"
	"encoding/json"
	"flag"
	"fmt"
	"go/format"
	"os"
	"path/filepath"
	"sort"
	"strings"
)

func main() {
	api := flag.String("api", "../../docs/api", "docs/api directory")
	out := flag.String("out", ".", "where to write protocol_gen.go and objects_gen.go")
	flag.Parse()
	files, err := generate(*api)
	if err != nil {
		fmt.Fprintln(os.Stderr, "generate:", err)
		os.Exit(1)
	}
	for name, src := range files {
		if err := os.WriteFile(filepath.Join(*out, name), src, 0o644); err != nil {
			fmt.Fprintln(os.Stderr, err)
			os.Exit(1)
		}
	}
}

// ── protocol.json ─────────────────────────────────────────────────────────

type schema struct {
	Ref         string             `json:"$ref"`
	Type        any                `json:"type"`
	Description string             `json:"description"`
	Properties  map[string]*schema `json:"properties"`
	Required    []string           `json:"required"`
	Items       *schema            `json:"items"`
	Enum        []any              `json:"enum"`
	Const       any                `json:"const"`
	OneOf       []*schema          `json:"oneOf"`
	AnyOf       []*schema          `json:"anyOf"`
	AllOf       []*schema          `json:"allOf"`
	Additional  json.RawMessage    `json:"additionalProperties"`
	Default     any                `json:"default"`
}

type spec struct {
	ProtocolVersion int `json:"protocolVersion"`
	Request         struct {
		Schema *schema `json:"schema"`
	} `json:"request"`
	Errors struct {
		Codes map[string]struct {
			Description string `json:"description"`
			Since       int    `json:"since"`
			HTTP        int    `json:"http"`
		} `json:"codes"`
	} `json:"errors"`
	Ops map[string]struct {
		Since       int     `json:"since"`
		Open        bool    `json:"open"`
		Description string  `json:"description"`
		Args        *schema `json:"args"`
		Reply       *schema `json:"reply"`
	} `json:"ops"`
	Events  map[string]json.RawMessage `json:"events"`
	Streams struct {
		Names map[string]struct {
			Format string `json:"format"`
		} `json:"names"`
		Record struct {
			HeaderSize int `json:"headerSize"`
		} `json:"record"`
		Flags map[string]struct {
			Bit         int    `json:"bit"`
			Description string `json:"description"`
		} `json:"flags"`
	} `json:"streams"`
	HTTP struct {
		Routes map[string]json.RawMessage `json:"routes"`
	} `json:"http"`
	Defs map[string]*schema `json:"$defs"`
}

type gen struct {
	spec spec
	buf  bytes.Buffer
}

func pascal(s string) string {
	var b strings.Builder
	up := true
	for _, r := range s {
		switch {
		case r == '_' || r == '-' || r == '.' || r == '/':
			up = true
		case up:
			b.WriteString(strings.ToUpper(string(r)))
			up = false
		default:
			b.WriteRune(r)
		}
	}
	return b.String()
}

func sortedKeys[V any](m map[string]V) []string {
	keys := make([]string, 0, len(m))
	for k := range m {
		keys = append(keys, k)
	}
	sort.Strings(keys)
	return keys
}

func (g *gen) resolve(ref string) (string, *schema) {
	const prefix = "#/$defs/"
	if !strings.HasPrefix(ref, prefix) {
		return "", nil
	}
	name := ref[len(prefix):]
	return name, g.spec.Defs[name]
}

func isScalar(s *schema) bool {
	t, ok := s.Type.(string)
	return ok && t != "object" && t != "array" && s.Properties == nil && s.AllOf == nil && s.OneOf == nil && s.AnyOf == nil
}

// goType renders a schema as a Go type; structs are rendered inline
// (callers name them).
func (g *gen) goType(s *schema, optional bool) string {
	if s == nil {
		return "any"
	}
	if s.Ref != "" {
		name, def := g.resolve(s.Ref)
		if def == nil {
			return "any"
		}
		if isScalar(def) {
			return g.goType(def, optional)
		}
		return pascal(name)
	}
	if s.Const != nil {
		switch s.Const.(type) {
		case bool:
			return "bool"
		case string:
			return "string"
		default:
			return "float64"
		}
	}
	if len(s.OneOf) > 0 || len(s.AnyOf) > 0 {
		return "json.RawMessage"
	}
	if len(s.AllOf) > 0 {
		return g.structOf(g.merge(s), optional)
	}
	var types []string
	switch t := s.Type.(type) {
	case string:
		types = []string{t}
	case []any:
		for _, x := range t {
			types = append(types, x.(string))
		}
	}
	if len(types) == 0 {
		return "any"
	}
	if len(types) == 2 && (types[1] == "null" || types[0] == "null") {
		base := types[0]
		if base == "null" {
			base = types[1]
		}
		return "*" + g.goType(&schema{Type: base, Items: s.Items, Properties: s.Properties}, false)
	}
	if len(types) > 1 {
		return "any"
	}
	switch types[0] {
	case "string":
		return "string"
	case "integer":
		if optional {
			return "*int"
		}
		return "int"
	case "number":
		if optional {
			return "*float64"
		}
		return "float64"
	case "boolean":
		if optional {
			return "*bool"
		}
		return "bool"
	case "null":
		return "any"
	case "array":
		if s.Items == nil {
			return "[]any"
		}
		return "[]" + g.goType(s.Items, false)
	case "object":
		return g.structOf(s, optional)
	}
	return "any"
}

// merge flattens an allOf of object schemas (one $ref deep) into one.
func (g *gen) merge(s *schema) *schema {
	out := &schema{Type: "object", Properties: map[string]*schema{}, Description: s.Description}
	for _, part := range append([]*schema{s}, s.AllOf...) {
		if part.Ref != "" {
			_, part = g.resolve(part.Ref)
		}
		if part == nil {
			continue
		}
		for k, v := range part.Properties {
			out.Properties[k] = v
		}
		out.Required = append(out.Required, part.Required...)
	}
	return out
}

func (g *gen) structOf(s *schema, optional bool) string {
	if len(s.Properties) == 0 {
		if len(s.Additional) > 0 && string(s.Additional) != "true" && string(s.Additional) != "false" {
			var add schema
			if json.Unmarshal(s.Additional, &add) == nil {
				return "map[string]" + g.goType(&add, false)
			}
		}
		return "map[string]any"
	}
	required := map[string]bool{}
	for _, r := range s.Required {
		required[r] = true
	}
	var b strings.Builder
	b.WriteString("struct {\n")
	for _, name := range sortedKeys(s.Properties) {
		p := s.Properties[name]
		if p.Description != "" {
			b.WriteString("\t// " + strings.ReplaceAll(p.Description, "\n", "\n\t// ") + "\n")
		}
		if p.Enum != nil {
			b.WriteString("\t// One of: " + enumList(p.Enum) + "\n")
		}
		tag := name
		if !required[name] {
			tag += ",omitempty"
		}
		b.WriteString(fmt.Sprintf("\t%s %s `json:\"%s\"`\n", pascal(name), g.goType(p, !required[name]), tag))
	}
	b.WriteString("}")
	return b.String()
}

func enumList(values []any) string {
	parts := make([]string, len(values))
	for i, v := range values {
		parts[i] = fmt.Sprintf("%v", v)
	}
	return strings.Join(parts, ", ")
}

func (g *gen) comment(text string) {
	if text == "" {
		return
	}
	for _, line := range wrap(text, 76) {
		g.buf.WriteString("// " + line + "\n")
	}
}

func wrap(text string, width int) []string {
	var lines []string
	for _, para := range strings.Split(text, "\n") {
		line := ""
		for _, word := range strings.Fields(para) {
			if line != "" && len(line)+1+len(word) > width {
				lines = append(lines, line)
				line = word
				continue
			}
			if line != "" {
				line += " "
			}
			line += word
		}
		lines = append(lines, line)
	}
	return lines
}

func (g *gen) protocolFile() ([]byte, error) {
	g.buf.Reset()
	w := func(format string, a ...any) { fmt.Fprintf(&g.buf, format, a...) }
	w("// Code generated from docs/api/protocol.json by internal/gen; DO NOT EDIT.\n// Protocol version %d. Regenerate with `go generate ./...`.\n\npackage octave\n\nimport \"encoding/json\"\n\n", g.spec.ProtocolVersion)
	w("// ProtocolVersion is the version protocol.json describes (docs/api/README.md, Versioning).\nconst ProtocolVersion = %d\n\n", g.spec.ProtocolVersion)

	// $defs
	for _, name := range sortedKeys(g.spec.Defs) {
		def := g.spec.Defs[name]
		if isScalar(def) {
			continue
		}
		if def.Description != "" {
			g.comment(pascal(name) + ": " + def.Description)
		} else {
			g.comment(pascal(name) + " is $defs/" + name + " of protocol.json.")
		}
		t := g.goType(def, false)
		if strings.HasPrefix(t, "struct") {
			w("type %s %s\n\n", pascal(name), t)
		} else {
			w("type %s = %s\n\n", pascal(name), t)
		}
	}

	// ops
	ops := sortedKeys(g.spec.Ops)
	var order []string
	if g.spec.Request.Schema != nil && g.spec.Request.Schema.Properties != nil {
		if op := g.spec.Request.Schema.Properties["op"]; op != nil {
			for _, v := range op.Enum {
				order = append(order, v.(string))
			}
		}
	}
	if len(order) == len(ops) {
		ops = order
	}
	w("// Ops is every op the protocol has, in the spec's order. The server's own list is DescribeAll().Ops.\nvar Ops = []string{")
	for i, op := range ops {
		if i > 0 {
			w(", ")
		}
		w("%q", op)
	}
	w("}\n\n// OpenOps are the ops an untrusted connection may use (protocol 2).\nvar OpenOps = []string{")
	first := true
	for _, op := range ops {
		if g.spec.Ops[op].Open {
			if !first {
				w(", ")
			}
			w("%q", op)
			first = false
		}
	}
	w("}\n\n")
	for _, name := range ops {
		op := g.spec.Ops[name]
		g.comment(fmt.Sprintf("%sArgs are the arguments of the %q op. %s", pascal(name), name, op.Description))
		if op.Since > 0 {
			w("//\n// Since protocol %d.\n", op.Since)
		}
		w("type %sArgs %s\n\n", pascal(name), g.goType(op.Args, false))
		rt := g.goType(op.Reply, false)
		desc := fmt.Sprintf("%sReply is the reply to %q.", pascal(name), name)
		if op.Reply != nil && op.Reply.Description != "" {
			desc += " " + op.Reply.Description
		}
		g.comment(desc)
		if strings.HasPrefix(rt, "struct") {
			w("type %sReply %s\n\n", pascal(name), rt)
		} else {
			w("type %sReply = %s\n\n", pascal(name), rt)
		}
	}

	// errors
	w("// Error codes a failure reply can carry (Error.Code); every other failure is a message only.\nconst (\n")
	for _, code := range sortedKeys(g.spec.Errors.Codes) {
		c := g.spec.Errors.Codes[code]
		g.buf.WriteString("\t// " + strings.Join(wrap(c.Description, 72), "\n\t// ") + "\n")
		w("\tCode%s = %q\n", pascal(code), code)
	}
	w(")\n\n")

	// events
	var eventNames []string
	for name, raw := range g.spec.Events {
		if bytes.HasPrefix(bytes.TrimSpace(raw), []byte("{")) {
			eventNames = append(eventNames, name)
		}
	}
	sort.Strings(eventNames)
	w("// Event names (Event.Kind).\nconst (\n")
	for _, name := range eventNames {
		w("\tEvent%s = %q\n", pascal(name), name)
	}
	w(")\n\n")
	for _, name := range eventNames {
		var ev struct {
			Description string  `json:"description"`
			Schema      *schema `json:"schema"`
		}
		if err := json.Unmarshal(g.spec.Events[name], &ev); err != nil {
			return nil, err
		}
		if ev.Schema != nil {
			delete(ev.Schema.Properties, "event")
		}
		g.comment(fmt.Sprintf("%sEvent is the %q event. %s Decode one with Event.Decode.", pascal(name), name, ev.Description))
		w("type %sEvent %s\n\n", pascal(name), g.goType(ev.Schema, false))
	}

	// streams
	w("// Stream record flags (Record.Flags).\nconst (\n")
	for _, name := range sortedKeys(g.spec.Streams.Flags) {
		f := g.spec.Streams.Flags[name]
		w("\t// %s\n\tFlag%s = %d\n", f.Description, pascal(name), f.Bit)
	}
	w(")\n\n// RecordHeader is the number of bytes before a record's payload.\nconst RecordHeader = %d\n\n", g.spec.Streams.Record.HeaderSize)
	w("// Streams maps every stream (\"object.name\") to its format.\nvar Streams = map[string]string{")
	for i, name := range sortedKeys(g.spec.Streams.Names) {
		if i > 0 {
			w(", ")
		}
		w("%q: %q", name, g.spec.Streams.Names[name].Format)
	}
	w("}\n\n// Routes are the HTTP routes (GET, trusted: a media ticket or a bearer token).\nvar Routes = []string{")
	for i, r := range sortedKeys(g.spec.HTTP.Routes) {
		if i > 0 {
			w(", ")
		}
		w("%q", r)
	}
	w("}\n\nvar _ json.RawMessage\n")
	return format.Source(g.buf.Bytes())
}

// ── the object files ──────────────────────────────────────────────────────

type member struct {
	Name     string `json:"name"`
	Type     string `json:"type"`
	Kind     string `json:"kind"`
	Readable bool   `json:"readable"`
	Writable bool   `json:"writable"`
	Params   []struct {
		Name string `json:"name"`
		Type string `json:"type"`
	} `json:"params"`
	Returns    string   `json:"returns"`
	Enum       []string `json:"enum"`
	Stability  string   `json:"stability"`
	Deprecated string   `json:"deprecated"`
}

type object struct {
	Object     string   `json:"object"`
	Class      string   `json:"class"`
	Stability  string   `json:"stability"`
	Deprecated string   `json:"deprecated"`
	Properties []member `json:"properties"`
	Methods    []member `json:"methods"`
	Signals    []member `json:"signals"`
}

var qtTypes = map[string]string{
	"QString": "string", "QUrl": "string", "QByteArray": "string", "bool": "bool",
	"int": "int", "uint": "int", "long": "int", "qlonglong": "int64", "qulonglong": "uint64",
	"float": "float64", "double": "float64", "qreal": "float64",
	"QStringList": "[]string", "QVariantList": "[]any", "QVariantMap": "map[string]any", "QVariant": "any",
	"QObject*": "any", "QAbstractListModel*": "ModelSummary", "": "",
}

func goQt(t string) string {
	if g, ok := qtTypes[t]; ok {
		return g
	}
	return "any" // an enum (Class::Name) or something new: the JSON value as is
}

func annotations(m member) string {
	var out string
	if m.Deprecated != "" {
		out += "//\n// Deprecated: " + m.Deprecated + "\n"
	}
	if m.Stability == "experimental" {
		out += "//\n// Experimental: not covered by the protocol version; may change or go in any release.\n"
	}
	return out
}

func (g *gen) objectsFile(api string) ([]byte, error) {
	indexBytes, err := os.ReadFile(filepath.Join(api, "index.json"))
	if err != nil {
		return nil, err
	}
	var index struct {
		Objects map[string]string `json:"objects"`
	}
	if err := json.Unmarshal(indexBytes, &index); err != nil {
		return nil, err
	}
	g.buf.Reset()
	w := func(format string, a ...any) { fmt.Fprintf(&g.buf, format, a...) }
	w("// Code generated from docs/api/<object>.json by internal/gen; DO NOT EDIT.\n// Regenerate with `go generate ./...` after `octave --dump-api docs/api`.\n\npackage octave\n\n")
	w("// ObjectNames is every service object, as hello lists them.\nvar ObjectNames = []string{")
	for i, name := range sortedKeys(index.Objects) {
		if i > 0 {
			w(", ")
		}
		w("%q", name)
	}
	w("}\n\n")
	for _, name := range sortedKeys(index.Objects) {
		data, err := os.ReadFile(filepath.Join(api, name+".json"))
		if err != nil {
			return nil, err
		}
		var o object
		if err := json.Unmarshal(data, &o); err != nil {
			return nil, err
		}
		typeName := pascal(name)
		w("// %s is the service object %q (C++ %s); New%s(c) binds it to a client.\n", typeName, name, o.Class, typeName)
		w(annotations(member{Stability: o.Stability, Deprecated: o.Deprecated}))
		w("type %s struct{ c *Client }\n\n", typeName)
		w("// New%s binds %q to c.\nfunc New%s(c *Client) %s { return %s{c} }\n\n", typeName, name, typeName, typeName, typeName)
		w("// Name is the object's name on the wire.\nfunc (%s) Name() string { return %q }\n\n", typeName, name)

		// Go names in use on this type, so a property getter and a method
		// (numBars / getNumBars) never collide: the method gets a Call suffix
		taken := map[string]bool{"Name": true}
		unique := func(n string) string {
			for taken[n] {
				n += "Call"
			}
			taken[n] = true
			return n
		}
		for _, p := range o.Properties {
			if !p.Readable {
				continue
			}
			taken["Get"+pascal(p.Name)] = true
			if p.Writable {
				taken["Set"+pascal(p.Name)] = true
			}
			rt := goQt(p.Type)
			if rt == "" {
				rt = "any"
			}
			w("// Get%s reads the property %q (%s%s).\n", pascal(p.Name), p.Name, p.Type, ifs(p.Writable, ", writable", ""))
			w(annotations(p))
			w("func (o %s) Get%s() (v %s, err error) {\n\terr = o.c.Get(%q, %q, &v)\n\treturn\n}\n\n", typeName, pascal(p.Name), rt, name, p.Name)
			if p.Writable {
				w("// Set%s writes the property %q and returns the value read back.\n", pascal(p.Name), p.Name)
				w(annotations(p))
				w("func (o %s) Set%s(value %s) (v %s, err error) {\n\terr = o.c.Set(%q, %q, value, &v)\n\treturn\n}\n\n", typeName, pascal(p.Name), rt, rt, name, p.Name)
			}
		}

		counts := map[string]int{}
		for _, m := range o.Methods {
			counts[m.Name]++
		}
		for _, m := range o.Methods {
			funcName := pascal(m.Name)
			if counts[m.Name] > 1 {
				funcName = fmt.Sprintf("%s%d", funcName, len(m.Params))
			}
			funcName = unique(funcName)
			var params, args []string
			for i, p := range m.Params {
				pn := p.Name
				if pn == "" {
					pn = fmt.Sprintf("arg%d", i)
				}
				pn = safeIdent(pn)
				params = append(params, pn+" "+goQt(p.Type))
				args = append(args, pn)
			}
			ret := goQt(m.Returns)
			w("// %s calls %s(%s)%s.\n", funcName, m.Name, qtList(m), ifs(m.Returns != "", " -> "+m.Returns, ""))
			w(annotations(m))
			if ret == "" {
				w("func (o %s) %s(%s) error {\n\treturn o.c.Call(%q, %q, nil%s)\n}\n\n", typeName, funcName, strings.Join(params, ", "), name, m.Name, prefixEach(args))
			} else {
				w("func (o %s) %s(%s) (v %s, err error) {\n\terr = o.c.Call(%q, %q, &v%s)\n\treturn\n}\n\n", typeName, funcName, strings.Join(params, ", "), ret, name, m.Name, prefixEach(args))
			}
		}
	}
	return format.Source(g.buf.Bytes())
}

func qtList(m member) string {
	parts := make([]string, len(m.Params))
	for i, p := range m.Params {
		parts[i] = p.Type
	}
	return strings.Join(parts, ", ")
}

func prefixEach(args []string) string {
	if len(args) == 0 {
		return ""
	}
	return ", " + strings.Join(args, ", ")
}

func ifs(cond bool, a, b string) string {
	if cond {
		return a
	}
	return b
}

var goKeywords = map[string]bool{"type": true, "func": true, "map": true, "range": true, "var": true, "const": true, "package": true, "import": true, "go": true, "select": true, "chan": true, "default": true, "case": true, "switch": true, "if": true, "else": true, "for": true, "return": true, "break": true, "continue": true, "defer": true, "fallthrough": true, "goto": true, "interface": true, "struct": true, "string": true, "int": true, "error": true, "v": true, "o": true, "err": true}

func safeIdent(s string) string {
	if goKeywords[s] {
		return s + "_"
	}
	return s
}

func generate(api string) (map[string][]byte, error) {
	data, err := os.ReadFile(filepath.Join(api, "protocol.json"))
	if err != nil {
		return nil, err
	}
	g := &gen{}
	if err := json.Unmarshal(data, &g.spec); err != nil {
		return nil, err
	}
	protocol, err := g.protocolFile()
	if err != nil {
		return nil, fmt.Errorf("protocol_gen.go: %w", err)
	}
	objects, err := g.objectsFile(api)
	if err != nil {
		return nil, fmt.Errorf("objects_gen.go: %w", err)
	}
	return map[string][]byte{"protocol_gen.go": protocol, "objects_gen.go": objects}, nil
}
