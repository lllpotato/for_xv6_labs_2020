//处理所有中断的代码
#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct spinlock tickslock;
uint ticks;

extern char trampoline[], uservec[], userret[];

// in kernelvec.S, calls kerneltrap().
void kernelvec();

extern int devintr();

void
trapinit(void)
{
  initlock(&tickslock, "time");
}

// set up to take exceptions and traps while in the kernel.
void
trapinithart(void)
{
  w_stvec((uint64)kernelvec);
}

//
// handle an interrupt, exception, or system call from user space.
// called from trampoline.S
// 用于处理从用户空间（user space）发生的中断、异常或系统调用,被trampoline.S调用，trampoline是内核与用户页表相同隐射的一个页
void
usertrap(void)
{
  int which_dev = 0;

  if((r_sstatus() & SSTATUS_SPP) != 0)//r_sstatus() 用于读取 sstatus 寄存器，SSTATUS_SPP 位表示当前的特权级别。
    panic("usertrap: not from user mode");//如果特权级别不是用户模式，则触发 panic，因为内核态触发的中断和用户态出发的中断处理流程不一样

  // send interrupts and exceptions to kerneltrap(),
  // since we're now in the kernel.
  w_stvec((uint64)kernelvec);//w_stvec() 用于写入中断向量寄存器 stvec。
                            //kernelvec 是一个指向内核中断处理程序的函数指针。当在内核态发生中断时，会跳转到这个地址处理。  这一步的目的是设置中断向量表为 kernelvec
  struct proc *p = myproc();
  
  // save user program counter.
  p->trapframe->epc = r_sepc();//r_sepc(): 读取 sepc 寄存器，它保存了引发中断或异常的指令的地址。
                               //保存用户态进程的程序计数器（即下一条要执行的指令的地址）到进程的 trapframe 中，以便在返回用户态时可以继续执行。
  
  if(r_scause() == 8){  //r_scause(): 读取 scause 寄存器，判断导致进入内核的原因。r_scause()==8表示这是一个系统调用
    // system call

    if(p->killed)
      exit(-1);

    // sepc points to the ecall instruction,
    // but we want to return to the next instruction.
    p->trapframe->epc += 4;  //将 sepc 寄存器的值加 4，跳过系统调用的指令，以便返回用户态时继续执行下一条指令，而不是重复的执行ecall

    // an interrupt will change sstatus &c registers,
    // so don't enable until done with those registers.
    intr_on();// XV6会在处理系统调用的时候使能中断，这样中断可以更快的服务，有些系统调用需要许多时间处理。中断总是会被RISC-V的trap硬件关闭，所以在这个时间点，我们需要显式的打开中断。

    syscall();//处理系统调用
  } else if((which_dev = devintr()) != 0){//如果不是系统调用，则检查是否是设备中断。devintr() 函数会返回一个非零值，表示某个设备中断被处理了。
    // ok
  } else { //如果既不是系统调用，也不是设备中断，则这是一个意外的异常。打印错误信息，并将进程标记为 killed
    printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
    printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
    p->killed = 1;
  }

  if(p->killed) //如果进程被标记为 killed，则调用 exit() 终止进程。
    exit(-1);

  // 原版本代码
  // give up the CPU if this is a timer interrupt.
  // if(which_dev == 2)  //如果这是一个定时器中断（which_dev == 2），则调用 yield() 让出 CPU。
  //   yield();

  //lab4 新版代码
  if(which_dev == 2) {
      if(p->alarm_interval != 0) { // 如果设定了时钟事件
        if(--p->alarm_ticks <= 0) { // 时钟倒计时 -1 tick，如果已经到达或超过设定的 tick 数
          if(!p->alarm_goingoff) { // 确保没有时钟正在运行
            p->alarm_ticks = p->alarm_interval;
            // jump to execute alarm_handler
            *p->alarm_trapframe = *p->trapframe; // backup trapframe
            p->trapframe->epc = (uint64)p->alarm_handler;
            p->alarm_goingoff = 1;
          }
          // 如果一个时钟到期的时候已经有一个时钟处理函数正在运行，则会推迟到原处理函数运行完成后的下一个 tick 才触发这次时钟
        }
      }
      yield();
    }
    
  usertrapret(); //这是一个恢复用户态的函数，执行必要的清理和状态恢复，然后返回用户态继续执行。
}

