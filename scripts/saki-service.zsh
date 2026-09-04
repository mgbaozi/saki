#!/bin/zsh

set -euo pipefail

SAKI_SERVICE_LABEL="com.saki.agent-display"
SAKI_SERVICE_SCRIPT_DIR="${0:A:h}"
SAKI_SERVICE_REPO_ROOT="${SAKI_SERVICE_SCRIPT_DIR:h}"
SAKI_SERVICE_HOST_PYTHON="$SAKI_SERVICE_REPO_ROOT/host/.venv/bin/python"
SAKI_SERVICE_HOST_EXECUTABLE="$SAKI_SERVICE_REPO_ROOT/host/.venv/bin/saki-host"
SAKI_SERVICE_USER_HOME="${HOME:?HOME is not set}"
SAKI_SERVICE_AGENT_DIR="$SAKI_SERVICE_USER_HOME/Library/LaunchAgents"
SAKI_SERVICE_PLIST="$SAKI_SERVICE_AGENT_DIR/$SAKI_SERVICE_LABEL.plist"
SAKI_SERVICE_LOG_DIR="$SAKI_SERVICE_USER_HOME/Library/Logs/Saki"
SAKI_SERVICE_DOMAIN="gui/$(id -u)"
SAKI_SERVICE_TARGET="$SAKI_SERVICE_DOMAIN/$SAKI_SERVICE_LABEL"
SAKI_SERVICE_COMMAND="${1:-status}"

saki_service_usage() {
  print -u2 -- "usage: scripts/saki-service.zsh {render PATH|install|start|stop|restart|status|logs|uninstall}"
}

saki_service_require_host() {
  if [[ ! -x "$SAKI_SERVICE_HOST_PYTHON" || ! -x "$SAKI_SERVICE_HOST_EXECUTABLE" ]]; then
    print -u2 -- "Saki Host venv is missing; install host/.venv before managing the service"
    exit 2
  fi
}

saki_service_render() {
  local destination="$1"
  saki_service_require_host
  "$SAKI_SERVICE_HOST_PYTHON" -m saki_host.launch_agent \
    --output "$destination" \
    --repo-root "$SAKI_SERVICE_REPO_ROOT" \
    --user-home "$SAKI_SERVICE_USER_HOME"
  /usr/bin/plutil -lint "$destination"
}

saki_service_loaded() {
  /bin/launchctl print "$SAKI_SERVICE_TARGET" >/dev/null 2>&1
}

case "$SAKI_SERVICE_COMMAND" in
  render)
    if [[ $# -ne 2 ]]; then
      saki_service_usage
      exit 2
    fi
    saki_service_render "$2"
    ;;
  install)
    if [[ $# -ne 1 ]]; then
      saki_service_usage
      exit 2
    fi
    saki_service_require_host
    /bin/mkdir -p "$SAKI_SERVICE_AGENT_DIR"
    /bin/mkdir -p -m 700 "$SAKI_SERVICE_LOG_DIR"
    /bin/chmod 700 "$SAKI_SERVICE_LOG_DIR"
    SAKI_SERVICE_TEMP_PLIST="$(/usr/bin/mktemp "$TMPDIR/saki-launch-agent.XXXXXX")"
    trap '/bin/rm -f "$SAKI_SERVICE_TEMP_PLIST"' EXIT
    saki_service_render "$SAKI_SERVICE_TEMP_PLIST"
    if saki_service_loaded; then
      /bin/launchctl bootout "$SAKI_SERVICE_TARGET"
    fi
    /usr/bin/install -m 600 "$SAKI_SERVICE_TEMP_PLIST" "$SAKI_SERVICE_PLIST"
    /bin/launchctl enable "$SAKI_SERVICE_TARGET"
    /bin/launchctl bootstrap "$SAKI_SERVICE_DOMAIN" "$SAKI_SERVICE_PLIST"
    /bin/launchctl kickstart -k "$SAKI_SERVICE_TARGET"
    print -- "installed $SAKI_SERVICE_TARGET"
    print -- "logs: $SAKI_SERVICE_LOG_DIR"
    ;;
  uninstall)
    if [[ $# -ne 1 ]]; then
      saki_service_usage
      exit 2
    fi
    if saki_service_loaded; then
      /bin/launchctl bootout "$SAKI_SERVICE_TARGET"
    fi
    /bin/rm -f "$SAKI_SERVICE_PLIST"
    print -- "uninstalled $SAKI_SERVICE_TARGET"
    print -- "logs retained at $SAKI_SERVICE_LOG_DIR"
    ;;
  start)
    if [[ $# -ne 1 ]]; then
      saki_service_usage
      exit 2
    fi
    saki_service_require_host
    if [[ ! -f "$SAKI_SERVICE_PLIST" ]]; then
      print -u2 -- "$SAKI_SERVICE_PLIST is missing; run install first"
      exit 1
    fi
    if saki_service_loaded; then
      print -- "$SAKI_SERVICE_TARGET is already running"
      exit 0
    fi
    /bin/launchctl enable "$SAKI_SERVICE_TARGET"
    /bin/launchctl bootstrap "$SAKI_SERVICE_DOMAIN" "$SAKI_SERVICE_PLIST"
    /bin/launchctl kickstart -k "$SAKI_SERVICE_TARGET"
    print -- "started $SAKI_SERVICE_TARGET"
    ;;
  stop)
    if [[ $# -ne 1 ]]; then
      saki_service_usage
      exit 2
    fi
    if saki_service_loaded; then
      /bin/launchctl bootout "$SAKI_SERVICE_TARGET"
      print -- "stopped $SAKI_SERVICE_TARGET"
    else
      print -- "$SAKI_SERVICE_TARGET is already stopped"
    fi
    ;;
  restart)
    if [[ $# -ne 1 ]]; then
      saki_service_usage
      exit 2
    fi
    if ! saki_service_loaded; then
      print -u2 -- "$SAKI_SERVICE_TARGET is not installed or loaded"
      exit 1
    fi
    /bin/launchctl kickstart -k "$SAKI_SERVICE_TARGET"
    print -- "restarted $SAKI_SERVICE_TARGET"
    ;;
  status)
    if [[ $# -ne 1 ]]; then
      saki_service_usage
      exit 2
    fi
    if ! saki_service_loaded; then
      print -u2 -- "$SAKI_SERVICE_TARGET is not installed or loaded"
      exit 1
    fi
    /bin/launchctl print "$SAKI_SERVICE_TARGET"
    ;;
  logs)
    if [[ $# -ne 1 ]]; then
      saki_service_usage
      exit 2
    fi
    print -- "==> $SAKI_SERVICE_LOG_DIR/host.stdout.log <=="
    /usr/bin/tail -n 80 "$SAKI_SERVICE_LOG_DIR/host.stdout.log" 2>/dev/null || true
    print -- "==> $SAKI_SERVICE_LOG_DIR/host.stderr.log <=="
    /usr/bin/tail -n 80 "$SAKI_SERVICE_LOG_DIR/host.stderr.log" 2>/dev/null || true
    ;;
  *)
    saki_service_usage
    exit 2
    ;;
esac
