// gui/web_control.js
// Web-based control panel for Central Door System
// Communicates with server.js to get door status and send commands from UDP

import * as server from './server.js';

async function refreshStatus() {
    for (let id = 1; id <= 3; id++) {
        const info = await server.getDoorInfo(id);
        const span = document.getElementById(`door${id}_status`);
        const btn = document.getElementById(`toggle_door${id}`);
        const div = document.getElementById(`door${id}`);
        const msg = document.getElementById(`door${id}_message`);

        if (!span || !btn || !div) continue;

        msg && (msg.textContent = '');

        if (!info) {
            span.textContent = 'disconnected';
            btn.textContent = 'Unavailable';
            btn.disabled = true;
            continue;
        }

        // Show combined human-friendly status and lock state
        const open = info.frontDoorOpen;
        const locked = info.frontLockLocked;
        span.textContent = `${open ? 'open' : 'closed'} / ${locked ? 'locked' : 'unlocked'}`;

        if (open) {
            btn.textContent = 'Cannot lock open door';
            btn.disabled = true;
        } else if (locked) {
            btn.textContent = 'Unlock Door';
            btn.disabled = false;
        } else {
            btn.textContent = 'Lock Door';
            btn.disabled = false;
        }
    }
}

async function toggleDoor(id) {
    const btn = document.getElementById(`toggle_door${id}`);
    const msg = document.getElementById(`door${id}_message`);
    if (!btn) return;

    const info = await server.getDoorInfo(id);
    if (!info) {
        msg && (msg.textContent = 'No connection to door');
        return;
    }

    try {
        if (info.frontDoorOpen) {
            msg && (msg.textContent = 'Door is open; cannot change lock');
            return;
        }
        if (info.frontLockLocked) {
            await server.unlockDoor(id);
        } else {
            await server.lockDoor(id);
        }
    } catch (e) {
        msg && (msg.textContent = `Error: ${e.message}`);
    }
    await refreshStatus();
}

document.addEventListener('DOMContentLoaded', async () => {
    server.initializeDoorSystem();

    // Wire up buttons for doors 1..3
    for (let id = 1; id <= 3; id++) {
        const btn = document.getElementById(`toggle_door${id}`);
        if (!btn) continue;
        btn.addEventListener('click', async () => {
            await toggleDoor(id);
        });
    }

    // Initial refresh and periodic updates
    await refreshStatus();
    setInterval(refreshStatus, 1000);
});