from pwn import *

# base64 编码
ip, port= "127.0.0.1", 1234

file = ''

with open(file, "r") as f:
    content = b64e(f.read())

# 保存到文件
# 传送
while True:
    try:
        r = remote(ip, port)
        for i in range(0, len(content), 100):
            r.recvuntil(b'/ $')
            payload = "echo '{}' >> /tmp/b64_exp".format(content[i:i+100])
            r.sendline(payload.encode())

        r.recvuntil(b'/ $')
        payload = "cat /tmp/b64_exp | base64 -d > /tmp/exp; chmod +x /tmp/exp"
        r.sendline(payload.encode)

        r.recvuntil(b'/ $')
        payload = "cat /tmp/b64_exp | base64 -d > /tmp/exp; chmod +x /tmp/exp; /tmp/exp"
        r.sendline(payload.encode)
        r.interactive()

    except:
        r.close()
        print("[-] maybe something wrong")
        continue