# Black Hand OS. Tiers stack: building TIER=N includes tiers 0..N.
#
#   make image TIER=N              image with tiers 0..N -> output/tierN-sdcard.img
#   make test TIER=N               host tests for tiers 0..N
#   make device-test TIER=N HOST=ip  on-device checks for tiers 1..N over SSH
#   make shell                     shell in the build container
#
# Wi-Fi credentials: tier1-boot/wifi.env (see tier1-boot/README.md).

TIER ?= 1
IMAGE = blackhand-buildroot
DOCKER_RUN = docker run --rm \
	-v blackhand-cache:/work/cache \
	-v "$(CURDIR)":/work/blackhand \
	-e BLACKHAND_WIFI_SSID -e BLACKHAND_WIFI_PSK \
	$(IMAGE)
TIER_DIRS = $(foreach t,$(shell seq 0 $(TIER)),$(wildcard tier$(t)-*))

.PHONY: image test device-test shell docker
image: docker
	$(DOCKER_RUN) tier0-build/build.sh $(TIER)

test:
	@set -e; for d in $(TIER_DIRS); do echo "== $$d"; $(MAKE) -s -C $$d/tests check; done

device-test:
	@test -n "$(HOST)" || { echo "usage: make device-test TIER=N HOST=<device ip>"; exit 2; }
	@fail=0; for d in $(TIER_DIRS); do \
		[ -f $$d/tests/device.sh ] || continue; \
		ssh root@$(HOST) sh -s < $$d/tests/device.sh || fail=1; \
	done; exit $$fail

shell: docker
	docker run --rm -it -v blackhand-cache:/work/cache -v "$(CURDIR)":/work/blackhand $(IMAGE) bash

docker:
	docker build -q -t $(IMAGE) tier0-build
