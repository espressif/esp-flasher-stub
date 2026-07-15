// Copyright 2026 Espressif Systems (Shanghai) CO LTD
// SPDX-License-Identifier: Apache-2.0 OR MIT

// @ts-check

// @ts-ignore
import { cp, mkdir, readdir, writeFile } from 'node:fs/promises';
// @ts-ignore
import path from 'node:path';
// @ts-ignore
import { fileURLToPath } from 'node:url';

const REPO_ROOT = path.join(path.dirname(fileURLToPath(import.meta.url)), '..');
const REPO_URL = 'https://github.com/espressif/esp-flasher-stub';

/**
 * @typedef {object} Options
 * @property {string} version
 * @property {string} stubDir
 * @property {string} outDir
 */

/**
 * @param {string[]} argv
 * @returns {Options}
 */
function parseArgs(argv) {
  /** @type {Partial<Options>} */
  const options = {};

  for (let index = 0; index < argv.length; index += 1) {
    const key = argv[index];
    const value = argv[index + 1];

    if (!value) {
      throw new Error(`Missing value for argument: ${key}`);
    }

    switch (key) {
      case '--version':
        options.version = value;
        break;
      case '--stub-dir':
        options.stubDir = value;
        break;
      case '--out-dir':
        options.outDir = value;
        break;
      default:
        throw new Error(`Unknown argument: ${key}`);
    }

    index += 1;
  }

  if (!options.version || !options.stubDir || !options.outDir) {
    throw new Error('Usage: node tools/generate_npm_package.mjs --version <version> --stub-dir <dir> --out-dir <dir>');
  }

  return /** @type {Options} */ (options);
}

/**
 * @param {string} stubDir
 * @returns {Promise<string[]>}
 */
async function collectStubFiles(stubDir) {
  const entries = await readdir(stubDir, { recursive: true, withFileTypes: true });

  return entries
    .filter((entry) => entry.isFile())
    .map((entry) => entry.parentPath ? path.join(entry.parentPath, entry.name) : path.join(stubDir, entry.name))
    .filter((filePath) => filePath.endsWith('.json'))
    .filter((filePath) => !filePath.endsWith('.base.json'))
    .filter((filePath) => path.basename(filePath).startsWith('esp'))
    .sort((left, right) => left.localeCompare(right));
}

/**
 * @param {string} version
 * @returns {string}
 */
function buildReadme(version) {
  const releaseTagUrl = `${REPO_URL}/releases/tag/v${version}`;
  const licenseYears = new Date().getFullYear() === 2026 ? '2026' : `2026-${new Date().getFullYear()}`;

  return `# esp-flasher-stub

ESP Flasher Stub is a set of small firmware programs (stubs) that run on Espressif ESP chips \
to enable fast and reliable flash programming via [esptool](https://github.com/espressif/esptool/) \
and [esp-serial-flasher](https://github.com/espressif/esp-serial-flasher). \
For the detailed introduction of the stubs, \
please refer to the [esp-flasher-stub](${REPO_URL}) repository.

This npm package (\`esp-flasher-stub\`) distributes the pre-built stub JSON files for all supported \
ESP chips. These files can be used by frontend projects to interact with ESP devices.

## How to Use

Install the package:

\`\`\`sh
npm install esp-flasher-stub
\`\`\`

Import a stub using [ES Module JSON import attributes] [^1]:

\`\`\`js
import esp32s31Stub from 'esp-flasher-stub/esp32s31.json' with { type: 'json' }
\`\`\`

## More Information

For full documentation, supported chips, build instructions, and contribution guidelines, \
visit the GitHub repository: ${REPO_URL}

The bundled stubs are from \`esp-flasher-stub\` **v${version}**. \
See the release notes at: ${releaseTagUrl}

## License

Copyright (c) ${licenseYears} Espressif Systems (Shanghai) Co., Ltd.

See https://github.com/espressif/esp-flasher-stub#license for the license of the source code.


[ES Module JSON import attributes]: https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Statements/import/with

[^1]: This feature requires Node.js 20.10+ or a modern JavaScript build tool such as \
Vite, Webpack, Parcel, and Turbopack. In older runtimes, you may need to use \`assert { type: 'json' }\` \
or other compatible import syntax. See [ES Module JSON import attributes] for more details.
`;
}

async function main() {
  const options = parseArgs(process.argv.slice(2));
  const stubFiles = await collectStubFiles(options.stubDir);

  if (stubFiles.length === 0) {
    throw new Error(`No stub JSON files found in ${options.stubDir}`);
  }

  const packageJson = {
    name: 'esp-flasher-stub',
    version: options.version,
    description: 'Stub JSON files for ESP flasher',
    exports: {
      './*.json': './stubs/*.json',
    },
    files: [
      'stubs',
      'README.md',
      'LICENSE-*',
    ],
    publishConfig: {
      access: 'public',
    },
    keywords: [
      'esp',
      'espressif',
      'esptool',
    ],
    license: 'Apache-2.0 OR MIT',
    type: 'module',
    repository: {
      type: 'git',
      url: `git+${REPO_URL}.git`,
    },
    homepage: REPO_URL,
  };

  const stubsDir = path.join(options.outDir, 'stubs');

  let dirEntries;
  try {
    dirEntries = await readdir(options.outDir);
  } catch {
    dirEntries = null;
  }
  if (dirEntries !== null && dirEntries.length > 0) {
    console.error(`Error: output directory "${options.outDir}" already exists and is not empty.`);
    process.exit(1);
  }
  await mkdir(stubsDir, { recursive: true });

  await writeFile(
    path.join(options.outDir, 'package.json'),
    `${JSON.stringify(packageJson, null, 2)}\n`,
    'utf8',
  );
  await writeFile(path.join(options.outDir, 'README.md'), buildReadme(options.version), 'utf8');

  const rootEntries = await readdir(REPO_ROOT, { withFileTypes: true });
  const licenseFiles = rootEntries
    .filter((entry) => entry.isFile() && entry.name.startsWith('LICENSE-'))
    .map((entry) => path.join(REPO_ROOT, entry.name));
  for (const licenseFile of licenseFiles) {
    await cp(licenseFile, path.join(options.outDir, path.basename(licenseFile)));
  }

  for (const stubFile of stubFiles) {
    await cp(stubFile, path.join(stubsDir, path.basename(stubFile)));
  }
}

await main();
