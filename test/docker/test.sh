#!/bin/bash
# Get the directory of the script
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)

# Go two levels up
PROJECT_ROOT=$(dirname "$(dirname "$SCRIPT_DIR")")

# Move to project root
cd "$PROJECT_ROOT" || exit 1

# Parse command-line arguments
f_keep_container=false
while [[ $# -gt 0 ]]; do
    case $1 in
        --keep-container)
            f_keep_container=true
            shift
            ;;
        *)
            echo "Unknown option: $1"
            echo "Usage: $0 [--keep-container]"
            echo "  --keep-container: Keep the container running after test for debugging"
            exit 1
            ;;
    esac
done

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

# if needs to do port forwarding -p <local_port>:<container_port>
# docker run -dit --name "$CONTAINER_NAME" -p 28443:28443 "$IMAGE_NAME"
echo "docker run -e PYTHONPATH=/app/test/functional -dit --name $CONTAINER_NAME --network net10 --ip 10.10.0.2 $IMAGE_NAME"
docker run -e PYTHONPATH=/app/test/functional -dit --name "$CONTAINER_NAME" --network net10 --ip 10.10.0.2 "$IMAGE_NAME"
echo "docker network connect --ip 81.10.0.2 net81 $CONTAINER_NAME"
docker network connect --ip 81.10.0.2 net81 "$CONTAINER_NAME"

echo "docker network connect --ip 8.8.0.2 net8 $CONTAINER_NAME"
docker network connect --ip 8.8.0.2 net8 "$CONTAINER_NAME"

echo "docker network connect --ip 203.10.0.2 net203 $CONTAINER_NAME"
docker network connect --ip 203.10.0.2 net203 "$CONTAINER_NAME"

echo "docker exec $CONTAINER_NAME bash -c \"rm -rf build/ && ./configure --debug --generator=\"Unix Makefiles\" && cd build && make -j8\""
docker exec "$CONTAINER_NAME" bash -c "rm -rf build/ && ./configure --debug --generator=\"Unix Makefiles\" && cd build && make -j8"
docker exec "$CONTAINER_NAME" bash -c "cd test/docker && cp ../functional/test_runner.py . && ./test_runner.py test_peer_eviction.py --nocleanup"

# Capture the test exit status
n_test_status=$?

# Cleanup if test was successful and --keep-container was not specified
if [ $n_test_status -eq 0 ]; then
    echo ""
    echo "Test passed successfully!"

    if [ "$f_keep_container" = true ]; then
        echo "Container and image kept for debugging (--keep-container specified)"
        echo "Container name: $CONTAINER_NAME"
        echo "To stop and remove manually, run:"
        echo "  docker stop $CONTAINER_NAME"
        echo "  docker rm $CONTAINER_NAME"
        echo "  docker rmi $IMAGE_NAME"
    else
        echo "Cleaning up container and image to save space..."
        echo "docker stop $CONTAINER_NAME"
        docker stop "$CONTAINER_NAME"
        echo "docker rm $CONTAINER_NAME"
        docker rm "$CONTAINER_NAME"
        echo "docker rmi $IMAGE_NAME"
        docker rmi "$IMAGE_NAME"
        echo "Removing Docker networks..."
        docker network rm net10 net8 net203 net81
        echo "Cleanup complete!"
    fi
else
    echo ""
    echo "Test failed with exit status $n_test_status"
    echo "Container kept for debugging"
    echo "Container name: $CONTAINER_NAME"
    echo "To inspect: docker exec -it $CONTAINER_NAME bash"
fi

exit $n_test_status
