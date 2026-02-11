/* GDS Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "freertos/queue.h"
#include "esp_task.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/spi_master.h"
#include "gpio_exp.h"
#include "GPIO.pb.h"
#define GPIO_EXP_INTR	0x100
#define	GPIO_EXP_WRITE	0x200

/* 
 shadow register is both output and input, so we assume that reading to the
 ports also reads the value set on output
*/
 
typedef struct gpio_exp_s {
	uint32_t first, last;
	int intr;
	bool intr_pending;
	struct  {
		struct gpio_exp_phy_s phy;
		spi_device_handle_t spi_handle;
	};
	uint32_t shadow, pending;
	TickType_t age;
	SemaphoreHandle_t mutex;
	uint32_t r_mask, w_mask;
	uint32_t pullup, pulldown;
	struct gpio_exp_isr_s {
		gpio_isr_t handler;
		void *arg;
		TimerHandle_t timer;
	} isr[32];
	struct gpio_exp_model_s const *model;
} gpio_exp_t;

typedef struct {
	enum { ASYNC_WRITE } type;
	int gpio;
	int level;
	gpio_exp_t *expander;
} queue_request_t;

static const char TAG[] = "gpio expander";

static void   IRAM_ATTR intr_isr_handler(void* arg);
static gpio_exp_t* find_expander(gpio_exp_t *expander, int *gpio);

static esp_err_t mpr121_init(gpio_exp_t* self);
static uint32_t  mpr121_read(gpio_exp_t* self);
static void      mpr121_write(gpio_exp_t* self);

static void 	pca9535_set_direction(gpio_exp_t* self);
static uint32_t pca9535_read(gpio_exp_t* self);
static void 	pca9535_write(gpio_exp_t* self);

static uint32_t	pca85xx_read(gpio_exp_t* self);
static void 	pca85xx_write(gpio_exp_t* self);

static esp_err_t mcp23017_init(gpio_exp_t* self);
static void      mcp23017_set_pull_mode(gpio_exp_t* self);
static void      mcp23017_set_direction(gpio_exp_t* self);
static uint32_t  mcp23017_read(gpio_exp_t* self);
static void      mcp23017_write(gpio_exp_t* self);

static esp_err_t mcp23s17_init(gpio_exp_t* self);
static void      mcp23s17_set_pull_mode(gpio_exp_t* self);
static void      mcp23s17_set_direction(gpio_exp_t* self);
static uint32_t  mcp23s17_read(gpio_exp_t* self);
static void      mcp23s17_write(gpio_exp_t* self);

static void   service_handler(void *arg);
static void   debounce_handler( TimerHandle_t xTimer );

static esp_err_t i2c_write(uint8_t port, uint8_t addr, uint8_t reg, uint32_t data, int len);
static uint32_t  i2c_read(uint8_t port, uint8_t addr, uint8_t reg, int len);

static spi_device_handle_t spi_config(struct gpio_exp_phy_s *phy);
static esp_err_t           spi_write(spi_device_handle_t handle, uint8_t addr, uint8_t reg, uint32_t data, int len);
static uint32_t            spi_read(spi_device_handle_t handle, uint8_t addr, uint8_t reg, int len);

static const struct gpio_exp_model_s {
	char *model;
	gpio_int_type_t trigger;
	esp_err_t (*init)(gpio_exp_t* self);
	uint32_t  (*read)(gpio_exp_t* self);
	void      (*write)(gpio_exp_t* self);
	void      (*set_direction)(gpio_exp_t* self);
	void      (*set_pull_mode)(gpio_exp_t* self);
} registered[] = {
	{ .model = "mpr121",
	  .trigger = GPIO_INTR_LOW_LEVEL,
	  .init = mpr121_init,
	  .read = mpr121_read,
	  .write = mpr121_write, },	
	{ .model = "pca9535",
	  .trigger = GPIO_INTR_LOW_LEVEL,
	  .set_direction = pca9535_set_direction,
	  .read = pca9535_read,
	  .write = pca9535_write, },
	{ .model = "pca85xx",
	  .trigger = GPIO_INTR_LOW_LEVEL,
	  .read = pca85xx_read,
	  .write = pca85xx_write, },
	{ .model = "mcp23017",
	  .trigger = GPIO_INTR_LOW_LEVEL,
	  .init = mcp23017_init,
	  .set_direction = mcp23017_set_direction,
	  .set_pull_mode = mcp23017_set_pull_mode,
	  .read = mcp23017_read,
	  .write = mcp23017_write, },
	{ .model = "mcp23s17",
	  .trigger = GPIO_INTR_LOW_LEVEL,
	  .init = mcp23s17_init,
	  .set_direction = mcp23s17_set_direction,
	  .set_pull_mode = mcp23s17_set_pull_mode,
	  .read = mcp23s17_read,
	  .write = mcp23s17_write, },
};

