#include <libopencm3/cm3/nvic.h>
#include <libopencm3/cm3/systick.h>
#include <libopencm3/stm32/dma.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/usart.h>

#include <stdio.h>
#include <errno.h>
#include <unistd.h>

#include <btstack/run_loop.h>

// Configuration
// LED2 on PA5
// Debug: USART2, TX on PA2
// Bluetooth: USART3. TX PB10, RX PB11, CTS PB13 (in), RTS PB14 (out), N_SHUTDOWN PB15
#define GPIO_LED2 GPIO5
#define USART_CONSOLE USART2
#define GPIO_BT_N_SHUTDOWN GPIO15

// btstack code starts there
void btstack_main(void);

static void bluetooth_power_cycle(void);

// hal_tick.h inmplementation
#include <btstack/hal_tick.h>

static void dummy_handler(void);
static void (*tick_handler)(void) = &dummy_handler;

static void dummy_handler(void){};

void hal_tick_init(void){
	/* clock rate / 1000 to get 1mS interrupt rate */
	systick_set_reload(8000);
	systick_set_clocksource(STK_CSR_CLKSOURCE_AHB);
	systick_counter_enable();
	systick_interrupt_enable();
}

void hal_tick_set_handler(void (*handler)(void)){
    if (handler == NULL){
        tick_handler = &dummy_handler;
        return;
    }
    tick_handler = handler;
}

int  hal_tick_get_tick_period_in_ms(void){
    return 1;
}

void sys_tick_handler(void)
{
	(*tick_handler)();
}

// hal_cpu.h implementation
#include <btstack/hal_cpu.h>

void hal_cpu_disable_irqs(void){

}
void hal_cpu_enable_irqs(void){

}
void hal_cpu_enable_irqs_and_sleep(void){

}

// hal_led.h implementation
#include <btstack/hal_led.h>

void hal_led_toggle(void){
	gpio_toggle(GPIOA, GPIO_LED2);
}

// hal_uart_dma.c implementation
#include <btstack/hal_uart_dma.h>

// handlers
static void (*rx_done_handler)(void) = dummy_handler;
static void (*tx_done_handler)(void) = dummy_handler;
static void (*cts_irq_handler)(void) = dummy_handler;

static uint8_t * rx_buffer;
static int bytes_to_receive = 0;

// DMA1_CHANNEL2 UART3_TX
void dma1_channel2_isr(void) {
	int done = 0;
	if ((DMA1_ISR & DMA_ISR_TCIF2) != 0) {
		DMA1_IFCR |= DMA_IFCR_CTCIF2;
		done = 1;
	}
	dma_disable_transfer_complete_interrupt(DMA1, DMA_CHANNEL2);
	usart_disable_tx_dma(USART3);
	dma_disable_channel(DMA1, DMA_CHANNEL2);

	if (done){
	(*tx_done_handler)();
}
}

// DMA1_CHANNEL2 UART3_RX
void dma1_channel3_isr(void){
	int done = 0;
	if ((DMA1_ISR & DMA_ISR_TCIF3) != 0) {
		DMA1_IFCR |= DMA_IFCR_CTCIF3;
		done = 1;
	}
	dma_disable_transfer_complete_interrupt(DMA1, DMA_CHANNEL3);
	usart_disable_rx_dma(USART3);
	dma_disable_channel(DMA1, DMA_CHANNEL3);
	if (done){
		gpio_clear(GPIOA, GPIO_LED2);
#if 0
		int i;
		printf("RX: ");
		for (i=0;i<bytes_to_receive;i++){
			printf("%02x ", rx_buffer[i]);
		}
		printf("\n");
#endif
		(*rx_done_handler)();
	}
}

void hal_uart_dma_init(void){
	bluetooth_power_cycle();
}
void hal_uart_dma_set_block_received( void (*the_block_handler)(void)){
    rx_done_handler = the_block_handler;
}

void hal_uart_dma_set_block_sent( void (*the_block_handler)(void)){
    tx_done_handler = the_block_handler;
}

void hal_uart_dma_set_csr_irq_handler( void (*the_irq_handler)(void)){
	// TODO: enable/disable interrupt
    cts_irq_handler = the_irq_handler;
}

