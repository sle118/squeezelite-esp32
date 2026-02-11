/*
 *  a crude button press/long-press/shift management based on GPIO
 *
 *  (c) Philippe G. 2019, philippe_44@outlook.com
 *
 *  This software is released under the MIT License.
 *  https://opensource.org/licenses/MIT
 *
 */
#define LOG_LOCAL_LEVEL ESP_LOG_DEBUG
#include "buttons.h"
#include "driver/gpio.h"
#include "driver/rmt.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_task.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "globdefs.h"
#include "gpio_exp.h"
#include "rotary_encoder.h"
#include "services.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char* TAG = "buttons";

static EXT_RAM_ATTR int n_buttons;
static EXT_RAM_ATTR uint32_t buttons_idle_since;

#define BUTTON_STACK_SIZE 4096
#define MAX_BUTTONS 32
#define DEBOUNCE 50
#define BUTTON_QUEUE_LEN 10

static EXT_RAM_ATTR struct button_s {
    void* client;
    int gpio;
    int debounce;
    button_handler handler;
    struct button_s *self, *shifter;
    int shifter_gpio; // this one is just for post-creation
    int long_press;
    bool long_timer, shifted, shifting;
    int type, level;
    TimerHandle_t timer;
} buttons[MAX_BUTTONS];

// can't use EXT_RAM_ATTR for initialized structure
static struct {
    int gpio, level;
    struct button_s* button;
} polled_gpio[] = {{36, -1, NULL}, {39, -1, NULL}, {-1, -1, NULL}};

static TimerHandle_t polled_timer;

static EXT_RAM_ATTR struct {
    QueueHandle_t queue;
    void* client;
    rotary_encoder_info_t info;
    int A, B, SW;
    rotary_handler handler;
} rotary;

static EXT_RAM_ATTR struct {
    RingbufHandle_t rb;
    infrared_handler handler;
} infrared;

static EXT_RAM_ATTR QueueHandle_t button_queue;
static EXT_RAM_ATTR QueueSetHandle_t common_queue_set;

static void buttons_task(void* arg);
static void buttons_handler(struct button_s* button, int level);

/****************************************************************************************
 * Start task needed by button,s rotaty and infrared
 */
static void common_task_init(void) {
    static DRAM_ATTR StaticTask_t xTaskBuffer __attribute__((aligned(4)));
    static EXT_RAM_ATTR StackType_t xStack[BUTTON_STACK_SIZE] __attribute__((aligned(4)));

    if (!common_queue_set) {
		ESP_LOGD(TAG,"Creating buttons task with a queue set length of %d",BUTTON_QUEUE_LEN+1);
        common_queue_set = xQueueCreateSet(BUTTON_QUEUE_LEN + 1);
        xTaskCreateStatic((TaskFunction_t)buttons_task, "buttons", BUTTON_STACK_SIZE, NULL, ESP_TASK_PRIO_MIN + 2, xStack, &xTaskBuffer);
    }
}

/****************************************************************************************
 * GPIO low-level ISR handler
 */
static void IRAM_ATTR gpio_isr_handler(void* arg) {
    struct button_s* button = (struct button_s*)arg;
    BaseType_t woken = pdFALSE;

    if (xTimerGetPeriod(button->timer) > pdMS_TO_TICKS(button->debounce)) {
        if (button->gpio < GPIO_NUM_MAX)
            xTimerChangePeriodFromISR(button->timer, pdMS_TO_TICKS(button->debounce), &woken);
        else
            xTimerChangePeriod(button->timer, pdMS_TO_TICKS(button->debounce), pdMS_TO_TICKS(10));
    } else {
        if (button->gpio < GPIO_NUM_MAX)
            xTimerResetFromISR(button->timer, &woken);
        else
            xTimerReset(button->timer, portMAX_DELAY);
    }

    if (woken) portYIELD_FROM_ISR();

    ESP_EARLY_LOGD(TAG, "INT gpio %u level %u", button->gpio, button->level);
}

/****************************************************************************************
 * Buttons debounce/longpress timer
 */
static void buttons_timer_handler(TimerHandle_t xTimer) {
    struct button_s* button = (struct button_s*)pvTimerGetTimerID(xTimer);
    // if this is an expanded GPIO, must give cache a chance
    buttons_handler(button, gpio_exp_get_level(button->gpio, (button->debounce * 3) / 2, NULL));
}

