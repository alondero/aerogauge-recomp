#!/usr/bin/env bash
set -euo pipefail

app_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
app_path="$app_dir/aerogauge_modern"
icon_path="$app_dir/assets/aerogauge-icon.png"

if [[ ! -x "$app_path" ]]; then
    printf 'Could not find executable: %s\n' "$app_path" >&2
    exit 1
fi
if [[ ! -f "$icon_path" ]]; then
    printf 'Could not find application icon: %s\n' "$icon_path" >&2
    exit 1
fi

data_home="${XDG_DATA_HOME:-${HOME:?HOME must be set}/.local/share}"
applications_dir="$data_home/applications"
icons_dir="$data_home/icons/hicolor/192x192/apps"
desktop_file="$applications_dir/aerogauge_modern.desktop"

desktop_quote_exec_arg() {
    local input="$1"
    local output=""
    local character
    local tick=$'\x60'

    while [[ -n "$input" ]]; do
        character="${input:0:1}"
        input="${input:1}"
        case "$character" in
            \\) output+='\\' ;;
            '"') output+='\"' ;;
            '$') output+='\$' ;;
            "$tick") output+="\\$tick" ;;
            '%') output+='%%' ;;
            *) output+="$character" ;;
        esac
    done

    printf '"%s"' "$output"
}

install -D -m 644 "$icon_path" "$icons_dir/aerogauge-icon.png"
install -d "$applications_dir"
cat > "$desktop_file" <<EOF
[Desktop Entry]
Type=Application
Name=AeroGauge: Recompiled
Comment=Play AeroGauge: Recompiled
Exec=env SDL_VIDEO_WAYLAND_WMCLASS=aerogauge_modern $(desktop_quote_exec_arg "$app_path")
Path=$app_dir
Icon=aerogauge-icon
Terminal=false
Categories=Game;
StartupWMClass=aerogauge_modern
EOF

printf 'Installed the AeroGauge launcher at %s\n' "$desktop_file"
