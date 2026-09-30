'use strict';
const fetch = require('node-fetch');
const crypto = require('crypto');
const fs = require('fs');
const path = require('path');
const { EventEmitter } = require('events');
const distribution = require('./playtera-distribution');
const approved = new WeakMap();
let running = false;
const root = path.resolve(__dirname, '..');
const statePath = path.join(root, '.playtera-release.json');
const required = ['bin/playtera-exitlag.js', 'bin/PlayTeraExitLagTransport.exe',
    'node_modules/tera-client-interface/index.js', 'node_modules/tera-network-proxy/lib/connection/index.js',
    'bin/index-gui.js', 'bin/update-self.js', 'bin/playtera-update.js', 'bin/playtera-distribution.js'];
const hash = bytes => crypto.createHash('sha256').update(bytes).digest('hex');

function safeRelative(relative) {
    if (typeof relative !== 'string' || relative.length > 240 || !/^[A-Za-z0-9_./ +()-]+$/.test(relative)
        || /^(mods|node_modules\/electron)(\/|$)/i.test(relative) || /^config\.json$/i.test(relative))
        throw new Error('Unsafe update path');
    const parts = relative.split('/');
    if (parts.some(part => !part || part === '.' || part === '..' || part.startsWith('.')
        || /[. ]$/.test(part) || /^(con|prn|aux|nul|com[1-9]|lpt[1-9])(\.|$)/i.test(part)))
        throw new Error('Unsafe update path');
    return parts;
}

function checkedPath(relative) {
    const parts = safeRelative(relative);
    const target = path.join(root, ...parts);
    let current = root;
    for (const part of ['', ...parts]) {
        if (part) current = path.join(current, part);
        if (fs.existsSync(current)) {
            const metadata = fs.lstatSync(current);
            if (metadata.isSymbolicLink() || (current !== target && !metadata.isDirectory()))
                throw new Error('Update path redirects or has an invalid parent');
        }
    }
    if (fs.existsSync(target) && !fs.lstatSync(target).isFile()) throw new Error('Update target is not a file');
    return target;
}

class PlayTeraUpdater extends EventEmitter {
    constructor(branch = 'master') {
        super(); this.setMaxListeners(0);
        const selected = require('./config').loadConfig().branch || branch;
        if (!['master', 'stable'].includes(selected)) throw new Error('Only the PlayTera stable update channel is supported');
        this.branch = 'stable';
    }

    async download(relative, limit) {
        if (!/^(toolbox-stable\/manifest\.(json|sig)|v[0-9]+\.[0-9]+\.[0-9]+\/[a-f0-9]{64}\.bin)$/.test(relative)
            || !Number.isSafeInteger(limit) || limit < 1 || limit > 32 * 1024 * 1024)
            throw new Error('Untrusted update asset');
        let url = new URL(relative, distribution.updateBase);
        if (url.origin !== 'https://github.com' || url.pathname !== `/ImakPwnz/PlayTera-Toolbox/releases/download/${relative}`
            || url.username || url.password || url.search || url.hash) throw new Error('Untrusted update URL');
        const assetHosts = ['release-assets.githubusercontent.com', 'objects.githubusercontent.com'];
        for (let redirects = 0; redirects <= 5; redirects++) {
            let response;
            try { response = await fetch(url.toString(), { redirect: 'manual', timeout: 20000, size: limit }); }
            catch (_) { throw new Error('GitHub asset request failed'); }
            if ([301, 302, 303, 307, 308].includes(response.status)) {
                const location = response.headers.get('location');
                if (response.body && typeof response.body.destroy === 'function') response.body.destroy();
                if (!location || redirects === 5) throw new Error('Invalid or excessive GitHub asset redirects');
                const next = new URL(location, url);
                if (next.protocol !== 'https:' || next.username || next.password || next.hash
                    || (next.port && next.port !== '443') || !assetHosts.includes(next.hostname))
                    throw new Error('Untrusted GitHub asset redirect');
                url = next;
                continue;
            }
            if (!response.ok) throw new Error(`PlayTera update HTTP ${response.status}`);
            const advertised = response.headers.get('content-length');
            if (advertised && (!/^[0-9]+$/.test(advertised) || Number(advertised) > limit))
                throw new Error('PlayTera update size limit exceeded');
            let bytes;
            try { bytes = await response.buffer(); }
            catch (_) { throw new Error('GitHub asset response could not be read within the size limit'); }
            if (bytes.length > limit) throw new Error('PlayTera update size limit exceeded');
            return bytes;
        }
        throw new Error('GitHub asset download did not finish');
    }

