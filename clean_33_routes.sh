#!/bin/bash

echo "Deleting Routes:"
ip route | grep "proto 33"
ip route | grep "proto 33" | awk '{print $1}' | xargs -I{} ip route del {}
