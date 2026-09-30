'use strict';

const { Duplex } = require('stream');
const { spawn } = require('child_process');
const crypto = require('crypto');
const fs = require('fs');
const path = require('path');

const HELPER_SHA256 = '0fc5d48d42fe74dcd0d1380b6e828db7ef73bc006f3c650f983204241fdc1cf4';

function transportError(reason) {
    const error = new Error(`PlayTera ExitLag transport: ${reason}. Use the matching Toolbox and game integration candidate; no direct fallback is allowed.`);
    error.code = 'PLAYTERA_EXITLAG_TRANSPORT';
    error.address = '1.2.3.14';
    error.port = 38101;
    return error;
}

class PlayTeraExitLagSocket extends Duplex {
    constructor(pid, eligible) {
        super({ autoDestroy: true, allowHalfOpen: false });
        this.gamePid = pid;
        this.eligible = eligible;
        this.remoteAddress = '1.2.3.14';
        this.remotePort = 38101;
        this.child = null;
        this.connected = false;
        this.controlBuffer = '';
        this.constructCallback = null;
        this.startupTimer = null;
        this.finishTimer = null;
    }

    _construct(callback) {
        this.constructCallback = callback;
        try {
            if (!this.eligible || !Number.isInteger(this.gamePid) || this.gamePid <= 0 || this.gamePid > 0xffffffff)
                throw transportError('invalid PlayTera target or game PID');
            const executable = path.join(__dirname, 'PlayTeraExitLagTransport.exe');
            const metadata = fs.lstatSync(executable);
            if (!metadata.isFile() || metadata.isSymbolicLink() || metadata.size > 4 * 1024 * 1024)
                throw transportError('invalid native helper');
            const checksum = crypto.createHash('sha256').update(fs.readFileSync(executable)).digest('hex');
            if (checksum !== HELPER_SHA256) throw transportError('native helper checksum mismatch');
            this.child = spawn(executable, [String(this.gamePid)], { windowsHide: true, stdio: ['pipe', 'pipe', 'pipe'] });
            this.child.stdout.pause();
            this.child.stdout.on('data', chunk => {
                if (!this.push(chunk)) this.child.stdout.pause();
            });
            this.child.stdout.on('end', () => { if (this.connected) this.push(null); });
            this.child.stdout.on('error', () => this._fail('helper output failed'));
            this.child.stdin.on('error', () => this._fail('helper input failed'));
            this.child.stderr.on('error', () => this._fail('helper control channel failed'));
            this.child.stderr.on('data', chunk => this._control(chunk));
            this.child.on('error', () => this._fail('native helper could not start'));
            this.child.on('close', code => {
                if (!this.destroyed && (!this.connected || code !== 0)) this._fail(`native helper exited (${code})`);
                else if (this.connected && !this.destroyed) this.push(null);
            });
            this.startupTimer = setTimeout(() => this._fail('startup timed out'), 15000);
        } catch (error) {
            this._fail(error.code === 'PLAYTERA_EXITLAG_TRANSPORT' ? error.message : 'native helper missing or unreadable');
        }
    }

    _control(chunk) {
        if (this.destroyed) return;
        this.controlBuffer += chunk.toString('utf8');
        if (this.controlBuffer.length > 4096) return this._fail('invalid helper control data');
        let end = this.controlBuffer.indexOf('\n');
        while (end >= 0 && !this.destroyed) {
            const line = this.controlBuffer.slice(0, end);
            this.controlBuffer = this.controlBuffer.slice(end + 1);
            let message;
            try { message = JSON.parse(line); } catch (_) { return this._fail('invalid helper control message'); }
            if (message.event === 'connected' && !this.connected && this.constructCallback) {
                clearTimeout(this.startupTimer);
                this.connected = true;
                const callback = this.constructCallback;
                this.constructCallback = null;
                callback();
                this.emit('connect');
                if (!this.destroyed) this.child.stdout.resume();
            } else if (message.event === 'error' && typeof message.domain === 'string' && /^[a-z_]{1,40}$/.test(message.domain)
                && Number.isInteger(message.code) && message.code >= 0 && message.code <= 0xffffffff) {
                return this._fail(`${message.domain}, code ${message.code}`);
            } else if (message.event !== 'closed' || !this.connected) {
                return this._fail('unexpected helper control message');
            }
            end = this.controlBuffer.indexOf('\n');
        }
    }

    _fail(reason) {
        const error = transportError(reason);
        if (this.constructCallback) {
            const callback = this.constructCallback;
            this.constructCallback = null;
            callback(error);
        } else if (!this.destroyed) {
            this.destroy(error);
        }
    }

    _read() {
        if (this.connected && this.child) this.child.stdout.resume();
    }

    _write(chunk, encoding, callback) {
        this.child.stdin.write(chunk, encoding, callback);
    }

    _final(callback) {
        this.finishTimer = setTimeout(() => this.destroy(), 7000);
        this.finishTimer.unref();
        this.child.stdin.end(callback);
    }

    _destroy(error, callback) {
        clearTimeout(this.startupTimer);
        clearTimeout(this.finishTimer);
        if (this.child) {
            this.child.stdin.destroy();
            this.child.stdout.destroy();
            this.child.stderr.destroy();
            this.child.kill();
        }
        callback(error);
    }

    setNoDelay() { return this; }
    unref() { if (this.child) this.child.unref(); return this; }
}

module.exports = function createPlayTeraExitLagSocket(target, metadata, clientInterfaceConnection) {
    return new PlayTeraExitLagSocket(clientInterfaceConnection && clientInterfaceConnection.info && clientInterfaceConnection.info.pid,
        target.ip === '1.2.3.14' && target.port === 38101 && metadata && metadata.serverId === 341);
};