    async check(serverIndex = 0) {
        this.emit('check_start', serverIndex);
        if (!distribution.updatesEnabled) {
            const result = Object.freeze({ serverIndex, operations: Object.freeze([]) });
            approved.set(result, { manifest: null, operations: [] });
            this.emit('check_success', serverIndex, result.operations);
            return result;
        }
        try {
            const raw = await this.download('toolbox-stable/manifest.json', 2 * 1024 * 1024);
            const signatureText = (await this.download('toolbox-stable/manifest.sig', 200)).toString('ascii').trim();
            if (!/^[A-Za-z0-9+/]{86}==$/.test(signatureText)) throw new Error('Invalid manifest signature encoding');
            const publicBytes = Buffer.from(distribution.updatePublicKey, 'base64');
            if (publicBytes.length !== 32) throw new Error('Invalid PlayTera update public key');
            const publicKey = crypto.createPublicKey({ key: Buffer.concat([
                Buffer.from('302a300506032b6570032100', 'hex'), publicBytes]), format: 'der', type: 'spki' });
            if (!crypto.verify(null, Buffer.concat([Buffer.from(distribution.signatureDomain), raw]), publicKey,
                Buffer.from(signatureText, 'base64'))) throw new Error('Untrusted PlayTera update signature');
            const manifest = JSON.parse(raw.toString('utf8'));
            let installedSequence = distribution.sequence;
            if (fs.existsSync(statePath)) {
                if (!fs.lstatSync(statePath).isFile() || fs.lstatSync(statePath).isSymbolicLink()) throw new Error('Invalid local update state');
                const state = JSON.parse(fs.readFileSync(statePath, 'utf8'));
                if (!Number.isSafeInteger(state.sequence) || state.sequence < distribution.sequence) throw new Error('Invalid local update sequence');
                installedSequence = state.sequence;
            }
            if (manifest.schemaVersion !== 1 || manifest.channel !== 'stable'
                || !/^[0-9]+\.[0-9]+\.[0-9]+$/.test(manifest.version)
                || !Number.isSafeInteger(manifest.sequence) || manifest.sequence < installedSequence
                || !manifest.files || Array.isArray(manifest.files) || typeof manifest.files !== 'object')
                throw new Error('Unsupported or older PlayTera update manifest');
            const entries = Object.entries(manifest.files);
            if (!entries.length || entries.length > 2000 || required.some(file => !Object.prototype.hasOwnProperty.call(manifest.files, file)))
                throw new Error('Incomplete PlayTera update manifest');
            const seen = new Set(), operations = [];
            let total = 0;
            for (const [relative, entry] of entries) {
                const target = checkedPath(relative);
                if (seen.has(relative.toLowerCase())) throw new Error('Duplicate Windows update path');
                seen.add(relative.toLowerCase());
                if (!entry || !/^[a-f0-9]{64}$/.test(entry.sha256)
                    || !Number.isSafeInteger(entry.size) || entry.size < 1 || entry.size > 32 * 1024 * 1024)
                    throw new Error('Invalid update file metadata');
                total += entry.size;
                if (total > 128 * 1024 * 1024) throw new Error('Update exceeds total size limit');
                if (!fs.existsSync(target) || hash(fs.readFileSync(target)) !== entry.sha256)
                    operations.push(Object.freeze({ type: 'update', relpath: relative, abspath: target,
                        hash: entry.sha256.toUpperCase(), size: entry.size }));
            }
            const result = Object.freeze({ serverIndex, operations: Object.freeze(operations) });
            approved.set(result, { manifest, operations });
            this.emit('check_success', serverIndex, operations);
            return result;
        } catch (error) {
            this.emit('check_fail', serverIndex, error); this.emit('check_fail_all'); return null;
        }
    }

