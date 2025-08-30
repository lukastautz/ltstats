#!/bin/bash
# LTstats server installer for systems using systemd as init system
DOMAIN=ltstats.de
VERSION=1.3
SERVER_HASH=7c6074789a41fb2e3eafa1eef5f754e520104c570e183fc5642d77895d3fd399
NTP_HASH=c00bcfc5b3e572842d90d82913a3e88813adbc1cb93689eb1f6b30e511638a48
STATUS_HTML_HASH=543f6df5ff55e8fa90aa6515e623f9bf4c53e645f6838465dbc62fea0786f068
MONITOR_HTML_HASH=0fe60673da899fcdcfc1fd3b71ccf55b0c4ad9ed183774cd0f905db7b78663cf
ADMIN_HTML_HASH=48044b8f2a46773b94fc6974f6936929b4313543a6732248aa69d2d6f896cd72
NOTIFY_HOOK_HASH=055f0f3459669301f1497676855a621182b866c4fedb3f5061dbe3364bcc2bfd
AGENT_INSTALL_HASH=123bdcc123d39dfe915eb3ed9223ea75c845a8bef5c79b994f4b2de20530085c

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

upgrade_from() {
    case "$1" in
        v1.0|1.0|v1.1|1.1|v1.2|1.2)
            echo "Upgrading from v1.0/v1.1/v1.2."
            read -p "Data path: " LTSTATS_PATH
            cd $LTSTATS_PATH
            systemctl stop ltstats_server
            download $SERVER_URL /bin/ltstats_server $SERVER_HASH
            chmod +x /bin/ltstats_server
            if [ -e /bin/ltstats_ntp ]; then
                download $NTP_URL /bin/ltstats_ntp $NTP_HASH
                chmod +x /bin/ltstats_ntp
                systemctl restart ltstats_ntp
            fi
            if [ "$LEAVE_HTML" == "" ]; then
                download $ADMIN_HTML_URL admin.html $ADMIN_HTML_HASH
                download $STATUS_HTML_URL status.html $STATUS_HTML_HASH
                download $MONITOR_HTML_URL monitor.html $MONITOR_HTML_HASH
                chown ltstats {admin,monitor,status}.html 2> /dev/null
            fi
            systemctl start ltstats_server
            echo "Upgrade done."
            cd - 2> /dev/null > /dev/null
            ;;
        *)
            echo "Unknown version."
            ;;
    esac
}

echo "LTstats server installer."
if [ -e /bin/ltstats_server ]; then
    V1_0_HASH=4f0fa237c973b9fef391412c5c79e4ea184e3da4b0376388debb772c11db8cea
    V1_1_HASH=eb19930043fe13f5f2185828c150342306a87ad6e2981c6ceb14b999e37ad748
    V1_2_BETA_HASH=94dcfcc86131380a2320d43d4df51684c4a9e54f07428c15d34c764c31db5847
    V1_2_HASH=423e767ff352f1a939b6065b8eb7f7de0dd88da6035afd7efb61dc847e2a7053
    if sha256sum -c <(echo $V1_0_HASH /bin/ltstats_server) > /dev/null 2> /dev/null; then
        upgrade_from v1.0
    elif sha256sum -c <(echo $V1_1_HASH /bin/ltstats_server) > /dev/null 2> /dev/null || sha256sum -c <(echo $V1_2_BETA_HASH /bin/ltstats_server) > /dev/null 2> /dev/null; then
        upgrade_from v1.1
    elif sha256sum -c <(echo $V1_2_HASH /bin/ltstats_server) > /dev/null 2> /dev/null; then
        upgrade_from v1.2
    elif sha256sum -c <(echo $SERVER_HASH /bin/ltstats_server) > /dev/null 2> /dev/null; then
        echo "Already up-to-date."
    else
        echo "Autodetection of current version failed."
        read -p "Version you want to upgrade from: " VERSION
        upgrade_from $VERSION
    fi
    rm -- "$0"
    exit
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
echo "{\"time\":$(date +%s),\"hash\":\"$(printf %s "$PASSWORD" | sha256sum | sed -E 's/\s+-//')\",\"monitors\":{},\"pages\":{\"main\":[\"Main page\",true,[]]},\"hide\":[],\"notifications\":{\"every\":60,\"exec\":[\"$LTSTATS_PATH/notify.sh\",\"NAME\",\"TYPE\",\"STILL_MET\"],\"sample\":30},\"copy\":\"curl -s https://ltstats.de/v1.3/systemd:agent | tee install.sh | sha256sum -c <(echo $AGENT_INSTALL_HASH -) && bash install.sh DOMAIN TOKEN ntp ADDITIONAL_PATHS # NAME\"}" > data.json
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