/****************************************************************************************
 * Buttons polling timer
 */
static void buttons_polling(TimerHandle_t xTimer) {
    for (int i = 0; polled_gpio[i].gpio != -1; i++) {
        if (!polled_gpio[i].button) continue;

        int level = gpio_get_level(polled_gpio[i].gpio);

        if (level != polled_gpio[i].level) {
            polled_gpio[i].level = level;
            buttons_handler(polled_gpio[i].button, level);
        }
    }
}

/****************************************************************************************
 * Buttons timer handler for press/longpress
 */
static void buttons_handler(struct button_s* button, int level) {
    button->level = level;

    if (button->shifter && button->shifter->type == button->shifter->level) button->shifter->shifting = true;

    if (button->long_press && !button->long_timer && button->level == button->type) {
        // detect a long press, so hold event generation
        ESP_LOGD(TAG, "setting long timer gpio:%u level:%u", button->gpio, button->level);
        xTimerChangePeriod(button->timer, button->long_press / portTICK_PERIOD_MS, 0);
        button->long_timer = true;
    } else {
        // send a button pressed/released event (content is copied in queue)
        ESP_LOGD(TAG, "sending event for gpio:%u level:%u", button->gpio, button->level);
        // queue will have a copy of button's context
        xQueueSend(button_queue, button, 0);
        button->long_timer = false;
    }
}

/****************************************************************************************
 * Get inactivity callback
 */
static uint32_t buttons_idle_callback(void) { return pdTICKS_TO_MS(xTaskGetTickCount()) - buttons_idle_since; }

/****************************************************************************************
 * Tasks that calls the appropriate functions when buttons are pressed
 */
static void buttons_task(void* arg) {
    ESP_LOGI(TAG, "starting button tasks");

    buttons_idle_since = pdTICKS_TO_MS(xTaskGetTickCount());
    services_sleep_setsleeper(buttons_idle_callback);

    while (1) {
        QueueSetMemberHandle_t xActivatedMember;
        bool active = true;

        // wait on button, rotary and infrared queues
        if ((xActivatedMember = xQueueSelectFromSet(common_queue_set, portMAX_DELAY)) == NULL) continue;

        if (xActivatedMember == button_queue) {
            struct button_s button;
            button_event_e event;
            button_press_e press;

            // received a button event
            xQueueReceive(button_queue, &button, 0);

            event = (button.level == button.type) ? BUTTON_PRESSED : BUTTON_RELEASED;

            ESP_LOGD(TAG, "received event:%u from gpio:%u level:%u (timer %u shifting %u)", event, button.gpio, button.level, button.long_timer,
                button.shifting);

            // find if shifting is activated
            if (button.shifter && button.shifter->type == button.shifter->level)
                press = BUTTON_SHIFTED;
            else
                press = BUTTON_NORMAL;

            /*
            long_timer will be set either because we truly have a long press
            or we have a release before the long press timer elapsed, so two
            events shall be sent
            */
            if (button.long_timer) {
                if (event == BUTTON_RELEASED) {
                    // early release of a long-press button, send press/release
                    if (!button.shifting) {
                        button.handler(button.client, BUTTON_PRESSED, press, false);
                        button.handler(button.client, BUTTON_RELEASED, press, false);
                    }
                    // button is a copy, so need to go to real context
                    button.self->shifting = false;
                } else if (!button.shifting) {
                    // normal long press and not shifting so don't discard
                    button.handler(button.client, BUTTON_PRESSED, press, true);
                }
            } else {
                // normal press/release of a button or release of a long-press button
                if (!button.shifting) button.handler(button.client, event, press, button.long_press);
                // button is a copy, so need to go to real context
                button.self->shifting = false;
            }
        } else if (xActivatedMember == rotary.queue) {
            rotary_encoder_event_t event = {0};

            // received a rotary event
            xQueueReceive(rotary.queue, &event, 0);

            ESP_LOGD(TAG, "Event: position %d, direction %s", event.state.position,
                event.state.direction ? (event.state.direction == ROTARY_ENCODER_DIRECTION_CLOCKWISE ? "CW" : "CCW") : "NOT_SET");

            rotary.handler(rotary.client, event.state.direction == ROTARY_ENCODER_DIRECTION_CLOCKWISE ? ROTARY_RIGHT : ROTARY_LEFT, false);
        } else {
            // this is IR
            active = infrared_receive(infrared.rb, infrared.handler);
        }

        // mark the last activity
        if (active) buttons_idle_since = pdTICKS_TO_MS(xTaskGetTickCount());
    }
}

