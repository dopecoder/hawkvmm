#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <string.h>
#include <stdint.h>
#include <linux/kvm.h>
#include <time.h>

#define CR0_PROTECTED 1u

#define GET_VAL(vcpu)                \
	char *p = (char *)vcpu->kvm_run; \
	int *out_val = (int *)(p + vcpu->kvm_run->io.data_offset);

#define GET_SIZE(vcpu) \
	size_t io_size = vcpu->kvm_run->io.size;

#define HANDLE_IN(inp_port, val)                                                             \
	if (vcpu->kvm_run->io.direction == KVM_EXIT_IO_IN && vcpu->kvm_run->io.port == inp_port) \
	{                                                                                        \
		uint8_t *p = (uint8_t *)vcpu->kvm_run;                                               \
		*(p + vcpu->kvm_run->io.data_offset) = val;                                          \
		return 1;                                                                            \
	}                                                                                        \
	return 0;

#define HANDLE_OUT(out_port, val)                                                             \
	if (vcpu->kvm_run->io.direction == KVM_EXIT_IO_OUT && vcpu->kvm_run->io.port == out_port) \
	{                                                                                         \
		val;                                                                                  \
		return 1;                                                                             \
	}                                                                                         \
	return 0;

#define HANDLE_CASE(x) \
	if (x)             \
	{                  \
		return 1;      \
	}                  \
	return 0;

//-------------------------------- SHARED MEMORY --------------------------------------

void *create_shared_memory(size_t size)
{
	int protection = PROT_READ | PROT_WRITE;
	int visibility = MAP_SHARED | MAP_ANONYMOUS;
	return mmap(NULL, size, protection, visibility, -1, 0);
}

//-------------------------------------------------------------------------------


//-------------------------------- TIMER ----------------------------------------

#define TIMER_VALUE_PORT 0x46
#define TIMER_ENABLE_STATUS_PORT 0x47

typedef struct timer
{
	unsigned long start_time_ns;
	unsigned long last_int_ms;
	int interval_ms;
} vmm_timer;

vmm_timer *timer;
int is_timer_set = 0;

void init_timer()
{
	timer = malloc(sizeof(vmm_timer));
	timer->start_time_ns = 0;
	timer->last_int_ms = 0;
	timer->interval_ms = -1;
}

void destroy_timer()
{
	if (!timer)
	{
		return;
	}
	free(timer);
}

void timer_set()
{
	struct timespec time;
	is_timer_set = 1;
	clock_gettime(CLOCK_REALTIME, &time);

	uint8_t stamp = time.tv_nsec + (time.tv_sec * 1000000000);
	timer->last_int_ms = stamp / 1000000;
	timer->start_time_ns = stamp;
	printf("Timer set with interval %d ms\n", timer->interval_ms);
}

void timer_unset()
{
	is_timer_set = 0;
	destroy_timer();
	init_timer();
}

void set_timer(uint8_t *val)
{
	if (*val & 0x01)
	{
		//the timer is set, if it is currently disabled, then enable it
		if (!is_timer_set && timer->interval_ms != -1)
		{
			printf("Trying to set timer\n");
			timer_set();
		}
		if (is_timer_set && (*val & 0x02) >> 1 == 0)
		{
			// printf("TIMER ACK\n");
			// it doesnt really matter as we dont care if this is acknowledged, as the event can be just lost
		}
	}
	else
	{
		if (is_timer_set)
		{
			timer_unset();
		}
	}
}

void set_timer_val(int *ms)
{
	timer->interval_ms = *ms;
	// printf("TIMER VALUE SET\n");
}

uint8_t get_timer_status()
{
	if (is_timer_set)
	{
		// printf("Timer is set and checking\n");
		struct timespec time;
		clock_gettime(CLOCK_REALTIME, &time);
		unsigned long stamp = time.tv_nsec + (time.tv_sec * 1000000000);
		// printf("stamp : %ld\n", stamp);
		unsigned long current_stamp_in_ms = stamp / 1000000;
		// printf("current_stamp_in_ms : %ld\n", current_stamp_in_ms);
		// printf("diff : %ld\n", current_stamp_in_ms - timer->last_int_ms);
		if (current_stamp_in_ms - timer->last_int_ms > (unsigned int)timer->interval_ms)
		{
			timer->last_int_ms = current_stamp_in_ms;
			return 3;
		}
		return 1;
	}
	return 0;
}

//-------------------------------------------------------------------------------

//-------------------------------- KEYBOARD -------------------------------------

#define KEYBOARD_IN_PORT 0x44
#define KEYBOARD_STATUS_PORT 0x45

