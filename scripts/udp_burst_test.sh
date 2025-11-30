#!/usr/bin/env bash
# Simple UDP burst test: send N messages quickly to hub
HOST=${1:-127.0.0.1}
PORT=${2:-12346}
COUNT=${3:-100}
for i in $(seq 1 $COUNT); do
  echo "D${i} HEARTBEAT D0=OPEN,UNLOCKED D1=CLOSED,UNLOCKED" | nc -u -w0 $HOST $PORT
done
echo "sent $COUNT packets to $HOST:$PORT"
