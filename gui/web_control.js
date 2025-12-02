// gui/web_control.js
// Web-based control panel for Central Door System
// Communicates with server.js via WebSocket to send commands and receive status updates via UDP

const socket = io();

// Module cache to track door states
const doorStates = {
    D1: { doorOpen: null, lockLocked: null, moduleId: 'D1' },
    D2: { doorOpen: null, lockLocked: null, moduleId: 'D2' },
    D3: { doorOpen: null, lockLocked: null, moduleId: 'D3' },
};

// ---------- Helpers ----------

// Normalize a STATUS_* string (LOCKED, UNLOCKED, OPEN, UNKNOWN) to doorStates entry
function applyStatusStateToDoor(moduleId, statusState) {
    const state = doorStates[moduleId];
    if (!state) return;

    const upper = (statusState || '').toUpperCase();

    switch (upper) {
        case 'LOCKED':
            state.lockLocked = true;
            state.doorOpen = false; // locked implies closed
            break;
        case 'UNLOCKED':
            state.lockLocked = false;
            state.doorOpen = false; // unlocked but closed
            break;
        case 'OPEN':
            state.doorOpen = true;
            // lockLocked may be unknown while door is open
            break;
        case 'UNKNOWN':
        default:
            state.doorOpen = null;
            state.lockLocked = null;
            break;
    }

    console.log(`[applyStatusStateToDoor] ${moduleId} ->`, state);
}

// Parse generic string like "CLOSED,UNLOCKED" into doorOpen / lockLocked
function applyLegacyStatusString(moduleId, statusStr) {
    const state = doorStates[moduleId];
    if (!state) return;

    const parts = (statusStr || '')
        .split(/[,\s]+/)
        .map(s => s && s.trim().toUpperCase())
        .filter(Boolean);

    let updated = false;

    if (parts.includes('OPEN')) {
        state.doorOpen = true;
        updated = true;
    } else if (parts.includes('CLOSED')) {
        state.doorOpen = false;
        updated = true;
    }

    if (parts.includes('LOCKED')) {
        state.lockLocked = true;
        updated = true;
    } else if (parts.includes('UNLOCKED')) {
        state.lockLocked = false;
        updated = true;
    }

    console.log(`[applyLegacyStatusString] ${moduleId} parts=[${parts.join(', ')}] ->`, state, `(updated=${updated})`);
}

