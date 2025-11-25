#!/bin/bash
# Get the directory of the script
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)

# Go two levels up
PROJECT_ROOT=$(dirname "$(dirname "$SCRIPT_DIR")")

# Move to project root
cd "$PROJECT_ROOT" || exit 1

CONTAINER_NAME="bweave"
IMAGE_TAG="bweave_test"
IMAGE_NAME="bweave_test:latest"
#IMAGE_NAME="bweave_test"

echo "docker stop $CONTAINER_NAME"
docker stop "$CONTAINER_NAME"

echo "docker rm $CONTAINER_NAME"
docker rm "$CONTAINER_NAME"

echo "docker rmi $IMAGE_NAME"
docker rmi "$IMAGE_NAME"

echo "docker build -f test/docker/dockerfile.dev -t $IMAGE_TAG ."
docker build -f test/docker/dockerfile.dev -t "$IMAGE_TAG" .

echo "docker network create --subnet=10.10.0.0/16 net10"
docker network rm net10
docker network create --subnet=10.10.0.0/16 net10
#echo "docker network connect net10 $CONTAINER_NAME"
#docker network connect net10 "$CONTAINER_NAME"

echo "docker network create --subnet=203.10.0.0/16 net203"
docker network rm net203
docker network create --subnet=203.10.0.0/16 net203
#echo "docker network connect net203 $CONTAINER_NAME"
#docker network connect net203 "$CONTAINER_NAME"

echo "docker network create --subnet=81.10.0.0/16 net81"
docker network rm net81
docker network create --subnet=81.10.0.0/16 net81
#echo "docker network connect net81 $CONTAINER_NAME"
#docker network connect net81 "$CONTAINER_NAME"

echo "docker network create --subnet=8.8.0.0/16 net8"
docker network rm net8
docker network create --subnet=8.8.0.0/16 net8
#echo "docker network connect net8 $CONTAINER_NAME"
#docker network connect net8 "$CONTAINER_NAME"

# echo "docker run -dit --name $CONTAINER_NAME $IMAGE_NAME"
# if needs to do port forwarding -p <local_port>:<container_port>
# docker run -dit --name "$CONTAINER_NAME" -p 28443:28443 "$IMAGE_NAME"
echo "docker run -e PYTHONPATH=/app/test/functional -dit --name $CONTAINER_NAME $IMAGE_NAME"
docker run -e PYTHONPATH=/app/test/functional -dit --name "$CONTAINER_NAME" --network net10 --ip 10.10.0.2 "$IMAGE_NAME"
docker network connect --ip 81.10.0.2 net81 "$CONTAINER_NAME"
docker network connect --ip 8.8.0.2 net8 "$CONTAINER_NAME"
docker network connect --ip 203.10.0.2 net203 "$CONTAINER_NAME"

echo "docker exec $CONTAINER_NAME bash -c \"rm -rf build/ && ./configure --generator=\"Unix Makefiles\" && cd build && make -j8\""
docker exec "$CONTAINER_NAME" bash -c "rm -rf build/ && ./configure --generator=\"Unix Makefiles\" && cd build && make -j8"
docker exec "$CONTAINER_NAME" bash -c "cd test/docker && cp ../functional/test_runner.py . && ./test_runner.py test_peer_eviction.py"
