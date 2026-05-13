#!/bin/bash
set -e

LIBRARYIT_ROOT="/libraryit"
LOG_DIR="${LIBRARYIT_ROOT}/logs"
LOG_FILE="${LOG_DIR}/libraryit.log"

# 1. Groups — 'staff' already exists in Ubuntu base (GID 50). 'readonly' must be created.
getent group staff    >/dev/null 2>&1 || groupadd -g 50   staff
getent group readonly >/dev/null 2>&1 || groupadd -g 1000 readonly

# 2. Users — Format: <username>:<uid>:<secondary_group>:<password>
USERS=(
    "member:1000:readonly:member123"
    "contributor:1001:staff:contrib456"
    "librarian:1002:staff:lib789"
)

for entry in "${USERS[@]}"; do
    IFS=':' read -r username uid group password <<< "$entry"

    if ! id -u "$username" >/dev/null 2>&1; then
        useradd -M -u "$uid" -s /usr/sbin/nologin -G "$group" "$username"
        echo "${username}:${password}" | chpasswd
    fi

    if ! pdbedit -L 2>/dev/null | grep -q "^${username}:"; then
        (echo "$password"; echo "$password") | smbpasswd -a -s "$username" >/dev/null
    fi
done

# 3. Collections
mkdir -p \
    "${LIBRARYIT_ROOT}/ebooks" \
    "${LIBRARYIT_ROOT}/papers" \
    "${LIBRARYIT_ROOT}/sourcecode" \
    "${LIBRARYIT_ROOT}/docs" \
    "${LOG_DIR}"

# ebooks, papers: writable by staff group, readable by everyone
chown root:staff "${LIBRARYIT_ROOT}/ebooks" "${LIBRARYIT_ROOT}/papers"
chmod 0775       "${LIBRARYIT_ROOT}/ebooks" "${LIBRARYIT_ROOT}/papers"

# sourcecode: 750 root:staff -- only owner and group group on host
chown root:staff "${LIBRARYIT_ROOT}/sourcecode"
chmod 0750       "${LIBRARYIT_ROOT}/sourcecode"

# docs: read-only on host (555) -- host users cannot modify directly.
# An ACL grants librarian write access so Samba-level write_list=librarian
# can be enforced while keeping the host directory read-only.
chown root:staff "${LIBRARYIT_ROOT}/docs"
chmod 0555       "${LIBRARYIT_ROOT}/docs"
setfacl -m u:librarian:rwx "${LIBRARYIT_ROOT}/docs"

# logs
chown root:root "${LOG_DIR}"
chmod 0755      "${LOG_DIR}"
touch "${LOG_FILE}"
chmod 0644 "${LOG_FILE}"

# 4. Validate Samba configuration
testparm -s /etc/samba/smb.conf >/dev/null 2>&1

# 5. Logger — receives vfs_full_audit syslog datagrams via socat, formats with shell.
cat > /usr/local/bin/format-audit.sh << 'SHEOF'
#!/bin/bash
LOG="/libraryit/logs/libraryit.log"
IFS= read -r line
line=$(printf '%s' "$line" | sed 's/^<[0-9]*>//')
if ! printf '%s' "$line" | grep -q "smbd_audit:"; then exit 0; fi
payload=$(printf '%s' "$line" | sed 's/.*smbd_audit: *//')
user=$(printf '%s' "$payload" | cut -d'|' -f1 | sed 's/^[[:space:]]*//;s/[[:space:]]*$//')
user=${user:-anonymous}
share=$(printf '%s' "$payload" | cut -d'|' -f2 | sed 's/^[[:space:]]*//;s/[[:space:]]*$//')
op=$(printf '%s' "$payload" | cut -d'|' -f3 | sed 's/^[[:space:]]*//;s/[[:space:]]*$//' | tr '[:upper:]' '[:lower:]')
status=$(printf '%s' "$payload" | cut -d'|' -f4 | sed 's/^[[:space:]]*//;s/[[:space:]]*$//' | tr '[:upper:]' '[:lower:]')
args=$(printf '%s' "$payload" | cut -d'|' -f5-)
if [ "$share" = "IPC_" ] || [ "$share" = "IPC$" ]; then exit 0; fi
if [ -n "$share" ]; then share=$(printf '%s' "$share" | sed 's/./\u&/'); else share="-"; fi
ts=$(date '+%Y-%m-%d %H:%M:%S')
if [ "$status" = "fail" ] || [ "$status" = "failed" ]; then
    level="WARNING"; action="DENIED"; target="$share"
    if [ "$target" = "-" ]; then
        target=$(printf '%s' "$args" | sed 's|/$||' | awk -F'/' '{print $NF}')
        [ -z "$target" ] && target="$share"
    fi
else
    case "$op" in
        connect) action="CONNECT" ;;
        disconnect) action="DISCONNECT" ;;
        openat|opendir) action="OPEN" ;;
        pread_send) action="READ" ;;
        pwrite_send) action="WRITE" ;;
        mkdir) action="MKDIR" ;;
        rmdir) action="RMDIR" ;;
        unlink) action="DELETE" ;;
        rename) action="RENAME" ;;
        *) exit 0 ;;
    esac
    level="INFO"
    if [ "$op" = "connect" ] || [ "$op" = "disconnect" ]; then target="$share"
    else
        target=$(printf '%s' "$args" | sed 's|/$||' | awk -F'/' '{print $NF}')
        [ -z "$target" ] && target="$share"
    fi
fi
if [ "$action" != "CONNECT" ] && [ "$action" != "WRITE" ] && [ "$action" != "DENIED" ]; then exit 0; fi
echo "[$ts] [$level] [$user] [$action] [$target]" >> "$LOG"
SHEOF
chmod +x /usr/local/bin/format-audit.sh

cat > /usr/local/bin/tail-denied.sh << 'SHEOF'
#!/bin/bash
LOG="/libraryit/logs/libraryit.log"
SAMBA_LOG="/libraryit/logs/samba.log"
while [ ! -f "$SAMBA_LOG" ]; do sleep 0.5; done
tail -f "$SAMBA_LOG" | while IFS= read -r line; do
    if printf '%s' "$line" | grep -q "not permitted to access this share"; then
        user=$(printf '%s' "$line" | sed -n "s/.*user '\([^']*\)'.*/\1/p")
        share=$(printf '%s' "$line" | sed -n "s/.*share (\([^)]*\)).*/\1/p")
        ts=$(date '+%Y-%m-%d %H:%M:%S')
        echo "[$ts] [WARNING] [$user] [DENIED] [$share]" >> "$LOG"
    fi
done
SHEOF
chmod +x /usr/local/bin/tail-denied.sh

rm -f /dev/log
socat UNIX-RECV:/dev/log,fork EXEC:/usr/local/bin/format-audit.sh >/dev/null 2>&1 &
/usr/local/bin/tail-denied.sh >/dev/null 2>&1 &

# Wait for /dev/log socket before starting smbd.
for _ in 1 2 3 4 5; do
    [ -S /dev/log ] && break
    sleep 0.2
done

# 6. Start smbd in the foreground (PID 1 of the container).
exec smbd --foreground --no-process-group
