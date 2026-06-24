#!/bin/bash

# sizes you want
sizes=(10 20 30 40)

# create subfolders
for s in "${sizes[@]}"; do
    mkdir -p "$s"
done

# process PNGs
for img in *.png; do
    for s in "${sizes[@]}"; do
        convert "$img" -resize "${s}x" "${s}/${img}"
    done
done
