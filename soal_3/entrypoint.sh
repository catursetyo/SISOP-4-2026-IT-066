#!/bin/bash
1#!/bin/bash
set -euo pipefail

create_group() {
  local group_name="$1"

  if ! getent group "$group_name" >/dev/null; then
    groupadd "$group_name"
  fi
}

create_user() {
  local username="$1"
  local password="$2"
  local extra_group="$3"
  local uid="$4"

  if ! id "$username" >/dev/null 2>&1; then
    useradd -m -u "$uid" -s /usr/sbin/nologin "$username"
  fi

  usermod -aG "$extra_group" "$username"

  if pdbedit -L 2>/dev/null | cut -d: -f1 | grep -qx "$username"; then
    printf '%s\n%s\n' "$password" "$password" | smbpasswd -s "$username" >/dev/null
  else
    printf '%s\n%s\n' "$password" "$password" | smbpasswd -a -s "$username" >/dev/null
  fi

  smbpasswd -e "$username" >/dev/null
}

create_group readonly
create_group staff

create_user member member123 readonly 1000
create_user contributor contrib456 staff 1001
create_user librarian lib789 staff 1002

mkdir -p /libraryit/ebooks
mkdir -p /libraryit/papers
mkdir -p /libraryit/sourcecode
mkdir -p /libraryit/docs
mkdir -p /logs
mkdir -p /var/log/samba
mkdir -p /run/samba
mkdir -p /var/run/samba
mkdir -p /var/cache/samba
mkdir -p /var/lib/samba/private

chown root:staff /libraryit/ebooks || true
chmod 2775 /libraryit/ebooks || true

chown root:staff /libraryit/papers || true
chmod 2775 /libraryit/papers || true

chown root:staff /libraryit/sourcecode || true
chmod 0750 /libraryit/sourcecode || true

chown root:staff /libraryit/docs || true
chmod 2775 /libraryit/docs || true

touch /logs/libraryit.log || true
chmod 0666 /logs/libraryit.log || true

touch /var/log/samba/audit.raw || true
chmod 0666 /var/log/samba/audit.raw || true

cat >/etc/rsyslog.d/50-samba-audit.conf <<'RSYSLOG'
local7.*    /var/log/samba/audit.raw
& stop
RSYSLOG

rsyslogd || true

python3 - <<'PY' &
import os
import time
from datetime import datetime

raw_path = "/var/log/samba/audit.raw"
out_path = "/logs/libraryit.log"

os.makedirs(os.path.dirname(raw_path), exist_ok=True)
os.makedirs(os.path.dirname(out_path), exist_ok=True)

open(raw_path, "a").close()
open(out_path, "a").close()

def write_log(level, username, action, name):
    timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    line = f"[{timestamp}] [{level}] [{username}] [{action}] [{name}]\n"
    with open(out_path, "a", buffering=1) as out:
        out.write(line)

with open(raw_path, "r", errors="ignore") as raw:
    raw.seek(0, 2)

    while True:
        line = raw.readline()

        if not line:
            time.sleep(0.5)
            continue

        if "smbd_audit:" not in line:
            continue

        data = line.split("smbd_audit:", 1)[1].strip()
        parts = data.split("|")

        if len(parts) < 4:
            continue

        username = parts[0].strip() or "unknown"
        share = parts[1].strip() if len(parts) > 1 else "-"
        operation = parts[2].strip().lower() if len(parts) > 2 else "-"
        result = parts[3].strip().lower() if len(parts) > 3 else "-"
        target = parts[4].strip() if len(parts) > 4 and parts[4].strip() else share

        if result not in ("ok", "success"):
            write_log("WARNING", username, "DENIED", target)
        elif operation == "connect":
            write_log("INFO", username, "CONNECT", share)
        elif operation in ("write", "pwrite"):
            write_log("INFO", username, "WRITE", target)
PY

testparm -s >/dev/null

exec smbd -F --no-process-group
