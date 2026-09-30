/* battery_port_adc.c — the Waveshare 1.69in watch: no PMIC, only the cell's
 * voltage through a 200k / 100k divider on GPIO1 (ADC1 channel 0), so VBAT is
 * three times what the pin reads.
 *
 * battery_port_init says false, as a board with no PMIC must: that is what makes
 * BOOT the sleep key and leaves power-off to deep sleep (main.c's s_pmic). The
 * meter still shows, because battery_port_read answers. Charge comes from the
 * same resting-voltage curve XR TAK uses on this board, so the two apps agree.
 *
 * There is no charge-status line. USB with a host on the other end is the one
 * thing the chip can see, so that stands for "on the cable": charging below the
 * top of the curve, full at it. A plain wall charger reads as on battery. */
#include "battery_port.h"
#include "board_pins.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "driver/usb_serial_jtag.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "battery";
#define DIVIDER      3.0f
#define SAMPLES      8
#define READ_EVERY_US 1000000
/* How far the reading moves towards a new one each second: the backlight and the
   radio pull the cell down in bursts, and the gauge should not flicker with them. */
#define SMOOTHING    0.2f

static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_cali;
static bool s_ready;
static float s_volts;                 /* smoothed; 0 before the first read */
static int64_t s_read_us;

/* Resting voltage against charge for one lithium cell, highest first: XR TAK's table. */
static const struct { float volts; int percent; } CURVE[] = {
    {4.20f, 100}, {4.10f, 90}, {4.00f, 80}, {3.90f, 65}, {3.80f, 50},
    {3.75f, 40},  {3.70f, 30}, {3.65f, 20}, {3.60f, 12}, {3.50f, 5}, {3.30f, 0},
};

static float percent_of(float volts) {
    const int n = sizeof CURVE / sizeof CURVE[0];
    if (volts >= CURVE[0].volts) return 100;
    if (volts <= CURVE[n - 1].volts) return 0;
    for (int i = 1; i < n; i++) {
        if (volts >= CURVE[i].volts) {
            float along = (volts - CURVE[i].volts) / (CURVE[i - 1].volts - CURVE[i].volts);
            return CURVE[i].percent + along * (CURVE[i - 1].percent - CURVE[i].percent);
        }
    }
    return 0;
}

static void sample(void) {
    int64_t now = esp_timer_get_time();
    if (s_volts > 0 && now - s_read_us < READ_EVERY_US) return;
    s_read_us = now;
    int total = 0, got = 0;
    for (int i = 0; i < SAMPLES; i++) {
        int raw, mv;
        if (adc_oneshot_read(s_adc, ADC_CHANNEL_0, &raw) != ESP_OK) continue;
        if (s_cali && adc_cali_raw_to_voltage(s_cali, raw, &mv) == ESP_OK) { total += mv; got++; }
    }
    if (!got) return;
    float volts = (float)total / got / 1000.0f * DIVIDER;
    s_volts = s_volts <= 0 ? volts : s_volts + (volts - s_volts) * SMOOTHING;
}

bool battery_port_init(i2c_master_bus_handle_t bus) {
    (void)bus;
    adc_oneshot_unit_init_cfg_t unit = { .unit_id = ADC_UNIT_1 };
    if (adc_oneshot_new_unit(&unit, &s_adc) != ESP_OK) { ESP_LOGW(TAG, "no ADC"); return false; }
    adc_oneshot_chan_cfg_t chan = { .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT };
    adc_oneshot_config_channel(s_adc, ADC_CHANNEL_0, &chan);   /* GPIO1 */
    adc_cali_curve_fitting_config_t cal = { .unit_id = ADC_UNIT_1, .chan = ADC_CHANNEL_0,
                                            .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT };
    if (adc_cali_create_scheme_curve_fitting(&cal, &s_cali) != ESP_OK) s_cali = NULL;
    s_ready = s_cali != NULL;
    sample();
    ESP_LOGI(TAG, "cell on GPIO%d: %.2f V, %.0f%% (no PMIC: BOOT sleeps)", PIN_BATTERY_ADC, s_volts, percent_of(s_volts));
    return false;                     /* no PMIC, whatever the meter says */
}

bool battery_port_read(float *frac, bool *charging) {
    if (!s_ready) return false;
    sample();
    if (s_volts < 2.5f) return false;  /* nothing there to measure: hide the meter */
    *frac = percent_of(s_volts) / 100.0f;
    *charging = battery_port_state() == BAT_CHARGING;
    return true;
}

int battery_port_state(void) {
    if (!usb_serial_jtag_is_connected()) return BAT_ON_BATTERY;
    return s_volts >= 4.15f ? BAT_FULL : BAT_CHARGING;
}

int battery_port_vbat_mv(void) { sample(); return (int)(s_volts * 1000 + 0.5f); }

/* Nothing to do on a board with no PMIC. */
bool battery_port_poweroff(void) { return false; }
void battery_port_key_init(void) {}
int  battery_port_key_poll(void) { return 0; }
void battery_port_key_trace(int seconds) { (void)seconds; ESP_LOGW(TAG, "no PWR key on this board"); }
void battery_port_dump(void) { ESP_LOGI(TAG, "%.3f V, %.1f%%, %s", s_volts, percent_of(s_volts), battery_port_state() == BAT_ON_BATTERY ? "on battery" : "on USB"); }
bool battery_port_set_rail(const char *name, bool on) { (void)name; (void)on; return false; }
void battery_port_trim_rails(void) {}
