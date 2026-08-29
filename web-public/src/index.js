// @ts-check

// scoreview-engine: the Qt-free successor of webmscore, API-compatible for
// the supported surface (SVG, MIDI, positions, metadata). Everything the Qt
// build provided through Qt — audio synthesis, soundfonts, PNG/PDF,
// MusicXML, MSCZ writing, excerpts, extra fonts — throws a NotSupportedError
// here, so a consumer migrating from webmscore fails loudly, not silently.

import {
    Module,
    RuntimeInitialized,
    getStrPtr,
    getTypedArrayPtr,
    WasmRes,
    freePtr,
} from './helper.js'

export class NotSupportedError extends Error {
    /** @param {string} what */
    constructor(what) {
        super(`${what} is not supported by scoreview-engine (the Qt-free build); use webmscore if you need it`)
        this.name = 'NotSupportedError'
    }
}

/**
 * Don't turn off logs if already set log level before `WebMscore.load(...)` is called
 * @see WebMscore.setLogLevel
 */
let _hasLogLevelSet = false

class WebMscore {

    /**
     * This promise is resolved when the runtime is fully initialized
     * @returns {Promise<void>}
     */
    static get ready() {
        return RuntimeInitialized
    }

    /**
     * The maximum MSCZ/MSCX file format version supported
     * @returns {Promise<number>} e.g. `470`
     */
    static async version() {
        await WebMscore.ready
        return Module.ccall('version', 'number')
    }

    /**
     * Set log level
     * @param {0 | 1 | 2} level
     *  - 0: Off
     *  - 1: Normal (`ERRR` or `WARN` or `INFO`)
     *  - 2: Debug  (`DEBG`)
     * @returns {Promise<void>}
     */
    static async setLogLevel(level) {
        _hasLogLevelSet = true
        await WebMscore.ready
        return Module.ccall('setLogLevel', null, ['number'], [level])
    }

    /**
     * Set custom stdout instead of `console.log`
     * Available before `WebMscore.ready`
     * @private Node.js exclusive
     * @param {(byte: number) => any} write
     */
    static set stdout(write) {
        Module.stdout = write
    }
    /** @private */
    static get stdout() {
        return Module.stdout
    }

    /**
     * Set custom stderr instead of `console.warn`
     * Available before `WebMscore.ready`
     * @private Node.js exclusive
     * @param {(byte: number) => any} write
     */
    static set stderr(write) {
        Module.stderr = write
    }
    /** @private */
    static get stderr() {
        return Module.stderr
    }

    /**
     * Load score data
     * @param {'mscz' | 'mscx'} format
     * @param {Uint8Array} data
     * @param {Uint8Array[] | Promise<Uint8Array[]>} fonts NOT SUPPORTED — pass `[]`
     * @param {boolean} doLayout set to false if you only need the score metadata or the midi file
     * @returns {Promise<WebMscore>}
     */
    static async load(format, data, fonts = [], doLayout = true) {
        const [_fonts] = await Promise.all([
            fonts,
            WebMscore.ready
        ])

        if (_fonts && _fonts.length > 0) {
            throw new NotSupportedError('loading extra fonts')
        }

        const fileformatptr = getStrPtr(format)
        const dataptr = getTypedArrayPtr(data)

        // get the pointer to the MasterScore class instance in C
        const resptr = Module.ccall('load',  // name of C function
            'number',  // return type
            ['number', 'number', 'number', 'boolean'],  // argument types
            [fileformatptr, dataptr, data.byteLength, doLayout]  // arguments
        )
        freePtr(fileformatptr)
        freePtr(dataptr)
        const scoreptr = WasmRes.readNum(resptr)

        if (!_hasLogLevelSet) {
            // turn off logs by default
            await WebMscore.setLogLevel(0);
        }

        const mscore = new WebMscore(scoreptr)
        return mscore
    }

    /**
     * NOT SUPPORTED — extra font loading (CJK) needs the Qt build
     * @private
     * @param {string | Uint8Array} font
     * @returns {Promise<boolean>}
     */
    static async addFont(font) {  // eslint-disable-line no-unused-vars
        throw new NotSupportedError('addFont')
    }

    /**
     * NOT SUPPORTED — audio needs the Qt build
     * @private
     * @param {Uint8Array} data
     */
    static async setSoundFont(data) {  // eslint-disable-line no-unused-vars
        throw new NotSupportedError('setSoundFont')
    }

    /**
     * @hideconstructor use `WebMscore.load`
     * @param {number} scoreptr the pointer to the MasterScore class instance in C++
     */
    constructor(scoreptr) {
        /** @private */
        this.scoreptr = scoreptr

        /** @private */
        this.excerptId = -1

        /** @private */
        this.destroyed = false
    }

    /**
     * Only the full score is supported: `-1` passes, anything else throws
     * @param {number} id
     */
    async setExcerptId(id) {
        if (id !== -1) {
            throw new NotSupportedError('excerpts')
        }
        this.excerptId = id
    }

    async getExcerptId() {
        return this.excerptId
    }

    /** NOT SUPPORTED */
    async generateExcerpts() {
        throw new NotSupportedError('excerpts')
    }

    /**
     * Get the score title
     * @returns {Promise<string>}
     */
    async title() {
        const dataptr = Module.ccall('title', 'number', ['number'], [this.scoreptr])
        return WasmRes.readText(dataptr)
    }

