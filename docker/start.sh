#!/bin/sh

DB_HOST=${DB_HOST:?}              # SQL server hostname
DB_USER=${DB_USER:?}              # SQL server username
DB_PASS=${DB_PASS:?}              # SQL server password
DB_DATABASE=${DB_DATABASE:?}      # MyDNS database name
HOSTED_DOMAIN=${HOSTED_DOMAIN:?}  # Healthcheck domain to validate

# generate conf file
eval "cat <<EOF
$(cat /app/etc/mydns.conf.tpl)
EOF
" 2> /dev/null > /app/etc/mydns.conf

# start the app
exec /app/sbin/mydns -c /app/etc/mydns.conf