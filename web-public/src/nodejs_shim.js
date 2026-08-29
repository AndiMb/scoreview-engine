
import { createRequire } from 'module'
import { dirname } from 'path'
import { fileURLToPath } from 'url'
import { IS_NODE, shimDom } from './utils.js'

if (IS_NODE) {

    // cjs require for the native Node.js ES Modules support
    if (typeof require == "undefined") {
        global.require = createRequire(import.meta.url)
    }

    // __dirname for the native Node.js ES Modules support
    if (typeof __dirname == "undefined") {
        global.__dirname = dirname(fileURLToPath(import.meta.url))
    }

    // NOTE No IndexedDB shim. It was here for IDBFS, which this build has never
    // linked - emscripten only bundles that filesystem for `-lidbfs.js`, and
    // nothing asks for it. The shim came with the LibreScore original and was
    // the package's only runtime dependency.

    shimDom()
}