//
// return to user space
//主要用于从内核态返回到用户态。它是处理器在用户态进程执行系统调用或异常后，完成相关处理并准备恢复用户态执行的重要部分。
void
usertrapret(void)
{
  struct proc *p = myproc();

  // we're about to switch the destination of traps from
  // kerneltrap() to usertrap(), so turn off interrupts until
  // we're back in user space, where usertrap() is correct.
  intr_off();//我们之前在系统调用的过程中是打开了中断的，这里关闭中断是因为我们将要更新STVEC寄存器来指向用户空间的trap处理代码，而之前在内核中的时候，我们指向的是内核空间的trap处理代码。
            //我们关闭中断因为当我们将STVEC更新到指向用户空间的trap处理代码时，我们仍然在内核中执行代码。如果这时发生了一个中断，
            //那么程序执行会走向用户空间的trap处理代码，即便我们现在仍然在内核中，出于各种各样具体细节的原因，这会导致内核出错。所以我们这里关闭中断。

  // send syscalls, interrupts, and exceptions to trampoline.S
  w_stvec(TRAMPOLINE + (uservec - trampoline));//设置了STVEC寄存器指向trampoline代码，这个地址是用户态进程发生中断时跳转的目标。uservec 是处理用户态中断的函数，它在 trampoline.S 汇编文件中定义。在那里最终会执行sret指令返回到用户空间。
                                                //位于trampoline代码最后的sret指令会重新打开中断。这样，即使我们刚刚关闭了中断，当我们在执行用户代码时中断是打开的。

  // 以下为设置trapframe中的数据，这样下一次从用户空间转换到内核空间时可以用到这些数据。
  // set up trapframe values that uservec will need when
  // the process next re-enters the kernel. //为下一次用户态进程进入内核态（通过 usertrap）设置必要的上下文信息。
  p->trapframe->kernel_satp = r_satp();         // kernel page table
  p->trapframe->kernel_sp = p->kstack + PGSIZE; // process's kernel stack
  p->trapframe->kernel_trap = (uint64)usertrap;
  p->trapframe->kernel_hartid = r_tp();         // hartid for cpuid()

  // set up the registers that trampoline.S's sret will use
  // to get to user space.
  
  // set S Previous Privilege mode to User.
  //接下来我们要设置SSTATUS寄存器，这是一个控制寄存器。这个寄存器的SPP bit位控制了sret指令的行为，该bit为0表示下次执行sret的时候，
  //我们想要返回user mode而不是supervisor mode。这个寄存器的SPIE bit位控制了，在执行完sret之后，是否打开中断。
  //因为我们在返回到用户空间之后，我们的确希望打开中断，所以这里将SPIE bit位设置为1。修改完这些bit位之后，我们会把新的值写回到SSTATUS寄存器
  unsigned long x = r_sstatus();
  x &= ~SSTATUS_SPP; // clear SPP to 0 for user mode
  x |= SSTATUS_SPIE; // enable interrupts in user mode
  w_sstatus(x);

  //我们在trampoline代码的最后执行了sret指令。这条指令会将程序计数器设置成SEPC寄存器的值，
  //所以现在我们将SEPC寄存器的值设置成之前保存的用户程序计数器的值。
  //在不久之前，我们在usertrap函数中将用户程序计数器保存在trapframe中的epc字段。
  // set S Exception Program Counter to the saved user pc.
  w_sepc(p->trapframe->epc);//恢复pc

  // tell trampoline.S the user page table to switch to.
  uint64 satp = MAKE_SATP(p->pagetable);  //接下来，我们根据user page table地址生成相应的SATP值，这样我们在返回到用户空间的时候才能完成page table的切换。
  //实际上，我们会在汇编代码trampoline中完成page table的切换，并且也只能在trampoline中完成切换，因为只有trampoline中代码是同时在用户和内核空间中映射。
  //但是我们现在还没有在trampoline代码中，我们现在还在一个普通的C函数中，所以这里我们将page table指针准备好，并将这个指针作为第二个参数传递给汇编代码，这个参数会出现在a1寄存器。
  
  
  // jump to trampoline.S at the top of memory, which 
  // switches to the user page table, restores user registers,
  // and switches to user mode with sret.
  uint64 fn = TRAMPOLINE + (userret - trampoline);//这里计算出我们将要跳转到汇编代码的地址。我们期望跳转的地址是tampoline中的userret函数，这个函数包含了所有能将我们带回到用户空间的指令。所以这里我们计算出了userret函数的地址。
  ((void (*)(uint64,uint64))fn)(TRAPFRAME, satp);//将fn指针作为一个函数指针，执行相应的函数（也就是userret函数）并传入两个参数，两个参数存储在a0，a1寄存器中。
}

