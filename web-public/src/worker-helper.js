// @ts-check

// The main entry point to use webmscore as a web worker,  
// implements the same API set as './index.js'

import { WebMscoreWorker } from '../.cache/worker.js'
import { getSelfURL, shimDom } from './utils.js'

const MSCORE_SCRIPT_URL = getSelfURL()

/**
 * Reconstruct `Error` objects sent from the web worker
 * 
 * Native `Error` types can't be cloned by structured clone algorithm
 */
class WorkerError extends Error {
    /**
     * @param {Error} err
     */
    constructor(err) {
        super()
        this.name = err.name
        this.message = err.message
        this.originalStack = err.stack
    }
}

/**
 * Set the log level when the instance is created  
 * default: 0 (Off)
 * @see WebMscore.setLogLevel
 */
let _logLevel = 0

/**
 * Use webmscore as a web worker
 * @implements {import('./index').default}
 */
class WebMscoreW {
    /**
     * @hideconstructor use `WebMscoreW.load`
     */
    constructor() {
        const url = URL.createObjectURL(
            new Blob([
                // JSON.stringify, not a bare "${...}": the URL comes from
                // document.baseURI / location.href in the bundle, and splicing
                // that into JS source unquoted is how a page URL turns into
                // script running in this worker's (same-origin) context.
                `(function () { var MSCORE_SCRIPT_URL = ${JSON.stringify(MSCORE_SCRIPT_URL)};`  // set the environment variable for worker
                + '(' + shimDom.toString() + ')();'
                + '(' + WebMscoreWorker.toString() + ')()'
                + '})()'
            ])
        )
        /** @private */
        this.worker = new Worker(url)
        /** @private */
        this.workerURL = url
        /** @private */
        this.terminated = false
    }

    /**
     * @returns {Promise<void>}
     */
    static get ready() {
        // not implemented
        return Promise.resolve()
    }

    /**
     * The maximum MSCZ/MSCX file format version supported by webmscore 
     */
    static async version() {
        // not implemented
        return -1
    }

    /**
     * Set log level
     * @param {0 | 1 | 2} level - See https://github.com/LibreScore/webmscore/blob/v1.0.0/src/framework/global/thirdparty/haw_logger/logger/log_base.h#L30-L33
     *  - 0: Off
     *  - 1: Normal (`ERRR` or `WARN` or `INFO`)
     *  - 2: Debug  (`DEBG`)
     * @returns {Promise<void>}
     */
    static async setLogLevel(level) {
        _logLevel = level
    }

    /**
     * Load score data
     * @param {import('../schemas').InputFileFormat} format 
     * @param {Uint8Array} data 
     * @param {Uint8Array[] | Promise<Uint8Array[]>} fonts load extra font files (CJK characters support)
     * @param {boolean} doLayout set to false if you only need the score metadata or the midi file (Super Fast, 3x faster than the musescore software)
     */
    static async load(format, data, fonts = [], doLayout = true) {
        const instance = new WebMscoreW()
        const [_fonts] = await Promise.all([
            fonts,
            instance.rpc('ready')
        ])
        await instance.rpc('setLogLevel', [_logLevel]) // default 0 (Off)
        await instance.rpc('load', [format, data, _fonts, doLayout], [data.buffer, ..._fonts.map(f => f.buffer)])
        return instance
    }

    /**
     * Communicate with the worker thread with JSON-RPC
     * @private
     * @typedef {{ id: number; result?: any; error?: any; }} RPCRes
     * @param {keyof import('./index').default | '_synthAudio' | 'processSynth' | 'processSynthBatch' | 'load' | 'ready' | 'setLogLevel'} method 
     * @param {any[]} params 
     * @param {Transferable[]} transfer
     */
    async rpc(method, params = [], transfer = []) {
        const id = Math.random()

        return new Promise((resolve, reject) => {
            const done = () => {
                this.worker.removeEventListener('message', listener)
                this.worker.removeEventListener('error', onError)
            }

            const listener = (e) => {
                /** @type {RPCRes} */
                const data = e.data
                if (data.id !== id) { return }
                done()
                if (data.error) {
                    reject(new WorkerError(data.error))
                } else {
                    resolve(data.result)
                }
            }

            // A worker that dies - a wasm trap, a module that fails to load -
            // never answers. Without this every call in flight would stay
            // unsettled for the lifetime of the page.
            const onError = (e) => {
                done()
                reject(new Error(`scoreview-engine worker failed: ${e.message || 'unknown error'}`))
            }

            this.worker.addEventListener('message', listener)
            this.worker.addEventListener('error', onError)

            this.worker.postMessage({
                id,
                method,
                params,
            }, transfer)
        })
    }

