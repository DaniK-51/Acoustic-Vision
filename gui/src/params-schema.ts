// Engine paramsSchema subset (docs/engines.md): type object + properties of
// number/integer/boolean/string with default/minimum/maximum/enum. Anything
// outside that subset falls back to a raw JSON editor in the form.

export interface SchemaProperty {
  type?: string;
  default?: unknown;
  minimum?: number;
  maximum?: number;
  exclusiveMinimum?: number;
  exclusiveMaximum?: number;
  enum?: Array<string | number | boolean>;
  description?: string;
}

export type SchemaProperties = Record<string, SchemaProperty>;

const SUPPORTED_TYPES = new Set(['number', 'integer', 'boolean', 'string']);

export function isSupportedType(type: string | undefined): boolean {
  return type !== undefined && SUPPORTED_TYPES.has(type);
}

export interface ParsedSchema {
  /** False when the schema is not a renderable object schema at all. */
  renderable: boolean;
  properties: SchemaProperties;
  /** Property names outside the supported subset (forces raw fallback). */
  unsupported: string[];
}

export function parseObjectSchema(schema: unknown): ParsedSchema {
  const empty: ParsedSchema = { renderable: true, properties: {}, unsupported: [] };
  if (schema == null) {
    return empty;
  }
  if (typeof schema !== 'object' || Array.isArray(schema)) {
    return { renderable: false, properties: {}, unsupported: [] };
  }
  const root = schema as Record<string, unknown>;
  if (root.type !== undefined && root.type !== 'object') {
    return { renderable: false, properties: {}, unsupported: [] };
  }
  const rawProps = root.properties;
  if (rawProps == null) {
    return empty;
  }
  if (typeof rawProps !== 'object' || Array.isArray(rawProps)) {
    return { renderable: false, properties: {}, unsupported: [] };
  }
  const properties: SchemaProperties = {};
  const unsupported: string[] = [];
  for (const [key, value] of Object.entries(rawProps as Record<string, unknown>)) {
    if (value == null || typeof value !== 'object' || Array.isArray(value)) {
      unsupported.push(key);
      continue;
    }
    const prop = value as SchemaProperty;
    if (!isSupportedType(prop.type)) {
      unsupported.push(key);
      continue;
    }
    if (prop.type === 'string' && prop.enum != null) {
      if (!Array.isArray(prop.enum)) {
        unsupported.push(key);
        continue;
      }
    }
    properties[key] = prop;
  }
  return {
    renderable: unsupported.length === 0,
    properties,
    unsupported,
  };
}

function defaultForProperty(prop: SchemaProperty): unknown {
  if (prop.default !== undefined) {
    return prop.default;
  }
  switch (prop.type) {
    case 'boolean':
      return false;
    case 'integer':
    case 'number':
      return prop.minimum ?? 0;
    case 'string':
      return prop.enum?.[0] ?? '';
    default:
      return null;
  }
}

/**
 * Initial methodParams for a form: schema defaults overlaid with values already
 * stored on the scenario. Only keys declared in the schema are kept — the form
 * is the authority for what gets submitted under methodParams.
 */
export function formValuesFrom(
  schema: unknown,
  stored: Record<string, unknown> | null | undefined,
): Record<string, unknown> {
  const { properties, renderable } = parseObjectSchema(schema);
  const out: Record<string, unknown> = {};
  const source = stored ?? {};
  if (!renderable) {
    // Raw mode: pass stored values through untouched.
    return { ...source };
  }
  for (const [key, prop] of Object.entries(properties)) {
    out[key] = source[key] !== undefined ? source[key] : defaultForProperty(prop);
  }
  return out;
}

/** Plain-JSON equality (methodParams never contain functions/undefined). */
export function jsonEqual(a: unknown, b: unknown): boolean {
  if (a === b) {
    return true;
  }
  if (
    a == null ||
    b == null ||
    typeof a !== 'object' ||
    typeof b !== 'object' ||
    Array.isArray(a) !== Array.isArray(b)
  ) {
    return false;
  }
  if (Array.isArray(a) && Array.isArray(b)) {
    return a.length === b.length && a.every((item, i) => jsonEqual(item, b[i]));
  }
  const ao = a as Record<string, unknown>;
  const bo = b as Record<string, unknown>;
  const aKeys = Object.keys(ao);
  const bKeys = Object.keys(bo);
  return (
    aKeys.length === bKeys.length &&
    aKeys.every((key) => jsonEqual(ao[key], bo[key]))
  );
}
