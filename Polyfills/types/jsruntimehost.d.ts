interface Event {
  type: string;
}

type EventListener = (evt: Event) => void;
type EventListenerOrEventListenerObject = EventListener | { handleEvent(evt: Event): void };

type XMLHttpRequestResponseType = "" | "arraybuffer" | "blob" | "document" | "json" | "text";

interface XMLHttpRequest {
  open(method: string, url: string, async?: boolean, user?: string, password?: string): void;
  send(data?: any): void;
  abort(): void;

  setRequestHeader(header: string, value: string): void;
  getResponseHeader(name: string): string | null;
  getAllResponseHeaders(): string;

  addEventListener(type: string, listener: EventListenerOrEventListenerObject): void;
  removeEventListener(type: string, listener: EventListenerOrEventListenerObject): void;

  onreadystatechange: ((this: XMLHttpRequest, ev: Event) => any) | null;
  onload: ((this: XMLHttpRequest, ev: Event) => any) | null;
  onerror: ((this: XMLHttpRequest, ev: Event) => any) | null;

  readyState: number;
  status: number;
  statusText: string;
  responseText: string;
  response: any;
  responseType: XMLHttpRequestResponseType;

  timeout: number;
  withCredentials: boolean;

  // Read-only constants
  readonly UNSENT: number;
  readonly OPENED: number;
  readonly HEADERS_RECEIVED: number;
  readonly LOADING: number;
  readonly DONE: number;
}

// Declare the constructor for XMLHttpRequest (value side)
declare var XMLHttpRequest: {
  prototype: XMLHttpRequest;
  new (): XMLHttpRequest;
};
