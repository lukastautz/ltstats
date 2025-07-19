#!/bin/bash
# LTstats server installer for systems using systemd as init system
DOMAIN=ltstats.de
VERSION=1.0
SERVER_HASH=4f0fa237c973b9fef391412c5c79e4ea184e3da4b0376388debb772c11db8cea
NTP_HASH=2819b97e9b528562ac41636505718f1372db0e938f5912d305d9edded2dad7b4
STATUS_HTML_HASH=d8e267e952d46ed356ab4e2dbc24fe988bd1120449dc6274719b4a7948add4b1
MONITOR_HTML_HASH=e97d3a075c2b7759cfa58b60dee94f1a33ea73a06c17e86ab474d9d6e293d437
ADMIN_HTML_HASH=54b698bd5a392770c71f86a052dd32ad0ffac2a5a6dcf7ea7d88b6a7887eb25e
NOTIFY_HOOK_HASH=055f0f3459669301f1497676855a621182b866c4fedb3f5061dbe3364bcc2bfd
AGENT_INSTALL_HASH=39150d748430884a9796e857b284f9b9102957be54870eeee27bd9febd8af685

SERVER_URL=https://$DOMAIN/v$VERSION/ltstats_server
NTP_URL=https://$DOMAIN/v$VERSION/ltstats_ntp
STATUS_HTML_URL=https://$DOMAIN/v$VERSION/status.html
MONITOR_HTML_URL=https://$DOMAIN/v$VERSION/monitor.html
ADMIN_HTML_URL=https://$DOMAIN/v$VERSION/admin.html
NOTIFY_HOOK_URL=https://$DOMAIN/v$VERSION/msmtp-hook.sh

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
    echo "This script needs to be run as root"
    exit
fi
echo "LTstats server installer."
if [ -e /bin/ltstats_server ]; then
    : # Upgrade, will need to be handled in future versions, for example with just replacement of /bin/ltstats_server in the simplest case or, if the binary format or the json format changes in the future, converting them
fi
read -p "Path where the data should be saved: " LTSTATS_PATH
if [ -e "$LTSTATS_PATH" ] || [ -d "$LTSTATS_PATH" ]; then
    read -p "This path already exists. Press any key to delete or Ctrl+C to quit."
    rm -rf "$LTSTATS_PATH"
fi
mkdir -p "$LTSTATS_PATH"
cd "$LTSTATS_PATH"
read -p "Install the LTstats NTP client? y/n: " NTP
case "$NTP" in
    y|Y)
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
        ;;
esac
stty -echo
read -p "Enter the admin password: " PASSWORD
stty echo
echo
read -p "Enter the port the server will listen on (you will need to setup a reverse proxy to this): " PORT

download $SERVER_URL /bin/ltstats_server $SERVER_HASH
chmod 555 /bin/ltstats_server
USER=ltstats
if ! grep ltstats /etc/passwd > /dev/null; then
    if ! useradd -MN ltstats 2> /dev/null; then
        echo "Failed to create user, falling back to root. You can manually create the ltstats user and re-run this script."
        USER=root
    fi
fi
if [ -e /etc/systemd/system/ltstats_server.service ]; then
    systemctl stop ltstats_server
fi
download $STATUS_HTML_URL status.html $STATUS_HTML_HASH
download $MONITOR_HTML_URL monitor.html $MONITOR_HTML_HASH
download $ADMIN_HTML_URL admin.html $ADMIN_HTML_HASH
download $NOTIFY_HOOK_URL notify.sh $NOTIFY_HOOK_HASH
chmod +x notify.sh
echo "For notifications to work, you will have to use a custom script, or setup msmtp and modify the default script."
echo "{\"time\":$(date +%s),\"hash\":\"$(printf %s "$PASSWORD" | sha256sum | sed -E 's/\s+-//')\",\"monitors\":{},\"pages\":{\"main\":[\"Main page\",true,[]]},\"hide\":[],\"notifications\":{\"every\":60,\"exec\":[\"$LTSTATS_PATH/notify.sh\",\"NAME\",\"TYPE\",\"STILL_MET\"],\"sample\":30},\"copy\":\"curl -s https://ltstats.de/v1.0/systemd:agent | tee install.sh | sha256sum -c <(echo $AGENT_INSTALL_HASH -) && bash install.sh DOMAIN TOKEN ntp ADDITIONAL_PATHS # NAME\"}" > data.json
chown -R $USER "$LTSTATS_PATH"
chmod -R 700 "$LTSTATS_PATH"
echo "[Unit]
Description=LTstats monitoring server
After=network.target

[Service]
Type=simple
ExecStart=/bin/ltstats_server "$LTSTATS_PATH" 128 $PORT
User=$USER

[Install]
WantedBy=multi-user.target" > /etc/systemd/system/ltstats_server.service
systemctl daemon-reload
systemctl enable ltstats_server 2> /dev/null
systemctl start ltstats_server
cd - > /dev/null 2> /dev/null
rm -- "$0"
echo "The LTstats monitoring server has been installed!"
