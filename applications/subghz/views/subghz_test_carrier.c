#include "subghz_test_carrier.h"
#include "../subghz_i.h"
#include "../helpers/subghz_testing.h"

#include <math.h>
#include <furi.h>
#include <furi-hal.h>
#include <input/input.h>

struct SubghzTestCarrier {
    View* view;
    osTimerId_t timer;
    SubghzTestCarrierCallback callback;
    void* context;
};

typedef enum {
    SubghzTestCarrierModelStatusRx,
    SubghzTestCarrierModelStatusTx,
} SubghzTestCarrierModelStatus;

typedef struct {
    uint8_t frequency;
    uint32_t real_frequency;
    FuriHalSubGhzPath path;
    float rssi;
    SubghzTestCarrierModelStatus status;
} SubghzTestCarrierModel;

void subghz_test_carrier_set_callback(
    SubghzTestCarrier* subghz_test_carrier,
    SubghzTestCarrierCallback callback,
    void* context) {
    furi_assert(subghz_test_carrier);
    furi_assert(callback);
    subghz_test_carrier->callback = callback;
    subghz_test_carrier->context = context;
}

void subghz_test_carrier_draw(Canvas* canvas, SubghzTestCarrierModel* model) {
    char buffer[64];

    canvas_set_color(canvas, ColorBlack);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 0, 8, "CC1101 Basic Test");

    canvas_set_font(canvas, FontSecondary);
    // Frequency
    snprintf(
        buffer,
        sizeof(buffer),
        "Freq: %03ld.%03ld.%03ld Hz",
        model->real_frequency / 1000000 % 1000,
        model->real_frequency / 1000 % 1000,
        model->real_frequency % 1000);
    canvas_draw_str(canvas, 0, 20, buffer);
    // Path
    char* path_name = "Unknown";
    if(model->path == FuriHalSubGhzPathIsolate) {
        path_name = "isolate";
    } else if(model->path == FuriHalSubGhzPath433) {
        path_name = "433MHz";
    } else if(model->path == FuriHalSubGhzPath315) {
        path_name = "315MHz";
    } else if(model->path == FuriHalSubGhzPath868) {
        path_name = "868MHz";
    }
    snprintf(buffer, sizeof(buffer), "Path: %d - %s", model->path, path_name);
    canvas_draw_str(canvas, 0, 31, buffer);
    if(model->status == SubghzTestCarrierModelStatusRx) {
        snprintf(
            buffer,
            sizeof(buffer),
            "RSSI: %ld.%ld dBm",
            (int32_t)(model->rssi),
            (int32_t)fabs(model->rssi * 10) % 10);
        canvas_draw_str(canvas, 0, 42, buffer);
    } else {
        canvas_draw_str(canvas, 0, 42, "TX");
    }
}

bool subghz_test_carrier_input(InputEvent* event, void* context) {
    furi_assert(context);
    SubghzTestCarrier* subghz_test_carrier = context;

    if(event->key == InputKeyBack || event->type != InputTypeShort) {
        return false;
    }

    with_view_model(
        subghz_test_carrier->view, (SubghzTestCarrierModel * model) {
            furi_hal_subghz_idle();

            if(event->key == InputKeyLeft) {
                if(model->frequency > 0) model->frequency--;
            } else if(event->key == InputKeyRight) {
                if(model->frequency < subghz_frequencies_count_testing - 1) model->frequency++;
            } else if(event->key == InputKeyDown) {
                if(model->path > 0) model->path--;
            } else if(event->key == InputKeyUp) {
                if(model->path < FuriHalSubGhzPath868) model->path++;
            } else if(event->key == InputKeyOk) {
                if(model->status == SubghzTestCarrierModelStatusTx) {
                    model->status = SubghzTestCarrierModelStatusRx;
                } else {
                    model->status = SubghzTestCarrierModelStatusTx;
                }
            }

            model->real_frequency =
                furi_hal_subghz_set_frequency(subghz_frequencies_testing[model->frequency]);
            furi_hal_subghz_set_path(model->path);

            if(model->status == SubghzTestCarrierModelStatusRx) {
                hal_gpio_init(&gpio_cc1101_g0, GpioModeInput, GpioPullNo, GpioSpeedLow);
                furi_hal_subghz_rx();
            } else {
                hal_gpio_init(&gpio_cc1101_g0, GpioModeOutputPushPull, GpioPullNo, GpioSpeedLow);
                hal_gpio_write(&gpio_cc1101_g0, true);
                if(!furi_hal_subghz_tx()) {
                    hal_gpio_init(&gpio_cc1101_g0, GpioModeInput, GpioPullNo, GpioSpeedLow);
                    subghz_test_carrier->callback(
                        SubghzTestCarrierEventOnlyRx, subghz_test_carrier->context);
                }
            }

            return true;
        });

    return true;
}