static EXT_RAM_ATTR uint8_t n_expanders;
static EXT_RAM_ATTR QueueHandle_t message_queue;
static EXT_RAM_ATTR gpio_exp_t expanders[4];
static EXT_RAM_ATTR TaskHandle_t service_task;

/******************************************************************************
 * Retrieve base from an expander reference
 */
uint32_t gpio_exp_get_base(gpio_exp_t *expander) { 
	return expander->first; 
}

/******************************************************************************
 * Retrieve reference from a GPIO
 */
gpio_exp_t *gpio_exp_get_expander(int gpio) { 
	int _gpio = gpio;
	return find_expander(NULL, &_gpio);
}

/******************************************************************************
 * Create an I2C expander
 */
gpio_exp_t* gpio_exp_create(const gpio_exp_config_t *config) {
	gpio_exp_t *expander = expanders + n_expanders;
	
	if (config->base < GPIO_NUM_MAX || n_expanders == sizeof(expanders)/sizeof(gpio_exp_t)) {
		ESP_LOGE(TAG, "Base %d GPIO must be at least %d for %s or too many expanders %d", config->base, GPIO_NUM_MAX, config->model, n_expanders);
		return NULL;
	}

	// See if we know that model (expanders is zero-initialized)
	for (int i = 0; !expander->model && i < sizeof(registered)/sizeof(struct gpio_exp_model_s); i++) {
		if (strcasestr(config->model, registered[i].model)) expander->model = registered + i;
    }

	// well... try again
	if (!expander->model) {
		ESP_LOGE(TAG, "Unknown GPIO expansion chip %s", config->model);
		return NULL;
	}
		
	memcpy(&expander->phy, &config->phy, sizeof(struct gpio_exp_phy_s));

	// try to initialize the expander if required
	if (expander->model->init && expander->model->init(expander) != ESP_OK) {
		ESP_LOGE(TAG, "Cannot create GPIO expander %s, check i2c/spi configuration", config->model);
		return NULL;
	}
	n_expanders++;
	expander->first = config->base;
	expander->last = config->base + config->count - 1;
	expander->intr = config->intr;
	expander->mutex = xSemaphoreCreateMutex();

	// create a task to handle asynchronous requests (only write at this time)
	if (!message_queue) {
		// we allocate TCB but stack is static to avoid SPIRAM fragmentation
		StaticTask_t* xTaskBuffer = (StaticTask_t*) heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
		static EXT_RAM_ATTR StackType_t xStack[4*1024] __attribute__ ((aligned (4)));

		message_queue = xQueueCreate(4, sizeof(queue_request_t));
		service_task = xTaskCreateStatic(service_handler, "gpio_expander", sizeof(xStack), NULL, ESP_TASK_PRIO_MIN + 1, xStack, xTaskBuffer);
	}
	if(config->phy.ena_pin>=0){
		ESP_LOGD(TAG,"Enabling expander with pin %d level %d",config->phy.ena_pin,config->phy.ena_lvl);
		esp_rom_gpio_pad_select_gpio(config->phy.ena_pin);
		gpio_set_direction(config->phy.ena_pin, GPIO_MODE_DEF_OUTPUT);
		gpio_set_level(config->phy.ena_pin, config->phy.ena_lvl);
	}

	// set interrupt if possible
	if (config->intr >= 0) {
		esp_rom_gpio_pad_select_gpio(config->intr);
		gpio_set_direction(config->intr, GPIO_MODE_INPUT);

		switch (expander->model->trigger) {
		case GPIO_INTR_NEGEDGE:
		case GPIO_INTR_LOW_LEVEL:
			gpio_set_pull_mode(config->intr, GPIO_PULLUP_ONLY);
			break;
		case GPIO_INTR_POSEDGE:
		case GPIO_INTR_HIGH_LEVEL:
			gpio_set_pull_mode(config->intr, GPIO_PULLDOWN_ONLY);
			break;
		default:	
			gpio_set_pull_mode(config->intr, GPIO_PULLUP_PULLDOWN);
			break;
		}	
		
		gpio_set_intr_type(config->intr, expander->model->trigger);		
		gpio_isr_handler_add(config->intr, intr_isr_handler, expander);
		gpio_intr_enable(config->intr);						
	}
	
	ESP_LOGI(TAG, "Create GPIO expander %s at base %u with intr %d at @%x on port/host %d/%d, enable pin: %d:%d", config->model, config->base, config->intr, config->phy.addr, config->phy.port, config->phy.host,config->phy.ena_pin,config->phy.ena_lvl);
	return expander;
}

