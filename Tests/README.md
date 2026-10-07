# Unit tests

Build and run the Windows tests from the repository root:

```powershell
cmake -S . -B Build\QuickJS -A x64 -D NAPI_JAVASCRIPT_ENGINE=QuickJS
cmake --build Build\QuickJS --config RelWithDebInfo --target UnitTests -- /m
Set-Location Build\QuickJS\Tests\UnitTests\RelWithDebInfo
.\UnitTests.exe
```

Use `NAPI_JAVASCRIPT_ENGINE=V8` and a separate build directory to run with V8.
The executable must run from its output directory so `app:///Assets/` resolves
the test assets.

## UrlLib consumer regressions

The `UrlLib HTTP transport` and `UrlLib data URLs` JavaScript suites exercise
the real Fetch and XMLHttpRequest polyfills against the configured UrlLib
dependency. They cover the fixes in
[BabylonJS/UrlLib#39](https://github.com/BabylonJS/UrlLib/pull/39).

The HTTP fixture runs inside the test process on an ephemeral IPv4 loopback
port. It echoes the received POST bytes and exposes the actual request method,
Content-Type, and body length in diagnostic response headers. It also serves
responses without Content-Type, including empty/204 responses and HTTP errors.
Connections have bounded reads/writes; fixture errors fail the native test.
Only the test apps enable cleartext HTTP for this fixture.

Coverage includes:

- Case-insensitive POST Content-Type through record, header-pair, and
  Headers-like `forEach` initializers; exact MIME parameters; omitted MIME;
  UTF-8, empty, and embedded-NUL string bodies; response JSON/text/bytes/Blob.
- Invalid Windows request MIME and reuse of the failed XHR without stale headers.
- Missing response Content-Type in Fetch and both XHR response modes, retaining
  Windows' existing 2xx-to-200 normalization and non-success HTTP statuses.
- Percent/base64 data URLs, MIME/defaults, binary bytes, fragments, malformed
  input, unsupported POST, cancellation, and XHR reuse across shared/HTTP transports.

POST tests explicitly skip Linux because that UrlLib backend does not implement
POST. The JS APIs currently accept only string request bodies, so invalid-UTF-8
POST bytes remain covered by UrlLib's native tests, not these consumer tests.
The echo response explicitly declares UTF-8 and uses lowercase diagnostic header
names; this does not test unrelated platform charset defaults or response-header
normalization. Blob MIME assertions retain the current polyfill's casing.

To run just these offline scenarios (from the executable's output directory):

```powershell
$env:JSRUNTIMEHOST_TEST_GREP = '^UrlLib '
.\UnitTests.exe --gtest_filter=JavaScript.All --gtest_repeat=3
Remove-Item Env:\JSRUNTIMEHOST_TEST_GREP
```

`JSRUNTIMEHOST_TEST_GREP` is a Mocha regular expression. An expression that matches
no tests fails rather than reporting an empty successful run. Unset it to restore
the full JavaScript suite. Native tests can be selected using the usual GoogleTest
flags.

For a negative control, configure the same build with
`-D FETCHCONTENT_SOURCE_DIR_URLLIB=<absolute-path-to-an-old-UrlLib-checkout>`,
rebuild, and run the same tests. Run the missing-Content-Type case separately:
the old Windows implementation can terminate the process with an access violation.
Remove the override with `cmake -S . -B <build-directory> -U FETCHCONTENT_SOURCE_DIR_URLLIB`
and rebuild before testing or using the fixed dependency again.
