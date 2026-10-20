#include <stdio.h>
#include <stdlib.h>
#include "lvgl.h"
#include "ui.h"
static lv_color_t frame[480*480], buffer[480*40];
static void flush(lv_disp_drv_t *driver,const lv_area_t *area,lv_color_t *data){
 for(int y=area->y1;y<=area->y2;y++)for(int x=area->x1;x<=area->x2;x++){
  lv_color_t color=*data++;
  if(x>=0&&x<480&&y>=0&&y<480)frame[y*480+x]=color;
 }
 lv_disp_flush_ready(driver);
}
static void settle(void){for(int i=0;i<100;i++){lv_tick_inc(10);lv_timer_handler();}}
int main(int argc,char **argv){
 if(argc!=2)return 2;
 lv_init();static lv_disp_draw_buf_t buf;lv_disp_draw_buf_init(&buf,buffer,NULL,480*40);
 static lv_disp_drv_t driver;lv_disp_drv_init(&driver);driver.hor_res=480;driver.ver_res=480;
 driver.draw_buf=&buf;driver.flush_cb=flush;lv_disp_drv_register(&driver);
 ui_create(NULL);
 app_state_t s={.connection=CONN_LINKED,.data_valid=true,.time_valid=true,
 .unix_time=1767225600,.utc_offset_minutes=480,.remaining_percent=73,.success_permille=992,
 .calls_today=128,.errors_today=1,.tokens_today=1234567,.average_ttft_ms=850,
 .latest_model="example-model",.latest_status=200,.latest_latency_ms=1250,
 .session_count=12,.total_input=4000000,.total_output=1200000,
 .total_cache_read=8000000,.total_reasoning=400000,.total_cost_micros=12340000,
 .top_model="example-model",.top_model_calls=96,.cache_hit_permille=588};
 ui_update(&s,0);
 for(int i=0;i<4;i++){
  settle();lv_refr_now(NULL);char name[1024];snprintf(name,sizeof(name),"%s/page-%d.ppm",argv[1],i);
  FILE *f=fopen(name,"wb");if(!f)return 3;fprintf(f,"P6\n480 480\n255\n");
  for(int n=0;n<480*480;n++){lv_color32_t c;c.full=lv_color_to32(frame[n]);
   fputc(c.ch.red,f);fputc(c.ch.green,f);fputc(c.ch.blue,f);}
  fclose(f);ui_cycle_page();
 }
 return 0;
}
