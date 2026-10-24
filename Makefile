DEVICES := crown biscuit

.PHONY: sync docker stage test $(DEVICES)

sync:
	./scripts/sync.sh

docker:
	docker build -t lineage-18.1-ubuntu20:latest -f docker/lineage-18.1.Dockerfile docker/

stage:
	DEVICE=$(DEVICE) ./scripts/stage-tree.sh

$(DEVICES):
	DEVICE=$@ ./scripts/build.sh

test:
	bash tests/test-stage-tree.sh
