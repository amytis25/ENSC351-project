"use strict";
/*
 * UDP-based door server bridge for web UI.
 *
 * - Sends COMMAND datagrams to hub at HUB_HOST:HUB_PORT (default 192.168.8.108:12345)
 * - Listens for incoming datagrams from hub and forwards to web clients
 * - Emits FEEDBACK messages for matching command ids back to requestor
 */

const dgram = require('dgram');
const socketio = require('socket.io');

const HUB_HOST = process.env.HUB_HOST || '192.168.8.108';
const HUB_PORT = parseInt(process.env.HUB_PORT || '12345', 10);
const UDP_FAMILY = 'udp4';

let io = null;
let udpSocket = null;        // single socket for sending/receiving
let cmdCounter = 1;          // simple increasing command id

// pending map: cmdid -> { socket, timer }
const pending = new Map();
const PENDING_TIMEOUT_MS = 5000;

// ---------- UDP Socket setup ----------

function startUdpSocket() {
    if (udpSocket) return;

    udpSocket = dgram.createSocket(UDP_FAMILY);

    udpSocket.on('error', (err) => {
        console.error('UDP socket error:', err);
    });

    udpSocket.on('listening', () => {
        const address = udpSocket.address();
        console.log(`UDP socket listening ${address.address}:${address.port}`);
    });

    udpSocket.on('message', (msgBuf, rinfo) => {
        const msg = msgBuf.toString('utf8').trim();
        console.log(`UDP rx from ${rinfo.address}:${rinfo.port} -> ${msg}`);

        const parts = msg.split(/\s+/);
        if (parts.length < 2) {
            if (io) io.sockets.emit('hub-raw', { raw: msg });
            return;
        }

        const moduleId = parts[0];
        const type = parts[1];

        if (type === 'FEEDBACK' && parts.length >= 5) {
            // <MODULE> FEEDBACK <CMDID> <TARGET> <ACTION...>
            const cmdid = parseInt(parts[2], 10);
            const target = parts[3];
            const rawAction = parts.slice(4).join(' '); // may be STATUS_* or CLOSED,UNLOCKED, etc.

            console.log(`[FEEDBACK] cmdid=${cmdid}, target=${target}, action="${rawAction}", pending.size=${pending.size}`);
            console.log(`[FEEDBACK] Full parts array:`, parts);
            console.log(`[FEEDBACK] Raw message: "${msg}"`);

            // If a pending request exists, emit FEEDBACK directly back to that socket
            const p = pending.get(cmdid);
            if (p) {
                console.log(`[FEEDBACK] Found pending entry for cmdid ${cmdid}, emitting to socket`);
                try {
                    p.socket.emit('command-feedback', {
                        module: moduleId,
                        cmdid,
                        target,
                        action: rawAction,
                        raw: msg,
                    });
                } catch (e) {
                    console.error('Error emitting feedback to socket:', e);
                }
                clearTimeout(p.timer);
                pending.delete(cmdid);
            } else {
                console.log(`[FEEDBACK] NO pending entry for cmdid ${cmdid}. Pending keys: [${Array.from(pending.keys()).join(', ')}]`);
            }

            // Broadcast to *all* web clients as well for UI status updates
            if (io) {
                // Normalize to STATUS_* if needed.
                // If doormod already sends STATUS_*, just passthrough.
                let formattedAction = rawAction;
                const upper = rawAction.toUpperCase().trim();

                if (upper.startsWith('STATUS_')) {
                    formattedAction = upper; // e.g., STATUS_LOCKED
                    console.log(`[FEEDBACK] Using STATUS_* directly: ${formattedAction}`);
                } else if (upper === 'STATUS') {
                    // Bare STATUS command - we need to query the hub for the actual status
                    // For now, default to UNKNOWN until the module sends proper STATUS_* responses
                    formattedAction = 'STATUS_UNKNOWN';
                    console.log(`[FEEDBACK] Got bare STATUS, defaulting to STATUS_UNKNOWN (module should send STATUS_LOCKED or STATUS_UNLOCKED)`);
                } else if (upper.includes('OPEN') || upper.includes('CLOSED')) {
                    // Legacy style, like "CLOSED,UNLOCKED" or "OPEN,LOCKED"
                    const tokens = upper
                        .split(/[,\s]+/)
                        .map(s => s && s.trim())
                        .filter(Boolean);

                    let doorState = null;
                    let lockState = null;
                    for (const tok of tokens) {
                        if (tok === 'OPEN' || tok === 'CLOSED') doorState = tok;
                        if (tok === 'LOCKED' || tok === 'UNLOCKED') lockState = tok;
                    }

                    // Prefer lockState for the toggle logic, else doorState
                    if (lockState) {
                        formattedAction = `STATUS_${lockState}`;
                    } else if (doorState) {
                        formattedAction = `STATUS_${doorState}`;
                    } else {
                        formattedAction = 'STATUS_UNKNOWN';
                    }

                    console.log(`[FEEDBACK] Converted legacy "${upper}" -> ${formattedAction}`);
                }

                // New, normalized channel for web_control.js
                io.sockets.emit('door-feedback', {
                    module: moduleId,
                    target,
                    action: formattedAction,
                });

                // Also broadcast raw/legacy command-feedback for compatibility
                io.sockets.emit('command-feedback', {
                    module: moduleId,
                    cmdid,
                    target,
                    action: rawAction,
                    raw: msg,
                });
            }

            return;
        }

        // Other hub messages
        if (io) {
            if (type === 'EVENT' && parts.length >= 4) {
                // e.g. D1 EVENT D0 DOOR OPEN
                const target = parts[2];
                const event = parts.slice(3).join(' ');
                io.sockets.emit('hub-event', {
                    module: moduleId,
                    type: 'EVENT',
                    target,
                    event,
                    raw: msg,
                });
            } else if (type === 'HEARTBEAT') {
                io.sockets.emit('hub-heartbeat', { module: moduleId, raw: msg });
            } else if (type === 'HELLO') {
                io.sockets.emit('hub-hello', { module: moduleId, raw: msg });
            } else {
                io.sockets.emit('hub-raw', { raw: msg });
            }
        }
    });

    // Bind to ephemeral port so hub can send FEEDBACK back
    udpSocket.bind(0, '0.0.0.0');
}

