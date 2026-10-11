#!/usr/bin/env python3
"""Host regression tests; these do not replace RF/hardware acceptance tests."""
from pathlib import Path
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[1]
s = (ROOT / 'mdc1200.c').read_text()
def function(name):
    import re
    m = re.search(r'^(?:static )?[^\n]+\b'+name+r'\([^;]*?\)\n\{', s, re.M)
    assert m, name
    start=m.start(); i=m.end(); depth=1
    while depth:
        depth += (s[i]=='{')-(s[i]=='}'); i+=1
    return s[start:i]+'\n'
def run(name, code, extras=()):
    with tempfile.TemporaryDirectory() as d:
        p=Path(d)/'test.c'; p.write_text(code)
        exe=Path(d)/'test'
        subprocess.run(['gcc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=undefined','-I',str(ROOT),str(p),*extras,'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
    print(name+': PASS',flush=True)
head='#include <stdint.h>\n#include <stdbool.h>\n#include <string.h>\n#include <assert.h>\n#include <stdio.h>\n'
codec=head+'#define MDC1200_FEC_K 7\nstatic const uint8_t mdc_sync[5]={7,9,42,68,111};\n'
codec+=''.join(function(n) for n in ['crc16_mdc','xor_modulation','error_correction','decode_data','encode_data','encode_packet','decode_rx_polarity','process_rx_data'])
codec+=r'''
int main(void) {
  uint8_t p[32],op,arg; uint16_t id;
  for(unsigned unit=1;unit<65535;unit++) {
    unsigned n=encode_packet(p,1,0x80,unit);
    assert(n==22);
    for(unsigned polarity=0;polarity<2;polarity++) {
      assert(process_rx_data(p+3,n-3,&op,&arg,&id));
      assert(op==1 && arg==0x80 && id==unit);
      assert(!process_rx_data(p+3,n-4,&op,&arg,&id));
      for(unsigned j=0;j<n;j++) p[j]^=255;
    }
  }
  /* Independent captured example from OneOfEleven's decoder comments. */
  uint8_t v[]={0x04,0x8d,0xbf,0x66,0x58,0x40,0xc4,0xb0,0x32,0xba,0xf9,0x33,0x18,0x35,0x08,0x83,0xf6,0x0c,0x36};
  assert(process_rx_data(v,sizeof(v),&op,&arg,&id));
  assert(op==1 && arg==0x80 && id==0x0740);
  printf("Reference vector: op=%02x arg=%02x id=%04x\n",op,arg,id);
  memset(p,0,sizeof(p)); assert(!process_rx_data(p,sizeof(p),&op,&arg,&id));
  puts("131068 packet round-trips, truncation and empty-data rejection");
}
'''
run('MDC codec',codec)
keys=head+'#include "driver/keyboard.h"\n'
keys+='''static bool active=true, edit_mode=false, gUpdateDisplay=false;
static unsigned edit_pos=0, saves=0; static KEY_Code_t edit_last_key=KEY_INVALID;
static char edit_name[11]; static unsigned history_selected=0, history_count=2;
static bool log_open=true; static unsigned mdc1200_rx_ready_tick_500ms=12;
static bool MDC1200_is_overlay_active(void){return active;}
static void save_edit(void){saves++;edit_mode=false;}
static void begin_edit(void){edit_mode=true;}
static char cycle_char(char c,int dir){return c+dir;}
static void keypad_letter(KEY_Code_t key){(void)key;}
'''+function('MDC1200_handle_key')+'''
int main(void){
 assert(!MDC1200_handle_key(KEY_PTT,true,false));
 assert(!MDC1200_handle_key(KEY_PTT,false,false));
 assert(MDC1200_handle_key(KEY_MENU,true,false)); assert(edit_mode);
 assert(!MDC1200_handle_key(KEY_PTT,false,false));
 assert(MDC1200_handle_key(KEY_MENU,true,false)); assert(edit_pos==1 && gUpdateDisplay);
 assert(MDC1200_handle_key(KEY_MENU,true,true)); assert(saves==1);
 assert(MDC1200_handle_key(KEY_EXIT,true,false)); assert(!log_open);
}
'''
run('MDC keypad/PTT',keys)
edit=head+'#include "driver/keyboard.h"\n'+'''typedef struct {uint16_t unit_id;} MDC1200_Event;
static MDC1200_Event incoming={0x1234}; static uint16_t edit_id,saved_id;
static char edit_name[11]; static unsigned edit_pos; static bool edit_mode,gUpdateDisplay;
static KEY_Code_t edit_last_key; static unsigned mdc1200_rx_ready_tick_500ms,gBeepToPlay;
#define BEEP_1KHZ_60MS_OPTIONAL 1
static MDC1200_Event *selected_event(void){return &incoming;}
static bool contact_find(uint16_t id,char *out,int *slot){(void)id;(void)slot;strcpy(out,"ABCDEFGHIJ");return true;}
static void contact_save(uint16_t id,const char *name){saved_id=id;assert(!strcmp(name,"ABCDEFGHIJ"));}
'''+function('begin_edit')+function('save_edit')+'''
int main(void){begin_edit();incoming.unit_id=0x9876;save_edit();assert(saved_id==0x1234);}
'''
run('Contact edit target remains stable on new reception',edit)
modes=(ROOT/'air_modes.c').read_text()
observer=modes[modes.index('void AIRMODES_ObserveKey'):modes.index('void AIRMODES_BeforePtt')]
run('Held UP release clears mode override',head+'#include "driver/keyboard.h"\nstatic bool up_pressed;\n'+observer+'''
int main(void){AIRMODES_ObserveKey(KEY_UP,true,true);assert(up_pressed);AIRMODES_ObserveKey(KEY_UP,false,true);assert(!up_pressed);}
''')
nv=head+'#include "air_nv.h"\nstatic uint8_t ee[8192];\n'+'''
void EEPROM_ReadBuffer(uint16_t a,void *p,uint8_t n){assert((unsigned)a+n<=sizeof(ee));memcpy(p,ee+a,n);}
void EEPROM_WriteBuffer(uint16_t a,const void *p){assert((unsigned)a+8<=sizeof(ee));memcpy(ee+a,p,8);}
int main(void){
 memset(ee,0x5a,sizeof(ee)); uint8_t data[AIRNV_SIZE],out[AIRNV_SIZE];
 for(unsigned i=0;i<sizeof(data);i++)data[i]=(uint8_t)i;
 AIRNV_Write(0,data,sizeof(data)); AIRNV_Read(0,out,sizeof(out)); assert(!memcmp(data,out,sizeof(data)));
 for(unsigned a=0;a<sizeof(ee);a++) {
   bool writable=a>=0xf50 && a<0xf50+(AIRNV_SIZE/6)*16 && ((a-0xf50)%16)>=10;
   if(!writable)assert(ee[a]==0x5a);
 }
 AIRNV_Write(AIRNV_SIZE-1,data,8); assert(ee[0x1e00]==0x5a);
}
'''
run('EEPROM boundaries and name preservation',nv,[str(ROOT/'air_nv.c')])
assert 'gScreenToDisplay == DISPLAY_MAIN && MDC1200_render()' in (ROOT/'ui/ui.c').read_text()
assert 'gScreenToDisplay == DISPLAY_MAIN && Key != KEY_PTT' in (ROOT/'app/app.c').read_text()
print('Menu overlay routing: PASS')
print('Hardware RF decode, physical keys, serial backup and boot remain unverified.')