/******************************************************************************
 * Add ISR handler for a GPIO
 */
esp_err_t gpio_exp_isr_handler_add(int gpio, gpio_isr_t isr_handler, uint32_t debounce, void *arg, struct gpio_exp_s *expander) {
	if (gpio < GPIO_NUM_MAX && !expander) return gpio_isr_handler_add(gpio, isr_handler, arg);
	if ((expander = find_expander(expander, &gpio)) == NULL) return ESP_ERR_INVALID_ARG;

	expander->isr[gpio].handler = isr_handler;
	expander->isr[gpio].arg = arg;
	if (debounce) expander->isr[gpio].timer = xTimerCreate("gpioExpDebounce", pdMS_TO_TICKS(debounce), 
	                                                       pdFALSE, expander->isr + gpio, debounce_handler );

	return ESP_OK;
}

/******************************************************************************
 * Remove ISR handler for a GPIO
 */
esp_err_t gpio_exp_isr_handler_remove(int gpio, struct gpio_exp_s *expander) {
	if (gpio < GPIO_NUM_MAX && !expander) return gpio_isr_handler_remove(gpio);
	if ((expander = find_expander(expander, &gpio)) == NULL) return ESP_ERR_INVALID_ARG;

	if (expander->isr[gpio].timer) xTimerDelete(expander->isr[gpio].timer, portMAX_DELAY);
	memset(expander->isr + gpio, 0, sizeof(struct gpio_exp_isr_s));

	return ESP_OK;
}

/******************************************************************************
 * Set GPIO direction
 */
esp_err_t gpio_exp_set_direction(int gpio, gpio_mode_t mode, gpio_exp_t *expander) {
	if (gpio < GPIO_NUM_MAX && !expander) return gpio_set_direction(gpio, mode);
	if ((expander = find_expander(expander, &gpio)) == NULL) return ESP_ERR_INVALID_ARG;

	xSemaphoreTake(expander->mutex, pdMS_TO_TICKS(portMAX_DELAY));

	if (mode == GPIO_MODE_INPUT) {
		expander->r_mask |= 1 << gpio;
		expander->shadow = expander->model->read(expander);
		expander->age = ~xTaskGetTickCount();
	} else {
		expander->w_mask |= 1 << gpio;
	}
	
	if (expander->r_mask & expander->w_mask) {
		xSemaphoreGive(expander->mutex);
		ESP_LOGE(TAG, "GPIO %d on expander base %u can't be r/w", gpio, expander->first);
		return ESP_ERR_INVALID_ARG;
	}

	// most expanders want unconfigured GPIO to be set to output
	if (expander->model->set_direction) expander->model->set_direction(expander);

	xSemaphoreGive(expander->mutex);

	return ESP_OK;
}	

/******************************************************************************
 * Get GPIO level with cache
 */
int gpio_exp_get_level(int gpio, int age, gpio_exp_t *expander) {
	if (gpio < GPIO_NUM_MAX && !expander) return gpio_get_level(gpio);
	if ((expander = find_expander(expander, &gpio)) == NULL) return -1;
	uint32_t now = xTaskGetTickCount();

	// return last thing we had if we can't get the mutex
	if (xSemaphoreTake(expander->mutex, pdMS_TO_TICKS(50)) == pdFALSE) {
		ESP_LOGW(TAG, "Can't get mutex for GPIO %d", expander->first + gpio);
		return (expander->shadow >> gpio) & 0x01;
	}

	// re-read the expander if data is too old
	if (age >= 0 && now - expander->age >= pdMS_TO_TICKS(age)) {
		uint32_t value = expander->model->read(expander);
		expander->pending |= (expander->shadow ^ value) & expander->r_mask;
		expander->shadow = value;
		expander->age = now;
	}

	// clear pending bit
	expander->pending &= ~(1 << gpio);

	xSemaphoreGive(expander->mutex);
	
	ESP_LOGD(TAG, "Get level for GPIO %u => read %x", expander->first + gpio, expander->shadow);
	return (expander->shadow >> gpio) & 0x01;
}

