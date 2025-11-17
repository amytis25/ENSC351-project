// doorAdapter.js
// link to Central Door System

const DoorState = {
    CLOSED: 'CLOSED',
    LOCKED: 'LOCKED',
    UNLOCKED: 'UNLOCKED',
    OPEN: 'OPEN',
    UNKNOWN: 'UNKNOWN',
    DISCONNECTED: 'DISCONNECTED'
};

// Internal simulated doors (id starts at 1)
const doors = [
    { id: 1, state: DoorState.UNLOCKED, dispState: DoorState.UNLOCKED},
    { id: 2, state: DoorState.UNLOCKED, dispState: DoorState.UNLOCKED},
    { id: 3, state: DoorState.UNLOCKED, dispState: DoorState.UNLOCKED},
    { id: 4, state: DoorState.DISCONNECTED, dispState: DoorState.DISCONNECTED}
];

export function initializeDoorSystem() {
// In the real system this would initialize sensors and motors.
    
}


// Get status: may randomly flip to OPEN to simulate someone opening the door
export async function getDoorStatus(id) {
   
}

export async function lockDoor(id) {

}

export async function unlockDoor(id) {

}

export { DoorState };
