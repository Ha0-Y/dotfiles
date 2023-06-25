#!/usr/bin/python3
from pwn import *


def s(data): return p.send(data)
def sa(delim, data): return p.sendafter(delim, data)
def sl(data): return p.sendline(data)
def sla(delim, data): return p.sendlineafter(delim, data)
def r(num=4096): return p.recv(num)
def ru(delim, drop=False): return p.recvuntil(delim, drop)
def rl(): return p.recvline(timeout=1)
def itr(): return p.interactive()
def lg(name): return log.success(
    '\033[32m%s ==> 0x%x\033[0m' % (name, eval(name)))


def uu64(): return u64(p.recvuntil(b"\x7f")[-6:].ljust(8, b"\x00"))
def uu32(): return u32(p.recvuntil(b"\xf7")[-4:].ljust(4, b"\x00"))


def itob(num): return str(num).encode()


def dbg(p):
    gdb.attach(p)
    # raw_input("debug")


file = ''


elf = ELF(file, checksec=False)
libc = elf.libc
context.binary = elf
context.log_level = 'debug'
context.terminal = ['tmux', 'splitw', '-h']


p = elf.process()
ip, port = "127.0.0.1", 1234
# p = remote(ip, port)


dbg(p)
itr()