    async run(checkResult = null) {
        if (running) throw new Error('Another PlayTera update is already running');
        running = true;
        try { return await this.runApproved(checkResult); }
        finally { running = false; }
    }

    async runApproved(checkResult = null) {
        this.emit('run_start');
        const result = checkResult || await this.check();
        const release = result && approved.get(result);
        if (!release) { this.emit('run_finish', false); return false; }
        if (!release.operations.length) { this.emit('run_finish', true); return false; }
        approved.delete(result);
        // A previous check may have become obsolete while another update completed.
        if (fs.existsSync(statePath) && JSON.parse(fs.readFileSync(statePath, 'utf8')).sequence > release.manifest.sequence) {
            this.emit('run_finish', false); return false;
        }
        let stage = null;
        const journal = [];
        try {
            this.emit('prepare_start');
            stage = fs.mkdtempSync(path.join(root, '.playtera-update-'));
            for (let index = 0; index < release.operations.length; index++) {
                const operation = release.operations[index];
                checkedPath(operation.relpath);
                this.emit('download_start', result.serverIndex, operation.relpath);
                const bytes = await this.download(`v${release.manifest.version}/${operation.hash.toLowerCase()}.bin`, operation.size);
                if (bytes.length !== operation.size || hash(bytes).toUpperCase() !== operation.hash) {
                    this.emit('download_error', operation.relpath, operation.hash, hash(bytes));
                    throw new Error('PlayTera update file integrity check failed');
                }
                fs.writeFileSync(path.join(stage, `${index}.new`), bytes, { flag: 'wx' });
                this.emit('download_finish', result.serverIndex, operation.relpath);
            }
            this.emit('prepare_finish'); this.emit('execute_start');
            for (let index = 0; index < release.operations.length; index++) {
                const operation = release.operations[index];
                const target = checkedPath(operation.relpath);
                fs.mkdirSync(path.dirname(target), { recursive: true }); checkedPath(operation.relpath);
                const item = { target, backup: path.join(stage, `${index}.old`), existed: fs.existsSync(target), installed: false };
                journal.push(item); this.emit('install_start', operation.relpath);
                if (item.existed) fs.renameSync(target, item.backup);
                fs.renameSync(path.join(stage, `${index}.new`), target); item.installed = true;
                this.emit('install_finish', operation.relpath);
            }
            const stateNew = path.join(stage, 'state.new');
            fs.writeFileSync(stateNew, JSON.stringify({ sequence: release.manifest.sequence, version: release.manifest.version }), { flag: 'wx' });
            if (fs.existsSync(statePath) && fs.lstatSync(statePath).isSymbolicLink()) throw new Error('Unsafe update state');
            fs.renameSync(stateNew, statePath);
            this.emit('execute_finish'); this.emit('run_finish', true);
            try { fs.rmSync(stage, { recursive: true }); } catch (_) { this.emit('cleanup_error', stage); }
            return true;
        } catch (error) {
            let restored = true;
            for (const item of journal.reverse()) {
                try {
                    if (item.installed) fs.unlinkSync(item.target);
                    if (item.existed && fs.existsSync(item.backup)) fs.renameSync(item.backup, item.target);
                } catch (_) { restored = false; }
            }
            if (stage && restored) { try { fs.rmSync(stage, { recursive: true }); } catch (_) {} }
            this.emit('install_error', 'PlayTera update', error); this.emit('run_finish', false); return false;
        }
    }
}
module.exports = PlayTeraUpdater;