int  hal_uart_dma_set_baud(uint32_t baud){
	usart_disable(USART3);
	usart_set_baudrate(USART3, baud);
	usart_enable(USART3);
	return 0;
}

void hal_uart_dma_send_block(const uint8_t *data, uint16_t size){

#if 0
	// dump data
	int i;
	printf("TX: ");
	for (i=0;i<size;i++){
		printf("%02x ", data[i]);
	}
	printf("\n");
#endif

#if 0 
	// Blocking USART implementation
	while (size){
		usart_send_blocking(USART3, *data);
		data++;
		size--;
	}
	(*tx_done_handler)();
#else

	/*
	 * USART3_TX Using DMA_CHANNEL2 
	 */

	/* Reset DMA channel*/
	dma_channel_reset(DMA1, DMA_CHANNEL2);

	dma_set_peripheral_address(DMA1, DMA_CHANNEL2, (uint32_t)&USART3_DR);
	dma_set_memory_address(DMA1, DMA_CHANNEL2, (uint32_t)data);
	dma_set_number_of_data(DMA1, DMA_CHANNEL2, size);
	dma_set_read_from_memory(DMA1, DMA_CHANNEL2);
	dma_enable_memory_increment_mode(DMA1, DMA_CHANNEL2);
	dma_set_peripheral_size(DMA1, DMA_CHANNEL2, DMA_CCR_PSIZE_8BIT);
	dma_set_memory_size(DMA1, DMA_CHANNEL2, DMA_CCR_MSIZE_8BIT);
	dma_set_priority(DMA1, DMA_CHANNEL2, DMA_CCR_PL_VERY_HIGH);
	dma_enable_transfer_complete_interrupt(DMA1, DMA_CHANNEL2);
	dma_enable_channel(DMA1, DMA_CHANNEL2);
    usart_enable_tx_dma(USART3);
#endif
}

void hal_uart_dma_receive_block(uint8_t *data, uint16_t size){
	/*
	 * USART3_RX is on DMA_CHANNEL3
	 */

	printf("hal_uart_dma_receive_block req size %u\n", size);
	bytes_to_receive = size;
	rx_buffer = data;

	/* Reset DMA channel*/
	dma_channel_reset(DMA1, DMA_CHANNEL3);

	dma_set_peripheral_address(DMA1, DMA_CHANNEL3, (uint32_t)&USART3_DR);
	dma_set_memory_address(DMA1, DMA_CHANNEL3, (uint32_t)data);
	dma_set_number_of_data(DMA1, DMA_CHANNEL3, size);
	dma_set_read_from_peripheral(DMA1, DMA_CHANNEL3);
	dma_enable_memory_increment_mode(DMA1, DMA_CHANNEL3);
	dma_set_peripheral_size(DMA1, DMA_CHANNEL3, DMA_CCR_PSIZE_8BIT);
	dma_set_memory_size(DMA1, DMA_CHANNEL3, DMA_CCR_MSIZE_8BIT);
	dma_set_priority(DMA1, DMA_CHANNEL3, DMA_CCR_PL_HIGH);
	dma_enable_transfer_complete_interrupt(DMA1, DMA_CHANNEL3);
	dma_enable_channel(DMA1, DMA_CHANNEL3);
    usart_enable_rx_dma(USART3);
}

void hal_uart_dma_set_sleep(uint8_t sleep){
	// TODO:
	(void) sleep;
}

/**
 * Use USART_CONSOLE as a console.
 * This is a syscall for newlib
 * @param file
 * @param ptr
 * @param len
 * @return
 */
int _write(int file, char *ptr, int len);
int _write(int file, char *ptr, int len){
	int i;

	if (file == STDOUT_FILENO || file == STDERR_FILENO) {
		for (i = 0; i < len; i++) {
			if (ptr[i] == '\n') {
				usart_send_blocking(USART_CONSOLE, '\r');
			}
			usart_send_blocking(USART_CONSOLE, ptr[i]);
		}
		return i;
	}
	errno = EIO;
	return -1;
}

