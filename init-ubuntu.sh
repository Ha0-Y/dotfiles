#!/bin/sh

sudo apt-get -y update
sudo apt-get -y upgrade


# tools
sudo apt-get install -y tmux openssh-server vim

# compile kernel
sudo apt-get install -y gcc make pkg-config flex bison \
    libncurses-dev libelf-dev libssl-dev \
    python3 python3-pip python3-dev \
    git libffi-dev build-essential \
    fakeroot ncurses-dev xz-utils libssl-dev bc \
    dwarves

# prepare for op-tee
sudo apt-get install -y git android-tools-adb android-tools-fastboot autoconf \
    automake bc bison build-essential cscope curl flex ftp-upload gdisk \
    libattr1-dev libc6:i386 libcap-dev libfdt-dev libftdi-dev \
    libglib2.0-dev libhidapi-dev libncurses5-dev libpixman-1-dev \
    libssl-dev libstdc++6:i386 libtool libz1:i386 make mtools netcat \
    python-crypto python-serial python-wand unzip uuid-dev xdg-utils \
    xterm xz-utils zlib1g-dev

# clang 
cd ~
wget https://apt.llvm.org/llvm.sh
chmod +x llvm.sh
sudo ./llvm.sh 14