/****************************************************************************************
 * dummy button handler
 */
void dummy_handler(void* id, button_event_e event, button_press_e press) { ESP_LOGW(TAG, "should not be here"); }

/****************************************************************************************
 * Create buttons
 */
void button_create(void* client, int gpio, int type, bool pull, int debounce, button_handler handler, int long_press, int shifter_gpio) {
    if (n_buttons >= MAX_BUTTONS) return;

    ESP_LOGI(TAG, "Creating button using GPIO %u, type %s, %s pull-up/down, long press %u shifter %d", gpio,
        sys_gpio_lvl_name(type == 0 ? sys_gpio_lvl_LOW : sys_gpio_lvl_HIGH), pull?"with":"without", long_press, shifter_gpio);

    if (!n_buttons) {
		ESP_LOGD(TAG,"Creating new buttton message queue with a length of %d entries",BUTTON_QUEUE_LEN);
        button_queue = xQueueCreate(BUTTON_QUEUE_LEN, sizeof(struct button_s));
        common_task_init();
        xQueueAddToSet(button_queue, common_queue_set);
    }

    // just in case this structure is allocated in a future release
    memset(buttons + n_buttons, 0, sizeof(struct button_s));

    // set mandatory parameters
    buttons[n_buttons].client = client;
    buttons[n_buttons].gpio = gpio;
    buttons[n_buttons].debounce = debounce ? debounce : DEBOUNCE;
    buttons[n_buttons].handler = handler;
    buttons[n_buttons].long_press = long_press;
    buttons[n_buttons].shifter_gpio = shifter_gpio;
    buttons[n_buttons].type = type;
    buttons[n_buttons].timer =
        xTimerCreate("buttonTimer", buttons[n_buttons].debounce / portTICK_PERIOD_MS, pdFALSE, (void*)&buttons[n_buttons], buttons_timer_handler);
    buttons[n_buttons].self = buttons + n_buttons;

    for (int i = 0; i < n_buttons; i++) {
        // first try to find our shifter
        if (buttons[i].gpio == shifter_gpio) {
            buttons[n_buttons].shifter = buttons + i;
            // a shifter must have a long-press handler
            if (!buttons[i].long_press) buttons[i].long_press = -1;
        }
        // then try to see if we are a non-assigned shifter
        if (buttons[i].shifter_gpio == gpio) {
            buttons[i].shifter = buttons + n_buttons;
            ESP_LOGI(TAG, "post-assigned shifter gpio %u", buttons[i].gpio);
        }
    }

    gpio_pad_select_gpio_x(gpio);
    gpio_set_direction_x(gpio, GPIO_MODE_INPUT);

    // do we need pullup or pulldown
    if (pull) {
        if (GPIO_IS_VALID_OUTPUT_GPIO(gpio) || gpio >= GPIO_NUM_MAX) {
            if (type == BUTTON_LOW)
                gpio_set_pull_mode_x(gpio, GPIO_PULLUP_ONLY);
            else
                gpio_set_pull_mode_x(gpio, GPIO_PULLDOWN_ONLY);
        } else {
            ESP_LOGW(TAG, "cannot set pull up/down for gpio %u", gpio);
        }
    }

    // and initialize level ...
    buttons[n_buttons].level = gpio_get_level_x(gpio);

    // nasty ESP32 bug: fire-up constantly INT on GPIO 36/39 if ADC1, AMP, Hall used which WiFi does when PS is activated
    for (int i = 0; polled_gpio[i].gpio != -1; i++)
        if (polled_gpio[i].gpio == gpio) {
            if (!polled_timer) {
                polled_timer = xTimerCreate("buttonsPolling", 100 / portTICK_PERIOD_MS, pdTRUE, polled_gpio, buttons_polling);
                xTimerStart(polled_timer, portMAX_DELAY);
            }

            polled_gpio[i].button = buttons + n_buttons;
            polled_gpio[i].level = gpio_get_level(gpio);
            ESP_LOGW(TAG, "creating polled gpio %u, level %u", gpio, polled_gpio[i].level);

            gpio = -1;
            break;
        }

    // only create ISR if this is not a polled gpio
    if (gpio != -1) {
        // we need any edge detection
        gpio_set_intr_type_x(gpio, GPIO_INTR_ANYEDGE);
        gpio_isr_handler_add_x(gpio, gpio_isr_handler, buttons + n_buttons);
        gpio_intr_enable_x(gpio);
    }

    n_buttons++;
}

