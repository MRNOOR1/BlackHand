set -e

echo "=========================================="
echo "  Black Hand OS - Development Setup"
echo "=========================================="

mkdir -p custom

echo "[1/2] Building Docker image..."
docker-compose build

echo "[2/2] Cloning Buildroot inside container..."
docker-compose run --rm buildroot bash -c '
    cd /home/builder
    rm -rf buildroot
    git clone https://gitlab.com/buildroot.org/buildroot.git buildroot
    cd buildroot
    LATEST_STABLE=$(git tag | grep "^2024" | sort -V | tail -1)
    echo "Checking out: $LATEST_STABLE"
    git checkout "$LATEST_STABLE"
'

echo ""
echo "Setup complete!"
echo ""
echo "Next: docker-compose run --rm buildroot"