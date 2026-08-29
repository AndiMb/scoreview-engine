
const MEM_FILE = 'scoreview.lib.mem.wasm'

// NOTE Two bundles used to be built here and are gone.
//  - webmscore.cdn.mjs fetched its .wasm from cdn.jsdelivr.net/npm/webmscore@<ver>,
//    a package this fork does not publish, so it could never load.
//  - webmscore.webpack.mjs was a Webpack 4 workaround, built on file-loader
//    inline syntax that Webpack 5 dropped.
// They were also the only two that went through Babel, which transpiled down to
// `ie >= 11` - for a library that needs WebAssembly. With them went
// @babel/core, @babel/preset-env and @rollup/plugin-babel.

const REPLACE_MEM_FILE = {
    transform(code, id) {
        if (id.endsWith("scoreview.lib.js")) {
            code = code.replace("scoreview.lib.js.mem", MEM_FILE)
        }
        return { code }
    }
}

// Browser bundles have no import.meta, and getSelfURL() falls back to
// document.currentScript or location when this comes back empty.
const REPLACE_IMPORT_META = {
    resolveImportMeta(property) {
        if (property === 'url') {
            return '""';
        }
        return null;
    },
}

// NOTE The CommonJS bundle cannot use the empty string. Emscripten's glue calls
// createRequire(import.meta.url) since 4.0, and createRequire("") throws
// ERR_INVALID_ARG_VALUE before anything runs. In CommonJS the module's own file
// URL is exactly what import.meta.url would have been.
const REPLACE_IMPORT_META_CJS = {
    resolveImportMeta(property) {
        if (property === 'url') {
            return "require('url').pathToFileURL(__filename).href";
        }
        return null;
    },
}

const BYPASS_EVAL_WARNING = {
    transform(code, id) {
        if (id.includes(".lib.js")) {
            code = code.replace(/eval\(/g, "var eval_=eval;eval_(")
        }
        return { code }
    }
}

export default [
    {
        input: "src/nodejs.js",
        output: {
            file: "scoreview.nodejs.cjs",
            format: "cjs",
            exports: "default",
            sourcemap: false,
        },
        plugins: [REPLACE_MEM_FILE, REPLACE_IMPORT_META_CJS, BYPASS_EVAL_WARNING],
    },
    {
        input: "src/worker.js",
        output: {
            file: ".cache/worker.js",
            format: "iife",
            sourcemap: false,
            banner: "export const WebMscoreWorker = function () { ",
            footer: "}\n",
        },
        plugins: [REPLACE_MEM_FILE, REPLACE_IMPORT_META, BYPASS_EVAL_WARNING],
    },
    {
        input: "src/worker-helper.js",
        output: {
            file: "scoreview.js",
            format: "iife",
            name: 'WebMscore',
            sourcemap: false,
        },
        plugins: [REPLACE_IMPORT_META],
    },
    {
        input: "src/worker-helper.js",
        output: {
            file: "scoreview.mjs",
            format: "esm",
            sourcemap: false,
        }
    },
]
