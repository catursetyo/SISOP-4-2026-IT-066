FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    samba \
    samba-vfs-modules \
    smbclient \
    rsyslog \
    python3 \
    && rm -rf /var/lib/apt/lists/*

RUN mkdir -p /libraryit /logs /var/log/samba /run/samba /var/run/samba /var/cache/samba /var/lib/samba/private

COPY smb.conf /etc/samba/smb.conf
COPY entrypoint.sh /entrypoint.sh

RUN python3 -c "from pathlib import Path; p=Path('/entrypoint.sh'); s=p.read_bytes(); s=s.replace(b'\xef\xbb\xbf', b''); s=s.replace(b'\r\n', b'\n').replace(b'\r', b'\n'); p.write_bytes(s)"
RUN chmod +x /entrypoint.sh

EXPOSE 445

ENTRYPOINT ["/bin/bash", "/entrypoint.sh"]