// ---------- Socket handlers ----------
function setupServerMessageHandlers() {
    // Connection status
    socket.on('connect', () => {
        console.log('Connected to server');
        const statusEl = document.getElementById('connection-status');
        if (statusEl) statusEl.textContent = 'Connected';
    });

    socket.on('disconnect', () => {
        console.log('Disconnected from server');
        const statusEl = document.getElementById('connection-status');
        if (statusEl) statusEl.textContent = 'Disconnected';
    });

    // Listen for hub events (EVENT lines from hub: D1 EVENT D0 DOOR OPEN, etc.)
    socket.on('hub-event', (data) => {
        console.log('Received hub-event:', data);
        const moduleId = data.module; // e.g., 'D1'
        const eventStr = data.event || ''; // e.g., 'DOOR OPEN' or 'LOCK LOCKED'

        if (!doorStates[moduleId]) return;

        const upper = eventStr.toUpperCase();
        const parts = upper.split(/[,\s]+/).map(s => s && s.trim()).filter(Boolean);

        let updated = false;

        if (parts.includes('OPEN')) {
            doorStates[moduleId].doorOpen = true;
            updated = true;
        } else if (parts.includes('CLOSED')) {
            doorStates[moduleId].doorOpen = false;
            updated = true;
        }

        if (parts.includes('LOCKED')) {
            doorStates[moduleId].lockLocked = true;
            updated = true;
        } else if (parts.includes('UNLOCKED')) {
            doorStates[moduleId].lockLocked = false;
            updated = true;
        }

        if (updated) {
            updateUIForModule(moduleId);
        }
    });

    // New primary path: door-feedback is normalized by door_server.js
    socket.on('door-feedback', ({ module, target, action }) => {
        console.log('[door-feedback] module=%s target=%s action=%s', module, target, action);
        if (!module || !doorStates[module]) return;

        const upper = (action || '').toUpperCase();
        
        // Handle STATUS_* feedback
        if (upper.startsWith('STATUS_')) {
            const state = upper.substring('STATUS_'.length);
            console.log('[door-feedback] Processing STATUS update: state=%s', state);
            applyStatusStateToDoor(module, state);
            updateUIForModule(module);
            return;
        }
        
        // Handle LOCK/UNLOCK completion feedback
        if (upper === 'LOCK' || upper === 'UNLOCK') {
            console.log('[door-feedback] %s command completed, polling for status update', upper);
            // After lock/unlock completes, poll for status until it reflects the change
            // Motor rotation takes ~4-5 seconds, so poll every 500ms for up to 10 seconds
            const expectedState = upper === 'LOCK' ? 'LOCKED' : 'UNLOCKED';
            let pollCount = 0;
            const maxPolls = 20; // 20 * 500ms = 10 seconds max
            
            const pollStatus = () => {
                requestModuleStatus(module);
                pollCount++;
                
                // Check status after a short delay to see if it updated
                setTimeout(() => {
                    const state = doorStates[module];
                    const isLocked = state.lockLocked;
                    const targetLocked = (expectedState === 'LOCKED');
                    
                    console.log(`[poll] pollCount=${pollCount}, expected=${expectedState}, lockLocked=${isLocked}`);
                    
                    // Stop polling if status matches expected state or max polls reached
                    if ((isLocked === targetLocked) || (pollCount >= maxPolls)) {
                        console.log(`[poll] Done polling: state is now ${isLocked ? 'LOCKED' : 'UNLOCKED'} (expected ${expectedState})`);
                        return;
                    }
                    
                    // Continue polling
                    pollStatus();
                }, 500);
            };
            
            pollStatus();
            return;
        }
    });

    // Legacy / generic feedback handler (kept for compatibility)
    socket.on('command-feedback', (data) => {
        console.log('Received command-feedback:', data);
        const moduleId = data.module;
        let action = (data.action || '').toUpperCase();
        const target = (data.target || '').toUpperCase();

        if (!moduleId || !doorStates[moduleId]) {
            console.warn(`No doorState found for module: ${moduleId}`);
            return;
        }

        console.log(`Parsing feedback for ${moduleId}: action="${action}", target="${target}"`);

        // If it is already STATUS_*, use the same helper
        if (action.startsWith('STATUS_')) {
            const state = action.substring('STATUS_'.length);
            console.log('[command-feedback] Detected STATUS_* format:', state);
            applyStatusStateToDoor(moduleId, state);
            updateUIForModule(moduleId);
            return;
        }

        // Otherwise treat it as legacy "CLOSED,UNLOCKED" etc
        applyLegacyStatusString(moduleId, action);
        updateUIForModule(moduleId);
    });

    // Heartbeats
    socket.on('hub-heartbeat', (data) => {
        console.log('Heartbeat from hub:', data.module);
    });

    // Errors
    socket.on('command-error', (data) => {
        console.error('Command error:', data);
        const moduleNum = data.module ? data.module.replace('D', '') : 'unknown';
        const msg = document.getElementById(`door${moduleNum}_message`);
        if (msg) {
            msg.textContent = `Error: ${data.error || 'Unknown error'}`;
            setTimeout(() => { msg.textContent = ''; }, 3000);
        }
    });

    socket.on('hub-raw', (data) => {
        console.log('Raw hub message:', data.raw);
    });
}

