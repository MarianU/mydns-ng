GO ?= $(shell which go)
OS ?= $(shell $(GO) env GOOS)
ARCH ?= $(shell $(GO) env GOARCH)

IMAGE_NAME := "ghcr.io/marianu/mydns-ng/mydns-health-monitor"
IMAGE_TAG := "latest"

OUT := $(shell pwd)/_out

HELM_FILES := $(shell find deploy/mydns-health-monitor)

.PHONY: all
all: build

.PHONY: build
build:
	$(GO) build -o $(OUT)/health-monitor -ldflags '-w -s' .

.PHONY: test-unit
test-unit:
	$(GO) test -v ./pkg/...

.PHONY: clean
clean:
	rm -rf _test $(OUT)

.PHONY: docker-build
docker-build:
	docker build -t "$(IMAGE_NAME):$(IMAGE_TAG)" .

.PHONY: docker-push
docker-push:
	docker push "$(IMAGE_NAME):$(IMAGE_TAG)"

.PHONY: helm-lint
helm-lint:
	helm lint deploy/mydns-health-monitor

.PHONY: helm-template
helm-template:
	helm template mydns-health-monitor deploy/mydns-health-monitor

.PHONY: mod-download
mod-download:
	$(GO) mod download

.PHONY: mod-tidy
mod-tidy:
	$(GO) mod tidy

$(OUT):
	mkdir -p $@