/******************************************************************************
 * Set GPIO level with cache
 */
esp_err_t gpio_exp_set_level(int gpio, int level, bool direct, gpio_exp_t *expander) {
	if (gpio < GPIO_NUM_MAX && !expander) return gpio_set_level(gpio, level);
	if ((expander = find_expander(expander, &gpio)) == NULL) return ESP_ERR_INVALID_ARG;
	uint32_t mask = 1 << gpio;

	// very limited risk with lack of semaphore here
	if ((expander->w_mask & mask) == 0) {
		ESP_LOGW(TAG, "GPIO %d is not set for output", expander->first + gpio);
		return ESP_ERR_INVALID_ARG;
	}

	if (direct) {
		xSemaphoreTake(expander->mutex, pdMS_TO_TICKS(portMAX_DELAY));

		level = level ? mask : 0;
		mask &= expander->shadow;

		// only write if shadow not up to date
		if ((mask ^ level) && expander->model->write) {
			expander->shadow = (expander->shadow & ~(mask | level)) | level;
			expander->model->write(expander);
		}

		xSemaphoreGive(expander->mutex);
		ESP_LOGD(TAG, "Set level %x for GPIO %u => wrote %x", level, expander->first + gpio, expander->shadow);
	} else {
		queue_request_t request = { .gpio = gpio, .level = level, .type = ASYNC_WRITE, .expander = expander };
		if (xQueueSend(message_queue, &request, 0) == pdFALSE) return ESP_ERR_INVALID_RESPONSE;

		// notify service task that will write it when it can
		xTaskNotify(service_task, GPIO_EXP_WRITE, eSetValueWithoutOverwrite);
	} 

	return ESP_OK;
}

/******************************************************************************
 * Set GPIO pullmode
 */
esp_err_t gpio_exp_set_pull_mode(int gpio, gpio_pull_mode_t mode, gpio_exp_t *expander) {
	if (gpio < GPIO_NUM_MAX && !expander) return gpio_set_pull_mode(gpio, mode);
	if ((expander = find_expander(expander, &gpio)) != NULL && expander->model->set_pull_mode) {

		expander->pullup &= ~(1 << gpio);
		expander->pulldown &= ~(1 << gpio);

		if (mode == GPIO_PULLUP_ONLY  || mode == GPIO_PULLUP_PULLDOWN) expander->pullup |= 1 << gpio;
		if (mode == GPIO_PULLDOWN_ONLY || mode == GPIO_PULLUP_PULLDOWN) expander->pulldown |= 1 << gpio;

		expander->model->set_pull_mode(expander);
		return ESP_OK;
	}
	return ESP_ERR_INVALID_ARG;
}

/******************************************************************************
 * Wrapper function
 */

void esp_rom_gpio_pad_select_gpio_x(uint32_t gpio){
	if (gpio < GPIO_NUM_MAX) {
		esp_rom_gpio_pad_select_gpio(gpio);
	} 
}

esp_err_t gpio_set_pull_mode_x(int gpio, gpio_pull_mode_t mode) {
	if (gpio < GPIO_NUM_MAX) return gpio_set_pull_mode(gpio, mode);
	return gpio_exp_set_pull_mode(gpio, mode, NULL);
}

esp_err_t gpio_set_direction_x(int gpio, gpio_mode_t mode) {
	if (gpio < GPIO_NUM_MAX) return gpio_set_direction(gpio, mode);
	return gpio_exp_set_direction(gpio, mode, NULL);
}

int gpio_get_level_x(int gpio) {
	if (gpio < GPIO_NUM_MAX) return gpio_get_level(gpio);
	return gpio_exp_get_level(gpio, 10, NULL);
}

esp_err_t gpio_set_level_x(int gpio, int level) {
	if (gpio < GPIO_NUM_MAX) return gpio_set_level(gpio, level);
	return gpio_exp_set_level(gpio, level, false, NULL);
}

