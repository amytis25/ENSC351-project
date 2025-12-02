"use strict";
/*
 * WebSocket server for door control system
 * Communicates with door-system API to get status and send commands
 */

const fs = require('fs');
const socketio = require('socket.io');
const path = require('path');
const mime = require('mime');
const http = require('http');

const PORT = 3000;

// HTTP server for serving static files
const httpServer = http.createServer(function(req, res) {
    let filePath = (req.url === '/') ? 'index.html' : req.url;
    const absPath = path.join(__dirname, filePath);
    
    fs.readFile(absPath, function(err, data) {
        if (err) {
            res.writeHead(404, {'Content-Type': 'text/plain'});
            res.end('Error 404: resource not found.');
            return;
        }
        const filename = path.basename(absPath);
        let mimeType = 'application/octet-stream';
        if (filename.endsWith('.html')) mimeType = 'text/html';
        else if (filename.endsWith('.css')) mimeType = 'text/css';
        else if (filename.endsWith('.js')) mimeType = 'application/javascript';
        else if (filename.endsWith('.json')) mimeType = 'application/json';
        
        res.writeHead(200, {'Content-Type': mimeType});
        res.end(data);
    });
});

// WebSocket server
const io = socketio(httpServer, {
    cors: { origin: "*" }
});

httpServer.listen(PORT, 'localhost', function() {
    console.log('Server listening on localhost:' + PORT);
    console.log('Access from this device: http://localhost:' + PORT + '/index.html');
    console.log('Access from other devices: http://<your-device-ip>:' + PORT + '/index.html');
});

const DoorState = {
    CLOSED: 'CLOSED',
    LOCKED: 'LOCKED',
    UNLOCKED: 'UNLOCKED',
    OPEN: 'OPEN',
    UNKNOWN: 'UNKNOWN',
    DISCONNECTED: 'DISCONNECTED'
};

// Start UDP door bridge
const doorServer = require('./lib/door_server');
doorServer.listen(httpServer);

io.on('connection', function(socket) {
    console.log('WebSocket client connected:', socket.id);

    socket.on('get-door-info', (moduleId, callback) => {
        console.log('get-door-info for module:', moduleId);
        // Call the door_server directly via its exported function
        doorServer.sendCommand(moduleId, 'D0', 'STATUS', {
            emit: (eventType, data) => {
                if (eventType === 'command-feedback') {
                    if (callback) callback({ success: true, data });
                } else if (eventType === 'command-error') {
                    if (callback) callback({ success: false, error: data.error });
                }
            }
        });
    });

    socket.on('lock-door', (moduleId, callback) => {
        console.log('lock-door for module:', moduleId);
        doorServer.sendCommand(moduleId, 'D0', 'LOCK', {
            emit: (eventType, data) => {
                if (eventType === 'command-feedback') {
                    if (callback) callback({ success: true, data });
                } else if (eventType === 'command-error') {
                    if (callback) callback({ success: false, error: data.error });
                }
            }
        });
    });

    socket.on('unlock-door', (moduleId, callback) => {
        console.log('unlock-door for module:', moduleId);
        doorServer.sendCommand(moduleId, 'D0', 'UNLOCK', {
            emit: (eventType, data) => {
                if (eventType === 'command-feedback') {
                    if (callback) callback({ success: true, data });
                } else if (eventType === 'command-error') {
                    if (callback) callback({ success: false, error: data.error });
                }
            }
        });
    });

    socket.on('disconnect', function() {
        console.log('WebSocket client disconnected:', socket.id);
    });
});
