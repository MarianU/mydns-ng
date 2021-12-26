FROM alpine:3.15 as build

COPY ./ /mydns-ng/

RUN apk add --no-cache --virtual build-dependencies \
        build-base \
        gcc \
        wget \
        git \
        autoconf \
        gettext \
        gettext-dev \
        automake \
        libtool \
        mysql-dev \
        texinfo \
        bash \
        openssh-client \
        libcap \
    && apk --no-cache add \
        libintl \
        mariadb-connector-c \
    && mkdir /app \
    && cd / \
    && cd /mydns-ng/ \
    && ./bootstrap.sh \
    && ./configure --prefix=/app \
    && make \
    && make install \
    && rm -rf /mydns-ng \
    && rm -rf /var/cache/apk/* \
    && /usr/sbin/setcap 'cap_net_bind_service=+ep' /app/sbin/mydns \
    && apk del build-dependencies

COPY mydns.conf /app/etc/mydns.conf.tpl
COPY docker/healthcheck.sh /app/bin/
COPY docker/start.sh /app/bin/

RUN chmod 755 /app/bin/healthcheck.sh \
    && chmod 755 /app/bin/start.sh

HEALTHCHECK --interval=30s --timeout=3s \
  CMD /app/bin/healthcheck.sh

EXPOSE 53

ENTRYPOINT ["/bin/sh", "-c", "/app/bin/start.sh"]

#    && git clone git://github.com/MarianU/mydns-ng.git \
