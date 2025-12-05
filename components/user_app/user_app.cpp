#include <stdio.h>
#include "user_app.h"
#include "i2c_bsp.h"
#include "user_config.h"
#include "sdcard_bsp.h"
#include "freertos/FreeRTOS.h"
#include "adc_bsp.h"
#include "button_bsp.h"
#include "user_audio_bsp.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "i2c_equipment.h"
#include "sdkconfig.h"

#if 0
/* Bluetooth and Wi-Fi removed from app: includes intentionally disabled */
#endif

/* IO expander (TCA9554) is I2C-based. Include the expander headers when either
   the full I2C equipment feature is enabled or the power-control-only option
   is enabled. Otherwise forward-declare the handle to avoid unresolved types. */
#if CONFIG_I2C_EQUIPMENT_ENABLED || CONFIG_POWER_CTRL_ENABLED
#include "esp_io_expander_tca9554.h"
#else
typedef void* esp_io_expander_handle_t;
#endif

#include "gui_guider.h"

#include "driver/gpio.h"

static const char *TAG_USER_APP = "user_app";

lv_ui user_ui;

static esp_io_expander_handle_t io_expander = NULL;
extern void setBrightnes(uint8_t brig);

// MTG counters state
static int mtg_life = 40;
static int pt_power = 0;
static int pt_tough = 0;

// Helper: hide flash overlay after timer
static void hide_flash_timer_cb(lv_timer_t * t)
{
  lv_obj_t * obj = (lv_obj_t *)t->user_data;
  if(obj) lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
  lv_timer_del(t);
}

// Transient life delta label (shows "+1" or "-1" under the life counter for 5s)
static lv_obj_t * life_delta_label = NULL;
static lv_timer_t * life_delta_timer = NULL;
// running cumulative delta shown while user is actively tapping; resets after timeout
static int life_delta_total = 0;

/* Commander damage screen: four quadrant totals and a modal per-quadrant editor */
static int commander_totals[4] = {0, 0, 0, 0};
static lv_obj_t * commander_element = NULL; /* carousel element container */
static lv_obj_t * commander_quad_labels[4] = {NULL, NULL, NULL, NULL};

/* Modal editor state */
static lv_obj_t * commander_modal = NULL;
static lv_obj_t * commander_modal_label = NULL;
static lv_obj_t * commander_modal_delta_label = NULL;
static lv_timer_t * commander_modal_timer = NULL;
static int commander_modal_total = 0; /* running total inside modal */
static int commander_modal_delta = 0; /* running delta inside modal */
static int commander_modal_idx = -1; /* which quadrant is being edited */

/* forward declare the modal event handler (defined later) so open_commander_modal can use it */
static void commander_modal_event_cb(lv_event_t * e);

static void commander_modal_hide_cb(lv_timer_t * t)
{
  (void)t;
  if (commander_modal) lv_obj_add_flag(commander_modal, LV_OBJ_FLAG_HIDDEN);
  if (commander_modal_timer) { lv_timer_del(commander_modal_timer); commander_modal_timer = NULL; }
  /* commit the edited total back to the quadrant and update its label */
  if (commander_modal_idx >= 0 && commander_modal_idx < 4) {
    commander_totals[commander_modal_idx] = commander_modal_total;
    if (commander_quad_labels[commander_modal_idx]) {
      char buf[16]; snprintf(buf, sizeof(buf), "%d", commander_totals[commander_modal_idx]);
      lv_label_set_text(commander_quad_labels[commander_modal_idx], buf);
    }
  }
  commander_modal_idx = -1;
  commander_modal_delta = 0;
}

static void show_commander_modal_delta(int delta)
{
  if (!commander_modal_delta_label) return;
  commander_modal_delta += delta;
  char tmp[16];
  snprintf(tmp, sizeof(tmp), "%+d", commander_modal_delta);
  lv_label_set_text(commander_modal_delta_label, tmp);
  lv_obj_clear_flag(commander_modal_delta_label, LV_OBJ_FLAG_HIDDEN);
  /* reset the auto-close timer */
  if (commander_modal_timer) lv_timer_reset(commander_modal_timer);
  else commander_modal_timer = lv_timer_create(commander_modal_hide_cb, 3000, NULL);
}

