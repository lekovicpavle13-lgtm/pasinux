#ifndef GUI_MAIN_H
#define GUI_MAIN_H

#include <stdint.h>



typedef enum {
    GUI_ACT_NONE = 0,
    GUI_ACT_STEP,            
    GUI_ACT_RUN,             
    GUI_ACT_RESET,           
    GUI_ACT_DUMP,            
    GUI_ACT_SPAWN,           
    GUI_ACT_CHESS_MOVE,      
    GUI_ACT_CHESS_STATE,     
    GUI_ACT_CHESS_DRAW_OFFER,
    GUI_ACT_CHESS_DRAW_ACCEPT,
    GUI_ACT_CHESS_RESIGN,
    GUI_ACT_RUN_DEMO,        
} gui_action_t;

#endif 