// ---------- UI update ----------
function updateUIForModule(moduleId) {
    console.log(`updateUIForModule called for ${moduleId}`);
    const num = moduleId.replace('D', '');
    const span = document.getElementById(`door${num}_status`);
    const btn = document.getElementById(`toggle_door${num}`);
    const state = doorStates[moduleId];

    console.log(`  span: ${span ? 'found' : 'NOT found'}`);
    console.log(`  btn: ${btn ? 'found' : 'NOT found'}`);
    console.log(`  state:`, state);

    if (!span || !btn || !state) {
        console.warn(`Missing elements for door${num}. Span: ${!!span}, Btn: ${!!btn}, State: ${!!state}`);
        return;
    }

    // Combined status text
    let statusText = 'unknown';
    if (state.doorOpen !== null && state.lockLocked !== null) {
        const doorStr = state.doorOpen ? 'open' : 'closed';
        const lockStr = state.lockLocked ? 'locked' : 'unlocked';
        statusText = `${doorStr} / ${lockStr}`;
    }
    console.log(`  Setting status to: "${statusText}"`);
    span.textContent = statusText;

    // Button state / label
    if (state.doorOpen === null || state.lockLocked === null) {
        btn.textContent = 'Loading...';
        btn.disabled = true;
    } else if (state.doorOpen) {
        btn.textContent = 'Cannot lock open door';
        btn.disabled = true;
    } else if (state.lockLocked) {
        btn.textContent = 'Unlock Door';
        btn.disabled = false;
    } else {
        btn.textContent = 'Lock Door';
        btn.disabled = false;
    }
    console.log(`  Button set to: "${btn.textContent}" (disabled: ${btn.disabled})`);
}

// ---------- Command helpers ----------
function sendCommandToModule(moduleId, target, action) {
    console.log(`Sending command: ${moduleId} -> ${action}`);
    socket.emit('send-command', {
        module: moduleId,
        target: target || 'D0',
        action: action,
    });
}

function requestModuleStatus(moduleId) {
    console.log(`Requesting status for ${moduleId}`);
    socket.emit('send-command', {
        module: moduleId,
        target: 'D0',
        action: 'STATUS',
    });
}

// Toggle lock for a door
async function toggleDoor(id) {
    const moduleId = `D${id}`;
    const btn = document.getElementById(`toggle_door${id}`);
    const msg = document.getElementById(`door${id}_message`);
    const state = doorStates[moduleId];

    if (!btn || !state) return;

    try {
        if (state.doorOpen) {
            msg && (msg.textContent = 'Door is open; cannot change lock');
            setTimeout(() => { msg && (msg.textContent = ''); }, 2000);
            return;
        }

        msg && (msg.textContent = 'Sending command...');
        const action = state.lockLocked ? 'UNLOCK' : 'LOCK';
        sendCommandToModule(moduleId, 'D0', action);

        setTimeout(() => { msg && (msg.textContent = ''); }, 2000);
    } catch (e) {
        msg && (msg.textContent = `Error: ${e.message}`);
        setTimeout(() => { msg && (msg.textContent = ''); }, 3000);
    }
}

// ---------- Bootstrapping ----------
document.addEventListener('DOMContentLoaded', () => {
    setupServerMessageHandlers();

    // Wire buttons
    for (let id = 1; id <= 3; id++) {
        const btn = document.getElementById(`toggle_door${id}`);
        if (!btn) continue;
        btn.addEventListener('click', () => toggleDoor(id));
    }

    // Wait for socket connection before requesting initial status
    if (socket.connected) {
        // If already connected, request status immediately
        requestModuleStatus('D1');
        requestModuleStatus('D2');
        requestModuleStatus('D3');
    } else {
        // Wait for connection event
        socket.on('connect', () => {
            console.log('Socket connected, requesting initial status');
            requestModuleStatus('D1');
            requestModuleStatus('D2');
            requestModuleStatus('D3');
        });
    }

    // Periodic polling (every 3 seconds)
    setInterval(() => {
        requestModuleStatus('D1');
        requestModuleStatus('D2');
        requestModuleStatus('D3');
    }, 3000);
});