import "./tests.setup";
import "./tests.abortController";
import "./tests.xmlHttpRequest";
import "./tests.fetch";
import "./tests.httpTransport";
import "./tests.dataUrl";
import "./tests.scheduling";
import "./tests.webSocket";
import "./tests.url";
import "./tests.urlSearchParams";
import "./tests.console";
import "./tests.blob";
import "./tests.nodeApi";
import "./tests.performance";
import "./tests.textDecoder";
import "./tests.textEncoder";
import "./tests.file";
import "./tests.fileReader";
import "./tests.webAssembly";

declare const setExitCode: (code: number) => void;

function runTests() {
    if (typeof testFilter === "string") {
        mocha.grep(testFilter);
    }
    const runner = mocha.run((failures: number) => {
        if (runner.total === 0) {
            console.error("No JavaScript tests matched JSRUNTIMEHOST_TEST_GREP.");
        }
        // Test program will wait for code to be set before exiting
        if (failures > 0 || runner.total === 0) {
            // Failure
            setExitCode(1);
        } else {
            // Success
            setExitCode(0);
        }
    });
}

runTests();
