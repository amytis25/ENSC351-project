import * as doorAdapter from './doorAdapter.js';

async function refreshStatus() {
    for (let id = 1; id <= 4; id++) {
        const state = await doorAdapter.getDoorStatus(id);
        const span = document.getElementById(`door${id}_status`);
        const btn = document.getElementById(`toggle_door${id}`);
        const div = document.getElementById(`door${id}`);
        // display as human-friendly lowercase
        span.textContent = state.toLowerCase();
        if (state === 'DISCONNECTED') {
            div.style.display = 'block';
            div.style.display = 'none';
            btn.disabled = true;
        }
        else if (state === 'LOCKED') {
            span.textContent = 'locked';
            btn.textContent = 'Unlock Door';
            btn.disabled = false;
        } else if (state === 'UNLOCKED') {
            span.textContent = 'unlocked';
            btn.textContent = 'Lock Door';
            btn.disabled = false;
        } else if (state === 'OPEN') {
            span.textContent = 'open';
            btn.textContent = 'cannot lock open door';
            btn.disabled = true;
        } else {
            span.textContent = 'unknown';
            btn.textContent = 'Cannot access door';
            btn.disabled = true;
        }
    }
}
async function toggleDoor(id) {
    const state = await doorAdapter.getDoorStatus(id);
    const messageSpan = document.getElementById(`door${id}_message`);
    if (state === 'LOCKED' && btn.textContent === 'Unlock Door') {
        await doorAdapter.unlockDoor(id);
        btn.textContent = 'Lock Door';
        btn.disabled = false;
    } else if (state === 'UNLOCKED' && btn.textContent === 'Lock Door') {
        await doorAdapter.lockDoor(id);
        btn.textContent = 'Unlock Door';
        btn.disabled = false;
    } else {
        span.textContent = state;
        messageSpan.textContent = `connection error with door ${id}.`;
    }
}
document.addEventListener('DOMContentLoaded', async () => {
    doorAdapter.initializeDoorSystem();

    // Wire up buttons
    for (let id = 1; id <= 2; id++) {
        const btn = document.getElementById(`toggle_door${id}`);
        btn.addEventListener('click', async () => {
            const state = await doorAdapter.getDoorStatus(id);
            if (state === 'LOCKED') {
                await doorAdapter.unlockDoor(id);
            } else if (state === 'UNLOCKED') {
                await doorAdapter.lockDoor(id);
            }
            await refreshStatus();
        });
    }

    // Initial refresh and periodic updates
    await refreshStatus();
    setInterval(refreshStatus, 1000);
});