esp_err_t gpio_isr_handler_add_x(int gpio, gpio_isr_t isr_handler, void* args) {
	if (gpio < GPIO_NUM_MAX) return gpio_isr_handler_add(gpio, isr_handler, args);
	return gpio_exp_isr_handler_add(gpio, isr_handler, 0, args, NULL);
}

esp_err_t gpio_isr_handler_remove_x(int gpio) {
	if (gpio < GPIO_NUM_MAX) return gpio_isr_handler_remove(gpio);
	return gpio_exp_isr_handler_remove(gpio, NULL);
}


/****************************************************************************************
 * INTR low-level handler
 */
static void IRAM_ATTR intr_isr_handler(void* arg) {
	gpio_exp_t *self = (gpio_exp_t*) arg;
	BaseType_t woken = pdFALSE;
	
	// edge interrupts do not work because of read/clear = potential short pulse
	gpio_intr_disable(self->intr);	
	
	// activate all, including ourselves
	for (int i = 0; i < n_expanders; i++) if (expanders[i].intr == self->intr) expanders[i].intr_pending = true; 
	
	xTaskNotifyFromISR(service_task, GPIO_EXP_INTR, eSetValueWithOverwrite, &woken);
	if (woken) portYIELD_FROM_ISR();

	ESP_EARLY_LOGD(TAG, "INTR for expander base %d", gpio_exp_get_base(self));
}

/****************************************************************************************
 * INTR debounce handler
 */
static void debounce_handler( TimerHandle_t xTimer ) {
	struct gpio_exp_isr_s *isr = (struct gpio_exp_isr_s*) pvTimerGetTimerID (xTimer);
	isr->handler(isr->arg);
}

/****************************************************************************************
 * Service task
 */
void service_handler(void *arg) {
	while (1) {
		queue_request_t request;
		uint32_t notif = ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

		// we have been notified of an interrupt
		if (notif == GPIO_EXP_INTR) {
			/* If we want a smarter bitmap of expanders with a pending interrupt
			   we'll have to disable interrupts while clearing that bitmap. For 
			   now, a loop will do */
			for (int i = 0; i < n_expanders; i++) {
				gpio_exp_t *expander = expanders + i;

				// no interrupt for that gpio or not pending (safe as interrupt is disabled)
				if (expander->intr < 0 || !expander->intr_pending) continue;

				xSemaphoreTake(expander->mutex, pdMS_TO_TICKS(50));

				// read GPIOs and clear all pending status
				uint32_t value = expander->model->read(expander);
				expander->age = xTaskGetTickCount();
				
				// re-enable interrupt now that it has been cleared
				expander->intr_pending = false;
				gpio_intr_enable(expander->intr);				
				
				uint32_t pending = expander->pending | ((expander->shadow ^ value) & expander->r_mask);
				expander->shadow = value;
				expander->pending = 0;

				xSemaphoreGive(expander->mutex);
				ESP_LOGD(TAG, "Handling GPIO %d reads 0x%04x and has 0x%04x pending", expander->first, expander->shadow, pending);
				
				for (int gpio = 31, clz = 0; pending && clz < 31; pending <<= (clz + 1)) {
					clz = __builtin_clz(pending);
					gpio -= clz;
					if (expander->isr[gpio].timer) xTimerReset(expander->isr[gpio].timer, 1);	// todo 0
					else if (expander->isr[gpio].handler) expander->isr[gpio].handler(expander->isr[gpio].arg);
				}	
			}
		}

		// check if we have some other pending requests
		while (xQueueReceive(message_queue, &request, 0) == pdTRUE) {
			esp_err_t err = gpio_exp_set_level(request.gpio, request.level, true, request.expander);
			if (err != ESP_OK) ESP_LOGW(TAG, "Can't execute async GPIO %d write request (%d)", request.gpio, err);  
		}
	}
}

/****************************************************************************************
 * Find the expander related to base
 */
static gpio_exp_t* find_expander(gpio_exp_t *expander, int *gpio) {
	// a mutex would be better, but risk is so small...
	for (int i = 0; !expander && i < n_expanders; i++) {
		if (*gpio >= expanders[i].first && *gpio <= expanders[i].last) expander = expanders + i;
	}
	
	// normalize GPIO number
	if (expander && *gpio >= expander->first) *gpio -= expander->first;
	
	return expander;
}

/****************************************************************************************
                                        DRIVERS                                       
****************************************************************************************/

