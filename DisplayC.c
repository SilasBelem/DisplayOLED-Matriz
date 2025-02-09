#include <stdio.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/gpio.h"
#include "hardware/uart.h"
#include "hardware/timer.h"
#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "pico/time.h"
#include "pico/bootrom.h"

#include "inc/ssd1306.h"
#include "inc/font.h"

#include "build/pio_matrix.pio.h" // Arquivo gerado pelo pioasm


#define I2C_PORT    i2c1
#define I2C_SDA     14
#define I2C_SCL     15
#define OLED_ADDR   0x3C
#define WIDTH       128
#define HEIGHT      64

// LEDs e botões
#define LED_G  11  // LED Verde
#define LED_B  12  // LED Azul
#define LED_R  13  // LED Vermelho (se quiser usar)
#define BTN_A  5
#define BTN_B  6

// WS2812
#define OUT_PIN    7
#define NUM_PIXELS 25

static volatile bool ledG_state = false; // Estado do LED verde
static volatile bool ledB_state = false; // Estado do LED azul
static volatile bool need_display_update = false; // Sinaliza ao loop principal para atualizar o display

// Debounce
static volatile uint32_t ultimo_tempo_a = 0;
static volatile uint32_t ultimo_tempo_b = 0;
const uint32_t debounce_delay_us = 200000; // 200 ms


static ssd1306_t ssd; // Instância do display


uint32_t matrix_rgb(double r, double g, double b) {
    // Ajuste de brilho, se desejar
    double brilho = 0.01; 
    unsigned char R = (unsigned char)(r * 255 * brilho);
    unsigned char G_ = (unsigned char)(g * 255 * brilho);
    unsigned char B_ = (unsigned char)(b * 255 * brilho);
    return ((uint32_t)(G_) << 24)
         | ((uint32_t)(R)  << 16)
         | ((uint32_t)(B_) <<  8);
}

// Mostra um dígito [0..9] na matriz 5x5 WS2812
void exibir_numero(PIO pio, uint sm, int numero) {
    static uint32_t numeros[10][NUM_PIXELS] = {
        // 0
        {0,1,1,1,0, 1,0,0,0,1, 1,0,0,0,1, 1,0,0,0,1, 0,1,1,1,0},
        // 1
        {0,1,1,1,0, 0,0,1,0,0, 0,0,1,0,0, 0,0,1,1,0, 0,0,1,0,0},
        // 2
        {1,1,1,1,1, 0,0,0,0,1, 0,1,1,1,0, 1,0,0,0,0, 1,1,1,1,1},
        // 3
        {1,1,1,1,1, 1,0,0,0,0, 0,1,1,1,0, 1,0,0,0,0, 1,1,1,1,1},
        // 4
        {0,0,0,0,1, 1,0,0,0,0, 1,1,1,1,1, 1,0,0,0,1, 1,0,0,0,1},
        // 5
        {1,1,1,1,1, 1,0,0,0,0, 1,1,1,1,1, 0,0,0,0,1, 1,1,1,1,1},
        // 6
        {1,1,1,1,1, 1,0,0,0,1, 1,1,1,1,1, 0,0,0,0,1, 1,1,1,1,1},
        // 7
        {1,0,0,0,0, 0,0,0,1,0, 0,0,1,0,0, 0,1,0,0,0, 1,1,1,1,1},
        // 8
        {1,1,1,1,1, 1,0,0,0,1, 1,1,1,1,1, 1,0,0,0,1, 1,1,1,1,1},
        // 9
        {1,1,1,1,1, 1,0,0,0,0, 1,1,1,1,1, 1,0,0,0,1, 1,1,1,1,1}
    };
    for (int i = 0; i < NUM_PIXELS; i++) {
        int mirrored_index = (i / 5) * 5 + (4 - (i % 5));
        uint32_t val = numeros[numero][mirrored_index];
        pio_sm_put_blocking(pio, sm, matrix_rgb(val, val, val));
    }
}