/****************************************************************************************
 * Get stored id
 */
void* button_get_client(int gpio) {
    for (int i = 0; i < n_buttons; i++) {
        if (buttons[i].gpio == gpio) return buttons[i].client;
    }
    return NULL;
}

/****************************************************************************************
 * Get stored id
 */
bool button_is_pressed(int gpio, void* client) {
    for (int i = 0; i < n_buttons; i++) {
        if (gpio != -1 && buttons[i].gpio == gpio)
            return buttons[i].level == buttons[i].type;
        else if (client && buttons[i].client == client)
            return buttons[i].level == buttons[i].type;
    }
    return false;
}

/****************************************************************************************
 * Update buttons
 */
void* button_remap(void* client, int gpio, button_handler handler, int long_press, int shifter_gpio) {
    int i;
    struct button_s* button = NULL;
    void* prev_client;

    ESP_LOGI(TAG, "remapping GPIO %u, long press %u shifter %u", gpio, long_press, shifter_gpio);

    // find button
    for (i = 0; i < n_buttons; i++) {
        if (buttons[i].gpio == gpio) {
            button = buttons + i;
            break;
        }
    }

    // don't know what we are doing here
    if (!button) return NULL;

    prev_client = button->client;
    button->client = client;
    button->handler = handler;
    button->long_press = long_press;
    button->shifter_gpio = shifter_gpio;

    // find our shifter	(if any)
    for (i = 0; shifter_gpio != -1 && i < n_buttons; i++) {
        if (buttons[i].gpio == shifter_gpio) {
            button->shifter = buttons + i;
            // a shifter must have a long-press handler
            if (!buttons[i].long_press) buttons[i].long_press = -1;
            break;
        }
    }

    return prev_client;
}

/****************************************************************************************
 * Rotary encoder handler
 */
static void rotary_button_handler(void* id, button_event_e event, button_press_e mode, bool long_press) {
    ESP_LOGI(TAG, "Rotary push-button %d", event);
    rotary.handler(id, event == BUTTON_PRESSED ? ROTARY_PRESSED : ROTARY_RELEASED, long_press);
}

/****************************************************************************************
 * Create rotary encoder
 */
bool create_rotary(void* id, int A, int B, int SW, int long_press, rotary_handler handler) {
    // nasty ESP32 bug: fire-up constantly INT on GPIO 36/39 if ADC1, AMP, Hall used which WiFi does when PS is activated
    if (A == -1 || B == -1 || A == 36 || A == 39 || B == 36 || B == 39) {
        ESP_LOGI(TAG, "Cannot create rotary %d %d", A, B);
        return false;
    }

    rotary.A = A;
    rotary.B = B;
    rotary.SW = SW;
    rotary.client = id;
    rotary.handler = handler;

    // Initialise the rotary encoder device with the GPIOs for A and B signals
    rotary_encoder_init(&rotary.info, A, B);

    // Create a queue for events from the rotary encoder driver.
    rotary.queue = rotary_encoder_create_queue();
    rotary_encoder_set_queue(&rotary.info, rotary.queue);

    common_task_init();
    xQueueAddToSet(rotary.queue, common_queue_set);

    // create companion button if rotary has a switch
    if (SW != -1) button_create(id, SW, BUTTON_LOW, true, 0, rotary_button_handler, long_press, -1);

    ESP_LOGI(TAG, "Created rotary encoder A:%d B:%d, SW:%d", A, B, SW);

    return true;
}

/****************************************************************************************
 * Create Infrared
 */
bool create_infrared(int gpio, infrared_handler handler, infrared_mode_t mode) {
    // initialize IR infrastructure
    infrared_init(&infrared.rb, gpio, mode);
    infrared.handler = handler;

    // join the queue set
    common_task_init();
    xRingbufferAddToQueueSetRead(infrared.rb, common_queue_set);

    ESP_LOGI(TAG, "Created infrared receiver using GPIO %u", gpio);

    return (infrared.rb != NULL);
}