// ---------- COMMAND sending ----------

function sendCommand(moduleId, target, action, socket) {
    if (!udpSocket) startUdpSocket();
    if (!moduleId) moduleId = 'D1';
    if (!target) target = 'D0';
    if (!action) action = '';

    const cmdid = cmdCounter++;
    const payload = `${moduleId} COMMAND ${cmdid} ${target} ${action}\n`;
    const buf = Buffer.from(payload, 'utf8');

    udpSocket.send(buf, 0, buf.length, HUB_PORT, HUB_HOST, (err) => {
        if (err) {
            console.error('UDP send error:', err);
            if (socket) socket.emit('command-error', { module: moduleId, cmdid, error: String(err) });
            return;
        }
        console.log(`Sent COMMAND to ${HUB_HOST}:${HUB_PORT} -> ${payload.trim()}`);

        // Track pending FEEDBACK if we were given a socket
        if (socket) {
            console.log(`[PENDING] Registering cmdid=${cmdid} for ${moduleId}`);
            const timer = setTimeout(() => {
                console.log(`[PENDING] Timeout for cmdid ${cmdid}`);
                pending.delete(cmdid);
                try {
                    socket.emit('command-error', {
                        module: moduleId,
                        cmdid,
                        error: 'No FEEDBACK from hub',
                    });
                } catch (e) {
                    console.error('Error emitting timeout to socket:', e);
                }
            }, PENDING_TIMEOUT_MS);

            pending.set(cmdid, { socket, timer });
        }
    });
}

// ---------- Socket.IO handlers ----------

