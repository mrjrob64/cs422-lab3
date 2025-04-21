#!/bin/bash
set +x

SERVER_IP=$1
SERVER_PORT=$2
NUM_FRAGMENTS=$3

if [ -z "$SERVER_IP" ] || [ -z "$SERVER_PORT" ] || [ -z "$NUM_FRAGMENTS" ]; then
  echo "Usage: $0 <SERVER_IP> <SERVER_PORT> <NUM_FRAGMENTS>"
  exit 1
fi

echo "Running $NUM_FRAGMENTS client(s) connecting to $SERVER_IP:$SERVER_PORT"

for ((i = 1; i <= NUM_FRAGMENTS; i++)); do
  echo "Running client #$i"
  ./client "$SERVER_IP" "$SERVER_PORT" &
done

wait
echo "All clients finished."
