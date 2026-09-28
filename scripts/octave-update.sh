#!/bin/bash
# Octave Update Script (Orange Pi head unit)
#
# Updates never delay boot and never change files under a running app:
#
#   octave-update.sh --apply   Boot, before octave-start.sh. Offline and
#                              instant: if a newer origin/main was fetched on
#                              an earlier run, reset the checkout to it.
#   octave-update.sh --fetch   Boot, in the background. Waits for the network
#                              (Wi-Fi usually isn't up when i3 starts, so the
#                              old one-shot check always said "No internet"),
#                              then fetches origin/main for the NEXT boot. It
#                              only downloads; OCTAVE loads some QML on demand,
#                              so swapping files mid-session could mix versions.
#   octave-update.sh           Old behaviour: fetch and reset now (manual use).
#
# i3 (~/.config/i3/config):
#   exec --no-startup-id /home/rob/Roxy/octave-update.sh --apply; /home/rob/Roxy/octave-start.sh
#   exec --no-startup-id /home/rob/Roxy/octave-update.sh --fetch
#
# Every path exits 0, so a failure here never stops OCTAVE from starting.

OCTAVE_DIR="${OCTAVE_DIR:-/home/rob/Roxy/octave}"
TIMEOUT_SECONDS=10
FETCH_WAIT_SECONDS=${FETCH_WAIT_SECONDS:-900}    # --fetch: give the network up to 15 min to appear
FETCH_RETRY_SECONDS=${FETCH_RETRY_SECONDS:-20}
LOG_FILE="${LOG_FILE:-/home/rob/Roxy/update.log}"

log() {
    echo "$(date '+%Y-%m-%d %H:%M:%S') - $1" >> "$LOG_FILE"
}

cd "$OCTAVE_DIR" || { log "Failed to cd to $OCTAVE_DIR"; exit 0; }

# Fetch origin/main; succeeds only with a working network. Uses git itself
# rather than ICMP ping, which some networks block even when online.
fetch_main() {
    timeout "$TIMEOUT_SECONDS" git fetch --quiet origin main &>/dev/null
}

# Reset the checkout to the fetched origin/main if it's newer. Refuses when
# tracked files have local edits (reset --hard would silently discard them).
apply_fetched() {
    local local_rev remote_rev
    local_rev=$(git rev-parse HEAD 2>/dev/null)
    remote_rev=$(git rev-parse --verify --quiet origin/main)
    if [ -z "$remote_rev" ] || [ "$local_rev" = "$remote_rev" ]; then
        log "Already up to date ($(git log -1 --format='%h %s' 2>/dev/null))"
        return
    fi
    if ! git diff --quiet || ! git diff --cached --quiet; then
        log "Update pending but tracked files have local changes; not resetting"
        return
    fi
    local req_before
    req_before=$(git rev-parse HEAD:requirements.txt 2>/dev/null)
    if timeout "$TIMEOUT_SECONDS" git reset --hard --quiet origin/main; then
        log "Updated to $(git log -1 --format='%h %s')"
        if [ "$req_before" != "$(git rev-parse HEAD:requirements.txt 2>/dev/null)" ]; then
            log "requirements.txt changed: run ./venv/bin/pip install -r requirements.txt"
        fi
    else
        log "Git reset failed"
    fi
}

case "$1" in
    --apply)
        log "Boot: applying any previously fetched update"
        apply_fetched
        ;;
    --fetch)
        log "Background fetch: waiting for network"
        waited=0
        until fetch_main; do
            if [ "$waited" -ge "$FETCH_WAIT_SECONDS" ]; then
                log "No network after ${FETCH_WAIT_SECONDS}s, giving up until next boot"
                exit 0
            fi
            sleep "$FETCH_RETRY_SECONDS"
            waited=$((waited + FETCH_RETRY_SECONDS))
        done
        if [ "$(git rev-parse HEAD)" = "$(git rev-parse origin/main)" ]; then
            log "Background fetch: already up to date"
        else
            log "Background fetch: $(git log -1 --format='%h %s' origin/main) downloaded, applies on next boot"
        fi
        ;;
    *)
        log "Update check started"
        if ! fetch_main; then
            log "Git fetch failed (offline?), skipping update"
            exit 0
        fi
        apply_fetched
        ;;
esac

exit 0
