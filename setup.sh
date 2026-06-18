#!/bin/bash
set -e

echo "=== mem2reg-sccp project setup ==="

if [ -f /etc/os-release ]; then
    . /etc/os-release
    DISTRO=$ID
else
    echo "Cannot detect distro. Run on Ubuntu/Debian."
    exit 1
fi

install_ubuntu() {
    echo "[1/3] Updating package lists..."
    sudo apt-get update -y

    echo "[2/3] Installing build tools..."
    sudo apt-get install -y \
        build-essential \
        cmake \
        ninja-build \
        git \
        llvm-14 \
        llvm-14-dev \
        llvm-14-tools \
        clang-14 \
        clang-format-14

    echo "[3/3] Setting up symlinks..."
        sudo update-alternatives --install /usr/bin/clang       clang       /usr/bin/clang-14       100
        sudo update-alternatives --install /usr/bin/clang++     clang++     /usr/bin/clang++-14     100
        sudo update-alternatives --install /usr/bin/opt         opt         /usr/bin/opt-14         100
        sudo update-alternatives --install /usr/bin/llvm-config llvm-config /usr/bin/llvm-config-14 100
        sudo update-alternatives --install /usr/bin/FileCheck   FileCheck   /usr/bin/FileCheck-14   100
}

install_arch() {
    echo "[1/1] Installing build tools and LLVM..."
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
echo "=== Run a pass ==="
echo "  clang -S -emit-llvm -Xclang -disable-O0-optnone tests/test1.c -o tests/test1.ll"
echo "  opt -load build/src/mem2reg/MatfSimpleMem2Reg.so -enable-new-pm=0 \\"
echo "      -matf-simple-mem2reg -S tests/test1.ll -o tests/test1_after.ll"
echo ""
echo "Setup complete."
 