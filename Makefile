all: build

YOCTO_VERSION=mickledore

SRC_PATH=$(CURDIR)
OUTPUT_PATH=$(CURDIR)/build
DESTINATION_PATH=$(SRC_PATH)
VERSION=$(shell git describe --tags `git rev-list --tags --max-count=1`)

ifeq ($(YOCTO_VERSION),kirkstone)
  DOCKERFILE=./res/kirkstone_qt_builder.Dockerfile
  DOCKER_TAG_NAME=advantech/qt-builder-kirkstone
else
  DOCKERFILE=./res/mickledore_qt_builder.Dockerfile
  DOCKER_TAG_NAME=advantech/qt-builder-mickledore
endif

build-image:
	docker buildx build --platform linux/arm64 -t $(DOCKER_TAG_NAME) -f $(DOCKERFILE) .

build: build-image clean generate-makefile
	@echo "build in docker"
	docker run --rm --platform linux/arm64 -v $(SRC_PATH):/src $(DOCKER_TAG_NAME) make linux-build

linux-build:
	@echo "make start"
	mkdir -p build/; cd build/; $(MAKE)

linux-generate-makefile:
	@echo "generate makefile in builder environment"
	mkdir -p build/; cd build/; cmake ..

generate-makefile:
	@echo "generate makefile in docker"
	docker run --rm --platform linux/arm64 -v $(SRC_PATH):/src $(DOCKER_TAG_NAME) make linux-generate-makefile

install:
	@echo "install xdg-desktop-portal-lxqt"
	cp build/xdg-desktop-portal-lxqt $(DESTINATION_PATH)/usr/local/libexec/
	cp build/org.freedesktop.impl.portal.desktop.lxqt.service $(DESTINATION_PATH)/usr/share/dbus-1/services/
	cp build/lxqt.portal $(DESTINATION_PATH)/usr/local/share/xdg-desktop-portal/portals/
	cp build/lxqt-portals.conf $(DESTINATION_PATH)/usr/local/share/xdg-desktop-portal/
	mkdir -p $(DESTINATION_PATH)/usr/share/common-licenses/xdg-desktop-portal-lxqt
	cp $(SRC_PATH)/LICENSE $(DESTINATION_PATH)/usr/share/common-licenses/xdg-desktop-portal-lxqt

clean:
	rm -rf build/*

clean-cmakefiles:
	@echo "copy xdg-desktop-portal-lxqt artifacts"
	cp build/src/xdg-desktop-portal-lxqt build/
	cp build/data/org.freedesktop.impl.portal.desktop.lxqt.service build/
	cp data/lxqt.portal build/
	cp data/lxqt-portals.conf build/
	@echo "remove cmakefiles"
	rm -rf build/.qt
	rm -rf build/CMakeFiles
	rm -rf build/data
	rm -rf build/src
	rm -rf build/cmake_install.cmake
	rm -rf build/CMakeCache.txt
	rm -rf build/Makefile

create-sbom: clean-sbom clean-cmakefiles
	cd build && sbom-tool generate -b . -bc . -pn xdg-desktop-portal-lxqt -pv $(VERSION) -ps Advantech -nsb "https://github.com/Advantech-IIoT"
	cp build/_manifest/spdx_2.2/manifest.spdx.json $(DESTINATION_PATH)/../../scripts/out/sbom/xdg-desktop-portal-lxqt.manifest.spdx.json

clean-sbom:
	rm -rf build/_manifest