    /**
     * Only save this excerpt (linked parts) of the score  
     * 
     * if no excerpts, generate excerpts from existing instrument parts
     * 
     * @param {number} id  `-1` means the full score 
     * @returns {Promise<void>}
     */
    setExcerptId(id) {
        return this.rpc('setExcerptId', [id])
    }

    /**
     * @returns {Promise<number>}
     */
    getExcerptId() {
        return this.rpc('getExcerptId')
    }

    /**
     * Generate excerpts from Parts (only parts that are visible) if no existing excerpts
     * @returns {Promise<void>}
     */
    generateExcerpts() {
        return this.rpc('generateExcerpts')
    }

    /**
     * Get the score title
     * @returns {Promise<string>}
     */
    title() {
        return this.rpc('title')
    }

    /**
     * Get the score title (filename safe, replaced some characters)
     * @returns {Promise<string>}
     */
    titleFilenameSafe() {
        return this.rpc('titleFilenameSafe')
    }

    /**
     * Get the number of pages in the score (or the excerpt if `excerptId` is set)
     * @returns {Promise<number>}
     */
    npages() {
        return this.rpc('npages')
    }

    /**
     * Get score metadata
     * @returns {Promise<import('../schemas').ScoreMetadata>}
     */
    metadata() {
        return this.rpc('metadata')
    }

    /**
     * Get the positions of measures
     * @returns {Promise<import('../schemas').Positions>}
     */
    measurePositions() {
        return this.rpc('measurePositions')
    }

    /**
     * Get the positions of segments
     * @returns {Promise<import('../schemas').Positions>}
     */
    segmentPositions() {
        return this.rpc('segmentPositions')
    }

    /**
     * Export score as MusicXML file
     * @returns {Promise<string>} contents of the MusicXML file (plain text)
     */
    saveXml() {
        return this.rpc('saveXml')
    }

    /**
     * Export score as compressed MusicXML file
     * @returns {Promise<Uint8Array>}
     */
    saveMxl() {
        return this.rpc('saveMxl')
    }

    /**
     * Save part score as MSCZ/MSCX file
     * @param {'mscz' | 'mscx'} format 
     * @returns {Promise<Uint8Array>}
     */
    saveMsc(format = 'mscz') {
        return this.rpc('saveMsc', [format])
    }

    /**
     * Export score as the SVG file of one page
     * @param {number} pageNumber integer
     * @param {boolean} drawPageBackground paint a white rectangle over the page
     *        rect before drawing (off by default)
     * @returns {Promise<string>} contents of the SVG file (plain text)
     */
    saveSvg(pageNumber = 0, drawPageBackground = false) {
        return this.rpc('saveSvg', [pageNumber, drawPageBackground])
    }

    /**
     * Export score as the PNG file of one page
     * @param {number} pageNumber integer
     * @param {boolean} drawPageBackground 
     * @param {boolean} transparent
     * @returns {Promise<Uint8Array>}
     */
    savePng(pageNumber = 0, drawPageBackground = false, transparent = true) {
        return this.rpc('savePng', [pageNumber, drawPageBackground, transparent])
    }

    /**
     * Export score as PDF file
     * @returns {Promise<Uint8Array>}
     */
    savePdf() {
        return this.rpc('savePdf')
    }