// interrupts and exceptions from kernel code go here via kernelvec,
// on whatever the current kernel stack is.
//kerneltrap为两种类型的陷阱做好了准备：设备中断和异常。它调用devintr来检查和处理前者。如果陷阱不是设备中断，则必定是一个异常，内核中的异常将是一个致命的错误；内核调用panic并停止执行。
void 
kerneltrap()
{
  int which_dev = 0;
  uint64 sepc = r_sepc();
  uint64 sstatus = r_sstatus();
  uint64 scause = r_scause();
  
  if((sstatus & SSTATUS_SPP) == 0)
    panic("kerneltrap: not from supervisor mode");
  if(intr_get() != 0)
    panic("kerneltrap: interrupts enabled");

  if((which_dev = devintr()) == 0){
    printf("scause %p\n", scause);
    printf("sepc=%p stval=%p\n", r_sepc(), r_stval());
    panic("kerneltrap");
  }

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2 && myproc() != 0 && myproc()->state == RUNNING)
    yield();

  // the yield() may have caused some traps to occur,
  // so restore trap registers for use by kernelvec.S's sepc instruction.
  w_sepc(sepc);
  w_sstatus(sstatus);
}

void
clockintr()
{
  acquire(&tickslock);
  ticks++;
  wakeup(&ticks);
  release(&tickslock);
}

// check if it's an external interrupt or software interrupt,
// and handle it.
// returns 2 if timer interrupt,
// 1 if other device,
// 0 if not recognized.
int
devintr()
{
  uint64 scause = r_scause();

  if((scause & 0x8000000000000000L) &&
     (scause & 0xff) == 9){
    // this is a supervisor external interrupt, via PLIC.

    // irq indicates which device interrupted.
    int irq = plic_claim();

    if(irq == UART0_IRQ){
      uartintr();
    } else if(irq == VIRTIO0_IRQ){
      virtio_disk_intr();
    } else if(irq){
      printf("unexpected interrupt irq=%d\n", irq);
    }

    // the PLIC allows each device to raise at most one
    // interrupt at a time; tell the PLIC the device is
    // now allowed to interrupt again.
    if(irq)
      plic_complete(irq);

    return 1;
  } else if(scause == 0x8000000000000001L){
    // software interrupt from a machine-mode timer interrupt,
    // forwarded by timervec in kernelvec.S.

    if(cpuid() == 0){
      clockintr();
    }
    
    // acknowledge the software interrupt by clearing
    // the SSIP bit in sip.
    w_sip(r_sip() & ~2);

    return 2;
  } else {
    return 0;
  }
}


// trap.c
int sigalarm(int ticks, void(*handler)()) {
  // 设置 myproc 中的相关属性
  struct proc *p = myproc();
  p->alarm_interval = ticks;
  p->alarm_handler = handler;
  p->alarm_ticks = ticks;
  return 0;
}

int sigreturn() {
  // 将 trapframe 恢复到时钟中断之前的状态，恢复原本正在执行的程序流
  struct proc *p = myproc();
  *p->trapframe = *p->alarm_trapframe;
  p->alarm_goingoff = 0;
  return 0;
}
