// gui/web_control.js
// Web-based control panel for Central Door System
// Communicates with server.js to get door status and send commands from UDP

import * as server from './server.js';

async function refreshStatus() {
    for (let id = 1; id <= 4; id++) {
        const state = await server.getDoorStatus(id);
        const span = document.getElementById(`door${id}_status`);
        const btn = document.getElementById(`toggle_door${id}`);
        const div = document.getElementById(`door${id}`);
        const msg = document.getElementById(`door${id}_message`);

        if (!span || !btn || !div) continue;

        msg && (msg.textContent = '');

        switch (state) {
            case 'DISCONNECTED':
                span.textContent = 'disconnected';
                btn.textContent = 'Unavailable';
                btn.disabled = true;
                break;
            case 'LOCKED':
                span.textContent = 'locked';
                btn.textContent = 'Unlock Door';
                btn.disabled = false;
                break;
            case 'UNLOCKED':
                span.textContent = 'unlocked';
                btn.textContent = 'Lock Door';
                btn.disabled = false;
                break;
            case 'OPEN':
                span.textContent = 'open';
                btn.textContent = 'Cannot lock open door';
                btn.disabled = true;
                break;
            default:
                span.textContent = 'unknown';
                btn.textContent = 'Unavailable';
                btn.disabled = true;
        }
    }
}

async function toggleDoor(id) {
    const btn = document.getElementById(`toggle_door${id}`);
    const msg = document.getElementById(`door${id}_message`);
    if (!btn) return;

    const state = await server.getDoorStatus(id);
    try {
        if (state === 'LOCKED') {
            await server.unlockDoor(id);
        } else if (state === 'UNLOCKED') {
            await server.lockDoor(id);
        } else {
            msg && (msg.textContent = `Cannot change door ${id} when state=${state}`);
            return;
        }
    } catch (e) {
        msg && (msg.textContent = `Error: ${e.message}`);
    }
    await refreshStatus();
}

document.addEventListener('DOMContentLoaded', async () => {
    server.initializeDoorSystem();

    // Wire up buttons for doors 1..4
    for (let id = 1; id <= 4; id++) {
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