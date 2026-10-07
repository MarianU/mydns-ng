#########################################################
# Stage 0: build the mydns-acme-webhook (Go, static)
#########################################################
FROM golang:1.21-alpine3.18 AS webhook-build

RUN apk add --no-cache git

WORKDIR /workspace

COPY cert-manager-webhook/go.mod ./go.mod
COPY cert-manager-webhook/go.sum ./go.sum
RUN go mod download

COPY cert-manager-webhook/ ./

RUN CGO_ENABLED=0 go build -o /out/webhook -ldflags '-w -s -extldflags "-static"' .

#########################################################
# Stage 1: build the mydns C server
#########################################################
FROM alpine:3.21 as build

COPY ./ /mydns-ng/

RUN apk add --no-cache --virtual build-dependencies \
        build-base \
        gcc \
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
        mariadb-client \
        wget \
    && mkdir /app \
    && cd /mydns-ng/ \
    && ./bootstrap.sh \
    && ./configure --prefix=/app \
    && make \
    && make install \
    && rm -rf /mydns-ng \
    && rm -rf /var/cache/apk/* \
    && /usr/sbin/setcap 'cap_net_bind_service=+ep' /app/sbin/mydns \
    && apk del build-dependencies \
    && mkdir -p /app/etc

#########################################################
# Stage 2: final runtime image (mydns + webhook)
#########################################################
FROM alpine:3.21

RUN apk --no-cache add \
        libintl \
        mariadb-connector-c \
        mariadb-client \
        ca-certificates \
    && adduser -D -H -u 1000 webhook \
    && mkdir -p /app/etc /app/bin /var/run/mydns

# mydns C server
COPY --from=build /app/sbin/ /app/sbin/
COPY --from=build /app/bin/ /app/bin/
COPY --from=build /app/etc/ /app/etc/

# mydns-acme-webhook binary (Go, static)
COPY --from=webhook-build /out/webhook /usr/local/bin/webhook

COPY mydns.conf /app/mydns.conf.tpl
COPY docker/healthcheck.sh /app/bin/
COPY docker/start.sh /app/bin/

RUN chmod 755 /app/bin/healthcheck.sh \
    && chmod 755 /app/bin/start.sh \
    && chmod 755 /usr/local/bin/webhook

HEALTHCHECK --interval=30s --timeout=3s --start-period=60s \
  CMD /app/bin/healthcheck.sh

EXPOSE 53
EXPOSE 53/udp

ENTRYPOINT ["/bin/sh", "-c", "/app/bin/start.sh"]
