#!/bin/bash
# Undo install.sh: remove the chain and restore what it moved.  usage: bash uninstall.sh [BACKUP_DIR]
set -euo pipefail
B=${1:-}
rm -f ~/.config/pipewire/pipewire.conf.d/dts-movie.conf
if [ -n "$B" ] && [ -d "$B" ]; then
    [ -f "$B/dts-movie.conf" ] && cp "$B/dts-movie.conf" ~/.config/pipewire/pipewire.conf.d/
    [ -f "$B/easyeffects-service.desktop" ] && mkdir -p ~/.config/autostart && cp "$B/easyeffects-service.desktop" ~/.config/autostart/
    if [ -d "$B/easyeffects-autoload" ]; then
        mkdir -p ~/.local/share/easyeffects/autoload/output && cp "$B"/easyeffects-autoload/*.json ~/.local/share/easyeffects/autoload/output/
    fi
fi
systemctl --user restart pipewire.service wireplumber.service pipewire-pulse.service
echo "Removed. Pick your normal output device again in the sound settings."
