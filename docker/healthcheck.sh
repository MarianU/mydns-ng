#!/bin/sh

nslookup ${HOSTED_DOMAIN} 127.0.0.1 >/dev/null 2>&1
exit $?