void subghz_test_carrier_enter(void* context) {
    furi_assert(context);
    SubghzTestCarrier* subghz_test_carrier = context;

    furi_hal_subghz_reset();
    furi_hal_subghz_load_preset(FuriHalSubGhzPresetOok650Async);

    hal_gpio_init(&gpio_cc1101_g0, GpioModeInput, GpioPullNo, GpioSpeedLow);

    with_view_model(
        subghz_test_carrier->view, (SubghzTestCarrierModel * model) {
            model->frequency = subghz_frequencies_433_92_testing; // 433
            model->real_frequency =
                furi_hal_subghz_set_frequency(subghz_frequencies_testing[model->frequency]);
            model->path = FuriHalSubGhzPathIsolate; // isolate
            model->rssi = 0.0f;
            model->status = SubghzTestCarrierModelStatusRx;
            return true;
        });

    furi_hal_subghz_rx();

    osTimerStart(subghz_test_carrier->timer, osKernelGetTickFreq() / 4);
}

void subghz_test_carrier_exit(void* context) {
    furi_assert(context);
    SubghzTestCarrier* subghz_test_carrier = context;

    osTimerStop(subghz_test_carrier->timer);

    // Reinitialize IC to default state
    furi_hal_subghz_sleep();
}

void subghz_test_carrier_rssi_timer_callback(void* context) {
    furi_assert(context);
    SubghzTestCarrier* subghz_test_carrier = context;

    with_view_model(
        subghz_test_carrier->view, (SubghzTestCarrierModel * model) {
            if(model->status == SubghzTestCarrierModelStatusRx) {
                model->rssi = furi_hal_subghz_get_rssi();
                return true;
            }
            return false;
        });
}

SubghzTestCarrier* subghz_test_carrier_alloc() {
    SubghzTestCarrier* subghz_test_carrier = furi_alloc(sizeof(SubghzTestCarrier));

    // View allocation and configuration
    subghz_test_carrier->view = view_alloc();
    view_allocate_model(
        subghz_test_carrier->view, ViewModelTypeLocking, sizeof(SubghzTestCarrierModel));
    view_set_context(subghz_test_carrier->view, subghz_test_carrier);
    view_set_draw_callback(subghz_test_carrier->view, (ViewDrawCallback)subghz_test_carrier_draw);
    view_set_input_callback(subghz_test_carrier->view, subghz_test_carrier_input);
    view_set_enter_callback(subghz_test_carrier->view, subghz_test_carrier_enter);
    view_set_exit_callback(subghz_test_carrier->view, subghz_test_carrier_exit);

    subghz_test_carrier->timer = osTimerNew(
        subghz_test_carrier_rssi_timer_callback, osTimerPeriodic, subghz_test_carrier, NULL);

    return subghz_test_carrier;
}

void subghz_test_carrier_free(SubghzTestCarrier* subghz_test_carrier) {
    furi_assert(subghz_test_carrier);
    osTimerDelete(subghz_test_carrier->timer);
    view_free(subghz_test_carrier->view);
    free(subghz_test_carrier);
}

View* subghz_test_carrier_get_view(SubghzTestCarrier* subghz_test_carrier) {
    furi_assert(subghz_test_carrier);
    return subghz_test_carrier->view;
}