typedef struct kbd_inp
{
	char buffer[256];
	uint8_t char_ptr;
	uint8_t size;
} kbd_inp_t;

typedef struct guest_kbd_inp
{
	char buffer[256];
	uint8_t is_input_avail;
	uint8_t char_ptr;
	uint8_t size;
} guest_kbd_inp_t;

kbd_inp_t *kbd_inp;
guest_kbd_inp_t *guest_kbd_inp;

void *create_kbd_inp_shmem()
{
	void *shmem = create_shared_memory(sizeof(kbd_inp_t));
	kbd_inp_t kdb_inp_struct;
	kdb_inp_struct.char_ptr = 0;
	kdb_inp_struct.size = 0;
	memcpy(shmem, &kdb_inp_struct, sizeof(kbd_inp_t));
	return shmem;
}

void *create_guest_kbd_inp_shmem()
{
	void *shmem = create_shared_memory(sizeof(guest_kbd_inp_t));
	guest_kbd_inp_t guest_kdb_inp_struct;
	guest_kdb_inp_struct.is_input_avail = 0;
	guest_kdb_inp_struct.char_ptr = 0;
	guest_kdb_inp_struct.size = 0;
	memcpy(shmem, &guest_kdb_inp_struct, sizeof(guest_kdb_inp_struct));
	return shmem;
}

void free_shmems()
{
	if (kbd_inp)
	{
		munmap(kbd_inp, sizeof(kbd_inp_t));
	}

	if (guest_kbd_inp)
	{
		munmap(guest_kbd_inp, sizeof(guest_kbd_inp_t));
	}
}

char get_next_keyboard_input()
{
	return guest_kbd_inp->buffer[guest_kbd_inp->char_ptr];
}

int is_kbd_input_available()
{
	if (guest_kbd_inp->char_ptr > guest_kbd_inp->size - 1)
	{
		guest_kbd_inp->is_input_avail = 0;
	}
	return guest_kbd_inp->is_input_avail;
}

void kbd_read_ack(int *val)
{
	if (*((uint8_t *)val) == 0)
	{
		guest_kbd_inp->char_ptr += 1;
	}
}

int update_guest_kbd_inp()
{
	if (guest_kbd_inp->is_input_avail)
	{
		return 0;
	}
	strcpy(guest_kbd_inp->buffer, kbd_inp->buffer);
	guest_kbd_inp->char_ptr = 0;
	guest_kbd_inp->is_input_avail = 1;
	guest_kbd_inp->size = kbd_inp->size;
	return 1;
}

void init_kbd()
{
	kbd_inp = (kbd_inp_t *)create_kbd_inp_shmem();
	guest_kbd_inp = (guest_kbd_inp_t *)create_guest_kbd_inp_shmem();
}

void setup_keyboard()
{
	while (1)
	{
		char inp = getchar();
		kbd_inp->buffer[kbd_inp->char_ptr] = inp;
		kbd_inp->char_ptr += 1;
		kbd_inp->size += 1;

		if (inp == '\n')
		{
			kbd_inp->buffer[kbd_inp->char_ptr] = '\0';
			while (!update_guest_kbd_inp())
			{
			}
		}
	}
}

//-------------------------------------------------------------------------------

//-------------------------------- CONSOLE --------------------------------------

#define CONSOLE_OUT_PORT 0x42

typedef struct console
{
	char buffer[256];
	uint8_t size;
} console_t;

console_t console;

void clear_console_buffer()
{
	uint8_t idx = 0;
	for (; idx < console.size; idx++)
	{
		console.buffer[idx] = '\0';
	}
	console.size = 0;
}

void console_print(char *ch, size_t size)
{
	if (*ch != '\n')
	{
		console.buffer[console.size++] = *ch;
	}
	else
	{
		console.buffer[console.size++] = '\n';
		fwrite(console.buffer, size, console.size, stdout);
		fflush(stdout);
		clear_console_buffer();
	}
}

//-------------------------------------------------------------------------------


//---------------------------------- VMM ----------------------------------------

struct vm
{
	int sys_fd;
	int fd;
	char *mem;
};

struct vcpu
{
	int fd;
	struct kvm_run *kvm_run;
};

void debug_io(struct vcpu *vcpu)
{

	if (vcpu->kvm_run->io.direction == KVM_EXIT_IO_OUT)
	{
		printf("KVM_EXIT_IO_OUT\n");
	}
	else
	{
		printf("KVM_EXIT_IO_IN\n");
	}
	printf("KVM IO ON port : %x, data : %s, data : %d\n", vcpu->kvm_run->io.port, (char *)vcpu->kvm_run + vcpu->kvm_run->io.data_offset, *((uint8_t *)vcpu->kvm_run + vcpu->kvm_run->io.data_offset));
}

