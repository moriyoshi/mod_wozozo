#!/bin/sh

case "$1" in
    server)
        go run test.go
        ;;
    client)
       /usr/sbin/apache2 -d /usr/lib/apache2 -f $PWD/httpd.minimal.conf -DFOREGROUND -DMOZART_INSTALL_PREFIX=/opt/mozart2
       ;;
    shell)
       /bin/bash --login
       ;;
esac
