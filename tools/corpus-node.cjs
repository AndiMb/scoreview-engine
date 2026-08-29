#!/usr/bin/env node
/**
 * Convert a corpus of scores with the wasm build under Node and fingerprint
 * every output — the wasm twin of corpus-native.py, same report shape.
 *
 * Phase-5 acceptance: this report must match the native build's exactly
 * (corpus-compare.py native.json node.json --exact-svg — no waivers, no
 * allowed metadata drift; both pipelines share sve::loadScore and the same
 * writers, so any difference is a wasm-side bug).
 *
 *   node corpus-node.cjs [--module ../web-public/scoreview.nodejs.cjs] \
 *       [--scores ../musescore/vtest/scores] [--out report.json] [--limit N]
 */

const fs = require('fs')
const path = require('path')

const args = process.argv.slice(2)
const argOf = (name, fallback) => {
    const i = args.indexOf(name)
    return i === -1 ? fallback : args[i + 1]
}

const modulePath = path.resolve(argOf('--module', path.join(__dirname, '..', 'web-public', 'scoreview.nodejs.cjs')))
const scoresDir = path.resolve(argOf('--scores', path.join(__dirname, '..', 'musescore', 'vtest', 'scores')))
const outFile = argOf('--out', 'corpus-node.json')
const limit = parseInt(argOf('--limit', '0'), 10)

const imported = require(modulePath)
const WebMscore = imported.default || imported

// Fields that move on their own — keep in step with corpus-native.py.
const VOLATILE = new Set(['programVersion', 'programRevision', 'mscoreVersion', 'encoding-date', 'title'])

const stableMeta = (meta) => {
    const out = {}
    for (const key of Object.keys(meta).sort()) {
        if (VOLATILE.has(key)) continue
        const v = meta[key]
        // Keep the shape, not the contents, for the big nested fields.
        if (Array.isArray(v)) out[key] = v.length
        else if (v && typeof v === 'object') out[key] = Object.keys(v).length
        else out[key] = v
    }
    return out
}

const fingerprint = async (file) => {
    const data = new Uint8Array(fs.readFileSync(file))
    const score = await WebMscore.load('mscz', data, [], true)
    try {
        const meta = JSON.parse(await score.saveMetadata())
        const positions = JSON.parse(await score.savePositions(false))
        const segments = JSON.parse(await score.savePositions(true))
        const midi = await score.saveMidi(true, true)
        const svg = await score.saveSvg(0, false)

        return {
            pages: await score.npages(),
            measures: meta.measures,
            parts: (meta.parts || []).length,
            posElements: (positions.elements || []).length,
            posMeasures: (positions.events || []).length,
            segElements: (segments.elements || []).length,
            midiBytes: midi.length,
            meta: stableMeta(meta),
            svgBytes: Buffer.byteLength(svg, 'utf8'),
        }
    } finally {
        score.destroy()
    }
}

const main = async () => {
    await WebMscore.ready
    await WebMscore.setLogLevel(0)

    const files = fs.readdirSync(scoresDir).sort()
        .filter((name) => /\.mscz$/i.test(name))
        .map((name) => path.join(scoresDir, name))
    const selected = limit > 0 ? files.slice(0, limit) : files

    const report = { module: modulePath, scores: {} }
    let ok = 0, failed = 0
    const t0 = Date.now()

    for (const file of selected) {
        const key = path.basename(file)
        try {
            report.scores[key] = await fingerprint(file)
            ok++
        } catch (err) {
            report.scores[key] = { error: String(err && err.message ? err.message : err) }
            failed++
        }
        if ((ok + failed) % 50 === 0) {
            process.stderr.write(`  ${ok + failed}/${selected.length}\n`)
        }
    }

    const seconds = (Date.now() - t0) / 1000
    fs.writeFileSync(outFile, JSON.stringify(report, null, 1))
    console.log(`${selected.length} scores: ${ok} converted, ${failed} failed in ${seconds.toFixed(1)}s -> ${outFile}`)
}

main().catch((err) => { console.error(err); process.exit(1) })
