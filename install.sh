#!/bin/bash
# Installs the measured "Movie" sound chain (PipeWire filter-chain + 3 LADSPA plugins) for the
# Acer Predator PHN16-71 (Realtek ALC245, SUBSYS 1025170F). Run with bash:  bash install.sh
# Undo with:  bash uninstall.sh
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
PW=$HOME/.config/pipewire
CONF_DST=$PW/pipewire.conf.d/dts-movie.conf
BACKUP=$HOME/dts-movie-backup-$(date +%Y%m%d-%H%M%S)
say() { printf '\n== %s\n' "$*"; }
die() { echo "!! $*" >&2; exit 1; }

say "Checking hardware"
if ! grep -qsi '1025170f' /proc/asound/card*/codec#* 2>/dev/null; then
    echo "WARNING: codec SUBSYS 1025170F (Acer PHN16-71) not found. The filters were measured on that laptop only;"
    echo "         on other hardware the result will be wrong."
    read -r -p "Continue anyway? [y/N] " a; [[ $a == [yY]* ]] || exit 1
fi

say "Checking tools"
for c in gcc make pw-cli wpctl pipewire; do command -v $c >/dev/null || die "missing: $c (install a compiler toolchain and PipeWire)"; done
if [ ! -f /usr/include/ladspa.h ]; then
    echo "ladspa.h not found; installing the LADSPA SDK (needs sudo)"
    if   command -v pacman  >/dev/null; then sudo pacman -S --needed --noconfirm ladspa
    elif command -v apt-get >/dev/null; then sudo apt-get install -y ladspa-sdk
    elif command -v dnf     >/dev/null; then sudo dnf install -y ladspa-devel
    else die "install the LADSPA SDK (provides ladspa.h) and re-run"; fi
fi
pipewire --version | tail -1
mkdir -p "$BACKUP"

say "Finding the speaker sink"
SPEAKER=$(pw-cli ls Node | sed -n 's/.*node.name = "\(alsa_output[^"]*Speaker__sink\)".*/\1/p' | head -1)
[ -n "$SPEAKER" ] || die "no ALSA 'Speaker' sink found (is PipeWire running? check: pw-cli ls Node | grep alsa_output)"
echo "$SPEAKER"
node_id() { pw-cli ls Node | awk -v n="$1" '/^[[:space:]]*id /{id=$2} $0 ~ "node.name = \""n"\""{gsub(",","",id); print id; exit}'; }

say "Turning EasyEffects off (if present; backups in $BACKUP)"
if pgrep -x easyeffects >/dev/null; then easyeffects -q 2>/dev/null || pkill -x easyeffects || true; sleep 1; fi
[ -f ~/.config/autostart/easyeffects-service.desktop ] && mv ~/.config/autostart/easyeffects-service.desktop "$BACKUP/"
AUTOLOAD=~/.local/share/easyeffects/autoload/output
if [ -d "$AUTOLOAD" ] && ls "$AUTOLOAD"/*.json >/dev/null 2>&1; then
    mkdir -p "$BACKUP/easyeffects-autoload" && mv "$AUTOLOAD"/*.json "$BACKUP/easyeffects-autoload/"
fi

say "Building plugins into ~/.ladspa"
(cd "$HERE/plugin" && make -B all && make install)

say "Installing impulse responses and preset"
mkdir -p "$PW/dts-ir" "$PW/pipewire.conf.d"
cp "$HERE"/ir/movie_{LL,LR,RL,RR}.wav "$PW/dts-ir/"
[ -f "$CONF_DST" ] && mv "$CONF_DST" "$BACKUP/"
sed -e "s#@HOME@#$HOME#g" -e "s#@SPEAKER@#$SPEAKER#g" "$HERE/dts-movie.conf.in" > "$CONF_DST"
systemctl --user restart pipewire.service wireplumber.service pipewire-pulse.service
sleep 3

say "Checking that the chain loaded"
if journalctl --user -u pipewire --since "-20s" --no-pager | grep -iE 'filter-chain|ladspa|dts_' | grep -iE 'error|fail|cannot'; then
    die "PipeWire reported errors above. Undo with: bash $HERE/uninstall.sh $BACKUP"
fi
DTS=$(node_id effect_input.dts_movie); SPK=$(node_id "$SPEAKER")
[ -n "$DTS" ] || die "effect_input.dts_movie not found. Undo with: bash $HERE/uninstall.sh $BACKUP"

say "Routing and levels"
wpctl set-default "$DTS"
if [ -n "$SPK" ]; then wpctl set-volume "$SPK" 1.0; wpctl set-mute "$SPK" 0; fi
# Hardware speaker control on the card that hosts the ALC245 (not always card 1)
CARD=$(grep -l 'ALC245' /proc/asound/card*/codec#0 2>/dev/null | sed -n 's#.*/card\([0-9]*\)/.*#\1#p' | head -1 || true)
[ -n "${CARD:-}" ] && amixer -c "$CARD" -q sset Speaker 100% unmute 2>/dev/null || true
wpctl set-volume "$DTS" 0.20

cat <<MSG

Done. "DTS Movie (speakers)" is now the default output. Use ITS slider / the Fn keys as the volume
(20% here sounds like 20% on Windows) and keep the Speaker device itself at 100%.
Backups: $BACKUP
Undo:    bash $HERE/uninstall.sh $BACKUP
MSG
