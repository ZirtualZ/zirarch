#define _GNU_SOURCE

static unsigned char screeninfo[160];

static int sys_open(const char *path, int flags)
{
	register int result asm("r0");
	register const char *pathreg asm("r0") = path;
	register int flagsreg asm("r1") = flags;
	register int number asm("r7") = 5;
	asm volatile("svc 0" : "=r"(result)
		     : "r"(pathreg), "r"(flagsreg), "r"(number)
		     : "memory", "cc");
	return result;
}

static int sys_close(int fd)
{
	register int result asm("r0");
	register int fdreg asm("r0") = fd;
	register int number asm("r7") = 6;
	asm volatile("svc 0" : "=r"(result)
		     : "r"(fdreg), "r"(number) : "memory", "cc");
	return result;
}

static int sys_read(int fd, void *buffer, unsigned int size)
{
	register int result asm("r0");
	register int fdreg asm("r0") = fd;
	register void *bufferreg asm("r1") = buffer;
	register unsigned int sizereg asm("r2") = size;
	register int number asm("r7") = 3;
	asm volatile("svc 0" : "=r"(result)
		     : "r"(fdreg), "r"(bufferreg), "r"(sizereg), "r"(number)
		     : "memory", "cc");
	return result;
}

static int sys_write(int fd, const void *buffer, unsigned int size)
{
	register int result asm("r0");
	register int fdreg asm("r0") = fd;
	register const void *bufferreg asm("r1") = buffer;
	register unsigned int sizereg asm("r2") = size;
	register int number asm("r7") = 4;
	asm volatile("svc 0" : "=r"(result)
		     : "r"(fdreg), "r"(bufferreg), "r"(sizereg), "r"(number)
		     : "memory", "cc");
	return result;
}

static int sys_ioctl(int fd, unsigned int request, void *argument)
{
	register int result asm("r0");
	register int fdreg asm("r0") = fd;
	register unsigned int requestreg asm("r1") = request;
	register void *argumentreg asm("r2") = argument;
	register int number asm("r7") = 54;
	asm volatile("svc 0" : "=r"(result)
		     : "r"(fdreg), "r"(requestreg), "r"(argumentreg),
		       "r"(number)
		     : "memory", "cc");
	return result;
}

static void __attribute__((noreturn)) sys_exit(int status)
{
	register int statusreg asm("r0") = status;
	register int number asm("r7") = 1;
	asm volatile("svc 0" : : "r"(statusreg), "r"(number) : "memory");
	__builtin_unreachable();
}

void __attribute__((noreturn)) initfb_main(void)
{
	unsigned char buffer[4096];
	int image;
	int framebuffer;
	int size;
	framebuffer = sys_open("/dev/fb0", 2);
	image = sys_open("/boot-splash.bgra", 0);
	if (framebuffer < 0 || image < 0) sys_exit(1);
	if (sys_ioctl(framebuffer, 0x4600, screeninfo) >= 0) {
		*(unsigned int *)(screeninfo + 16) = 0;
		*(unsigned int *)(screeninfo + 20) = 0;
		sys_ioctl(framebuffer, 0x4606, screeninfo);
	}
	while ((size = sys_read(image, buffer, sizeof(buffer))) > 0) {
		const unsigned char *next = buffer;
		while (size > 0) {
			int written = sys_write(framebuffer, next, size);
			if (written <= 0) sys_exit(1);
			next += written;
			size -= written;
		}
	}
	sys_close(image);
	sys_close(framebuffer);
	sys_exit(size < 0);
}

__asm__(
".global _start\n"
"_start:\n"
" bl initfb_main\n");