/****************************************************************************************
 * MPR121 family : init, direction, read and write
 */
static esp_err_t mpr121_init(gpio_exp_t* self) {
	static const struct {
		uint8_t addr;
		uint8_t data;
	} mpr121_init_table[] = { 
		{ 0x80, 0x63 }, /* Soft reset */

		{ 0x2b, 0x01 }, { 0x2c, 0x01 }, { 0x2d, 0x10 }, { 0x2e, 0x20 },                /* MHDR, NHDR, NCLR, FDLR */
		{ 0x2f, 0x01 }, { 0x30, 0x01 }, { 0x31, 0x10 }, { 0x32, 0x20 },                /* MHDF, NHDF, NCLF, FDLF */
		{ 0x33, 0x01 }, { 0x34, 0x10 }, { 0x35, 0xff },                                /* NHDT, NCLT, FDLT */
		{ 0x5b, 0x11 }, { 0x5c, 0xff }, { 0x5d, 0x30 },                                /* DTR, AFE1, AFE2 */
		{ 0x7b, 0x00 }, { 0x7c, 0x00 }, { 0x7d, 0x00 }, { 0x7e, 0x00 }, { 0x7f, 0x00 },/* ACCR0, ACCR1, USL, LSL, TL */

		{ 0x41, 0x28 }, { 0x42, 0x14 }, /* ELE0: Touch Threshold, Release Threshold */
		{ 0x43, 0x28 }, { 0x44, 0x14 }, /* ELE1: Touch Threshold, Release Threshold */
		{ 0x45, 0x28 }, { 0x46, 0x14 }, /* ELE2: Touch Threshold, Release Threshold */
		{ 0x47, 0x28 }, { 0x48, 0x14 }, /* ELE3: Touch Threshold, Release Threshold */
		{ 0x49, 0x28 }, { 0x4a, 0x14 }, /* ELE4: Touch Threshold, Release Threshold */
		{ 0x4b, 0x28 }, { 0x4c, 0x14 }, /* ELE5: Touch Threshold, Release Threshold */
		{ 0x4d, 0x28 }, { 0x4e, 0x14 }, /* ELE6: Touch Threshold, Release Threshold */
		{ 0x4f, 0x28 }, { 0x50, 0x14 }, /* ELE7: Touch Threshold, Release Threshold */
		{ 0x51, 0x28 }, { 0x52, 0x14 }, /* ELE8: Touch Threshold, Release Threshold */
		{ 0x53, 0x28 }, { 0x54, 0x14 }, /* ELE9: Touch Threshold, Release Threshold */
		{ 0x55, 0x28 }, { 0x56, 0x14 }, /* ELE10: Touch Threshold, Release Threshold */
		{ 0x57, 0x28 }, { 0x58, 0x14 }, /* ELE11: Touch Threshold, Release Threshold */

		{ 0x5e, 0xcc }, /* ECR - must be set last */
		{ 0, 0 } 
	};

	esp_err_t err = 0;
	for (int i = 0; mpr121_init_table[i].addr; i++) {
		err |= i2c_write(self->phy.port, self->phy.addr, mpr121_init_table[i].addr, mpr121_init_table[i].data, 1);
	}

	return err;
}

static uint32_t mpr121_read(gpio_exp_t* self) {
	// only return the lower 12 bits of the pin status registers
	return i2c_read(self->phy.port, self->phy.addr, 0x00, 2) & 0x0fff;
}

static void mpr121_write(gpio_exp_t* self) {
	ESP_LOGE(TAG, "MPR121 GPIO write not implemented");
}

/****************************************************************************************
 * PCA9535 family : direction, read and write
 */
static void pca9535_set_direction(gpio_exp_t* self) {
	i2c_write(self->phy.port, self->phy.addr, 0x06, self->r_mask, 2);
}

static uint32_t pca9535_read(gpio_exp_t* self) {
	return i2c_read(self->phy.port, self->phy.addr, 0x00, 2);
}

static void pca9535_write(gpio_exp_t* self) {
	i2c_write(self->phy.port, self->phy.addr, 0x02, self->shadow, 2);
}

/****************************************************************************************
 * PCA85xx family : read and write
 */
static uint32_t pca85xx_read(gpio_exp_t* self) {
	// must return the full set of pins, not just inputs
	uint32_t data = i2c_read(self->phy.port, self->phy.addr, 0xff, 2);
	return (data & self->r_mask) | (self->shadow & ~self->r_mask);
}

