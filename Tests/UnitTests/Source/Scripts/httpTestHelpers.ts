export interface XhrResult {
    xhr: XMLHttpRequest;
    errors: number;
}

export function sendXhr(url: string, options: {
    method?: string;
    body?: string;
    headers?: Record<string, string>;
    responseType?: XMLHttpRequestResponseType;
    xhr?: XMLHttpRequest;
    abortBeforeSend?: boolean;
} = {}): Promise<XhrResult> {
    return new Promise((resolve, reject) => {
        const xhr = options.xhr ?? new XMLHttpRequest();
        let errors = 0;
        const timeout = setTimeout(() => {
            xhr.abort();
            reject(new Error(`XHR timed out: ${url}`));
        }, 10000);
        try {
            xhr.open(options.method ?? "GET", url);
            xhr.responseType = options.responseType ?? "arraybuffer";
            for (const [name, value] of Object.entries(options.headers ?? {})) {
                xhr.setRequestHeader(name, value);
            }
            xhr.addEventListener("error", () => { ++errors; });
            xhr.addEventListener("loadend", () => {
                clearTimeout(timeout);
                // XHR clears its event handlers after loadend; reopen only after that cleanup.
                setTimeout(() => resolve({ xhr, errors }), 0);
            });
            if (options.abortBeforeSend) {
                xhr.abort();
            }
            xhr.send(options.body);
        } catch (error) {
            clearTimeout(timeout);
            reject(error);
        }
    });
}

export function bytes(text: string): number[] {
    return Array.from(new TextEncoder().encode(text));
}
