// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

// CMake needs a deterministic output file for the npm install edge. Hashing
// the lockfile is sufficient: `npm ci` guarantees node_modules exactly matches
// it, and unlike the former background tar pipeline this cannot race with npm.
import { createHash } from "node:crypto";
import { readFileSync, writeFileSync } from "node:fs";

const directory = new URL(".", import.meta.url);
const lockfile = new URL("package-lock.json", directory);
const hash = createHash("sha256").update(readFileSync(lockfile)).digest("hex");
writeFileSync(new URL("node_modules.sha256", directory), `${hash}\n`);