static void clock_setup(void){
	/* Enable clocks for GPIO port A (for GPIO_USART1_TX) and USART1 + USART2. */
	/* needs to be done before initializing other peripherals */
	rcc_periph_clock_enable(RCC_GPIOA);
	rcc_periph_clock_enable(RCC_GPIOB);
	rcc_periph_clock_enable(RCC_USART2);
	rcc_periph_clock_enable(RCC_USART3);
	rcc_periph_clock_enable(RCC_DMA1);
}

static void gpio_setup(void){
	/* Set GPIO5 (in GPIO port A) to 'output push-pull'. [LED] */
	gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_2_MHZ, GPIO_CNF_OUTPUT_PUSHPULL, GPIO_LED2);
}

static void debug_usart_setup(void){
	/* Setup GPIO pin GPIO_USART2_TX/GPIO2 on GPIO port A for transmit. */
	gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_50_MHZ, GPIO_CNF_OUTPUT_ALTFN_PUSHPULL, GPIO_USART2_TX);

	/* Setup UART parameters. */
	usart_set_baudrate(USART2, 9600);
	usart_set_databits(USART2, 8);
	usart_set_stopbits(USART2, USART_STOPBITS_1);
	usart_set_mode(USART2, USART_MODE_TX);
	usart_set_parity(USART2, USART_PARITY_NONE);
	usart_set_flow_control(USART2, USART_FLOWCONTROL_NONE);

	/* Finally enable the USART. */
	usart_enable(USART2);
}

static void bluetooth_setup(void){
	printf("\nBluetooth starting...\n");

	// n_shutdown as output
	gpio_set_mode(GPIOB, GPIO_MODE_OUTPUT_50_MHZ, GPIO_CNF_OUTPUT_PUSHPULL,GPIO_BT_N_SHUTDOWN);

	// tx output
	gpio_set_mode(GPIOB, GPIO_MODE_OUTPUT_50_MHZ, GPIO_CNF_OUTPUT_ALTFN_PUSHPULL, GPIO_USART3_TX);
	// rts output
	gpio_set_mode(GPIOB, GPIO_MODE_OUTPUT_50_MHZ, GPIO_CNF_OUTPUT_ALTFN_PUSHPULL, GPIO_USART3_RTS);
	// rx input
	gpio_set_mode(GPIOB, GPIO_MODE_INPUT, GPIO_CNF_INPUT_FLOAT, GPIO_USART3_RX);
	// cts as input
	gpio_set_mode(GPIOB, GPIO_MODE_INPUT, GPIO_CNF_INPUT_FLOAT, GPIO_USART3_CTS);

	/* Setup UART parameters. */
	usart_set_baudrate(USART3, 115200);
	usart_set_databits(USART3, 8);
	usart_set_stopbits(USART3, USART_STOPBITS_1);
	usart_set_mode(USART3, USART_MODE_TX_RX);
	usart_set_parity(USART3, USART_PARITY_NONE);
	usart_set_flow_control(USART3, USART_FLOWCONTROL_RTS_CTS);

	/* Finally enable the USART. */
	usart_enable(USART3);

	// TX
	nvic_set_priority(NVIC_DMA1_CHANNEL2_IRQ, 0);
	nvic_enable_irq(NVIC_DMA1_CHANNEL2_IRQ);

	// RX
	nvic_set_priority(NVIC_DMA1_CHANNEL3_IRQ, 0);
	nvic_enable_irq(NVIC_DMA1_CHANNEL3_IRQ);
}

// reset Bluetooth using n_shutdown

static void msleep(uint32_t delay)
{
	uint32_t wake = embedded_get_ticks() + delay;
	while (wake > embedded_get_ticks());
}

static void bluetooth_power_cycle(void){
	printf("n_shutdown low\n");
	gpio_clear(GPIOA, GPIO_LED2);
	gpio_clear(GPIOB, GPIO_BT_N_SHUTDOWN);
	msleep(1000);
	printf("n_shutdown high\n");
	gpio_set(GPIOA, GPIO_LED2);
	gpio_set(GPIOB, GPIO_BT_N_SHUTDOWN);
	msleep(1000);
	printf("n_shutdown toggling down\n");	
}

int main(void)
{
	clock_setup();
	gpio_setup();
	hal_tick_init();
	debug_usart_setup();
	bluetooth_setup();

	// hand over to btstack embedded code 
	btstack_main();

	return 0;
}