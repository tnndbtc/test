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

echo "docker network create --subnet=172.18.0.0/16 net172"
docker network create --subnet=172.18.0.0/16 net172

echo "docker network create --subnet=10.10.0.0/16 net10"
docker network create --subnet=10.10.0.0/16 net10

echo "docker run -dit --name $CONTAINER_NAME --network net172 $IMAGE_NAME"
docker run -dit --name "$CONTAINER_NAME" --network net172 "$IMAGE_NAME"

echo "docker network connect net10 $CONTAINER_NAME"
docker network connect net10 "$CONTAINER_NAME"

echo "docker exec $CONTAINER_NAME bash -c \"rm -rf build/ && ./configure --generator=\"Unix Makefiles\" && cd build && make -j8\""
docker exec "$CONTAINER_NAME" bash -c "rm -rf build/ && ./configure --generator=\"Unix Makefiles\" && cd build && make -j8"
