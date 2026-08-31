/**
 * Built-in knowledge about Novus: keywords, primitive types, annotations and
 * the concept standard library used in test/syntax.nv.
 */
import { NSymbol, mkParam, mkType, newSymbol } from './symbols';

export interface KeywordDoc {
  name: string;
  detail: string;
  doc: string;
}

const IMPLEMENTED_NOTE = '\n\n_Implemented by the current interpreter._';
const CONCEPT_NOTE = '\n\n_Concept syntax – not yet implemented by the interpreter._';

export const KEYWORDS: KeywordDoc[] = [
  { name: 'package', detail: 'package <name>', doc: 'Declares the package of this file.' + IMPLEMENTED_NOTE },
  { name: 'import', detail: 'import <module>', doc: 'Imports another `.nv` file (`import "file.nv"`) or a stdlib module (`json`, `path`).' + IMPLEMENTED_NOTE },
  { name: 'method', detail: 'method name(params): Type { … }', doc: 'Declares a method. `method main` is the program entry point. The parameter list and the return type are optional.' + IMPLEMENTED_NOTE },
  { name: 'var', detail: 'var name[: Type] = value', doc: 'Declares a variable. The type is inferred from the initializer when omitted.' + IMPLEMENTED_NOTE },
  { name: 'println', detail: 'println <value>', doc: 'Prints a value followed by a line break. Strings support `${expression}` interpolation.' + IMPLEMENTED_NOTE },
  { name: 'return', detail: 'return [value]', doc: 'Returns from the current method. The value is coerced to the declared return type.' + IMPLEMENTED_NOTE },
  { name: 'define', detail: 'define class|enum|interface|abstract|annotation Name { … }', doc: 'Defines a new type.' + IMPLEMENTED_NOTE },
  { name: 'class', detail: 'define class Name[(params)] [based Base] { … }', doc: 'Defines a class. Parameters after the name declare a primary constructor.' + IMPLEMENTED_NOTE },
  { name: 'enum', detail: 'define enum Name { A("a"), B("b"); … }', doc: 'Defines an enumeration. Constants come first, terminated by `;`, followed by members.' + IMPLEMENTED_NOTE },
  { name: 'interface', detail: 'define interface Name { method() }', doc: 'Defines an interface with method signatures.' + IMPLEMENTED_NOTE },
  { name: 'abstract', detail: 'define abstract Name { … } / abstract method name()', doc: 'Defines an abstract class or marks a method as abstract.' + IMPLEMENTED_NOTE },
  { name: 'annotation', detail: 'define annotation Name { text(): string }', doc: 'Defines an annotation type. Use it with `@Name{ key=value }`.' + IMPLEMENTED_NOTE },
  { name: 'based', detail: 'define class Name based Base, IOther', doc: 'Declares base classes and interfaces of a type.' + IMPLEMENTED_NOTE },
  { name: 'construct', detail: 'construct(params) { … }', doc: 'Declares a constructor.' + IMPLEMENTED_NOTE },
  { name: 'this', detail: 'this', doc: 'The current instance.' + IMPLEMENTED_NOTE },
  { name: 'private', detail: 'private', doc: 'Restricts visibility to the declaring type.' + IMPLEMENTED_NOTE },
  { name: 'public', detail: 'public', doc: 'Makes a member visible everywhere.' + CONCEPT_NOTE },
  { name: 'protected', detail: 'protected', doc: 'Makes a member visible to subclasses.' + CONCEPT_NOTE },
  { name: 'final', detail: 'final', doc: 'The value can only be assigned once.' + IMPLEMENTED_NOTE },
  { name: 'static', detail: 'static', doc: 'Belongs to the type rather than an instance.' + CONCEPT_NOTE },
  { name: 'if', detail: 'if (condition) { … } else { … }', doc: 'Conditional statement.' + IMPLEMENTED_NOTE },
  { name: 'else', detail: 'else { … }', doc: 'Alternative branch of an `if` statement.' + IMPLEMENTED_NOTE },
  { name: 'for', detail: 'for (item in items) { … }', doc: 'Iterates over a collection.' + IMPLEMENTED_NOTE },
  { name: 'in', detail: 'for (item in items)', doc: 'Separates the loop variable from the collection in a `for` loop.' + IMPLEMENTED_NOTE },
  { name: 'while', detail: 'while (condition) { … }', doc: 'Loops while the condition holds.' + IMPLEMENTED_NOTE },
  { name: 'break', detail: 'break', doc: 'Leaves the innermost loop.' + IMPLEMENTED_NOTE },
  { name: 'continue', detail: 'continue', doc: 'Continues with the next loop iteration.' + IMPLEMENTED_NOTE },
  { name: 'true', detail: 'true', doc: 'Boolean literal.' + IMPLEMENTED_NOTE },
  { name: 'false', detail: 'false', doc: 'Boolean literal.' + IMPLEMENTED_NOTE },
  { name: 'null', detail: 'null', doc: 'The absence of a value.' + CONCEPT_NOTE },
  { name: 'get', detail: 'Type field: get', doc: 'Generates a getter `field()` for the field.' + IMPLEMENTED_NOTE },
  { name: 'set', detail: 'Type field: set', doc: 'Generates a setter `field(value)` for the field.' + IMPLEMENTED_NOTE },
];