    /**
     * Get the score title (filename safe, replaced some characters)
     */
    async titleFilenameSafe() {
        const title = await this.title()
        return title.replace(/[\s<>:{}"/\\|?*~.\0\cA-\cZ]+/g, '_')
    }

    /**
     * Get the number of pages in the score
     * @returns {Promise<number>}
     */
    async npages() {
        const dataptr = Module.ccall('npages', 'number', ['number', 'number'], [this.scoreptr, this.excerptId])
        return WasmRes.readNum(dataptr)
    }

    /**
     * Get score metadata
     * @returns {Promise<import('../schemas').ScoreMetadata>}
     */
    async metadata() {
        return JSON.parse(await this.saveMetadata())
    }

    /**
     * Get the positions of measures
     * @returns {Promise<import('../schemas').Positions>}
     */
    async measurePositions() {
        return JSON.parse(await this.savePositions(false))
    }

    /**
     * Get the positions of segments
     * @returns {Promise<import('../schemas').Positions>}
     */
    async segmentPositions() {
        return JSON.parse(await this.savePositions(true))
    }

    /** NOT SUPPORTED — MusicXML export needs the Qt build */
    async saveXml() {
        throw new NotSupportedError('saveXml')
    }

    /** NOT SUPPORTED — MusicXML export needs the Qt build */
    async saveMxl() {
        throw new NotSupportedError('saveMxl')
    }

    /**
     * NOT SUPPORTED — MSCZ/MSCX writing needs the Qt build
     * @param {'mscz' | 'mscx'} format
     */
    async saveMsc(format = 'mscz') {  // eslint-disable-line no-unused-vars
        throw new NotSupportedError('saveMsc')
    }

    /**
     * Export score as the SVG file of one page
     * @param {number} pageNumber integer
     * @param {boolean} drawPageBackground ignored — the writer never paints a background
     * @returns {Promise<string>} contents of the SVG file (plain text)
     */
    async saveSvg(pageNumber = 0, drawPageBackground = false) {
        const dataptr = Module.ccall('saveSvg',
            'number',
            ['number', 'number', 'boolean', 'number'],
            [this.scoreptr, pageNumber, drawPageBackground, this.excerptId]
        )
        return WasmRes.readText(dataptr)
    }

    /** NOT SUPPORTED — raster export needs the Qt build */
    async savePng() {
        throw new NotSupportedError('savePng')
    }

    /** NOT SUPPORTED — PDF export needs the Qt build */
    async savePdf() {
        throw new NotSupportedError('savePdf')
    }

    /**
     * Export score as MIDI file
     * @param {boolean} midiExpandRepeats
     * @param {boolean} exportRPNs
     * @returns {Promise<Uint8Array>}
     */
    async saveMidi(midiExpandRepeats = true, exportRPNs = true) {
        const dataptr = Module.ccall('saveMidi',
            'number',
            ['number', 'boolean', 'boolean', 'number'],
            [this.scoreptr, midiExpandRepeats, exportRPNs, this.excerptId]
        )
        return WasmRes.readData(dataptr)
    }

    /** NOT SUPPORTED — audio needs the Qt build */
    async setSoundFont() {
        throw new NotSupportedError('setSoundFont')
    }

    /** NOT SUPPORTED — audio needs the Qt build */
    async saveAudio() {
        throw new NotSupportedError('saveAudio')
    }

    /** NOT SUPPORTED — audio needs the Qt build */
    async getAudioOutputParams() {
        throw new NotSupportedError('getAudioOutputParams')
    }

    /** NOT SUPPORTED — audio needs the Qt build */
    async setAudioOutputParams() {
        throw new NotSupportedError('setAudioOutputParams')
    }

    /** NOT SUPPORTED — audio needs the Qt build */
    async synthAudio() {
        throw new NotSupportedError('synthAudio')
    }

    /** NOT SUPPORTED — audio needs the Qt build */
    async synthAudioBatch() {
        throw new NotSupportedError('synthAudioBatch')
    }

    /** @private NOT SUPPORTED */
    async processSynth() {
        throw new NotSupportedError('processSynth')
    }

    /** @private NOT SUPPORTED */
    async processSynthBatch() {
        throw new NotSupportedError('processSynthBatch')
    }

    /**
     * Export positions of measures or segments (if `ofSegments` == true) as JSON
     * @param {boolean} ofSegments
     * @also `score.measurePositions()` and `score.segmentPositions()`
     * @returns {Promise<string>}
     */
    async savePositions(ofSegments) {
        const dataptr = Module.ccall('savePositions',
            'number',
            ['number', 'boolean', 'number'],
            [this.scoreptr, ofSegments, this.excerptId]
        )
        return WasmRes.readText(dataptr)
    }

    /**
     * Export score metadata as JSON text
     * @also `score.metadata()`
     * @returns {Promise<string>} contents of the JSON file
     */
    async saveMetadata() {
        const dataptr = Module.ccall('saveMetadata', 'number', ['number'], [this.scoreptr])
        return WasmRes.readText(dataptr)
    }

    /**
     * Release this score and everything the engine still holds for it.
     *
     * Safe to call more than once, and safe to keep loading further scores in
     * the same process afterwards.
     *
     * @param {boolean=} soft (default `true`)
     *                 * `true`  destroy this score instance only, or
     *                 * `false` also reset the whole engine context, which
     *                           invalidates every other score still loaded
     * @returns {void}
     */
    destroy(soft = true) {
        if (this.destroyed) {
            return
        }
        this.destroyed = true

        Module.ccall('destroy', 'void', ['number'], [this.scoreptr])

        // NOTE Do not free(this.scoreptr). It is the address of a C++ object
        // that destroy() has just released, not a buffer allocated here.

        if (!soft) {
            Module.ccall('destroyAll', 'void', [], [])
        }
    }

}

export default WebMscore