    /**
     * Export score as MIDI file
     * @param {boolean} midiExpandRepeats 
     * @param {boolean} exportRPNs 
     * @returns {Promise<Uint8Array>}
     */
    saveMidi(midiExpandRepeats = true, exportRPNs = true) {
        return this.rpc('saveMidi', [midiExpandRepeats, exportRPNs])
    }

    /**
     * Set the soundfont (sf2/sf3) data
     * @param {Uint8Array} data 
     * @returns {Promise<void>}
     */
    setSoundFont(data) {
        return this.rpc('setSoundFont', [data], [data.buffer])
    }

    /**
     * Export score as audio file (wav/ogg/flac/mp3)
     * @param {'wav' | 'ogg' | 'flac' | 'mp3'} format 
     * @returns {Promise<Uint8Array>}
     */
    saveAudio(format) {
        return this.rpc('saveAudio', [format])
    }

    /**
     * Read synthesis output params (master + tracks) for the current sequence.
     * @returns {Promise<import('../schemas').AudioOutputParamsSnapshot>}
     */
    getAudioOutputParams() {
        return this.rpc('getAudioOutputParams')
    }

    /**
     * Patch synthesis output params.
     * @param {import('../schemas').AudioOutputParamsPatchRequest} patch
     * @returns {Promise<void>}
     */
    setAudioOutputParams(patch) {
        return this.rpc('setAudioOutputParams', [patch])
    }

    /**
     * Export positions of measures or segments (if `ofSegments` == true) as JSON string
     * @param {boolean} ofSegments
     * @also `score.measurePositions()` and `score.segmentPositions()`
     * @returns {Promise<string>}
     */
    savePositions(ofSegments) {
        return this.rpc('savePositions', [ofSegments])
    }

    /**
     * Synthesize audio frames
     * @param {number} starttime The start time offset in seconds
     * @returns {Promise<(cancel?: boolean) => Promise<import('../schemas').SynthRes>>} The iterator function
     */
    async synthAudio(starttime = 0) {
        // 'synthAudio', not '_synthAudio': the latter does not exist on the
        // worker's WebMscore, so the not-supported path answered with a
        // TypeError instead of the NotSupportedError it means.
        const fnptr = await this.rpc('synthAudio', [starttime])
        return (cancel) => {
            return this.rpc('processSynth', [fnptr, cancel])
        }
    }

    /**
     * Synthesize audio frames in bulk
     * @param {number} starttime - The start time offset in seconds
     * @param {number} batchSize - max number of result SynthRes' (n * 512 frames)
     * @returns {Promise<(cancel?: boolean) => Promise<import('../schemas').SynthRes[]>>}
     */
    async synthAudioBatch(starttime, batchSize) {
        const fnptr = await this.rpc('synthAudioBatch', [starttime, batchSize])
        return (cancel) => {
            return this.rpc('processSynthBatch', [fnptr, batchSize, cancel])
        }
    }

    /**
     * Export score metadata as JSON string
     * @also `score.metadata()`
     * @returns {Promise<string>} contents of the JSON file
     */
    saveMetadata() {
        return this.rpc('saveMetadata')
    }

    /**
     * @param {boolean=} soft (default `true`)
     *                 * `true`  destroy the score instance only, or
     *                 * `false` destroy the whole WebMscore webworker context 
     * @returns {void}
     */
    destroy(soft = true) {
        if (this.terminated) {
            return
        }
        this.terminated = true

        if (soft) {
            // Release the score, then take the worker down with it. Each
            // instance owns exactly one worker and one score and nothing can
            // be loaded into it again, so keeping the worker alive only held
            // on to its whole wasm heap.
            this.rpc('destroy', [true])
                .catch(() => { /* the worker is going away regardless */ })
                .then(() => { this._terminate() })
        } else {
            // destroy the whole WebMscore webworker context
            // the default behaviour prior to v0.9.0
            this._terminate()
        }
    }

    /** @private */
    _terminate() {
        this.worker.terminate()
        URL.revokeObjectURL(this.workerURL) // GC
    }
}

export default WebMscoreW