export const KEYWORD_MAP = new Map(KEYWORDS.map(k => [k.name, k]));

export interface PrimitiveType {
  name: string;
  doc: string;
  implemented: boolean;
  generic?: boolean;
}

export const PRIMITIVE_TYPES: PrimitiveType[] = [
  { name: 'string', doc: 'Text value.', implemented: true },
  { name: 'integer', doc: 'Whole number.', implemented: true },
  { name: 'float', doc: 'Floating point number.', implemented: true },
  { name: 'void', doc: 'No value (default return type).', implemented: true },
  { name: 'str', doc: 'Alias for `string`.', implemented: true },
  { name: 'int', doc: 'Alias for `integer`.', implemented: true },
  { name: 'double', doc: 'Double precision floating point number.', implemented: false },
  { name: 'doub', doc: 'Alias for `double`.', implemented: false },
  { name: 'bool', doc: 'Boolean value (`true`/`false`).', implemented: true },
  { name: 'boolean', doc: 'Alias for `bool`.', implemented: true },
  { name: 'array', doc: 'Ordered collection of elements: `array<Type>`.', implemented: true, generic: true },
  { name: 'map', doc: 'Key/value collection.', implemented: true, generic: true },
  { name: 'image', doc: 'Image resource.', implemented: false },
];

export const PRIMITIVE_MAP = new Map(PRIMITIVE_TYPES.map(t => [t.name, t]));

export function isPrimitive(name: string): boolean {
  return PRIMITIVE_MAP.has(name);
}

export function isStringType(name: string): boolean {
  return name === 'string' || name === 'str';
}

export function isFloatType(name: string): boolean {
  return name === 'float' || name === 'double' || name === 'doub';
}

export function isBoolType(name: string): boolean {
  return name === 'bool' || name === 'boolean';
}

export interface BuiltinAnnotation {
  name: string;
  doc: string;
  args: { name: string; type: string; doc: string }[];
}

export const BUILTIN_ANNOTATIONS: BuiltinAnnotation[] = [
  {
    name: 'Deprecated',
    doc: 'Marks a member as deprecated.',
    args: [
      { name: 'text', type: 'string', doc: 'Explanation and migration hint.' },
      { name: 'since', type: 'string', doc: 'Version in which the member was deprecated.' },
    ],
  },
  { name: 'Override', doc: 'The method overrides a base implementation.', args: [] },
  { name: 'Interface', doc: 'The method implements an interface member.', args: [] },
  { name: 'Abstract', doc: 'The method implements an abstract member.', args: [] },
  { name: 'Warning', doc: 'Emits a warning when the annotated member is used.', args: [] },
];

function method(container: NSymbol | undefined, name: string, params: [string, string][], returnType: string | undefined, doc: string, note: string = CONCEPT_NOTE): NSymbol {
  return newSymbol({
    kind: 'method',
    name,
    uri: '',
    builtin: true,
    container,
    params: params.map(([t, n]) => mkParam(t, n)),
    returnType: returnType ? mkType(returnType) : undefined,
    doc: doc + note,
  });
}

function field(container: NSymbol, name: string, type: string, doc: string, note: string = CONCEPT_NOTE): NSymbol {
  return newSymbol({ kind: 'field', name, uri: '', builtin: true, container, type: mkType(type), doc: doc + note, modifiers: ['final'] });
}

function module(name: string, doc: string, build: (m: NSymbol) => NSymbol[], note: string = CONCEPT_NOTE): NSymbol {
  const m = newSymbol({ kind: 'module', name, uri: '', builtin: true, doc: doc + note });
  m.members = build(m);
  return m;
}

let cachedModules: NSymbol[] | undefined;

/** Standard library modules (json and path are implemented, http is still a concept). */
export function builtinModules(): NSymbol[] {
  if (cachedModules) return cachedModules;
  cachedModules = [
    module('http', 'HTTP client.', m => [
      method(m, 'get', [['string', 'url']], 'string', 'Performs a GET request and returns the response body.'),
      method(m, 'post', [['string', 'url'], ['map', 'body']], 'string', 'Performs a POST request with a JSON body.'),
    ]),
    module('json', 'JSON serialization.', m => [
      method(m, 'parse', [['string', 'text']], 'map', 'Parses JSON text into maps, arrays and primitive values.', IMPLEMENTED_NOTE),
      method(m, 'stringify', [['object', 'value']], 'string', 'Serializes a value (maps, arrays, objects, primitives) to JSON text.', IMPLEMENTED_NOTE),
      method(m, 'save', [['object', 'value'], ['string', 'directory'], ['string', 'fileName']], undefined, 'Writes a value as pretty-printed JSON to `directory/fileName`.', IMPLEMENTED_NOTE),
    ], IMPLEMENTED_NOTE),
    module('path', 'File system paths.', m => [
      field(m, 'absolute', 'string', 'Absolute path of the working directory.', IMPLEMENTED_NOTE),
      method(m, 'join', [['string', 'first'], ['string', 'second']], 'string', 'Joins path segments with the platform separator.', IMPLEMENTED_NOTE),
      method(m, 'exists', [['string', 'path']], 'bool', 'Whether the path exists.', IMPLEMENTED_NOTE),
    ], IMPLEMENTED_NOTE),
  ];
  return cachedModules;
}

