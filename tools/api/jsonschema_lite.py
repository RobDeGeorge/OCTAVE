"""A small JSON Schema (2020-12 subset) validator for docs/api/protocol.json.

The spec's schemas use only: type (one or a list), properties, required,
additionalProperties (bool or schema), items, enum, const, minimum,
maximum, minLength, pattern, default (ignored), description (ignored),
oneOf, anyOf, allOf, and `$ref` to a JSON pointer inside the same
document ("#/$defs/row"). Nothing else is needed, and the stdlib has no
validator, so this is the one the tests and tools use.

    from jsonschema_lite import Validator
    v = Validator(spec)                       # the whole protocol.json
    v.validate(value, spec["ops"]["hello"]["reply"])          # raises SchemaError
    v.validate(value, schema, strict=True)    # objects may not carry unknown keys

`strict` treats every object schema that lists `properties` as if it had
additionalProperties false, so a test against the live server finds fields
the spec does not describe (a client never validates strictly: additions
are allowed without a version bump).
"""
import re


class SchemaError(ValueError):
    pass


_TYPES = {
    "object": lambda v: isinstance(v, dict),
    "array": lambda v: isinstance(v, list),
    "string": lambda v: isinstance(v, str),
    "integer": lambda v: isinstance(v, int) and not isinstance(v, bool),
    "number": lambda v: isinstance(v, (int, float)) and not isinstance(v, bool),
    "boolean": lambda v: isinstance(v, bool),
    "null": lambda v: v is None,
}


class Validator:
    def __init__(self, document):
        self.document = document

    def resolve(self, ref):
        if not ref.startswith("#/"):
            raise SchemaError(f"only local refs are supported: {ref}")
        node = self.document
        for part in ref[2:].split("/"):
            part = part.replace("~1", "/").replace("~0", "~")
            if isinstance(node, list):
                node = node[int(part)]
            elif part in node:
                node = node[part]
            else:
                raise SchemaError(f"unresolved ref {ref}")
        return node

    def validate(self, value, schema, strict=False, path="$", known=frozenset()):
        """Raise SchemaError naming the path of the first violation. `known`
        is the set of object keys an enclosing allOf already accounts for
        (strict mode only)."""
        if schema is True or schema == {}:
            return
        if schema is False:
            raise SchemaError(f"{path}: nothing is allowed here")
        if "$ref" in schema:
            self.validate(value, self.resolve(schema["$ref"]), strict, path, known)
            rest = {k: v for k, v in schema.items() if k != "$ref"}
            if rest:
                self.validate(value, rest, strict, path, known)
            return
        if "const" in schema and value != schema["const"]:
            raise SchemaError(f"{path}: {value!r} is not {schema['const']!r}")
        if "enum" in schema and value not in schema["enum"]:
            raise SchemaError(f"{path}: {value!r} not in {schema['enum']}")
        if "type" in schema:
            types = schema["type"] if isinstance(schema["type"], list) else [schema["type"]]
            if not any(_TYPES[t](value) for t in types):
                raise SchemaError(f"{path}: {value!r} is not {' or '.join(types)}")
        if "allOf" in schema:
            # Strict: a key any member describes is known to all of them
            known = known | self._property_names(schema)
        for key in ("allOf", "anyOf", "oneOf"):
            if key in schema:
                self._combine(key, value, schema[key], strict, path, known)
        if isinstance(value, dict):
            props = schema.get("properties", {})
            for name, sub in props.items():
                if name in value:
                    self.validate(value[name], sub, strict, f"{path}.{name}")
            for name in schema.get("required", []):
                if name not in value:
                    raise SchemaError(f"{path}: missing {name}")
            extra = schema.get("additionalProperties", False if (strict and (props or known)) else True)
            if extra is not True:
                for name in value:
                    if name in props or name in known:
                        continue
                    if extra is False:
                        raise SchemaError(f"{path}: unexpected field {name}")
                    self.validate(value[name], extra, strict, f"{path}.{name}")
        if isinstance(value, list) and "items" in schema:
            for i, item in enumerate(value):
                self.validate(item, schema["items"], strict, f"{path}[{i}]")
        if isinstance(value, (int, float)) and not isinstance(value, bool):
            if "minimum" in schema and value < schema["minimum"]:
                raise SchemaError(f"{path}: {value} < {schema['minimum']}")
            if "maximum" in schema and value > schema["maximum"]:
                raise SchemaError(f"{path}: {value} > {schema['maximum']}")
        if isinstance(value, str):
            if "minLength" in schema and len(value) < schema["minLength"]:
                raise SchemaError(f"{path}: shorter than {schema['minLength']}")
            if "pattern" in schema and not re.search(schema["pattern"], value):
                raise SchemaError(f"{path}: {value!r} does not match {schema['pattern']}")

    def _property_names(self, schema):
        """Every property name `schema` or its allOf members (one $ref deep) list."""
        names = set(schema.get("properties", {}))
        for sub in schema.get("allOf", []):
            if "$ref" in sub:
                sub = self.resolve(sub["$ref"])
            names |= self._property_names(sub)
        return frozenset(names)

    def _combine(self, key, value, schemas, strict, path, known):
        errors = []
        passed = 0
        for sub in schemas:
            try:
                self.validate(value, sub, strict, path, known)
                passed += 1
            except SchemaError as e:
                errors.append(str(e))
        if key == "allOf" and errors:
            raise SchemaError(errors[0])
        if key == "anyOf" and passed == 0:
            raise SchemaError(f"{path}: matched none of {len(schemas)} schemas: " + "; ".join(errors))
        if key == "oneOf" and passed != 1:
            raise SchemaError(f"{path}: matched {passed} of {len(schemas)} schemas (need exactly one): " + "; ".join(errors))

    def refs(self, node=None, found=None):
        """Every $ref in the document (to check they all resolve)."""
        if found is None:
            found, node = [], self.document
        if isinstance(node, dict):
            for k, v in node.items():
                if k == "$ref":
                    found.append(v)
                else:
                    self.refs(v, found)
        elif isinstance(node, list):
            for v in node:
                self.refs(v, found)
        return found
