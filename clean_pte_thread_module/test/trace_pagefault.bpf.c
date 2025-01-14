#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

SEC("fentry/do_user_addr_fault")
int BPF_PROG(do_user_addr_fault, struct pt_regs *regs,
			unsigned long error_code,
			unsigned long address)
{
    // 获取当前进程的 PID
    u32 pid = bpf_get_current_pid_tgid() >> 32;

    // 获取当前进程的 task_struct
    struct task_struct *task = (struct task_struct *)bpf_get_current_task();

    // 定义一个字符数组来存储进程名称
    char comm[16];

    // 从 task_struct 中读取进程名称（comm 字段）
    bpf_probe_read_user(&comm, sizeof(comm), &task->comm);

    // 比较进程名称是否为 "test"
    // if (comm[0] == 't' && comm[1] == 'e' && comm[2] == 's' &&l comm[3] == 't' && comm[4] == '\0') {
    //     // 打印出发生页错误时的地址、进程 PID 和进程名
    //     bpf_printk("Page fault: address = 0x%lx, pid = %d, comm = %s", address, pid, comm);
    // }
    bpf_printk("Page fault: address = 0x%lx, pid = %d, comm = %s", address, pid, comm);
    return 0;
}

char _license[] SEC("license") = "GPL";

