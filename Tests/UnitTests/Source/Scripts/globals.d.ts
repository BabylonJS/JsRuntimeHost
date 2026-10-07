declare const hostPlatform: string;
declare const hostEngine: string;
declare const httpTestUrl: string;
declare const testFilter: string | undefined;
interface XMLHttpRequest {
    readonly errorCode: string;
    readonly errorDetail: string;
}
declare const napiGetPropertyNames: (object: any) => string[];
declare const napiGetPropertyNamesRaw: (object: any) => string[];