void gpio_callback(uint gpio, uint32_t events) {
    uint32_t agora = time_us_32();

    if (gpio == BTN_A) {
        // Verifica debounce
        if ((agora - ultimo_tempo_a) > debounce_delay_us) {
            ultimo_tempo_a = agora;
            // Alterna estado do LED Verde
            ledG_state = !ledG_state;
            gpio_put(LED_G, ledG_state);

            // Mensagem no Serial
            printf("Botao A pressionado. LED Verde %s\n", ledG_state ? "LIGADO" : "DESLIGADO");
            // Sinaliza que precisamos atualizar o display no loop principal
            need_display_update = true;
        }
    }
    else if (gpio == BTN_B) {
        // Verifica debounce
        if ((agora - ultimo_tempo_b) > debounce_delay_us) {
            ultimo_tempo_b = agora;
            // Alterna estado do LED Azul
            ledB_state = !ledB_state;
            gpio_put(LED_B, ledB_state);

            // Mensagem no Serial
            printf("Botao B pressionado. LED Azul %s\n", ledB_state ? "LIGADO" : "DESLIGADO");
            // Sinaliza atualização do display
            need_display_update = true;
        }
    }
}


void atualiza_display_com_informacoes(char digitado) {
    // Limpa a tela
    ssd1306_fill(&ssd, false);

    ssd1306_rect(&ssd, 3, 3, 122, 58, true, false);

    // Exibe caractere digitado
    char str[2] = {digitado, '\0'};
    ssd1306_draw_string(&ssd, str, 10, 10);

    // Mensagem sobre LED Verde
    if (ledG_state) {
        ssd1306_draw_string(&ssd, "LED Verde: ON ", 10, 30);
    } else {
        ssd1306_draw_string(&ssd, "LED Verde: OFF", 10, 30);
    }

    // Mensagem sobre LED Azul
    if (ledB_state) {
        ssd1306_draw_string(&ssd, "LED Azul : ON ", 10, 45);
    } else {
        ssd1306_draw_string(&ssd, "LED Azul : OFF", 10, 45);
    }

    // Envia tudo pro display
    ssd1306_send_data(&ssd);
}

int main() {
    stdio_init_all();


    i2c_init(I2C_PORT, 400 * 1000);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);

    ssd1306_init(&ssd, WIDTH, HEIGHT, false, OLED_ADDR, I2C_PORT);
    ssd1306_config(&ssd);
    ssd1306_send_data(&ssd);

    // Limpa o display
    ssd1306_fill(&ssd, false);
    ssd1306_send_data(&ssd);


    PIO pio = pio0;
    uint sm = pio_claim_unused_sm(pio, true);
    uint offset = pio_add_program(pio, &pio_matrix_program);
    pio_matrix_program_init(pio, sm, offset, OUT_PIN);


    for(int i=0; i<NUM_PIXELS; i++){
        pio_sm_put_blocking(pio, sm, matrix_rgb(0,0,0));
    }

    gpio_init(LED_G);
    gpio_set_dir(LED_G, GPIO_OUT);
    gpio_put(LED_G, ledG_state);

    gpio_init(LED_B);
    gpio_set_dir(LED_B, GPIO_OUT);
    gpio_put(LED_B, ledB_state);

    // Se quiser usar o LED_R, inicialize também:
    gpio_init(LED_R);
    gpio_set_dir(LED_R, GPIO_OUT);
    gpio_put(LED_R, 0); // Desligado

    // Botões:
    gpio_init(BTN_A);
    gpio_set_dir(BTN_A, GPIO_IN);
    gpio_pull_up(BTN_A);

    gpio_init(BTN_B);
    gpio_set_dir(BTN_B, GPIO_IN);
    gpio_pull_up(BTN_B);

    // Habilitar interrupções
    gpio_set_irq_enabled_with_callback(BTN_A, GPIO_IRQ_EDGE_FALL, true, &gpio_callback);
    gpio_set_irq_enabled(BTN_B, GPIO_IRQ_EDGE_FALL, true);


    while (true) {
        int ch = getchar_timeout_us(0);
        if (ch != PICO_ERROR_TIMEOUT && ch != EOF) {
            char c = (char)ch;
            printf("digitado: %c\n", c);

            if (c >= '0' && c <= '9') {
                int num = c - '0';
                exibir_numero(pio, sm, num);
            }

            atualiza_display_com_informacoes(c);
        }

        if (need_display_update) {
            need_display_update = false;
            atualiza_display_com_informacoes(' '); 
        }

        sleep_ms(50);
    }

    return 0;
}