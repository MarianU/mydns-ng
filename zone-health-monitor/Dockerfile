FROM golang:1.21-alpine3.18 AS build_deps

RUN apk add --no-cache git

WORKDIR /workspace

COPY go.mod .
COPY go.sum .

RUN go mod download

FROM build_deps AS build

COPY . .

RUN CGO_ENABLED=0 go build -o health-monitor -ldflags '-w -s -extldflags "-static"' .

FROM alpine:3.18

RUN apk add --no-cache ca-certificates && \
    adduser -D -H -u 1000 monitor

COPY --from=build /workspace/health-monitor /usr/local/bin/health-monitor

USER monitor

ENTRYPOINT ["health-monitor"]
