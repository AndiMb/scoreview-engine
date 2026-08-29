
export const IS_NODE = typeof process === 'object' && typeof process.versions === 'object' && typeof process.versions.node === 'string'

export const getSelfURL = () => {
    let url = import.meta.url  // transforms to "" in the generated bundle
    if (!url) {
        if (typeof document !== 'undefined') {
            url = document.currentScript && document.currentScript.src || document.baseURI
        } else if (typeof location !== 'undefined') {
            url = location.href
        }
    }
    return url
}

/**
 * EVERYTHING THIS FUNCTION NEEDS HAS TO LIVE INSIDE IT. worker-helper.js ships
 * it to the worker as `shimDom.toString()`, and a name it closes over is simply
 * not there on the other side - the worker then dies on the first line it runs,
 * with a bare ReferenceError and no hint of where the name went. That is what
 * `getGlobalThis` is nested for, and it is why `defineIfMissing` is nested too.
 */
export const shimDom = () => {
    const getGlobalThis = () => {
        if (typeof globalThis !== 'undefined') { return globalThis }
        if (typeof self !== 'undefined') { return self }
        if (typeof window !== 'undefined') { return window }
        if (typeof global !== 'undefined') { return global }
        throw new Error('unable to locate global object')
    }

    /**
     * Define a global only if it is not there yet.
     *
     * NOTE Plain assignment is not enough. Node 21 added a global `navigator` as
     * a getter-only accessor, so `globalthis.navigator = ...` throws
     * "Cannot set property navigator of #<Object> which has only a getter" -
     * which is what stopped webmscore from loading on Node > 20 at all. Reading
     * it is fine, so anything already present is left alone, and what is missing
     * is installed with defineProperty, which does not care about accessors.
     *
     * @param {any} target
     * @param {string} name
     * @param {any} value
     */
    const defineIfMissing = (target, name, value) => {
        if (typeof target[name] !== 'undefined' && target[name] !== null) {
            return
        }
        try {
            Object.defineProperty(target, name, {
                value,
                writable: true,
                configurable: true,
                enumerable: false,
            })
        } catch (_) {
            // A non-configurable accessor. Nothing to do - whatever is there has
            // to serve, and emscripten only ever reads these.
        }
    }

    const globalthis = getGlobalThis()
    defineIfMissing(globalthis, 'window', {
        addEventListener() { },
        location: new URL("file:///"),
        encodeURIComponent,
        // Qt's WebAssembly event dispatcher schedules its timers through
        // window, and takes the handle back as a number. Node returns a Timeout
        // object, whose primitive value is the id clearTimeout also accepts.
        // unref keeps a pending Qt timer - the font cache arms one on the first
        // score - from holding the process open after the caller is done.
        setTimeout(handler, timeout) {
            const t = globalthis.setTimeout(handler, timeout)
            t.unref?.()
            return Number(t)
        },
        clearTimeout(id) {
            globalthis.clearTimeout(id)
        },
    })
    defineIfMissing(globalthis, 'navigator', {})
}
