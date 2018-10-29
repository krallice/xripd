#!/bin/bash

ip route | grep "proto 33" | awk '{print $1}' | xargs -I{} ip route del {}
