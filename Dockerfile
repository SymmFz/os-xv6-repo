FROM archlinux:latest

RUN pacman-key --init

# RUN echo 'Server = https://mirrors.ustc.edu.cn/archlinux/$repo/os/$arch' | tee /etc/pacman.d/mirrorlist

RUN echo 'Server = https://mirror.nju.edu.cn/archlinux/$repo/os/$arch' | tee /etc/pacman.d/mirrorlist

RUN pacman -Syyu --noconfirm

RUN pacman -S --noconfirm gcc make perl wget git python

RUN pacman -S --noconfirm riscv64-elf-gdb riscv64-elf-gcc riscv64-elf-binutils riscv64-elf-newlib qemu-emulators-full

RUN wget -P ~ https://github.com/cyrus-and/gdb-dashboard/raw/master/.gdbinit && \
    pacman -S --noconfirm python-pygments