let cachedGlobals: NSymbol[] | undefined;

/** Free builtin functions provided by the interpreter and the compiler. */
export function builtinGlobals(): NSymbol[] {
  if (cachedGlobals) return cachedGlobals;
  cachedGlobals = [
    method(undefined, 'readFile', [['string', 'path']], 'string', 'Reads a file and returns its content (empty string when unreadable).', IMPLEMENTED_NOTE),
    method(undefined, 'writeFile', [['string', 'path'], ['string', 'content']], undefined, 'Writes text to a file.', IMPLEMENTED_NOTE),
    method(undefined, 'fileExists', [['string', 'path']], 'bool', 'Whether a file exists.', IMPLEMENTED_NOTE),
    method(undefined, 'args', [], 'array', 'The command line arguments passed after the script name.', IMPLEMENTED_NOTE),
    method(undefined, 'parseInt', [['string', 'text']], 'integer', 'Parses an integer from text.', IMPLEMENTED_NOTE),
    method(undefined, 'chr', [['integer', 'code']], 'string', 'One-character string for a character code (e.g. `chr(34)` is a quote).', IMPLEMENTED_NOTE),
    method(undefined, 'ord', [['string', 'text']], 'integer', 'Character code of the first character.', IMPLEMENTED_NOTE),
    method(undefined, 'typeOf', [['object', 'value']], 'string', 'Type name of a value: `integer`, `float`, `bool`, `string`, `array`, `map` or a class name.', IMPLEMENTED_NOTE),
  ];
  return cachedGlobals;
}

const memberCache = new Map<string, NSymbol[]>();

/** Members available on values of primitive types (array, string). */
export function builtinMembers(typeName: string, elementType?: string): NSymbol[] {
  const key = `${typeName}<${elementType ?? ''}>`;
  const cached = memberCache.get(key);
  if (cached) return cached;
  const owner = newSymbol({ kind: 'type', name: typeName, uri: '', builtin: true });
  const elem = elementType ?? 'object';
  let members: NSymbol[] = [];
  if (typeName === 'array') {
    members = [
      method(owner, 'length', [], 'integer', 'Number of elements.', IMPLEMENTED_NOTE),
      method(owner, 'append', [[elem, 'items']], undefined, 'Appends one or more elements.', IMPLEMENTED_NOTE),
      method(owner, 'remove', [[elem, 'item']], 'bool', 'Removes the first occurrence of an element.'),
      method(owner, 'get', [['integer', 'index']], elem, 'Returns the element at the given index.'),
      method(owner, 'size', [], 'integer', 'Number of elements.'),
      method(owner, 'isEmpty', [], 'bool', 'Whether the array has no elements.'),
      method(owner, 'contains', [[elem, 'item']], 'bool', 'Whether the array contains the element.'),
      method(owner, 'clear', [], undefined, 'Removes all elements.'),
    ];
  } else if (isStringType(typeName)) {
    members = [
      method(owner, 'length', [], 'integer', 'Number of characters.', IMPLEMENTED_NOTE),
      method(owner, 'charAt', [['integer', 'index']], 'string', 'Character at an index as a one-character string.', IMPLEMENTED_NOTE),
      method(owner, 'substring', [['integer', 'start'], ['integer', 'end']], 'string', 'Substring from start (inclusive) to end (exclusive), clamped to the bounds.', IMPLEMENTED_NOTE),
      method(owner, 'upper', [], 'string', 'Upper-case copy.'),
      method(owner, 'lower', [], 'string', 'Lower-case copy.'),
      method(owner, 'trim', [], 'string', 'Copy without surrounding whitespace.'),
      method(owner, 'contains', [['string', 'text']], 'bool', 'Whether the string contains the text.'),
      method(owner, 'split', [['string', 'separator']], 'array', 'Splits the string.'),
      method(owner, 'replace', [['string', 'search'], ['string', 'replacement']], 'string', 'Replaces all occurrences.'),
    ];
  } else if (typeName === 'map') {
    members = [
      method(owner, 'length', [], 'integer', 'Number of entries.', IMPLEMENTED_NOTE),
      method(owner, 'has', [['string', 'key']], 'bool', 'Whether the key exists.', IMPLEMENTED_NOTE),
      method(owner, 'keys', [], 'array', 'All keys, sorted.', IMPLEMENTED_NOTE),
      method(owner, 'remove', [['string', 'key']], undefined, 'Removes an entry.', IMPLEMENTED_NOTE),
      method(owner, 'get', [['string', 'key']], 'object', 'Returns the value for a key (use `m[key]` instead).'),
      method(owner, 'put', [['string', 'key'], ['object', 'value']], undefined, 'Stores a value (use `m[key] = v` instead).'),
    ];
  }
  memberCache.set(key, members);
  return members;
}
