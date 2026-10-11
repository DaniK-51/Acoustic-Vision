// API base URL. Override with VITE_API_BASE at build time
// (e.g. VITE_API_BASE=http://127.0.0.1:9000/v1 npm run build).
export const apiBaseUrl: string =
  (import.meta.env.VITE_API_BASE as string | undefined) ??
  'http://127.0.0.1:8787/v1';

/** Derive the WS events URL from an HTTP /v1 base: http(s)://host:port/v1 → ws(s)://host:port/v1/events. */
export function eventsUrlFromBase(baseUrl: string): string {
  return baseUrl.replace(/^http/i, 'ws').replace(/\/+$/, '') + '/events';
}

export const eventsUrl: string = eventsUrlFromBase(apiBaseUrl);