function handleSocket(ws) {
    console.log('Client connected');

    ws.on('send-command', (payload) => {
        const moduleId = payload.module || 'D1';
        const target = payload.target || 'D0';
        const action = payload.action || 'STATUS';
        console.log('Web->server send-command', payload);
        sendCommand(moduleId, target, action, ws);
    });

    // Convenience: raw text send (not usually needed)
    ws.on('raw-send', (raw) => {
        if (!raw) return;
        if (!udpSocket) startUdpSocket();
        const buf = Buffer.from(String(raw), 'utf8');
        udpSocket.send(buf, 0, buf.length, HUB_PORT, HUB_HOST, (err) => {
            if (err) ws.emit('command-error', { error: String(err) });
            else ws.emit('raw-sent', { raw });
        });
    });

    // Legacy API: get-door-info (used by older UI; keep it tolerant)
    ws.on('get-door-info', (moduleId, callback) => {
        console.log(`Client requested door info for module ${moduleId}`);
        if (!udpSocket) startUdpSocket();

        const mod = String(moduleId).startsWith('D') ? String(moduleId) : `D${moduleId}`;
        const cmdid = cmdCounter++;
        const payload = `${mod} COMMAND ${cmdid} D0 STATUS\n`;
        const buf = Buffer.from(payload, 'utf8');

        const timer = setTimeout(() => {
            pending.delete(cmdid);
            callback(null); // timeout -> no info
        }, PENDING_TIMEOUT_MS);

        // Fake socket object that only cares about this one FEEDBACK
        pending.set(cmdid, {
            socket: {
                emit: (eventType, data) => {
                    if (eventType !== 'command-feedback') return;
                    clearTimeout(timer);
                    pending.delete(cmdid);

                    const rawAction = data.action || '';
                    const upper = rawAction.toUpperCase();

                    let frontDoorOpen = null;
                    let frontLockLocked = null;

                    if (upper.startsWith('STATUS_')) {
                        // Handle STATUS_* format
                        const state = upper.substring('STATUS_'.length);
                        if (state === 'OPEN') frontDoorOpen = true;
                        if (state === 'CLOSED') frontDoorOpen = false;
                        if (state === 'LOCKED') frontLockLocked = true;
                        if (state === 'UNLOCKED') frontLockLocked = false;
                    } else if (upper.includes('OPEN') || upper.includes('CLOSED')) {
                        // Legacy "CLOSED,UNLOCKED" etc
                        const parts = upper.split(/[,\s]+/).map(s => s && s.trim()).filter(Boolean);
                        if (parts.includes('OPEN')) frontDoorOpen = true;
                        if (parts.includes('CLOSED')) frontDoorOpen = false;
                        if (parts.includes('LOCKED')) frontLockLocked = true;
                        if (parts.includes('UNLOCKED')) frontLockLocked = false;
                    }

                    callback({
                        moduleId: data.module,
                        target: data.target,
                        status: rawAction,
                        frontDoorOpen: !!frontDoorOpen,
                        frontLockLocked: !!frontLockLocked,
                        success: true,
                    });
                },
            },
            timer,
        });

        udpSocket.send(buf, 0, buf.length, HUB_PORT, HUB_HOST, (err) => {
            if (err) {
                clearTimeout(timer);
                pending.delete(cmdid);
                callback(null);
            }
        });
    });

    ws.on('lock-door', (moduleId, callback) => {
        console.log(`Client requested lock for module ${moduleId}`);
        sendCommand(moduleId, 'D0', 'LOCK', {
            emit: (eventType, data) => {
                if (eventType === 'command-feedback') {
                    callback({ success: true, action: 'LOCK', module: moduleId });
                } else if (eventType === 'command-error') {
                    callback({ success: false, error: data.error });
                }
            },
        });
    });

    ws.on('unlock-door', (moduleId, callback) => {
        console.log(`Client requested unlock for module ${moduleId}`);
        sendCommand(moduleId, 'D0', 'UNLOCK', {
            emit: (eventType, data) => {
                if (eventType === 'command-feedback') {
                    callback({ success: true, action: 'UNLOCK', module: moduleId });
                } else if (eventType === 'command-error') {
                    callback({ success: false, error: data.error });
                }
            },
        });
    });

    ws.on('disconnect', () => {
        console.log('Client disconnected');
    });
}

// ---------- Public API ----------

exports.listen = function (server) {
    io = socketio(server, {
        cors: { origin: "*" },
    });

    startUdpSocket();

    io.on('connection', (socket) => {
        handleSocket(socket);
    });

    console.log(`Door server: forwarding commands to ${HUB_HOST}:${HUB_PORT}`);
};

exports.sendCommand = sendCommand;