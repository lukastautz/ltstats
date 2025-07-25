# LTstats server docker image. NOT recommended!

FROM debian:bookworm-slim

RUN apt update && \
    apt upgrade -y && \
    apt install -y msmtp curl ca-certificates && \
    apt clean && \
    rm -rf /var/lib/apt/lists/* && \
    mkdir /storage /status && \
    curl https://ltstats.de/v1.2/ltstats_server_docker -o /bin/ltstats_server && \
    chmod +x /bin/ltstats_server && \
    curl https://ltstats.de/v1.2/admin.html -o /storage/admin.html && \
    curl https://ltstats.de/v1.2/monitor.html -o /storage/monitor.html && \
    curl https://ltstats.de/v1.2/status.html -o /storage/status.html && \
    curl https://ltstats.de/v1.2/msmtp-hook-docker.sh -o /storage/hook.sh && \
    cat > /start.sh << 'EOF'
#!/bin/bash
cat > /status/msmtprc << EOC
defaults
auth on
tls on
tls_trust_file /etc/ssl/certs/ca-certificates.crt
account default
host $SMTP_HOST
port $SMTP_PORT
tls on
tls_starttls off
auth plain
user $SMTP_USER
from $SMTP_USER
password "$SMTP_PASSWORD"
EOC
chmod 600 /status/msmtprc
if [ -f /status/version ]; then
    if [ "$(cat /status/version)" != "1.2" ]; then
        echo 1.2 > /status/version
        : # handle conversions
    fi
else
    echo 1.2 > /status/version
fi
if [ ! -f /status/data.json ]; then
    echo "{\"time\":$(date +%s),\"hash\":\"$(printf admin | sha256sum | sed -E 's/\s+-//')\",\"monitors\":{},\"pages\":{\"main\":[\"Main page\",true,[]]},\"hide\":[],\"notifications\":{\"every\":60,\"exec\":[\"/bin/bash\",\"/status/msmtp.sh\",\"NAME\",\"TYPE\",\"STILL_MET\"],\"sample\":30},\"copy\":\"curl -s https://ltstats.de/v1.1/systemd:agent | tee install.sh | sha256sum -c <(echo f555d4e5cdc64a08b81c2d6fa879f366c19d705f0678b6db9e0f5fd4ee09d825 -) && bash install.sh DOMAIN TOKEN ntp ADDITIONAL_PATHS # NAME\"}" > /status/data.json
fi
if [ ! -e /status/status.html ]; then
    ln -s /storage/status.html /status/status.html
fi
if [ ! -e /status/monitor.html ]; then
    ln -s /storage/monitor.html /status/monitor.html
fi
if [ ! -e /status/admin.html ]; then
    ln -s /storage/admin.html /status/admin.html
fi
if [ ! -e /status/hook.sh ]; then
    ln -s /storage/hook.sh /status/msmtp.sh
fi
exec /bin/ltstats_server /status 128 8080
EOF
RUN chmod +x /start.sh

VOLUME ["/status"]

EXPOSE 8080

ENTRYPOINT ["/start.sh"]