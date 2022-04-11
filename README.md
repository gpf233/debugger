# debugger
Based on ptrace.

## 零、前言

该调试器主要参考`Eli Bendersky`的博客（见参考资料）完成。

根据`linux`系统调用`ptrace`的`man-page`，`tracee thread`的信号都会被`tracer`拦截。

> While being traced, the tracee will stop each time a signal is delivered, even if the signal is being ignored. (An exception is **SIGKILL**, which has its usual effect.) The tracer will be notified at its next call to ***[waitpid](https://linux.die.net/man/2/waitpid)**(2)* (or one of the related "wait" system calls); that call will return a *status* value containing information that indicates the cause of the stop in the tracee. While the tracee is stopped, the tracer can use various ptrace requests to inspect and modify the tracee. The tracer then causes the tracee to continue, optionally ignoring the delivered signal (or even delivering a different signal instead).

信号拦截功能是`trace`后自带的，故下文不再讨论这个功能。

## 一、程序的主要设计思路、实现方式

### 要实现的功能：

1. 启动一个进程并`trace`它。
2. 设置一个新的断点，查看断点信息。
3. 在断点处继续运行程序。
4. 被调试进程进入中断后能够查看被调试进程任意CPU寄存器的内容。
5. 被调试进程进入中断后能够查看被调试进程任意内存区域的内容。

### `CLI`设计：

仿照`GDB`的`CLI`。

功能1，`./main program_name`启动一个需要调试的进程。

功能2，`b addr` e.g. `b 0x40128e`设置一个新的断点，以`i b`的形式查看当前所有断点的信息。

功能3，`c`继续运行程序。

功能4，`i r`查看`RIP, RBP, RSP`等寄存器的内容。

功能5，`x addr`查看`[addr, addr+8)`8字节内存区域的内容。

### 功能1的实现方式

借助`ptrace`系统调用，进程可以通过调用`fork`，让生成的子进程执行`PTRACE_TRACEME`，然后执行`execve`。

> A process can initiate a trace by calling ***[fork](https://linux.die.net/man/2/fork)**(2)* and having the resulting child do a **PTRACE_TRACEME**, followed (typically) by an ***[execve](https://linux.die.net/man/2/execve)**(2)*. Alternatively, one process may commence tracing another process using **PTRACE_ATTACH** or **PTRACE_SEIZE**.

### 功能2的实现方式

#### 程序起始的临时断点

子进程通过exec执行一个程序时，会在执行之前产生一个trap信号，使得父进程能够在新程序执行前获得控制权，这相当于在程序的开始自动打了一个断点。

>If the **PTRACE_O_TRACEEXEC** option is not in effect, all successful calls to ***[execve](https://linux.die.net/man/2/execve)**(2)* by the traced process will cause it to be sent a **SIGTRAP** signal, giving the parent a chance to gain control before the new program begins execution.

#### 人为设置的断点

使用`PTRACE_PEEKTEXT`和`PTRACE_POKETEXT`将断点代码备份并修改为`int3`中断。

### 功能3的实现方式

分为5步：

1. 使用`PTRACE_SETREGS`将`RIP`寄存器的内容修改为断点的地址。（程序起始的临时断点直接跳到第5步）
2. 使用`PTRACE_PEEKTEXT`和`PTRACE_POKETEXT`复原断点代码。
3. 使用`PTRACE_SINGLESTEP`单步执行断点处的指令。
4. 使用`PTRACE_PEEKTEXT`和`PTRACE_POKETEXT`重新将断点代码备份并修改为`int3`中断。
5. 使用`PTRACE_CONT`继续执行代码。

### 功能4的实现方式

使用`PTRACE_GETREGS`可以查看被调试进程任意寄存器的内容。

### 功能5的实现方式

使用`PTRACE_PEEKDATA`可以查看被调试进程任意内存区域的内容。

## 二、程序的模块划分，以及对每个模块的说明

### 主模块

`main.c`

负责`CLI`接口实现，交互逻辑和信息输出。

### 库模块

`debuglib.h`和`debuglib.c`

将复杂的功能封装为函数供主模块调用。

1. 设置被调试子程序的追踪状态。
2. 断点的生成、启用和禁用。
3. 寄存器内容的获取。
4. 内存数据的获取。
5. 恢复运行状态。

### 测试模块

`test.c`

调试测试程序，有一个全局变量`cnt`，一个修改此变量的函数`advance`，主函数中循环调用此函数4次，测试调试功能的正确性。

## 三、所遇到的问题及解决的方法

### 1. 如何获取断点地址和变量地址？

1. 可以使用`libbfd`或者`libdwarf`解析源程序和汇编代码之间的映射，但是学习成本比较高。

2. `readelf -s`可以得到函数地址和全局变量地址，可以在函数处设置断点、查看全局变量内存区的内容来验证调试器功能的正确性。

   ![image-20220223021324458](/home/gpf233/.config/Typora/typora-user-images/image-20220223021324458.png)

上图为`readelf -s test`得到的结果，其中有`main`函数、`advance`函数和全局变量`cnt`的地址。

### 2. `RIP`的值不固定，无法推算断点地址。

每次调试，测试程序在相同代码处的`RIP`都不一样。

<img src="/home/gpf233/.config/Typora/typora-user-images/image-20220223021142171.png" alt="image-20220223021142171"  />

上图为使用`mygdb`对`test`程序连续进行四次调试的结果，可以发现每次起始的`RIP`都不一样。

`ASLR`，全称为 Address Space Layout Randomization，地址空间布局随机化。该技术在 kernel 2.6.12 中被引入到 Linux 系统，它将进程的某些内存空间地址进行随机化来增大入侵者预测目的地址的难度，从而降低进程被成功入侵的风险。

Linux 平台上 ASLR 分为 0，1，2 三级，用户可以通过内核参数 randomize_va_space 进行等级控制，不同级别的含义如下：

- 0 = 关
- 1 = 半随机；共享库、栈、mmap() 以及 VDSO 将被随机化
- 2 = 全随机；除了 1 中所述，还会随机化 heap

注：系统默认开启 2 全随机模式，PIE 会影响 heap 的随机化。

通过读写 /proc/sys/kernel/randomize_va_space 内核文件可以查看或者修改 ASLR 等级：

```sh
// 查看ASLR。
cat /proc/sys/kernel/randomize_va_space
// 关闭ASLR。
sudo sh -c "echo 0 > /proc/sys/kernel/randomize_va_space"
```

开启 `ASLR`，在每次程序运行时的时候，装载的可执行文件和共享库都会被映射到虚拟地址空间的不同地址处；而关掉 `ASLR`，则可以保证每次运行时都会被映射到虚拟地址空间的相同地址处。

![image-20220223042025603](/home/gpf233/.config/Typora/typora-user-images/image-20220223042025603.png)

如上图所示，关闭`ASLR`后，每次运行都会映射到虚拟地址空间的相同地址处。

### 3. 生成了`PIC`（地址无关代码），导致调试过程与预期不符。

解决了`ASLR`的问题之后，调试程序发现不符合预期，`test`并没有在`0x40128e`处（即`advance`函数地址）中断，如下图所示。

![image-20220223040521821](/home/gpf233/.config/Typora/typora-user-images/image-20220223040521821.png)

使用`GDB`进行调试，发现`advance`处的`RIP`和`readelf -s test`得到的地址不一样，如下图所示。

![image-20220223040010549](/home/gpf233/.config/Typora/typora-user-images/image-20220223040010549.png)

程序运行前`b advance`在`advance`函数处打断点，输出信息说断点地址为`0x12a1`，和`readelf -s test`得到的一致，但是运行到断点后断点地址变为了`0x5555555552a1`，`RIP`的值也为`0x5555555552a1`。

后来查了很多资料，发现是`PIE`的问题，编译被调试的程序时要加上`-no-pie`选项禁用`pie`。

![image-20220223024058037](/home/gpf233/.config/Typora/typora-user-images/image-20220223024058037.png)

从上图可以发现禁用`pie`后，`Type`由`DYN`变为了`EXEC`，入口地址也由`0x10c0`变为了`0x4010b0`，而正常情况下64位可执行程序的`text-segment`就是从`0x400000`开始的（见上图最后一条命令）。

再次用`GDB`进行调试，发现`advance`处的`RIP`和`readelf -s test`得到的地址一致了。

![image-20220223041404269](/home/gpf233/.config/Typora/typora-user-images/image-20220223041404269.png)

这样一来，自己的`debugger`终于可以正常停在断点处了。

## 四、程序运行结果及使用说明

### 使用说明：

仿照了`GDB`的接口，实现了以下功能：

`./main program_name`启动一个需要调试的进程。

`b addr` e.g. `b 0x40128e`设置一个新的断点，以`i b`的形式查看当前所有断点的信息。

`c`继续运行程序。

`i r`查看`RIP, RBP, RSP`等寄存器的内容。

`x addr`查看`[addr, addr+8)`8字节内存区域的内容。

### 程序运行结果放在第五节以图片的形式展示。

## 五、程序运行截图

<img src="/home/gpf233/.config/Typora/typora-user-images/image-20220223025318335.png" alt="image-20220223025318335"  />

1. `b 0x40128e`

   在`0x40128e`即`advance`函数处打下断点。

2. `i b`

   查看断点信息，显示了断点地址和断点处的原始代码。

3. `c`

   程序继续运行，在第1次进入`advance`函数前中断。

4. `x 0x404048`

   查看内存`[0x404048, 0x404050)`即全局变量`cnt`的值，由低地址到高地址分别为`0x8877665544332211`和`cnt`的初值`0x1122334455667788`一致，符合预期。

5. `c`

   程序继续运行，执行`++cnt`，`cnt`变为`0x1122334455667789`，在第2次进入`advance`函数前中断。

6. `x 0x404048`

   查看内存`[0x404048, 0x404050)`即全局变量`cnt`的值，由低地址到高地址分别为`0x8977665544332211`和`cnt`的当前值`0x1122334455667789`一致，符合预期。

7. `c`

   程序继续运行，执行`++cnt`，`cnt`变为`0x112233445566778a`，在第3次进入`advance`函数前中断。

8. `i r`

   查看寄存器信息，所有寄存器的内容获取方法都是一样的，为了缩小篇幅，`tracer`仅输出`RIP`，`RBP`，`RSP`三个寄存器的内容.

9. `c`

   程序继续运行，执行`++cnt`，`cnt`变为`0x112233445566778b`，在第4次进入`advance`函数前中断。

10. `c`

    程序继续运行，执行`++cnt`，`cnt`变为`0x112233445566778c`，`for`循环结束，`main`函数`return`后进程结束，`tracer`进程判断`WIFEXITED(wait_status)`满足，输出`child exited`后退出命令循环，结束进程，整个过程均符合预期。

## 六、参考资料

[https://linux.die.net/man/2/ptrace](https://linux.die.net/man/2/ptrace)

[GDB原理之ptrace实现原理](https://cloud.tencent.com/developer/article/1742878)

[How debuggers work: Part 1 - Basics](https://eli.thegreenplace.net/2011/01/23/how-debuggers-work-part-1)

[How debuggers work: Part 2 - Breakpoints](https://eli.thegreenplace.net/2011/01/27/how-debuggers-work-part-2-breakpoints)

[How debuggers work: Part 3 - Debugging information](https://eli.thegreenplace.net/2011/02/07/how-debuggers-work-part-3-debugging-information)