static void pca85xx_write(gpio_exp_t* self) {
	/* 
	 There is no good option with this chip: normally, unused pin should be set to input
	 to avoid any conflict but then they float and create tons of suprious. So option 1 is
	 to le tthem float and option 2 is to set them as output to 0.
	 In addition, setting an output pin to 1 equals is making it an input and if this is
	 use to short a led (e.g.) instead of being the sink, the it generates a spurious
	*/
	// option 1 
	// i2c_write(self->phy.port, self->phy.addr, 0xff, (self->shadow & self->w_mask) | ~self->w_mask, 2);
	// option 2
	i2c_write(self->phy.port, self->phy.addr, 0xff, (self->shadow & self->w_mask) | self->r_mask, 2);
}

/****************************************************************************************
 * MCP23017 family : init, direction, read and write
 */
static esp_err_t mcp23017_init(gpio_exp_t* self) {
	/*
	0111 x10x = same bank, mirrot single int, no sequentµial, open drain, active low
	not sure about this funny change of mapping of the control register itself, really?
	*/
	esp_err_t err = i2c_write(self->phy.port, self->phy.addr, 0x05, 0x74, 1);
	err |= i2c_write(self->phy.port, self->phy.addr, 0x0a, 0x74, 1);

	// no interrupt on comparison or on change
	err |= i2c_write(self->phy.port, self->phy.addr, 0x04, 0x00, 2);
	err |= i2c_write(self->phy.port, self->phy.addr, 0x08, 0x00, 2);

	return err;
}

static void mcp23017_set_direction(gpio_exp_t* self) {
	// default to input and set real input to generate interrupt
	i2c_write(self->phy.port, self->phy.addr, 0x00, ~self->w_mask, 2);
	i2c_write(self->phy.port, self->phy.addr, 0x04, self->r_mask, 2);
}

static void mcp23017_set_pull_mode(gpio_exp_t* self) {
	i2c_write(self->phy.port, self->phy.addr, 0x0c, self->pullup, 2);
}

static uint32_t mcp23017_read(gpio_exp_t* self) {
	// read the pins value, not the stored one @interrupt
	return i2c_read(self->phy.port, self->phy.addr, 0x12, 2);
}

static void mcp23017_write(gpio_exp_t* self) {
	i2c_write(self->phy.port, self->phy.addr, 0x12, self->shadow, 2);
}

/****************************************************************************************
 * MCP23s17 family : init, direction, read and write
 */
static esp_err_t mcp23s17_init(gpio_exp_t* self) {
	if ((self->spi_handle = spi_config(&self->phy)) == NULL) return ESP_ERR_INVALID_ARG;
	
	/*
	0111 x10x = same bank, mirrot single int, no sequentµial, open drain, active low
	not sure about this funny change of mapping of the control register itself, really?
	*/
	esp_err_t err = spi_write(self->spi_handle, self->phy.addr, 0x05, 0x74, 1);
	err |= spi_write(self->spi_handle, self->phy.addr, 0x0a, 0x74, 1);

	// no interrupt on comparison or on change
	err |= spi_write(self->spi_handle, self->phy.addr, 0x04, 0x00, 2);
	err |= spi_write(self->spi_handle, self->phy.addr, 0x08, 0x00, 2);

	return err;
}

static void mcp23s17_set_direction(gpio_exp_t* self) {
	// default to input and set real input to generate interrupt
	spi_write(self->spi_handle, self->phy.addr, 0x00, ~self->w_mask, 2);
	spi_write(self->spi_handle, self->phy.addr, 0x04, self->r_mask, 2);
}

static void mcp23s17_set_pull_mode(gpio_exp_t* self) {
	spi_write(self->spi_handle, self->phy.addr, 0x0c, self->pullup, 2);
}

static uint32_t mcp23s17_read(gpio_exp_t* self) {
	// read the pins value, not the stored one @interrupt
	return spi_read(self->spi_handle, self->phy.addr, 0x12, 2);
}

static void mcp23s17_write(gpio_exp_t* self) {
	spi_write(self->spi_handle, self->phy.addr, 0x12, self->shadow, 2);
}

/***************************************************************************************
                                     I2C low level                                   
***************************************************************************************/

