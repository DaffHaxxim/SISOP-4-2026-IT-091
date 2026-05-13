FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        samba \
        samba-vfs-modules \
        python3 \
        ca-certificates \
        acl && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/*

COPY smb.conf      /etc/samba/smb.conf
COPY entrypoint.sh /usr/local/bin/entrypoint.sh
COPY logger.py     /usr/local/bin/logger.py

RUN chmod 0755 /usr/local/bin/entrypoint.sh /usr/local/bin/logger.py

EXPOSE 445

CMD ["/usr/local/bin/entrypoint.sh"]