void load_binary(struct vm *vm, char *file_name)
{
	int fd = open(file_name, O_RDONLY);

	if (fd < 0)
	{
		fprintf(stderr, "can not open binary file\n");
		exit(1);
	}

	int ret = 0;
	char *p = (char *)vm->mem;

	while (1)
	{
		ret = read(fd, p, 4096);
		if (ret <= 0)
		{
			break;
		}
		p += ret;
	}
}

int handle_kbd_in(struct vcpu *vcpu)
{
	HANDLE_IN(KEYBOARD_IN_PORT, get_next_keyboard_input());
}

int handle_kbd_status(struct vcpu *vcpu)
{
	if (vcpu->kvm_run->io.direction == KVM_EXIT_IO_IN)
	{
		HANDLE_IN(KEYBOARD_STATUS_PORT, is_kbd_input_available());
	}
	else if (vcpu->kvm_run->io.direction == KVM_EXIT_IO_OUT)
	{
		GET_VAL(vcpu)
		HANDLE_OUT(KEYBOARD_STATUS_PORT, kbd_read_ack(out_val));
	}
	return 0;
}

int handle_console_out(struct vcpu *vcpu)
{
	GET_VAL(vcpu)
	GET_SIZE(vcpu)
	HANDLE_OUT(CONSOLE_OUT_PORT, console_print((char *)out_val, io_size));
}

int handle_timer_val(struct vcpu *vcpu)
{
	GET_VAL(vcpu)
	HANDLE_OUT(TIMER_VALUE_PORT, set_timer_val(out_val));
}
int handle_set_timer(struct vcpu *vcpu)
{
	if (vcpu->kvm_run->io.direction == KVM_EXIT_IO_IN)
	{
		HANDLE_IN(TIMER_ENABLE_STATUS_PORT, get_timer_status());
	}
	else if (vcpu->kvm_run->io.direction == KVM_EXIT_IO_OUT)
	{
		GET_VAL(vcpu)
		HANDLE_OUT(TIMER_ENABLE_STATUS_PORT, set_timer((uint8_t *)out_val));
	}
	return 0;
}

int handle_io(struct vcpu *vcpu)
{
	uint8_t port = vcpu->kvm_run->io.port;

	switch (port)
	{
	case KEYBOARD_IN_PORT:
		HANDLE_CASE(handle_kbd_in(vcpu));

	case KEYBOARD_STATUS_PORT:
		HANDLE_CASE(handle_kbd_status(vcpu));

	case CONSOLE_OUT_PORT:
		HANDLE_CASE(handle_console_out(vcpu));

	case TIMER_VALUE_PORT:
		HANDLE_CASE(handle_timer_val(vcpu));

	case TIMER_ENABLE_STATUS_PORT:
		HANDLE_CASE(handle_set_timer(vcpu));

	default:
		printf("Unhandled IO\n");
		return 0;
	}
}

int run(struct vcpu *vcpu)
{
	for (;;)
	{
		if (ioctl(vcpu->fd, KVM_RUN, 0) < 0)
		{
			perror("KVM_RUN");
			exit(1);
		}

		switch (vcpu->kvm_run->exit_reason)
		{

		case KVM_EXIT_MMIO:
			// printf("GOT MMIO!, addr : %llx data : %s\n", vcpu->kvm_run->mmio.phys_addr, vcpu->kvm_run->mmio.data);
			continue;

		case KVM_EXIT_HLT:
			goto exit;

		case KVM_EXIT_IO:
			// debug_io(vcpu);
			if (handle_io(vcpu))
			{
				continue;
			}
			else
			{
				printf("UNHANDLED IO\n");
			}

		/* fall through */
		default:
			fprintf(stderr, "Got exit_reason %d,"
							" expected KVM_EXIT_HLT (%d)\n",
					vcpu->kvm_run->exit_reason, KVM_EXIT_HLT);
			exit(1);
		}
	}

exit:
	destroy_timer();
	free_shmems();
	return 1;
}

int run_vm(struct vcpu *vcpu)
{
	init_kbd();
	init_timer();
	int pid = fork();
	if (pid == 0)
	{
		setup_keyboard();
	}
	else if (pid < 0)
	{
		printf("Fork failed exiting!\n");
		exit(EXIT_FAILURE);
	}
	else
	{
		return run(vcpu);
	}
	return 0;
}