static void open_commander_modal(int idx)
{
  if (idx < 0 || idx > 3) return;
  commander_modal_idx = idx;
  commander_modal_total = commander_totals[idx];
  commander_modal_delta = 0;
  char buf[32];
  if (!commander_modal) {
    /* create full-screen modal overlay */
    commander_modal = lv_obj_create(lv_scr_act());
    lv_obj_set_size(commander_modal, lv_disp_get_hor_res(NULL), lv_disp_get_ver_res(NULL));
    lv_obj_set_style_bg_color(commander_modal, lv_color_hex(0x000000), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(commander_modal, LV_OPA_80, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_add_flag(commander_modal, LV_OBJ_FLAG_HIDDEN);
    /* big label in center */
    commander_modal_label = lv_label_create(commander_modal);
    lv_obj_set_style_text_font(commander_modal_label, &lv_font_montserratMedium_42, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_label_set_text(commander_modal_label, "0");
    lv_obj_align(commander_modal_label, LV_ALIGN_CENTER, 0, -20);
    /* running delta below */
    commander_modal_delta_label = lv_label_create(commander_modal);
    lv_obj_set_style_text_font(commander_modal_delta_label, &lv_font_montserratMedium_16, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_label_set_text(commander_modal_delta_label, "");
    lv_obj_add_flag(commander_modal_delta_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(commander_modal_delta_label, LV_ALIGN_CENTER, 0, 40);
    /* modal captures presses to increment/decrement like life screen */
    lv_obj_add_event_cb(commander_modal, commander_modal_event_cb, LV_EVENT_ALL, NULL);
  }
  /* update label and show modal */
  snprintf(buf, sizeof(buf), "%d", commander_modal_total);
  lv_label_set_text(commander_modal_label, buf);
  lv_obj_clear_flag(commander_modal, LV_OBJ_FLAG_HIDDEN);
  /* start/reset the auto-close timer */
  if (commander_modal_timer) lv_timer_reset(commander_modal_timer);
  else commander_modal_timer = lv_timer_create(commander_modal_hide_cb, 3000, NULL);
}

static void commander_quad_event_cb(lv_event_t * e)
{
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_SHORT_CLICKED) {
    /* figure which quad was clicked by comparing current_target to our labels' parents */
    lv_obj_t * target = e->current_target;
    for (int i = 0; i < 4; ++i) {
      if (target == lv_obj_get_parent(commander_quad_labels[i]) || target == commander_quad_labels[i]) {
        open_commander_modal(i);
        break;
      }
    }
  } else if (code == LV_EVENT_PRESSED) {
    /* flash feedback */
    lv_obj_t * target = e->current_target;
    for (int i = 0; i < 4; ++i) {
      if (target == lv_obj_get_parent(commander_quad_labels[i]) || target == commander_quad_labels[i]) {
        lv_obj_t * flash = lv_obj_create(lv_obj_get_parent(commander_quad_labels[i]));
        lv_obj_set_size(flash, lv_obj_get_width(lv_obj_get_parent(commander_quad_labels[i])), lv_obj_get_height(lv_obj_get_parent(commander_quad_labels[i])));
        lv_obj_align(flash, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_bg_color(flash, lv_color_hex(0xFFFFFF), LV_PART_MAIN|LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(flash, LV_OPA_30, LV_PART_MAIN|LV_STATE_DEFAULT);
        lv_timer_create(hide_flash_timer_cb, 120, flash);
        break;
      }
    }
  }
}

/* Event callback for the commander modal (increment/decrement with top/bottom tap).
   Implemented as a named function so it can be passed as a C-style function pointer. */
static void commander_modal_event_cb(lv_event_t * e)
{
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_PRESSED || code == LV_EVENT_SHORT_CLICKED) {
    lv_point_t p; lv_indev_get_point(lv_indev_get_act(), &p);
    lv_area_t coords; lv_obj_get_coords(e->current_target, &coords);
    int height = coords.y2 - coords.y1 + 1;
    int rely = p.y - coords.y1;
    int delta = (rely < height/2) ? +1 : -1;
    commander_modal_total += delta;
    char buf_local[32]; snprintf(buf_local, sizeof(buf_local), "%d", commander_modal_total);
    if (commander_modal_label) lv_label_set_text(commander_modal_label, buf_local);
    show_commander_modal_delta(delta);
  }
  /* reset/extend the auto-close timer on any click */
  if (commander_modal_timer) lv_timer_reset(commander_modal_timer);
}

// Simple animation helpers: pulse a label by animating its transform zoom.
static void anim_set_zoom(void * var, int32_t v)
{
  lv_obj_set_style_transform_zoom((lv_obj_t *)var, (lv_coord_t)v, 0);
}

static void anim_pulse_label(lv_obj_t * label)
{
  if (!label) return;
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, label);
  /* Zoom from the default 512 (100%) up to 768 (~150%) and back. */
  lv_anim_set_values(&a, 512, 768);
  lv_anim_set_time(&a, 160);
  lv_anim_set_playback_time(&a, 160);
  lv_anim_set_playback_delay(&a, 0);
  lv_anim_set_repeat_count(&a, 0);
  /* Use the LVGL exec callback typedef present in this LVGL version. */
  lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)anim_set_zoom);
  lv_anim_start(&a);
}

static void life_delta_hide_cb(lv_timer_t * t)
{
  (void)t;
  if(life_delta_label) lv_obj_add_flag(life_delta_label, LV_OBJ_FLAG_HIDDEN);
  if(life_delta_timer) {
    lv_timer_del(life_delta_timer);
    life_delta_timer = NULL;
  }
  // reset running total when hiding
  life_delta_total = 0;
}

// Show a signed delta (e.g. "+1" or "-1") under the life counter for 5 seconds
void show_life_delta(int delta)
{
  if(!life_delta_label) return;
  // accumulate running total
  life_delta_total += delta;
  char tmp[16];
  snprintf(tmp, sizeof(tmp), "%+d", life_delta_total);
  lv_label_set_text(life_delta_label, tmp);
  /* Make the delta visible and attach it to the life label so it only
     appears on the life page. Positioning mirrors the original behavior. */
  lv_obj_clear_flag(life_delta_label, LV_OBJ_FLAG_HIDDEN);
  if (user_ui.screen_label_life_obj) {
    int16_t hor_res = lv_disp_get_hor_res(NULL);
    int16_t ver_res = lv_disp_get_ver_res(NULL);
    int16_t delta_h = lv_font_get_line_height(&lv_font_montserratMedium_42) / 2;
    int16_t delta_extra = (ver_res * 25) / 100; /* additional downward shift */
    /* Move the delta slightly to the right (~5% of screen width) */
    int16_t x_shift = (hor_res * 5) / 100;
    lv_obj_align_to(life_delta_label, user_ui.screen_label_life_obj, LV_ALIGN_OUT_BOTTOM_MID, x_shift, 4 + delta_h + delta_extra);
    lv_obj_move_foreground(life_delta_label);
  }
  // (re)start/reset the 5s timer
  if(life_delta_timer) {
    lv_timer_reset(life_delta_timer);
  } else {
    life_delta_timer = lv_timer_create(life_delta_hide_cb, 5000, NULL);
  }
}

static void life_screen_event_cb(lv_event_t * e)
{
  lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t * cont = e->current_target;
  lv_point_t p;
  lv_indev_get_point(lv_indev_get_act(), &p);
  lv_area_t coords;
  lv_obj_get_coords(cont, &coords);
  int height = coords.y2 - coords.y1 + 1;
  int rely = p.y - coords.y1;
  if(code == LV_EVENT_PRESSED) {
    if(rely < height/2) {
      lv_obj_clear_flag(user_ui.screen_life_top_flash, LV_OBJ_FLAG_HIDDEN);
      lv_timer_create(hide_flash_timer_cb, 150, user_ui.screen_life_top_flash);
    } else {
      lv_obj_clear_flag(user_ui.screen_life_bottom_flash, LV_OBJ_FLAG_HIDDEN);
      lv_timer_create(hide_flash_timer_cb, 150, user_ui.screen_life_bottom_flash);
    }
  } else if(code == LV_EVENT_SHORT_CLICKED) {
    int delta = (rely < height/2) ? +1 : -1;
    if(rely < height/2) mtg_life++; else mtg_life--;
    char buf[16]; snprintf(buf, sizeof(buf), "%d", mtg_life);
    lv_label_set_text(user_ui.screen_label_life_obj, buf);
    // Show transient running delta under the life counter
    show_life_delta(delta);
  }
}

static void pt_screen_event_cb(lv_event_t * e)
{
  lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t * cont = e->current_target;
  lv_point_t p;
  lv_indev_get_point(lv_indev_get_act(), &p);
  lv_area_t coords;
  lv_obj_get_coords(cont, &coords);
  int width = coords.x2 - coords.x1 + 1;
  int height = coords.y2 - coords.y1 + 1;
  int relx = p.x - coords.x1;
  int rely = p.y - coords.y1;
  bool left = relx < width/2;
  bool top = rely < height/2;
  if(code == LV_EVENT_PRESSED) {
    if(left && top) {
      lv_obj_clear_flag(user_ui.pt_quad_tl_flash, LV_OBJ_FLAG_HIDDEN);
      lv_timer_create(hide_flash_timer_cb, 150, user_ui.pt_quad_tl_flash);
    } else if(left && !top) {
      lv_obj_clear_flag(user_ui.pt_quad_bl_flash, LV_OBJ_FLAG_HIDDEN);
      lv_timer_create(hide_flash_timer_cb, 150, user_ui.pt_quad_bl_flash);
    } else if(!left && top) {
      lv_obj_clear_flag(user_ui.pt_quad_tr_flash, LV_OBJ_FLAG_HIDDEN);
      lv_timer_create(hide_flash_timer_cb, 150, user_ui.pt_quad_tr_flash);
    } else {
      lv_obj_clear_flag(user_ui.pt_quad_br_flash, LV_OBJ_FLAG_HIDDEN);
      lv_timer_create(hide_flash_timer_cb, 150, user_ui.pt_quad_br_flash);
    }
  } else if(code == LV_EVENT_SHORT_CLICKED) {
    if(left && top) pt_power++;
    else if(left && !top) pt_power--;
    else if(!left && top) pt_tough++;
    else pt_tough--;
    char buf[16];
    // show power with sign
    snprintf(buf, sizeof(buf), "%+d", pt_power);
    lv_label_set_text(user_ui.screen_label_power, buf);
    snprintf(buf, sizeof(buf), "%+d", pt_tough);
    lv_label_set_text(user_ui.screen_label_toughness, buf);
  }
}

static void example_button_task(void* parmeter)
{
  lv_ui *ui = (lv_ui *)parmeter;
  uint8_t even_flag = 0x01;
  uint8_t ticks = 0;
  uint32_t sdcard_test = 0;
  char sdcard_buf[35] = {""};
  char sdcard_rec[35] = {""};
  for (;;)
  {
    EventBits_t even = xEventGroupWaitBits(key_groups,BIT_EVEN_ALL,pdTRUE,pdFALSE,pdMS_TO_TICKS(2 * 1000));
    if(READ_BIT(even,0)) //boot
    {
      /* Make the BOOT button behavior depend on the currently active
         carousel element so each page can have its own action. */
      lv_obj_t * act_elem = lv_carousel_get_element_act(ui->screen_carousel_1);
      if (act_elem == ui->screen_carousel_1_element_life) {
        /* Life screen: reset life to 40 */
        mtg_life = 40;
        {
          char buf[16]; snprintf(buf, sizeof(buf), "%d", mtg_life);
          if (ui->screen_label_life_obj) {
            lv_label_set_text(ui->screen_label_life_obj, buf);
            /* Pulse the label so user sees the reset occurred. */
            anim_pulse_label(ui->screen_label_life_obj);
          }
        }
        /* hide and reset running life delta */
        if (life_delta_label) lv_obj_add_flag(life_delta_label, LV_OBJ_FLAG_HIDDEN);
        if (life_delta_timer) { lv_timer_del(life_delta_timer); life_delta_timer = NULL; }
        life_delta_total = 0;
      } else if (act_elem == ui->screen_carousel_1_element_mid) {
        /* PT screen: reset power and toughness */
        pt_power = 0;
        pt_tough = 0;
        if (ui->screen_label_power) {
          lv_label_set_text(ui->screen_label_power, "+0");
          anim_pulse_label(ui->screen_label_power);
        }
        if (ui->screen_label_toughness) {
          lv_label_set_text(ui->screen_label_toughness, "+0");
          anim_pulse_label(ui->screen_label_toughness);
        }
      } else if (act_elem == ui->screen_carousel_1_element_2) {
        /* Touch-test screen: preserve existing toggle behavior */
        if(READ_BIT(even_flag,0))
        {
          CLEAR_BIT(even_flag,0);
          lv_obj_clear_flag(ui->screen_carousel_1,LV_OBJ_FLAG_SCROLLABLE); //unmovable
          lv_obj_clear_flag(ui->screen_cont_4,LV_OBJ_FLAG_HIDDEN); 
          lv_obj_add_flag(ui->screen_cont_3, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
          SET_BIT(even_flag,0);
          lv_obj_add_flag(ui->screen_carousel_1,LV_OBJ_FLAG_SCROLLABLE); //removable
          lv_obj_clear_flag(ui->screen_cont_3,LV_OBJ_FLAG_HIDDEN); 
          lv_obj_add_flag(ui->screen_cont_4, LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_invalidate(ui->screen_carousel_1);  // mark redraw
      } else if (act_elem == ui->screen_carousel_1_element_3) {
        /* Brightness screen: reset to max */
        setBrightnes(255);
        if (ui->screen_slider_1) lv_slider_set_value(ui->screen_slider_1, 255, LV_ANIM_ON);
      } else {
        /* Unknown/other element: no-op */
      }
    }
    if(READ_BIT(even,5)) //长按 boot
    {
      audio_Test_flag = 0;
    }
    if(READ_BIT(even,3)) //弹起 boot
    {
      audio_Test_flag = 1;
    }
    if(READ_BIT(even,12)) //长按 pwr
    {
      if(READ_BIT(even_flag,1))
      {
        /* Prefer the expander (if initialized). If it isn't available, fall
           back to the direct board GPIO if provided. */
  /* If expander support is compiled in (either full I2C equipment or
     power-control-only), prefer using it. Otherwise fall back to the
     direct GPIO (if provided). Wrapping the expander call in a
     preprocessor guard avoids undefined-symbol errors when the
     expander headers/macros are not available. */
#if CONFIG_I2C_EQUIPMENT_ENABLED || CONFIG_POWER_CTRL_ENABLED
  if (io_expander) {
    esp_io_expander_set_level(io_expander, IO_EXPANDER_PIN_NUM_6, 0);
  } else
#endif
  {
#ifdef POWER_CTRL_GPIO
    ESP_LOGI(TAG_USER_APP, "Expander not present, pulsing POWER_CTRL_GPIO (%d)", POWER_CTRL_GPIO);
    gpio_set_direction(POWER_CTRL_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(POWER_CTRL_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(200));
    gpio_set_level(POWER_CTRL_GPIO, 1);
#else
    ESP_LOGW(TAG_USER_APP, "No expander and POWER_CTRL_GPIO not defined; power action skipped");
#endif
  }
      }
    }
    if(!READ_BIT(even_flag,1))  //
    {
      ticks++;
      if(READ_BIT(even,10) || (ticks == 4))
      {
        SET_BIT(even_flag,1);
      }
    }
    if(READ_BIT(even,1)) //dc boot
    {
      sdcard_test++;
      snprintf(sdcard_buf,33,"sdcardTest : %ld",sdcard_test);
      sdcard_file_write("/sdcard/sdcardTest.txt",sdcard_buf);
      sdcard_file_read("/sdcard/sdcardTest.txt",sdcard_rec,NULL);
      if(!strcmp(sdcard_rec,sdcard_buf))
      {
        lv_label_set_text(ui->screen_label_25, "sdcard test passed");
      }
      else
      {
        lv_label_set_text(ui->screen_label_25, "sdcard test failed");
      }
    }
    else
    {
      lv_label_set_text(ui->screen_label_25, "");
    }
  }
}
static void example_color_task(void *arg)
{
  lv_ui *ui = (lv_ui *)arg;
  lv_obj_clear_flag(ui->screen_carousel_1,LV_OBJ_FLAG_SCROLLABLE); //unmovable
  lv_obj_clear_flag(ui->screen_cont_2,LV_OBJ_FLAG_HIDDEN); 
  lv_obj_add_flag(ui->screen_cont_3, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(ui->screen_cont_4, LV_OBJ_FLAG_HIDDEN);

  lv_obj_clear_flag(ui->screen_img_1,LV_OBJ_FLAG_HIDDEN); 
  lv_obj_add_flag(ui->screen_img_2, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(ui->screen_img_3, LV_OBJ_FLAG_HIDDEN);
  vTaskDelay(pdMS_TO_TICKS(1500));
  lv_obj_clear_flag(ui->screen_img_2,LV_OBJ_FLAG_HIDDEN); 
  lv_obj_add_flag(ui->screen_img_1, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(ui->screen_img_3, LV_OBJ_FLAG_HIDDEN);
  vTaskDelay(pdMS_TO_TICKS(1500));
  lv_obj_clear_flag(ui->screen_img_3,LV_OBJ_FLAG_HIDDEN); 
  lv_obj_add_flag(ui->screen_img_2, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(ui->screen_img_1, LV_OBJ_FLAG_HIDDEN);
  vTaskDelay(pdMS_TO_TICKS(1500));

  lv_obj_clear_flag(ui->screen_cont_3,LV_OBJ_FLAG_HIDDEN); 
  lv_obj_add_flag(ui->screen_cont_2, LV_OBJ_FLAG_HIDDEN); 

  lv_obj_add_flag(ui->screen_carousel_1,LV_OBJ_FLAG_SCROLLABLE); //removable
  vTaskDelete(NULL); 
}
static void example_user_task(void *arg)
{
  lv_ui *ui = (lv_ui *)arg;
  char obj_send_data[30] = {""};
  float adc_value = 0;
  uint32_t times = 0;
  uint32_t rtc_time = 0;
  uint32_t qmi_time = 0;
  uint32_t adc_time = 0;
  for(;;)
  {
    if(times - adc_time == 10) //2s
    {
      adc_time = times;
      adc_get_value(&adc_value,NULL);
      snprintf(obj_send_data,28,"%.2fV",adc_value);
      lv_label_set_text(ui->screen_label_7, obj_send_data);
    }
    if(times - rtc_time == 5) //1s
    {
      rtc_time = times;
#if CONFIG_I2C_EQUIPMENT_ENABLED
  RtcDateTime_t rtc = i2c_rtc_get();
  snprintf(obj_send_data,28,"%d/%d/%d %02d:%02d:%02d",rtc.year,rtc.month,rtc.day,rtc.hour,rtc.minute,rtc.second);
  lv_label_set_text(ui->screen_label_10, obj_send_data);
#else
  lv_label_set_text(ui->screen_label_10, "N/A");
#endif
    }
    if(times - qmi_time == 1) //200ms
    {
      qmi_time = times;
#if CONFIG_I2C_EQUIPMENT_ENABLED
  ImuDate_t qmi = i2c_imu_get();
  snprintf(obj_send_data,28,"%.2f,%.2f,%.2f (g)",qmi.accx,qmi.accy,qmi.accz);
  lv_label_set_text(ui->screen_label_12, obj_send_data);
  snprintf(obj_send_data,28,"%.2f,%.2f,%.2f (dps)",qmi.gyrox,qmi.gyroy,qmi.gyroz);
  lv_label_set_text(ui->screen_label_19, obj_send_data);
#else
  lv_label_set_text(ui->screen_label_12, "N/A");
  lv_label_set_text(ui->screen_label_19, "N/A");
#endif
    }
    vTaskDelay(pdMS_TO_TICKS(200));
    times++;
  }
}
#if SD_CARD_EN
static void example_sdcard_task(void *arg)
{
  lv_ui *ui = (lv_ui *)arg;
  char obj_send_data[10] = {""};
  EventBits_t even = xEventGroupWaitBits(sdcard_even_,(0x01),pdTRUE,pdFALSE,pdMS_TO_TICKS(8 * 1000));
  if(READ_BIT(even,0))
  {
    snprintf(obj_send_data,10,"%.2fG",user_sdcard_bsp.sdcard_size);
    lv_label_set_text(ui->screen_label_6, obj_send_data);
  }
  else
  {
    lv_label_set_text(ui->screen_label_6, "NULL");
  }
  vTaskDelay(pdMS_TO_TICKS(1000));
  vTaskDelete(NULL); 
}
#endif
/* example_scan_wifi_ble_task removed: this build does not use Wi‑Fi or BLE */
static void lvgl_obj_event_handler(lv_event_t *e)
{
  lv_event_code_t code = lv_event_get_code(e);
  lv_ui *ui = (lv_ui *)e->user_data;
  lv_obj_t * module = e->current_target;
  switch (code)
  {
    case LV_EVENT_CLICKED:
    {
      if(module == ui->screen_slider_1)
      {
        uint8_t value = lv_slider_get_value(module);
        setBrightnes(value);
      }
      break;
    }
    default:
      break;
  }
}
void tp_event_callback(uint16_t x,uint16_t y)
{
  char str[12] = {""};
  snprintf(str,11,"(%hd,%hd)",x,y);
  lv_label_set_text(user_ui.screen_label_24, str);
}
void tca9554_init(void)
{
/* Initialize the TCA9554 expander if either the full I2C equipment feature is
   enabled or the power-control-only option is enabled. The I2C master bus is
   created unconditionally in `i2c_bsp`, so the expander can be attached even
   when RTC/IMU registration is skipped. */
#if CONFIG_I2C_EQUIPMENT_ENABLED || CONFIG_POWER_CTRL_ENABLED
  esp_io_expander_new_i2c_tca9554(user_i2c_port0_handle, ESP_IO_EXPANDER_I2C_TCA9554_ADDRESS_000, &io_expander);

  /* Configure only the pins we need as outputs (7, 0, 6). Pin 6 is used for
     board power control on this hardware. */
  esp_io_expander_set_dir(io_expander, IO_EXPANDER_PIN_NUM_7 | IO_EXPANDER_PIN_NUM_0 | IO_EXPANDER_PIN_NUM_6, IO_EXPANDER_OUTPUT);
  esp_io_expander_set_level(io_expander, IO_EXPANDER_PIN_NUM_7 | IO_EXPANDER_PIN_NUM_0 | IO_EXPANDER_PIN_NUM_6, 1);
#else
  /* I2C equipment disabled and power-control not enabled: keep previous fallback
     behavior (optional direct GPIO) if POWER_CTRL_GPIO is defined. */
  (void)io_expander;
#ifdef POWER_CTRL_GPIO
  /* Configure fallback power-control GPIO (keep it high by default). */
  ESP_LOGI(TAG_USER_APP, "I2C disabled - configuring POWER_CTRL_GPIO (%d) as fallback", POWER_CTRL_GPIO);
  gpio_config_t io_conf = {};
  io_conf.intr_type = GPIO_INTR_DISABLE;
  io_conf.mode = GPIO_MODE_OUTPUT;
  io_conf.pin_bit_mask = (1ULL << (int)POWER_CTRL_GPIO);
  io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
  io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
  gpio_config(&io_conf);
  gpio_set_level(POWER_CTRL_GPIO, 1);
#endif
#endif
}
void user_app_init(void)
{
  setup_ui(&user_ui);
  // Make life and PT numbers 100% larger via transform zoom (200% = 512 / 256 units)
  /* Calculate 10% offsets of the display to shift the counters left/up */
  int16_t hor_res = lv_disp_get_hor_res(NULL);
  int16_t ver_res = lv_disp_get_ver_res(NULL);
  int16_t dx = -(hor_res * 5) / 100; // 5% left
  int16_t dy = -(ver_res * 5) / 100; // 5% up

  if(user_ui.screen_label_life_obj) {
    lv_obj_set_style_transform_zoom(user_ui.screen_label_life_obj, 512, 0);
    /* Re-apply alignment after transform so the label stays centered but shifted */
    lv_obj_align(user_ui.screen_label_life_obj, LV_ALIGN_CENTER, dx, dy);
  }
  if(user_ui.screen_label_power) {
    lv_obj_set_style_transform_zoom(user_ui.screen_label_power, 512, 0);
    /* keep power/toughness at their designed offsets from center and shifted */
    lv_obj_align(user_ui.screen_label_power, LV_ALIGN_CENTER, -80 + dx, dy);
  }
  if(user_ui.screen_label_toughness) {
    lv_obj_set_style_transform_zoom(user_ui.screen_label_toughness, 512, 0);
    lv_obj_align(user_ui.screen_label_toughness, LV_ALIGN_CENTER, 80 + dx, dy);
  }
  // Create transient delta label under life counter (hidden until needed)
  if(user_ui.screen_cont_life) {
    /* create the delta label as a child of the life container so it is only
       visible when that carousel page is active */
    life_delta_label = lv_label_create(user_ui.screen_cont_life);
    lv_label_set_text(life_delta_label, "");
    lv_obj_add_flag(life_delta_label, LV_OBJ_FLAG_HIDDEN);
    // Use a much larger font (~3x) for the running delta and make it white
    lv_obj_set_style_text_font(life_delta_label, &lv_font_montserratMedium_42, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(life_delta_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN|LV_STATE_DEFAULT);
    /* ensure the delta is centered under the life label and visible on top */
    lv_obj_set_width(life_delta_label, 200);
    lv_obj_set_style_text_align(life_delta_label, LV_TEXT_ALIGN_CENTER, 0);
    /* position it relative to the life label (attached under it) */
    int16_t delta_h = lv_font_get_line_height(&lv_font_montserratMedium_42) / 2;
    int16_t delta_extra = (ver_res * 25) / 100; /* additional 25% downward shift */
    lv_obj_align_to(life_delta_label, user_ui.screen_label_life_obj, LV_ALIGN_OUT_BOTTOM_MID, 0, 4 + delta_h + delta_extra);
    lv_obj_move_foreground(life_delta_label);
  }
  /* Create commander-damage carousel element with four quadrants */
  if (user_ui.screen_carousel_1) {
    /* add a new element to the carousel (id 4) */
    commander_element = lv_carousel_add_element(user_ui.screen_carousel_1, 4);
    if (commander_element) {
      int hor = lv_disp_get_hor_res(NULL);
      int ver = lv_disp_get_ver_res(NULL);
      int w = hor / 2;
      int h = ver / 2;
      for (int i = 0; i < 4; ++i) {
        lv_obj_t * quad = lv_obj_create(commander_element);
        lv_obj_set_size(quad, w, h);
        /* position: TL=0, TR=1, BL=2, BR=3 */
        int ofs_x = (i % 2 == 0) ? -w/2 : w/2;
        int ofs_y = (i < 2) ? -h/2 : h/2;
        lv_obj_align(quad, LV_ALIGN_CENTER, ofs_x, ofs_y);
        lv_obj_set_style_bg_opa(quad, LV_OPA_TRANSP, LV_PART_MAIN|LV_STATE_DEFAULT);
        /* label showing the total */
        commander_quad_labels[i] = lv_label_create(quad);
        lv_obj_set_style_text_font(commander_quad_labels[i], &lv_font_montserratMedium_42, LV_PART_MAIN|LV_STATE_DEFAULT);
        lv_label_set_text(commander_quad_labels[i], "0");
        lv_obj_align(commander_quad_labels[i], LV_ALIGN_CENTER, 0, 0);
        lv_obj_add_event_cb(quad, commander_quad_event_cb, LV_EVENT_ALL, NULL);
      }
    }
  }
  user_button_init();
  adc_bsp_init();
#if CONFIG_I2C_EQUIPMENT_ENABLED
  i2c_rtc_setup();
  i2c_rtc_setTime(2025,7,7,18,43,30);
  i2c_qmi_setup();
#else
  /* I2C equipment disabled: skip RTC/IMU init */
#endif
  /* Wi‑Fi initialization removed */
  user_audio_bsp_init();
  tca9554_init();
#if SD_CARD_EN
  _sdcard_init();
  xTaskCreatePinnedToCore(example_sdcard_task, "example_sdcard_task", 2 * 1024, &user_ui, 2, NULL,0);      //sd card测试
#endif 
  xTaskCreatePinnedToCore(example_user_task, "example_user_task", 4 * 1024, &user_ui, 2, NULL,0);          //用户事件
xTaskCreatePinnedToCore(example_button_task, "example_button_task", 4 * 1024, &user_ui, 2, NULL,0);      //按钮事件  
  xTaskCreatePinnedToCore(example_color_task, "example_color_task", 4 * 1024, &user_ui, 2, NULL,0);        //RGB颜色测试
  /* Wi‑Fi / BT scan task creation removed */
  xTaskCreatePinnedToCore(i2s_audio_Test, "i2s_audio_Test", 4 * 1024, &audio_Test_flag, 2, NULL,0);           
  /*even add*/
  lv_obj_add_event_cb(user_ui.screen_slider_1, lvgl_obj_event_handler, LV_EVENT_ALL, &user_ui); 
  // add MTG event handlers
  // Register PRESSED and SHORT_CLICKED separately (lv_obj_add_event_cb does not accept bitwise OR for the filter)
  if(user_ui.screen_cont_life) {
    lv_obj_add_event_cb(user_ui.screen_cont_life, life_screen_event_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(user_ui.screen_cont_life, life_screen_event_cb, LV_EVENT_SHORT_CLICKED, NULL);
  }
  if(user_ui.screen_cont_pt) {
    lv_obj_add_event_cb(user_ui.screen_cont_pt, pt_screen_event_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(user_ui.screen_cont_pt, pt_screen_event_cb, LV_EVENT_SHORT_CLICKED, NULL);
  }
}



