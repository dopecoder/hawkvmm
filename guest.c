#define uint8_t unsigned char  
#define uint16_t unsigned short
#define uint32_t unsigned int

static void outw(uint16_t port, uint16_t value) {
	asm("outw %w0,%1" : /* empty */ : "a" (value), "Nd" (port) : "memory");
}

static void outb(uint16_t port, uint8_t value) {
	asm("outb %0,%1" : /* empty */ : "a" (value), "Nd" (port) : "memory");
}

#define CONSOLE_OUT_PORT 0x42
static uint8_t inb(uint16_t port) {
	uint8_t value;
    asm volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
	return value;
}

//-------------------------------Console Driver--------------------------------------
uint8_t console_buffer[256];
uint8_t console_buffer_count = 0;

void print(uint8_t * string, uint8_t count){
	uint8_t char_idx = 0;
	if(!count){
		return;
	}

	for(; char_idx < count; char_idx++)
		outb(CONSOLE_OUT_PORT, string[char_idx]);

}

void strcpy(uint8_t *dest, uint8_t *src)
{
  unsigned i;
  for (i=0; src[i] != '\0'; ++i)
    dest[i] = src[i];

  dest[i]= '\0';
}

void flush_console_buffer(){
	int idx = 0;
	for(; idx<console_buffer_count; idx++){
		console_buffer[idx] = (uint8_t)'\0';
	}
	console_buffer_count = 0;
}

//-----------------------------------------------------------------------------------

//-------------------------------Keyboard Driver-------------------------------------
#define KEYBOARD_IN_PORT 0x44
#define KEYBOARD_STATUS_PORT 0x45
#define KEYBOARD_READ_ACK 0

uint8_t kbd_buffer[256];
uint8_t kbd_buffer_count = 0;

int is_key_pressed(){
	int status = inb(KEYBOARD_STATUS_PORT);
	if(status){
		return 1;
	}
	return 0;
}

void kbd_read_ack(){
	outb(KEYBOARD_STATUS_PORT, KEYBOARD_READ_ACK);
}

int read_key(){
	uint8_t inp_key = inb(KEYBOARD_IN_PORT);
	return inp_key;
}

void add_key(uint8_t input_key){
	kbd_buffer[kbd_buffer_count++] = input_key;
}

void terminate_buffer(){
	kbd_buffer[kbd_buffer_count] = '\0';
}

int check_and_exit(uint8_t inp_key){
	if(inp_key == '\n'){
		return 1;
	}
	return 2;
}

int check_and_get_key(){
	if(is_key_pressed()){
		uint8_t inp_key = read_key();
		add_key(inp_key);
		return check_and_exit(inp_key);
	}
	return 0;
}

void flush_kdb_buffer(){
	int idx = 0;
	for(; idx<kbd_buffer_count; idx++){
		kbd_buffer[idx] = (uint8_t)'\0';
	}
	kbd_buffer_count = 0;
}

void cpy_kbd_to_cons_buf(uint8_t *console_buffer, uint8_t *kbd_buffer)
{
	flush_console_buffer();
	strcpy(console_buffer, kbd_buffer);
	console_buffer_count = kbd_buffer_count;
}

//-----------------------------------------------------------------------------------

//-------------------------------Timer Driver--------------------------------------
#define TIMER_VALUE_PORT 0x46
#define TIMER_ENABLE_PORT 0x47
#define TIMER_STATUS_PORT 0x47
#define TIMER_ENABLE 1
#define TIMER_DISABLE 0

void set_timer(uint32_t millisecs){
	outw(TIMER_VALUE_PORT, millisecs);
	outb(TIMER_ENABLE_PORT, TIMER_ENABLE);
}

void disable_timer(){
	outb(TIMER_ENABLE_PORT, TIMER_DISABLE);
}

void timer_ack(){
	outb(TIMER_STATUS_PORT, 1);
}

int is_timer_int(){
	uint8_t timer_val = inb(TIMER_STATUS_PORT);
	if(((timer_val & 0x02) >> 1) == 1){
		return 1;
	}
	return 0;
}
//-----------------------------------------------------------------------------------

void __attribute__((noreturn)) __attribute__((section(".start"))) _start(void) {
	
	set_timer(5000);
	while(1){
		if(is_timer_int()){
			timer_ack();
			print(console_buffer, console_buffer_count);
		}

		int status = check_and_get_key();

		if(status == 1){
			kbd_read_ack();
			terminate_buffer();
			cpy_kbd_to_cons_buf(console_buffer, kbd_buffer);
			flush_kdb_buffer();
		}else if(status == 2){
			kbd_read_ack();
		}
	}

	for (;;)
		asm("hlt");
}
