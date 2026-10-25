DEVICES := crown cronos biscuit

.PHONY: sync docker stage test check $(DEVICES)

sync:
	./scripts/sync.sh

docker:
	docker build -t lineage-18.1-ubuntu20:latest -f docker/lineage-18.1.Dockerfile docker/

stage:
	DEVICE=$(DEVICE) ./scripts/stage-tree.sh

$(DEVICES):
	DEVICE=$@ ./scripts/build.sh

check:
	DEVICE=$(DEVICE) ./scripts/check-image.sh

test:
	bash tests/test-stage-tree.sh
	bash tests/test-biscuit-system-update.sh
	bash tests/test-device-layers.sh
