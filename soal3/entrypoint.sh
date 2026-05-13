#!/bin/bash
set -e

LIBRARYIT_ROOT="/libraryit"
LOG_DIR="${LIBRARYIT_ROOT}/logs"
LOG_FILE="${LOG_DIR}/libraryit.log"

# ---------------------------------------------------------------------------
# 1. Groups
# ---------------------------------------------------------------------------
# 'staff' already exists in Ubuntu base (GID 50). 'readonly' must be created.
getent group staff    >/dev/null 2>&1 || groupadd -g 50   staff
getent group readonly >/dev/null 2>&1 || groupadd -g 1000 readonly

# ---------------------------------------------------------------------------
# 2. Users
# ---------------------------------------------------------------------------
# Format: <username>:<uid>:<secondary_group>:<password>
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

# ---------------------------------------------------------------------------
# 3. Collections
# ---------------------------------------------------------------------------
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

# ---------------------------------------------------------------------------
# 4. Validate Samba configuration
# ---------------------------------------------------------------------------
testparm -s /etc/samba/smb.conf >/dev/null 2>&1

# ---------------------------------------------------------------------------
# 5. Logger -- Python script binds /dev/log to translate vfs_full_audit
#     syslog messages into the LibraryIT log format.
# ---------------------------------------------------------------------------
python3 /usr/local/bin/logger.py >/var/log/libraryit-logger.err 2>&1 &
LOGGER_PID=$!

# Give the logger a moment to bind /dev/log before smbd opens syslog().
for _ in 1 2 3 4 5; do
    [ -S /dev/log ] && break
    sleep 0.2
done

# ---------------------------------------------------------------------------
# 6. Start smbd in the foreground (PID 1 of the container).
# ---------------------------------------------------------------------------
exec smbd --foreground --no-process-group
