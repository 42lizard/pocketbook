#!/bin/sh
# Diagnostic launcher only: captures loader, Qt plugin and QML startup errors.
umask 077
log=/mnt/ext1/system/readest-sync/qt-startup.log
{
    printf 'Readest Sync Qt startup diagnostic\n'
    QT_DEBUG_PLUGINS=1 /mnt/ext1/applications/readest-sync.app
    result=$?
    printf '\nExit status: %s\n' "$result"
} > "$log" 2>&1
