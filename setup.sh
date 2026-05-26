#!/bin/bash
set -e

echo "=== mem2reg-sccp project setup ==="

# Detect distro
if [ -f /etc/os-release ]; then
    . /etc/os-release
    DISTRO=$ID
else
    echo "Cannot detect distro. Run on Ubuntu/Debian."
    exit 1
fi

install_ubuntu() {
    echo "[1/4] Updating package lists..."
    sudo apt-get update -y

    echo "[2/4] Installing build tools..."
    sudo apt-get install -y \
        build-essential \
        cmake \
        ninja-build \
        git

    echo "[3/4] Installing LLVM 17..."
    # Official LLVM apt repository
    sudo apt-get install -y wget gnupg lsb-release software-properties-common
    wget -qO- https://apt.llvm.org/llvm-snapshot.gpg.key | sudo tee /etc/apt/trusted.gpg.d/apt.llvm.org.asc > /dev/null
    sudo add-apt-repository -y \
        "deb http://apt.llvm.org/$(lsb_release -cs)/ llvm-toolchain-$(lsb_release -cs)-17 main"
    sudo apt-get update -y
    sudo apt-get install -y \
        llvm-17 \
        llvm-17-dev \
        llvm-17-tools \
        clang-17 \
        libclang-17-dev \
        clang-format-17

    echo "[4/4] Setting up symlinks..."
    sudo update-alternatives --install /usr/bin/clang   clang   /usr/bin/clang-17   100
    sudo update-alternatives --install /usr/bin/clang++ clang++ /usr/bin/clang++-17 100
    sudo update-alternatives --install /usr/bin/opt     opt     /usr/bin/opt-17     100
    sudo update-alternatives --install /usr/bin/llvm-config llvm-config /usr/bin/llvm-config-17 100
    sudo update-alternatives --install /usr/bin/FileCheck   FileCheck   /usr/bin/FileCheck-17   100
}

install_arch() {
    echo "[1/3] Installing build tools and LLVM..."
    sudo pacman -Syu --noconfirm \
        base-devel \
        cmake \
        ninja \
        git \
        llvm \
        clang
}

case "$DISTRO" in
    ubuntu|debian|linuxmint|pop)
        install_ubuntu
        ;;
    arch|manjaro|endeavouros)
        install_arch
        ;;
    *)
        echo "Unsupported distro: $DISTRO"
        echo "Install manually: cmake 3.20+, LLVM 17 dev headers, clang 17, ninja"
        exit 1
        ;;
esac

echo ""
echo "=== Verifying installation ==="
cmake --version       | head -1
llvm-config --version | xargs echo "LLVM:"
clang++ --version     | head -1
opt --version         | head -1

echo ""
echo "=== Build instructions ==="
echo "  cmake -S . -B build -DLLVM_DIR=\$(llvm-config --cmakedir) -G Ninja"
echo "  cmake --build build"
echo ""
echo "Setup complete."
