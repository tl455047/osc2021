#include "shell.h"
#include <uart.h>
#include <reset.h>
#include <string.h>
#include <cpio.h>
#include <devicetree.h>
#include <buddy.h>
#include <dynamic.h>
#include <exception.h>
#include <printf.h>
#include <timer.h>
#include <test.h>
#include <sched.h>

void shell() {
  uart_puts("*****************************Hello World*****************************\r\n");
  uart_puts("% ");
  char command[SHELL_COMMNAD_SIZE];
  memset(command, strlen(command), '\0');
  int i = 0;
  while(1) {
    char c = uart_getc();
    c = c=='\r'?'\n':c;
    if(c != '\n' && c != '\x7f') {
      command[i++] = c;
      uart_send(c);
    }
    else if(c == '\x7f') {
      uart_send('\x08');
      uart_send(' ');
      uart_send('\x08');
      command[--i] = '\0';
    }
    else {
      command[i] = '\0';
      uart_puts("\n");
      if(i > 0) 
        do_command(command);
      
      uart_puts("% ");
      i = 0;
      memset(command, strlen(command), '\0');
    }
  }
}
void do_command(char* command) {
  if(strncmp(command, "help", 5) == 0) {
    printf("help: print all available commands.\n");
    printf("hello: print Hello World!.\n");
    printf("reboot: reboot rpi3.\n");
    printf("loadimg: load kernel image.\n");
    printf("lscpio: list cpio files.\n");
    printf("lsdtb: list dtb node name.\n");
    printf("lsdtbprop [node name]: list [node name] property.\n");
    printf("cat [file]: cat cpio file.\n");
    printf("bmalloc [size]: buddy malloc.\n");
    printf("bfree [address]: buddy free.\n");
    printf("dmalloc [size]: dynamic malloc.\n");
    printf("dfree [address]: dynamic free.\n");
    printf("svc: trigger exception.\n");
    printf("run [address]: run user program in el0.\n");
    printf("el12el0: from el1 to el0.\n");
    printf("asyncw [input]: asynchronous write.\n");
    printf("asyncr: asynchronous read.\n");
    printf("settimeout [message] [timeout]: set time out and print message.\n");
    printf("dynamictest: dynamic malloc testing.\n");
    printf("buddytest: buddy malloc testing.\n");
    printf("variedtest: varied malloc test\n");
    printf("enabtimer: enable core timer interrupt.\n");
    printf("disatimer: disable core timer interrupt.\n");
    printf("lab5_test1: kernel thread test\n");
    printf("lab5_test2: argv, fork test\n");
  } 
  else if(strncmp(command, "hello", 6) == 0) {
    printf("Hello World!\n");
  }
  else if(strncmp(command, "reboot", 7) == 0) {
    reset(100);
  }
  else if(strncmp(command, "loadimg", 7) == 0) {
    loadimg();
  }
  else if(strncmp(command, "cat ", 4) == 0) {
    cpio_get_file_content(command + 4, strlen(command + 4));
  }
  else if(strncmp(command, "lscpio", 3) == 0) {
    cpio_get_all_pathname();
  }
  else if(strncmp(command, "lsbss", 6) == 0) {
    extern void *_bss_begin;
    extern void *_bss_end;
    printf("bss_begin: %x\n _bss_end %x\n", (unsigned long)&_bss_begin, (unsigned long)&_bss_end);
  }
  else if(strncmp(command, "lsdtb", 6) == 0) {
    devicetree_parse(get_dtb_address(), DISPLAY_DEVICE_NAME, null);
  }
  else if(strncmp(command, "lsdtbprop", 9) == 0) {
    devicetree_parse(get_dtb_address(), DISPLAY_DEVICE_PROPERTY, command + 10);
  }
  else if(strncmp(command, "bmalloc", 7) == 0) {
    buddy_malloc(strtol(command + 7, 0, 16));
  }
  else if(strncmp(command, "bfree", 5) == 0) {
    buddy_free((void *)strtol(command + 5, 0, 16));
  }
  else if(strncmp(command, "dmalloc", 7) == 0) {
    dynamic_malloc(strtol(command + 7, 0, 16));
  }
  else if(strncmp(command, "dfree", 5) == 0) {
    dynamic_free((void *)strtol(command + 5, 0, 16));
  }
  else if(strncmp(command, "svc", 3) == 0) {
    int syscall_num = strtol(command + 4, 0, 10);
    printf("syscall_number: %d\n", syscall_num);
    asm volatile("mov x0, %0\n" "svc #0\n"::"r"(syscall_num));
  }
  else if(strncmp(command, "run", 3) == 0) {
    void* addr;
    addr = cpio_load_program("user_test", 9, (void *)strtol(command + 4, 0, 16));
    if(addr != null) {
      printf("addr: %x\n", addr);
      //from el1 to el0 and jump to user_program
      asm volatile("mov x0, #0x3c0\n" "msr spsr_el1, x0\n");
      asm volatile("mov x0, %0\n" "msr elr_el1, x0\n" "eret\n"::"r"(addr));
      //asm volatile("blr %0\n"::"r"(addr));
    }
  }
  else if(strncmp(command, "el12el0", 8) == 0) {
    //from el1 to el0
    asm volatile("mov x0, #0\n" "msr spsr_el1, x0\n");
    asm volatile("mov x0, %0\n" "msr elr_el1, x0\n" "eret\n"::"r"((void*)shell));
  }
  else if(strncmp(command, "asyncw", 6) == 0) {
    uart_async_write(command + 7, strlen(command) - 6);
  }
  else if(strncmp(command, "asyncr", 7) == 0) {
    int count = uart_async_read(command, SHELL_COMMNAD_SIZE);
    printf("%s", command);
    printf("read %d bytes\n", count);
  }
  else if(strncmp(command, "settimeout", 10) == 0) {
    int i;
    for(i = 11; i < strlen(command); i++) {
      if(command[i] == ' ') {
        command[i] = '\0';
        i++;
        break;
      }
    }
    core_timer_queue_push((void* )core_timer_print_message_callback, strtol(command + i, 0, 10), command + 11, strlen(command + 11));
  }
  else if(strncmp(command, "dynamictest", 12) == 0) {
    test_dynamic_main();
  }
  else if(strncmp(command, "buddytest", 9) == 0) {
    test_buddy_main();
  }
  else if(strncmp(command, "variedtest", 10) == 0) {
    test_varied_main();
  }
  else if(strncmp(command, "enabtimer", 9) == 0) {
    
    asm volatile("blr %0\n"::"r"(&core_timer_enable));
  }
  else if(strncmp(command, "disatimer", 9) == 0) {
    extern void* core_timer_disable;
    asm volatile("blr %0\n"::"r"(&core_timer_disable));
  }
  else if(strncmp(command, "backdoor", 8) == 0) {
    printf("This is a backdoor\n");
  }
  else if(strncmp(command, "lab5_test1", 10) == 0) {
    core_timer_enable();
    task_test1_init();
  }
  else if(strncmp(command, "lab5_test2", 10) == 0) {
    core_timer_enable();
    task_test2_init();
  }
  else {
    printf("unknown command\n");
  }	
}