/****************************************************************************************
 * I2C write up to 32 bits
 */
static esp_err_t i2c_write(uint8_t port, uint8_t addr, uint8_t reg, uint32_t data, int len) {
	i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
	
	i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, I2C_MASTER_NACK);
	if (reg != 0xff) i2c_master_write_byte(cmd, reg, I2C_MASTER_NACK);

	// works with our endianness
	if (len > 1) i2c_master_write(cmd, (uint8_t*) &data, len, I2C_MASTER_NACK);
	else i2c_master_write_byte(cmd, data, I2C_MASTER_NACK);
    
	i2c_master_stop(cmd);
	esp_err_t ret = i2c_master_cmd_begin(port, cmd, 100 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(cmd);
	
	if (ret != ESP_OK) {		
		ESP_LOGW(TAG, "I2C write failed");
	}
	
    return ret;
}

/****************************************************************************************
 * I2C read up to 32 bits
 */
static uint32_t i2c_read(uint8_t port, uint8_t addr, uint8_t reg, int len) {
	uint32_t data = 0;
	
	i2c_cmd_handle_t cmd = i2c_cmd_link_create();

    i2c_master_start(cmd);

	// when using a register, write it's value then the device address again
	if (reg != 0xff) {
		i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, I2C_MASTER_NACK);
		i2c_master_write_byte(cmd, reg, I2C_MASTER_NACK);
		i2c_master_start(cmd);
		i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_READ, I2C_MASTER_NACK);
	} else {
		i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_READ, I2C_MASTER_NACK);
	}
	
	// works with our endianness
	if (len > 1) i2c_master_read(cmd, (uint8_t*) &data, len, I2C_MASTER_LAST_NACK);
	else i2c_master_read_byte(cmd, (uint8_t*) &data, I2C_MASTER_NACK);
		
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(port, cmd, 100 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(cmd);
	
	if (ret != ESP_OK) {
		ESP_LOGW(TAG, "I2C read failed");
	}

	return data;
}

/***************************************************************************************
                                     SPI low level                                   
***************************************************************************************/

/****************************************************************************************
 * SPI device addition
 */
static spi_device_handle_t spi_config(struct gpio_exp_phy_s *phy) {
    spi_device_interface_config_t config = { };
    spi_device_handle_t handle = NULL;

	config.command_bits = config.address_bits = 8;
    config.clock_speed_hz = phy->speed ? phy->speed : SPI_MASTER_FREQ_8M;
    config.spics_io_num = phy->cs_pin;
    config.queue_size = 1;
	config.flags = SPI_DEVICE_NO_DUMMY;

    spi_bus_add_device( phy->host, &config, &handle );
	ESP_LOGI(TAG, "SPI expander initialized on host:%d with cs:%d and speed:%dHz", phy->host, phy->cs_pin, config.clock_speed_hz);

	return handle;
}

/****************************************************************************************
 * SPI write up to 32 bits
 */
static esp_err_t spi_write(spi_device_handle_t handle, uint8_t addr, uint8_t reg, uint32_t data, int len) {
    spi_transaction_t transaction = { };

	// rx_buffer is NULL, nothing to receive
	transaction.flags = SPI_TRANS_USE_TXDATA;
	transaction.cmd = addr << 1;
	transaction.addr = reg;
	transaction.tx_data[0] = data; transaction.tx_data[1] = data >> 8;
	transaction.length = len * 8;

	// only do polling as we don't have contention on SPI (otherwise DMA for transfers > 16 bytes)		
	return spi_device_polling_transmit(handle, &transaction);
}

/****************************************************************************************
 * SPI read up to 32 bits
 */
static uint32_t spi_read(spi_device_handle_t handle, uint8_t addr, uint8_t reg, int len) {
	spi_transaction_t *transaction = heap_caps_calloc(1, sizeof(spi_transaction_t), MALLOC_CAP_DMA);

	// tx_buffer is NULL, nothing to transmit except cmd/addr
	transaction->flags = SPI_TRANS_USE_RXDATA;
	transaction->cmd = (addr << 1) | 0x01;
	transaction->addr = reg;
	transaction->length = len * 8;

	// only do polling as we don't have contention on SPI (otherwise DMA for transfers > 16 bytes)		
	spi_device_polling_transmit(handle, transaction);
	uint32_t data = *(uint32_t*) transaction->rx_data;
	free(transaction);

	return data;
}