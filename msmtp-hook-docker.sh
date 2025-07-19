#!/bin/bash
NAME="$1"
TYPE="$2"
STILL_MET="$3"

if [ "$STILL_MET" = "TRUE" ]; then
    subject="LTstats: ALERT: $TYPE threshold exceeded for $NAME"
    message="Alert condition $TYPE detected for $NAME"
else
    subject="LTstats: RESOLVED: $TYPE threshold no longer exceeded for $NAME"
    message="Alert condition $TYPE resolved for $NAME"
fi

if [ "$TYPE" = "DOWN" ]; then
    if [ "$STILL_MET" = "TRUE" ]; then
        subject="LTstats: ALERT: DOWN: $NAME"
        message="$NAME is now DOWN"
    else
        subject="LTstats: RESOLVED: UP: $NAME"
        message="$NAME is now UP"
    fi
fi

echo -e "Subject: $subject\n\n$message" | msmtp -C /status/msmtprc $SMTP_SENDTO