void loadimg() {
  void *load_address, *relocated_readimg_jump, *relocated_bootloader;
  char buf[9]; 
  size_t img_size;
  //count bootloader size
  extern void *_start_bootloader;
  extern void *_end_bootloader;
  size_t _bootloader_size = (size_t) &_end_bootloader - (size_t) &_start_bootloader;
  //read load address
  uart_puts("Input the address to load image(0x): ");
  uart_readline(buf, 8);
  load_address = (void* )strtol(buf, 0, 16);
  uart_puts("\nLoad image at: 0x");
  uart_hex((size_t)load_address);
  //read size
  uart_puts("\nInput the image size(0x): ");
  do_uart_read(buf, 8);
  buf[8] = '\0';
  img_size = strtol(buf, 0, 16);
  uart_puts("\nimage size: 0x");
  uart_hex(img_size);
  uart_puts("\n");
  //check bootloader, and image is overlap 
  size_t img_end = (size_t)load_address + img_size;
  
  relocated_readimg_jump = (void* )readimg_jump;
  if(img_end > (size_t) &_start_bootloader) {
    uart_puts("image overlapped to bootloader.\n");
    relocated_bootloader = (void *)img_end + BOOTLOADER_OFFSET;
    relocated_bootloader += 16 - (size_t)relocated_bootloader % 16;
    uart_puts("relocated rest of bootloader to address: ");
    uart_hex((size_t)relocated_bootloader);
    uart_puts("\n");
    //relocate bootloader
    memcpy((char *)relocated_bootloader, &_start_bootloader, _bootloader_size);
    
    //relocated readimg_jump address
    relocated_readimg_jump = relocated_bootloader + ((size_t)&readimg_jump - (size_t)&_start_bootloader);
  }
  //jump to readimg_jump
  asm volatile ("mov x0, %0\n" "mov x1, %1\n" "mov sp, %2\n" "mov x2, %3\n" "blr %4\n"::
  "r" (load_address),
  "r" (img_size),
  "r" (load_address),
  "r" (get_dtb_address()),
	"r" (relocated_readimg_jump): "x0", "x1", "x2");
}
//read kernel img, and jump 
void readimg_jump(void* load_address, size_t img_size, size_t dtb_address) {
  do_uart_read((char* )load_address, img_size);
  asm volatile ("mov x0, %0\n" "mov sp, %1\n" "blr %2\n"::
  "r" (dtb_address),
  "r" (load_address),
	"r" (load_address): "x0");
  //((void (*)(void))(load_address))();
}