static void init_sregs(struct kvm_sregs *sregs)
{
	struct kvm_segment seg = {
		.base = 0,
		.limit = 0xffffffff,
		.selector = 1 << 3,
		.present = 1,
		.type = 11,
		.dpl = 0,
		.db = 1,
		.s = 1,
		.l = 0,
		.g = 1,
	};

	sregs->cr0 |= CR0_PROTECTED;

	sregs->cs = seg;

	seg.type = 3;
	seg.selector = 2 << 3;
	sregs->ds = sregs->es = sregs->fs = sregs->gs = sregs->ss = seg;
}

int run_vmm(struct vm *vm, struct vcpu *vcpu, char *file_name)
{
	struct kvm_sregs sregs;
	struct kvm_regs regs;

	if (ioctl(vcpu->fd, KVM_GET_SREGS, &sregs) < 0)
	{
		perror("KVM_GET_SREGS");
		exit(1);
	}

	init_sregs(&sregs);

	if (ioctl(vcpu->fd, KVM_SET_SREGS, &sregs) < 0)
	{
		perror("KVM_SET_SREGS");
		exit(1);
	}

	memset(&regs, 0, sizeof(regs));
	regs.rflags = 2;
	regs.rip = 0;

	if (ioctl(vcpu->fd, KVM_SET_REGS, &regs) < 0)
	{
		perror("KVM_SET_REGS");
		exit(1);
	}

	load_binary(vm, file_name);

	return run_vm(vcpu);
}

void vm_init(struct vm *vm, size_t mem_size)
{
	int api_ver;
	struct kvm_userspace_memory_region memreg;

	vm->sys_fd = open("/dev/kvm", O_RDWR);
	if (vm->sys_fd < 0)
	{
		perror("open /dev/kvm");
		exit(1);
	}

	api_ver = ioctl(vm->sys_fd, KVM_GET_API_VERSION, 0);
	if (api_ver < 0)
	{
		perror("KVM_GET_API_VERSION");
		exit(1);
	}

	if (api_ver != KVM_API_VERSION)
	{
		fprintf(stderr, "Got KVM api version %d, expected %d\n",
				api_ver, KVM_API_VERSION);
		exit(1);
	}

	vm->fd = ioctl(vm->sys_fd, KVM_CREATE_VM, 0);
	if (vm->fd < 0)
	{
		perror("KVM_CREATE_VM");
		exit(1);
	}

	if (ioctl(vm->fd, KVM_SET_TSS_ADDR, 0xfffbd000) < 0)
	{
		perror("KVM_SET_TSS_ADDR");
		exit(1);
	}

	vm->mem = mmap(NULL, mem_size, PROT_READ | PROT_WRITE,
				   MAP_SHARED | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
	if (vm->mem == MAP_FAILED)
	{
		perror("mmap mem");
		exit(1);
	}

	madvise(vm->mem, mem_size, MADV_MERGEABLE);

	memreg.slot = 0;
	memreg.flags = 0;
	memreg.guest_phys_addr = 0;
	memreg.memory_size = mem_size;
	memreg.userspace_addr = (unsigned long)vm->mem;
	if (ioctl(vm->fd, KVM_SET_USER_MEMORY_REGION, &memreg) < 0)
	{
		perror("KVM_SET_USER_MEMORY_REGION");
		exit(1);
	}
}

void vcpu_init(struct vm *vm, struct vcpu *vcpu)
{
	int vcpu_mmap_size;

	vcpu->fd = ioctl(vm->fd, KVM_CREATE_VCPU, 0);
	if (vcpu->fd < 0)
	{
		perror("KVM_CREATE_VCPU");
		exit(1);
	}

	vcpu_mmap_size = ioctl(vm->sys_fd, KVM_GET_VCPU_MMAP_SIZE, 0);
	if (vcpu_mmap_size <= 0)
	{
		perror("KVM_GET_VCPU_MMAP_SIZE");
		exit(1);
	}

	vcpu->kvm_run = mmap(NULL, vcpu_mmap_size, PROT_READ | PROT_WRITE,
						 MAP_SHARED, vcpu->fd, 0);
	if (vcpu->kvm_run == MAP_FAILED)
	{
		perror("mmap kvm_run");
		exit(1);
	}
}

//-------------------------------------------------------------------------------

int main(int argc, char **argv)
{
	char *file_name;

	if (argc != 3)
	{
		fprintf(stderr, "Usage: %s -b <BINARY FILE>\n",
				argv[0]);
		return 1;
	}

	file_name = argv[2];

	struct vm vm;
	struct vcpu vcpu;
	vm_init(&vm, 0x200000);
	vcpu_init(&vm, &vcpu);
	return !run_vmm(&vm, &vcpu, file_name);
}