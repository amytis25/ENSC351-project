// gui/server.js — client for local door_system HTTP API

const DoorState = {
    CLOSED: 'CLOSED',
    LOCKED: 'LOCKED',
    UNLOCKED: 'UNLOCKED',
    OPEN: 'OPEN',
    UNKNOWN: 'UNKNOWN',
    DISCONNECTED: 'DISCONNECTED'
};

function normalizeStatusJson(json) {
    if (!json) return DoorState.DISCONNECTED;
    if (typeof json.state !== 'undefined') {
        switch (json.state) {
            case 0: return DoorState.LOCKED;
            case 1: return DoorState.UNLOCKED;
            case 2: return DoorState.OPEN;
            default: return DoorState.UNKNOWN;
        }
    }
    if (typeof json.d0_open !== 'undefined' && typeof json.d0_locked !== 'undefined') {
        if (json.d0_open) return DoorState.OPEN;
        if (json.d0_locked) return DoorState.LOCKED;
        return DoorState.UNLOCKED;
    }
    return DoorState.DISCONNECTED;
}

export async function initializeDoorSystem() {
    // noop for now
}

function formatModuleId(moduleId) {
    if (moduleId === null || moduleId === undefined) return '';
    const s = String(moduleId);
    if (/^[Dd]\d+$/.test(s)) return s.toUpperCase();
    if (/^\d+$/.test(s)) return `D${s}`;
    return s; // leave as-is
}

export async function getDoorStatus(moduleId) {
    const mod = formatModuleId(moduleId);
    try {
        const res = await fetch(`/api/status?module=${encodeURIComponent(mod)}`);
        if (!res.ok) return DoorState.DISCONNECTED;
        const j = await res.json();
        return normalizeStatusJson(j);
    } catch (e) {
        return DoorState.DISCONNECTED;
    }
}

export async function getDoorInfo(moduleId) {
    const mod = formatModuleId(moduleId);
    try {
        const res = await fetch(`/api/status?module=${encodeURIComponent(mod)}`);
        if (!res.ok) return null;
        const j = await res.json();
        return {
            state: normalizeStatusJson(j),
            raw: j,
            frontDoorOpen: !!j.front_door_open || !!j.d0_open,
            frontLockLocked: !!j.front_lock_locked || !!j.d1_locked
        };
    } catch (e) {
        return null;
    }
}

async function sendCommand(moduleId, target, action) {
    const mod = formatModuleId(moduleId);
    const body = `module=${encodeURIComponent(mod)}&target=${encodeURIComponent(target||'')}&action=${encodeURIComponent(action)}`;
    const res = await fetch(`/api/command`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body
    });
    if (!res.ok) throw new Error(await res.text());
    return res.json();
}

export async function lockDoor(moduleId) {
    return sendCommand(moduleId, 'D0', 'LOCK');
}

export async function unlockDoor(moduleId) {
    return sendCommand(moduleId, 'D0', 'UNLOCK');
}

export { DoorState };
