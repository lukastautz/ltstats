#!/bin/bash
# LTstats agent installer for systems using systemd as init system
DOMAIN=ltstats.de
VERSION=1.1
AGENT_HASH=0bad48dd1aca7c47f0355375d57fa9f6d9eea8357cd084b9281e287c00449019
NTP_HASH=2819b97e9b528562ac41636505718f1372db0e938f5912d305d9edded2dad7b4

AGENT_URL=https://$DOMAIN/v$VERSION/ltstats_agent
NTP_URL=https://$DOMAIN/v$VERSION/ltstats_ntp
DOMAIN=$1
TOKEN=$2
NTP=$3
PATHS=$4

download() {
    URL=$1
    TO=$2
    HASH=$3
    rm $TO 2> /dev/null
    if [ "$(which curl)" != "" ]; then
        if ! curl -s $URL -o $TO; then
            echo "Downloading $TO failed."
            exit
        fi
    elif [ "$(which wget)" != "" ]; then
        if ! wget -q $URL -O $TO; then
            echo "Downloading $TO failed."
            exit
        fi
    else
        echo "Please install either curl or wget. If you are getting this error but have installed at least one of them, check your PATH."
        exit
    fi
    if ! sha256sum -c <(echo $HASH $TO) > /dev/null 2> /dev/null; then
        rm $TO
        echo "Hash mismatch for $TO."
        exit
    fi
}

if [ "$EUID" -ne 0 ]; then
    echo "This script needs to be run as root."
    exit
fi
if [ "$#" -ne 3 ] && [ "$#" -ne 4 ]; then
    echo "Missing argument(s)"
    echo "$0 DOMAIN TOKEN NTP [PATHS]."
    exit
fi
if [ "${#TOKEN}" -ne 32 ]; then
    echo "Invalid token length."
    exit
fi
if [ "$NTP" = "ntp" ]; then
    if [ -e /bin/ltstats_ntp ] && sha256sum -c <(echo $NTP_HASH /bin/ltstats_ntp) > /dev/null 2> /dev/null; then
        : # NTP client already installed and up-to-date, skipping
    elif systemctl is-active --quiet systemd-timesyncd || systemctl is-active --quiet chrony || systemctl is-active --quiet ntpd || systemctl is-active --quiet ntp; then
        echo "Detected running NTP client, skipping installation of ltstats_ntp."
    else
        download $NTP_URL /bin/ltstats_ntp $NTP_HASH
        chmod +x /bin/ltstats_ntp
        echo '[Unit]
Description=LTstats ntp client
After=network.target

[Service]
Type=simple
ExecStart=/bin/ltstats_ntp
User=root

[Install]
WantedBy=multi-user.target' > /etc/systemd/system/ltstats_ntp.service
        systemctl daemon-reload
        systemctl enable ltstats_ntp 2> /dev/null
        systemctl start ltstats_ntp
    fi
fi
download $AGENT_URL /bin/ltstats_agent $AGENT_HASH
chmod 555 /bin/ltstats_agent
echo $TOKEN > /etc/monitoring_token
chmod 600 /etc/monitoring_token
USER=ltstats
if ! grep ltstats /etc/passwd > /dev/null; then
    if ! useradd -MN ltstats 2> /dev/null; then
        echo "Failed to create user, falling back to root. You can manually create the ltstats user and re-run this script."
        USER=root
    fi
fi
chown $USER /etc/monitoring_token
if [ "$PATHS" != "" ]; then
    PATHS=" $PATHS"
fi
if [ -e /etc/systemd/system/ltstats_agent.service ]; then
    systemctl stop ltstats_agent
fi
echo "[Unit]
Description=LTstats monitoring agent
After=network.target

[Service]
Type=simple
ExecStart=/bin/ltstats_agent $DOMAIN /etc/monitoring_token$PATHS
User=$USER

[Install]
WantedBy=multi-user.target" > /etc/systemd/system/ltstats_agent.service
systemctl daemon-reload
systemctl enable ltstats_agent 2> /dev/null
systemctl start ltstats_agent
rm -- "$0"
echo "The LTstats monitoring agent has been installed! In around a minute you will be able to see the first statistics."
