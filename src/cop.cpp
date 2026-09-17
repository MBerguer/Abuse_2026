/*
 *  Abuse - dark 2D side-scrolling platform game
 *  Copyright (c) 1995 Crack dot Com
 *  Copyright (c) 2005-2011 Sam Hocevar <sam@hocevar.net>
 *
 *  This software was released into the Public Domain. As with most public
 *  domain software, no warranty is made or implied by Crack dot Com, by
 *  Jonathan Clark, or by Sam Hocevar.
 */

#if defined HAVE_CONFIG_H
#   include "config.h"
#endif

#include "common.h"
#include <cmath>
#include "remaster/remaster_config.h"
#include "remaster/remaster_particles.h"
#include "palette.h"

#include "lisp.h"
#include "lisp_gc.h"
#include "compiled.h"
#include "objects.h"
#include "level.h"
#include "game.h"
#include "jrand.h"
#include "clisp.h"
#include "view.h"
#include "ant.h"
#include "dev.h"

enum { point_angle, fire_delay1 };

typedef enum
{
  SHOTGUN = 10,
  GRENADE = 2,
  ROCKET = 3,
  PLASMA = 4,
  FIREBOMB = 5,
  DFRIS = 6,
  LSABER = 7
} WeaponType;

signed char small_fire_off[24*2]=  // x & y offset from character to end of gun.
  { 17,20,     // 1
    17,23,     // 2
    17,28,
    15,33,
    11,39,     // 5
    7,43,
    -3,44,     // 7
    -10,42,
    -16,39,
    -20,34,
    -20,28,
    -20,25,
    -19,20,
    -19,16,
    -16,14,
    -14,11,
    -11,9,
    -7,8,
    -3,8,
    2,8,
    6,9,
    10,10,
    14,13,
    16,15 };

signed char large_fire_off[24*2]=
  { 18,25,
    17,30,
    15,34,
    14,36,
    10,39,
    7,41,
    4,42,
    -3,41,
    -8,39,
    -11,37,
    -14,33,
    -16,30,
    -18,25,
    -17,21,
    -14,17,
    -11,15,
    -7,13,
    -4,12,
    3,12,
    9,12,
    12,15,
    14,16,
    15,18,
    16,21 };


enum { in_climbing_area,
    disable_top_draw,
    just_hit,
    ship_pan_x,
    special_power,
    used_special_power,
    last1_x, last1_y,
    last2_x, last2_y,
    has_saved_this_level,
    r_ramp, g_ramp, b_ramp,
        is_teleporting,
       just_fired};


enum { sgb_speed,
       sgb_angle,
       sgb_lastx,
       sgb_lasty,
       sgb_bright_color,
       sgb_medium_color,
       sgb_lifetime };

enum { NO_POWER,
       FAST_POWER,
       FLY_POWER,
       SNEAKY_POWER,
       HEALTH_POWER } ;

enum { top_point_angle,
       top_fire_delay1,
       top_just_fired };

inline int angle_diff(int a1, int a2)
{
  if (a1<a2)
  { if ((a2-a1)<180)
      return a2-a1;
    else return 360-a2+a1;
  } else
  {
    if ((a1-a2)<180)
      return a1-a2;
    else return 360-a1+a2;
  }
  return 0;
}

void *top_ai()
{
  game_object *o=current_object;
  if (o->total_objects())            // make sure we are linked to the main character
  {
    game_object *q=o->get_object(0);

    view *v=q->controller();
    if (v)
    {
      if (!v->freeze_time)
      {
    o->direction=1;                 // always face right

    if (q->direction<0)
          q->x+=4;
    int i;
    signed char *fire_off=o->otype==S_DFRIS_TOP ? large_fire_off :
                                    (o->otype==S_ROCKET_TOP ? large_fire_off :
                     (o->otype==S_BFG_TOP ? large_fire_off : small_fire_off));
    signed char *f=fire_off,*fb=NULL;
    int best_diff=200,best_num=0;
    int iy=f[1],ix=f[6*2];

    int best_angle=lisp_atan2(q->y-iy-v->pointer_y,v->pointer_x-q->x-ix);
    for (i=0; i<24; i++,f+=2)             // check all the angles to see which would best fit animation wise
    {
      int this_angle=lisp_atan2(f[1]-iy,f[0]-ix);
      int this_diff=angle_diff(this_angle,best_angle);
      if (this_diff<best_diff)
      {
        best_diff=this_diff;
        best_num=i;
        fb=f;
      }
    }


    // if the pointer is too close to the player go with the angle shown, not the angle through the pointer
    if (abs(q->y-fb[1]-v->pointer_y)<45 && abs(v->pointer_x-q->x+fb[0])<40)
      o->lvars[point_angle]=lisp_atan2(fb[1]-iy,fb[0]-ix);
    else
      o->lvars[point_angle]=lisp_atan2(q->y-fb[1]-v->pointer_y,v->pointer_x-(q->x+fb[0]));


    if (q->direction<0)
          q->x-=4;

    o->x=q->x;
    o->y=q->y+29-q->picture()->Size().y;

    rand_on+=o->lvars[point_angle];
    o->current_frame=best_num;


    if (o->lvars[fire_delay1])
      o->lvars[fire_delay1]--;

    o->otype=weapon_types[v->current_weapon];  // switch to correct top part
      }
    }
  }
  return true_symbol;
}


static int player_fire_weapon(game_object *o, int type, game_object *target, int angle, signed char *fire_off)
{

  if (!o->total_objects()) return 0;
  game_object *other=o->get_object(0);
  int ox=other->x,oy=other->y;
  if (other->direction<0) other->x+=4;

  int firex=other->x+fire_off[o->current_frame*2];
  int firey=other->y-fire_off[o->current_frame*2+1];

  if (RemasterConfig::get().enabled)
  {
    int iy = fire_off[1];
    int ix = fire_off[6 * 2];
    int shoulder_x = other->x + ix;
    int shoulder_y = other->y - iy;
    float vx = (float)(fire_off[o->current_frame * 2] - ix);
    float vy = (float)(fire_off[o->current_frame * 2 + 1] - iy);
    float R = std::hypot(vx, vy);
    float rad = (float)angle * 3.141592653589793f / 180.0f;
    firex = shoulder_x + (int)std::round(R * std::cos(rad));
    firey = shoulder_y - (int)std::round(R * std::sin(rad));
  }



  // fire try to move up to gun level

  int32_t x2=o->x,y2=firey;
//  current_level->foreground_intersect(other->x,other->y,x2,y2);      // find first location we can actuall "see"
//  current_level->all_boundary_setback(o,other->x,other->y,x2,y2);       // to make we don't fire through walls
  other->y=y2;

  if (other->y==firey)             // now try to move out to end of gun if we were not blocked above
  {
    x2=firex;
    current_level->foreground_intersect(other->x,other->y,x2,y2);      // find first location we can actuall "see"
    current_level->all_boundary_setback(other,other->x,other->y,x2,y2);       // to make we don't fire through walls
    o->x=x2;
  }

  void *list=NULL;
  PtrRef r1(list);
  push_onto_list(LPointer::Create(target),list);
  push_onto_list(LNumber::Create(angle),list);
  push_onto_list(LNumber::Create(y2),list);
  push_onto_list(LNumber::Create(x2),list);
  push_onto_list(LNumber::Create(type),list);
  push_onto_list(LPointer::Create(o->get_object(0)),list);
  ((LSymbol *)l_fire_object)->EvalFunction(list);
  o->lvars[top_just_fired]=1;
  other->lvars[just_fired]=1;
  other->x=ox;
  other->y=oy;

  return 1;
}



void *laser_ufun(void *args)
{
  game_object *o=current_object;
  PtrRef r1(args);
  void *signal=CAR(args);  args=CDR(args);
  void *ret=NULL;

  if (signal==l_FIRE)
  {
    if (!o->lvars[fire_delay1])                   // make sur we are not waiting of previous fire
    {
      int32_t value=lnumber_value(CAR(args)->Eval());
      if (value)                                   // do we have ammo ?
      {
    o->lvars[fire_delay1]=3;
    if (player_fire_weapon(o,SHOTGUN,NULL,o->lvars[point_angle],small_fire_off))
          ret=LNumber::Create(-1);
    else ret=LNumber::Create(0);
      } else
      {
    o->lvars[fire_delay1]=5;                  // no ammo, set large fire delay for next shot
    player_fire_weapon(o,SHOTGUN,NULL,o->lvars[point_angle],small_fire_off);
    ret=LNumber::Create(0);
      }
    } else ret=LNumber::Create(0);                // can't fire yet, return 0 ammo subtract
  }
  return ret;
}


static int ammo_type(int otype)
{
  if (otype==S_GRENADE_TOP)
    return GRENADE;
  else if (otype==S_FIREBOMB_TOP)
    return FIREBOMB;
  else if (otype==S_DFRIS_TOP)
    return DFRIS;
  else return SHOTGUN;
}


void *top_ufun(void *args)                       // generic top character ai GRENADE && FIREBOMB
{
  game_object *o=current_object;
  PtrRef r1(args);
  void *signal=CAR(args);  args=CDR(args);
  void *ret=NULL;

  if (signal==l_FIRE)
  {
    if (!o->lvars[fire_delay1])                   // make sur we are not waiting of previous fire
    {
      int32_t value=lnumber_value(CAR(args)->Eval());
      if (value)                                   // do we have ammo ?
      {
    o->lvars[fire_delay1]=6;
    if (player_fire_weapon(o,ammo_type(o->otype),NULL,o->lvars[point_angle],
                   o->otype==DFRIS ? large_fire_off : small_fire_off ))
          ret=LNumber::Create(-1);
    else ret=LNumber::Create(0);
      } else ret=LNumber::Create(0);
    } else ret=LNumber::Create(0);                // can't fire yet, return 0 ammo subtract
  }
  return ret;
}

static int climb_handler(game_object *, int xm, int ym, int but);

void *plaser_ufun(void *args)
{
  game_object *o=current_object;
  PtrRef r1(args);
  void *signal=CAR(args);  args=CDR(args);
  void *ret=NULL;

  if (signal==l_FIRE)
  {
    if (!o->lvars[fire_delay1])                   // make sur we are not waiting of previous fire
    {
      int32_t value=lnumber_value(CAR(args)->Eval());
      if (value)                                   // do we have ammo ?
      {
    o->lvars[fire_delay1]=2;
    if (player_fire_weapon(o,PLASMA,NULL,o->lvars[point_angle],small_fire_off))
          ret=LNumber::Create(-1);
    else ret=LNumber::Create(0);
      } else ret=LNumber::Create(0);
    } else ret=LNumber::Create(0);                // can't fire yet, return 0 ammo subtract
  }
  return ret;
}

void *lsaber_ufun(void *args)
{
  game_object *o=current_object;
  PtrRef r1(args);
  void *signal=CAR(args);  args=CDR(args);
  void *ret=NULL;

  if (signal==l_FIRE)
  {
    if (!o->lvars[fire_delay1])                   // make sur we are not waiting of previous fire
    {
      int32_t value=lnumber_value(CAR(args)->Eval());
      if (value)                                   // do we have ammo ?
      {
    o->lvars[fire_delay1]=1;
    if (player_fire_weapon(o,LSABER,NULL,o->lvars[point_angle]+(current_level->tick_counter()&7)-8,
                   small_fire_off))
          ret=LNumber::Create(-1);
    else ret=LNumber::Create(0);
      } else ret=LNumber::Create(0);
    } else ret=LNumber::Create(0);                // can't fire yet, return 0 ammo subtract
  }
  return ret;
}



void *player_rocket_ufun(void *args)
{
  game_object *o=current_object;
  PtrRef r1(args);
  void *signal=CAR(args);  args=CDR(args);
  void *ret=NULL;
  int xd,yd,cl=0xfffffff,d;
  if (signal==l_FIRE)
  {
    if (!o->lvars[fire_delay1])                   // make sur we are not waiting of previous fire
    {
      int32_t value=lnumber_value(CAR(args)->Eval());
      if (value)                                   // do we have ammo ?
      {
    o->lvars[fire_delay1]=6;
    game_object *target=NULL,*p,*bot=o->total_objects() ? o->get_object(0) : 0;
    if (bad_guy_array)
    {
      game_object *other=current_object->total_objects() ? current_object->get_object(0) : 0;
      for (p=current_level->first_active_object(); p; p=p->next_active)
      {
        xd=abs(p->x-o->x);
        yd=abs(p->y-o->y);
        if (xd<160 && yd<130 && bad_guy_array[p->otype] && p!=other)
        {
          if (p->targetable() &&
          !(p->otype==S_ROCKET && p->total_objects() && p->get_object(0)==bot))  // don't track onto own missles
          {
        d=xd*xd+yd*yd;
        if (d<cl)
        {
          cl=d;
          target=p;
        }
          }
        }
      }
    }
    if (player_fire_weapon(o,ROCKET,target,o->lvars[point_angle],large_fire_off))
          ret=LNumber::Create(-1);
    else ret=LNumber::Create(0);

      } else ret=LNumber::Create(0);
    } else ret=LNumber::Create(0);                // can't fire yet, return 0 ammo subtract
  }
  return ret;
}

static int player_move(game_object *o, int xm, int ym, int but)
{
  if (!o->lvars[in_climbing_area])
  {
    if (o->state==S_climbing)
    {
      o->set_gravity(1);
      o->set_state(run_jump_fall);
    }
    o->next_picture();
    return o->mover(xm,ym,but);
  } else return climb_handler(o,xm,ym,but);

}


static void undo_special_power(game_object *o)
{
  switch (o->lvars[special_power])
  {
    case SNEAKY_POWER :
    {
      if (o->lvars[used_special_power]>0)
        o->lvars[used_special_power]--;
    } break;
    case FAST_POWER :
    {
      o->lvars[used_special_power]=0;
    } break;
  }
}

static void do_special_power(game_object *o, int xm, int ym, int but, game_object *top)
{
  switch (o->lvars[special_power])
  {
    case FLY_POWER :
    {
      game_object *cloud=create(S_CLOUD,o->x+o->direction*-10,o->y+jrand()%5);
      if (current_level)
        current_level->add_object(cloud);
      o->set_state(run_jump);
      o->set_gravity(1);
      o->set_yacel(0);
      if (o->yvel()>0) o->set_yvel(o->yvel()/2);
      if (ym<0)
        o->set_yvel(o->yvel()-3);
      else
        o->set_yvel(o->yvel()-2);
      the_game->play_sound(S_FLY_SND,32,o->x,o->y);
    } break;
    case FAST_POWER :
    {
      if ((current_level->tick_counter()%16)==0)
      the_game->play_sound(S_SPEED_SND,100,o->x,o->y);

      o->lvars[used_special_power]=1;
      o->lvars[last1_x]=o->x;
      o->lvars[last1_y]=o->y;
      int32_t oyvel=o->yvel();
      int in=o->lvars[in_climbing_area];

      player_move(o,xm,ym,but);
      if (ym<0 && !oyvel && o->yvel()<0)             // if they just jumped, make them go higher
      o->set_yvel(o->yvel()+o->yvel()/3);
      o->lvars[in_climbing_area]=in;

      o->lvars[last2_x]=o->x;
      o->lvars[last2_x]=o->y;
    } break;
    case SNEAKY_POWER :
    {
      if (o->lvars[used_special_power]<15)
        o->lvars[used_special_power]++;
    } break;
  }
}

static int climb_off_handler(game_object *o)
{
  if (o->next_picture())
    o->controller()->pan_y-=4;
  else
  {
    o->y-=28;
    o->controller()->pan_y += 28;
    o->controller()->m_lastpos.y -= 28;
    o->set_state(stopped);
  }
  return 0;
}


static int climb_on_handler(game_object *o)
{
  if (o->next_picture())
    o->controller()->pan_y+=4;
  else
    o->set_state((character_state)S_climbing);
  return 0;
}

static int climb_handler(game_object *o, int xm, int ym, int but)
{
  int yd=o->lvars[in_climbing_area];  // see how from the top we are
  o->lvars[in_climbing_area]=0;          // set 0, ladders will set back to proper if still in area
  if (o->state==S_climb_off)
    climb_off_handler(o);
  else if (o->state==S_climb_on)
    climb_on_handler(o);
  else
  {
    if (o->state==S_climbing)
    {
      if (ym>0)                       // going down
      {

    if (o->current_frame==0) o->current_frame=9;
      o->current_frame--;

/*    if (o->lvars[special_power]==FAST_POWER)
    {
      int32_t xv=0,yv=4;
      o->try_move(o->x,o->y,xv,yv,1);
      if (yv==4)
        o->y+=3;
      else
      {
        o->set_gravity(1);
          o->set_state(run_jump_fall);
      }
    }
    else */ o->y+=3;


      } else if (ym<0)
      {
    if (yd<32)
      o->set_state((character_state)S_climb_off);
    else
    {
      if (!o->next_picture()) o->set_state((character_state)S_climbing);
      o->y-=3;
    }
      }
      if (xm)                     // trying to get off the ladder, check to see if that's ok
      {
    int32_t x2=0,y2=-20;
    o->try_move(o->x,o->y,x2,y2,3);
    if (y2==-20)
    {
      o->set_gravity(1);
      if (ym>=0)
        o->set_state(run_jump_fall);
      else
      { o->set_state(run_jump);
        o->set_yvel(get_ability(o->otype,jump_yvel));
      }
    }
      }
    }  else if (ym>0 && yd<10)
    {
      o->y+=28;
      o->controller()->pan_y-=28;
      o->controller()->m_lastpos.y += 28;
      o->set_state((character_state)S_climb_on);
    }
    else if (o->yvel()>=0 && (ym>0 || (ym<0 && yd>8)))
    {
      o->set_state((character_state)S_climbing);
      o->set_gravity(0);
      o->set_xvel(0);
      o->set_yvel(0);
      o->set_xacel(0);
      o->set_yacel(0);
      return 0;
    } else
    {
      o->next_picture();
      return o->mover(xm,ym,but);
    }
  }
  return 0;
}


void *cop_mover(int xm, int ym, int but)
{

  int ret=0;
  game_object *o=current_object,*top;
  if (o->controller() && o->controller()->freeze_time)
  {
    o->controller()->freeze_time--;
    if (but || o->controller()->key_down(JK_SPACE) || o->controller()->key_down(JK_ENTER))
      o->controller()->freeze_time=0;
  }
  else
  {
    if (!o->total_objects())                  // if no top create one
    {
      top=create(S_MGUN_TOP,o->x,o->y,0,0);
      current_level->add_object_after(top,o);
      o->add_object(top);
      top->add_object(o);
    } else top=o->get_object(0);

    if (o->yvel()>10)
    {
      o->set_yacel(0);
      o->set_yvel(o->yvel()-1);            // terminal velocity
    }

    if (o->aistate()==0)  // just started, wait for button
    {
      o->set_aistate(1);
    } else if (o->aistate()==1)         // normal play
    {
      if (o->hp()==0)
      {
    o->set_aistate(2);                // go to deing state
    o->set_state(dead);
      }
      else
      {
    if (o->hp()<40 && (current_level->tick_counter()%16)==0) // if low on health play heart beat
      the_game->play_sound(S_LOW_HEALTH_SND,127,o->x,o->y);
    else if (o->hp()<15 && (current_level->tick_counter()%8)==0) // if low on health play heart beat
      the_game->play_sound(S_LOW_HEALTH_SND,127,o->x,o->y);
    else if (o->hp()<7 && (current_level->tick_counter()%4)==0) // if low on health play heart beat
      the_game->play_sound(S_LOW_HEALTH_SND,127,o->x,o->y);

    if (but&1)
        do_special_power(o,xm,ym,but,top);
    else
    undo_special_power(o);
    ret=player_move(o,xm,ym,but);
    top->x=o->x;
    top->y=o->y+29-top->picture()->Size().y;

    if ((but&2) && !o->lvars[is_teleporting] && o->state!=S_climbing && o->state!=S_climb_off)
    {
      void *args=NULL;
      PtrRef r1(args);
      view *v=o->controller();

      push_onto_list(LNumber::Create(v->weapon_total(v->current_weapon)),args);
      push_onto_list(l_FIRE,args);

      current_object=top;
      void *ret = ((LSymbol *)figures[top->otype]->get_fun(OFUN_USER_FUN))->EvalFunction(args);
      current_object=o;
      v->add_ammo(v->current_weapon,lnumber_value(ret));
    }
      }
    }
	else if(o->aistate()==3)
	{
		//AR "Press SPACEBAR to continue", reset after death
		if(!o->controller() || o->controller()->key_down(JK_SPACE) || o->controller()->key_down(JK_ENTER))
		{
			// call the user function to reset the player
			((LSymbol *)l_restart_player)->EvalFunction(NULL);
			o->controller()->reset_player();
      o->controller()->reset_keymap();
      o->set_aistate(0);
    }
		else if(o->controller() && o->controller()->local_player())
			the_game->show_help(symbol_str("space_cont"));

	}
	else o->set_aistate(o->aistate()+1);
  }

  return LNumber::Create(ret);
}



void *ladder_ai()
{
  view *f=player_list;
  game_object *o=current_object;
  if (o->total_objects())
  {
    game_object *other=o->get_object(0);
    for (; f; f=f->next)
    {
      int mex=f->m_focus->x;
      int mey=f->m_focus->y;

      if (o->x<=mex && o->y<=mey && other->x>=mex && other->y>=mey)
      {
    if (f->m_focus->state==S_climbing)
      f->m_focus->x=(o->x+other->x)/2;
        f->m_focus->lvars[in_climbing_area]=mey-o->y;
      }
    }
  }
  return true_symbol;
}



void *player_draw(int just_fired_var, int num)
{
  game_object *o=current_object;
  if (num==0)
  {
    if (o->lvars[just_fired_var])
    {
      o->draw_tint(S_bright_tint);
      o->lvars[just_fired_var]=0;
    } else
      o->drawer();
  }
  else
  {
    if (o->lvars[just_fired_var])
    {
      o->draw_double_tint(lnumber_value(((LArray *)((LSymbol *)l_player_tints)->GetValue())->Get(num)), S_bright_tint);
      o->lvars[just_fired_var]=0;
    } else
      o->draw_tint(lnumber_value(((LArray *)((LSymbol *)l_player_tints)->GetValue())->Get(num)));
  }
  return NULL;
}


void *top_draw()
{
  game_object *o=current_object;
  if (o->total_objects())
  {
    game_object *bot=o->get_object(0);
    if (bot->state==stopped  || bot->state==running ||
    bot->state==run_jump || bot->state==run_jump_fall ||
    bot->state==end_run_jump)
    {
      int oldy=o->y;
      o->x=bot->x;
      if (bot->direction<0)
        o->x+=4;
      o->y=bot->y+29-bot->picture()->Size().y;

      view *v = bot->controller();
      bool use_smooth_draw = (v != nullptr && RemasterConfig::get().enabled &&
                              bot->lvars[special_power] != SNEAKY_POWER);

      if (use_smooth_draw)
      {
        signed char *fire_off = (o->otype == S_DFRIS_TOP || o->otype == S_ROCKET_TOP || o->otype == S_BFG_TOP)
                                    ? large_fire_off
                                    : small_fire_off;
        int iy = fire_off[1];
        int ix = fire_off[6 * 2];

        int shoulder_world_x = o->x + ix;
        int shoulder_world_y = bot->y - iy;

        int target_x = v->pointer_x;
        int target_y = v->pointer_y;
        if (target_x == 0 && target_y == 0 && wm && the_game)
        {
          ivec2 mp = wm->GetMousePos();
          ivec2 mg = the_game->MouseToGame(mp, v);
          target_x = mg.x;
          target_y = mg.y;
        }

        float dx = (float)(target_x - shoulder_world_x);
        float dy = (float)(shoulder_world_y - target_y); // dy > 0 is up
        float len = std::hypot(dx, dy);

        float aim_deg = 0.0f;
        if (len > 0.001f)
        {
          aim_deg = std::atan2(dy, dx) * 180.0f / 3.141592653589793f;
          if (aim_deg < 0.0f) aim_deg += 360.0f;
        }
        else
        {
          aim_deg = (bot->direction < 0) ? 180.0f : 0.0f;
        }

        // Find nearest base frame among 24 frames
        int best_frame = 0;
        float best_diff = 360.0f;
        float best_base_deg = 0.0f;

        for (int i = 0; i < 24; i++)
        {
          float vx = (float)(fire_off[i * 2] - ix);
          float vy = (float)(fire_off[i * 2 + 1] - iy);
          float b_deg = std::atan2(vy, vx) * 180.0f / 3.141592653589793f;
          if (b_deg < 0.0f) b_deg += 360.0f;

          float diff = std::abs(aim_deg - b_deg);
          if (diff > 180.0f) diff = 360.0f - diff;

          if (diff < best_diff)
          {
            best_diff = diff;
            best_frame = i;
            best_base_deg = b_deg;
          }
        }

        o->current_frame = best_frame;

        float delta_deg = aim_deg - best_base_deg;
        if (delta_deg > 180.0f) delta_deg -= 360.0f;
        if (delta_deg < -180.0f) delta_deg += 360.0f;

        uint8_t *tint_map = nullptr;
        uint8_t *tint_map2 = nullptr;
        int tint_num = bot->get_tint();
        if (tint_num == 0)
        {
          if (o->lvars[top_just_fired])
          {
            tint_map = cache.ctint(S_bright_tint)->data;
            o->lvars[top_just_fired] = 0;
          }
        }
        else
        {
          int t_id = lnumber_value(((LArray *)((LSymbol *)l_player_tints)->GetValue())->Get(tint_num));
          tint_map = cache.ctint(t_id)->data;
          if (o->lvars[top_just_fired])
          {
            tint_map2 = cache.ctint(S_bright_tint)->data;
            o->lvars[top_just_fired] = 0;
          }
        }

        TransImage *cpict = o->picture();
        int spr_w = cpict->Size().x;
        int spr_h = cpict->Size().y;

        ivec2 dest_pos((o->direction < 0 ? o->x - (spr_w - o->x_center() - 1) : o->x - o->x_center()) - current_vxadd,
                       o->y - spr_h + 1 - current_vyadd);

        if (std::abs(delta_deg) < 0.25f)
        {
          if (tint_map2 && tint_map)
            cpict->PutDoubleRemap(main_screen, dest_pos, tint_map, tint_map2);
          else if (tint_map)
            cpict->PutRemap(main_screen, dest_pos, tint_map);
          else
            cpict->PutImage(main_screen, dest_pos);
        }
        else
        {
          // Continuous smooth rotation around shoulder pivot
          float rot_rad = -delta_deg * (3.141592653589793f / 180.0f);
          float cos_a = std::cos(rot_rad);
          float sin_a = std::sin(rot_rad);

          int pvt_screen_x = shoulder_world_x - current_vxadd;
          int pvt_screen_y = shoulder_world_y - current_vyadd;

          uint8_t src_pixels[64 * 64];
          memset(src_pixels, 0, sizeof(src_pixels));
          uint8_t *p = cpict->Data();
          for (int py = 0; py < spr_h; py++)
          {
            for (int px = 0; px < spr_w; )
            {
              int skip = *p++;
              px += skip;
              if (px >= spr_w) break;
              int run = *p++;
              for (int r = 0; r < run; r++, px++)
              {
                src_pixels[py * 64 + px] = *p++;
              }
            }
          }

          // Calculate rotated bounding box from unrotated corners
          float corners_x[4] = { (float)dest_pos.x, (float)(dest_pos.x + spr_w), (float)dest_pos.x, (float)(dest_pos.x + spr_w) };
          float corners_y[4] = { (float)dest_pos.y, (float)dest_pos.y, (float)(dest_pos.y + spr_h), (float)(dest_pos.y + spr_h) };

          float min_rx = 1e9f, max_rx = -1e9f, min_ry = 1e9f, max_ry = -1e9f;
          for (int c = 0; c < 4; c++)
          {
            float rel_x = corners_x[c] - (float)pvt_screen_x;
            float rel_y = corners_y[c] - (float)pvt_screen_y;
            float rx = (float)pvt_screen_x + cos_a * rel_x - sin_a * rel_y;
            float ry = (float)pvt_screen_y + sin_a * rel_x + cos_a * rel_y;
            if (rx < min_rx) min_rx = rx;
            if (rx > max_rx) max_rx = rx;
            if (ry < min_ry) min_ry = ry;
            if (ry > max_ry) max_ry = ry;
          }

          int min_dst_x = (int)std::floor(min_rx) - 1;
          int max_dst_x = (int)std::ceil(max_rx) + 1;
          int min_dst_y = (int)std::floor(min_ry) - 1;
          int max_dst_y = (int)std::ceil(max_ry) + 1;

          ivec2 clip_aa, clip_bb;
          main_screen->GetClip(clip_aa, clip_bb);
          min_dst_x = std::max(min_dst_x, clip_aa.x);
          max_dst_x = std::min(max_dst_x, clip_bb.x - 1);
          min_dst_y = std::max(min_dst_y, clip_aa.y);
          max_dst_y = std::min(max_dst_y, clip_bb.y - 1);

          if (min_dst_x <= max_dst_x && min_dst_y <= max_dst_y)
          {
            main_screen->Lock();
            for (int dy = min_dst_y; dy <= max_dst_y; dy++)
            {
              float ry = (float)(dy - pvt_screen_y);
              uint8_t *dst_row = main_screen->scan_line(dy);
              for (int dx = min_dst_x; dx <= max_dst_x; dx++)
              {
                float rx = (float)(dx - pvt_screen_x);

                // Inverse rotation
                float unrot_rx =  cos_a * rx + sin_a * ry;
                float unrot_ry = -sin_a * rx + cos_a * ry;

                int sx = (int)std::round((float)pvt_screen_x + unrot_rx) - dest_pos.x;
                int sy = (int)std::round((float)pvt_screen_y + unrot_ry) - dest_pos.y;

                if (sx >= 0 && sx < spr_w && sy >= 0 && sy < spr_h)
                {
                  uint8_t col = src_pixels[sy * 64 + sx];
                  if (col != 0)
                  {
                    if (tint_map) col = tint_map[col];
                    if (tint_map2) col = tint_map2[col];
                    dst_row[dx] = col;
                  }
                }
              }
            }
            main_screen->Unlock();
            main_screen->AddDirty(ivec2(min_dst_x, min_dst_y), ivec2(max_dst_x + 1, max_dst_y + 1));
          }
        }
      }
      else
      {
        void *ret=NULL;
        PtrRef r1(ret);

        push_onto_list(LNumber::Create(bot->get_tint()),ret);

        if (bot->lvars[special_power]==SNEAKY_POWER)
        {
          if (bot->lvars[used_special_power]==0)
            player_draw(top_just_fired,bot->get_tint());
          else if (bot->lvars[used_special_power]<15)
            o->draw_trans(bot->lvars[used_special_power],16);
          else
            o->draw_predator();
        } else
          ((LSymbol *)l_player_draw)->EvalFunction(ret);
      }

      o->y=oldy;
      if (bot->direction<0)
        o->x-=4;
    }
  }
  return NULL;
}



void *bottom_draw()
{
  game_object *o=current_object;

  if (o->lvars[r_ramp] || o->lvars[g_ramp] || o->lvars[b_ramp])
  {
    int r=o->lvars[r_ramp];
    if (r>7) r-=7;
    else r=0;
    o->lvars[r_ramp]=r;

    int g=o->lvars[g_ramp];
    if (g>7) g-=7;
    else g=0;
    o->lvars[g_ramp]=g;

    int b=o->lvars[b_ramp];
    if (b>7) b-=7;
    else b=0;
    o->lvars[b_ramp]=b;

    palette *p=pal->copy();
    uint8_t *addr=(uint8_t *)p->addr();
    int ra,ga,ba;

    for (int i=0; i<256; i++)
    {
      ra=(int)*addr+r; if (ra>255) ra=255; else if (ra<0) r=0; *addr=(uint8_t)ra; addr++;
      ga=(int)*addr+g; if (ga>255) ga=255; else if (ga<0) g=0; *addr=(uint8_t)ga; addr++;
      ba=(int)*addr+b; if (ba>255) ba=255; else if (ba<0) b=0; *addr=(uint8_t)ba; addr++;
    }
    p->load();
    delete p;
  }

  if (o->aistate()>0)
  {
    switch (o->lvars[special_power])
    {
      case NO_POWER :
      { player_draw(just_fired,o->get_tint()); } break;

      case HEALTH_POWER :
      {
    player_draw(just_fired,o->get_tint());
    if (o->controller() && o->controller()->local_player())
      main_screen->PutImage(cache.img(S_health_image),
                            ivec2(o->controller()->m_bb.x - 20,
                                  o->controller()->m_aa.y + 5), 1);
      } break;
      case FAST_POWER :
      {
    ((LSymbol *)l_draw_fast)->EvalFunction(NULL);
    int old_state=o->state;
    switch (o->state)
    {
      case stopped : o->state=(character_state)S_fast_stopped; break;
      case running : o->state=(character_state)S_fast_running; break;
      case start_run_jump : o->state=(character_state)S_fast_start_run_jump; break;
      case run_jump : o->state=(character_state)S_fast_run_jump; break;
      case run_jump_fall : o->state=(character_state)S_fast_run_jump_fall; break;
      case end_run_jump : o->state=(character_state)S_fast_end_run_jump; break;
      default: break;
    }

    player_draw(just_fired,o->get_tint());
    o->state=(character_state)old_state;
    if (o->controller() && o->controller()->local_player())
      main_screen->PutImage(cache.img(S_fast_image),
                            ivec2(o->controller()->m_bb.x - 20,
                                  o->controller()->m_aa.y + 5), 1);
      } break;
      case FLY_POWER :
      {
    int old_state=o->state;
    switch (o->state)
    {
      case stopped : o->state=(character_state)S_fly_stopped; break;
      case running : o->state=(character_state)S_fly_running; break;
      case start_run_jump : o->state=(character_state)S_fly_start_run_jump; break;
      case run_jump : o->state=(character_state)S_fly_run_jump; break;
      case run_jump_fall : o->state=(character_state)S_fly_run_jump_fall; break;
      case end_run_jump : o->state=(character_state)S_fly_end_run_jump; break;
      default: break;
    }

    player_draw(just_fired,o->get_tint());
    o->state=(character_state)old_state;

    if (o->controller() && o->controller()->local_player())
      main_screen->PutImage(cache.img(S_fly_image),
                            ivec2(o->controller()->m_bb.x - 20,
                                  o->controller()->m_aa.y + 5), 1);
      } break;
      case SNEAKY_POWER :
      {
    if (o->lvars[used_special_power]==0)
      player_draw(just_fired,o->get_tint());
    else if (o->lvars[used_special_power]<15)
      o->draw_trans(o->lvars[used_special_power],16);
    else
      o->draw_predator();

    if (o->controller() && o->controller()->local_player())
      main_screen->PutImage(cache.img(S_sneaky_image),
                            ivec2(o->controller()->m_bb.x - 20,
                                  o->controller()->m_aa.y + 5), 1);
      } break;
    }
  }
  return NULL;
}



void *sgun_ai()
{
  game_object *o=current_object;

  if (o->lvars[sgb_lifetime]==0)
    return NULL;
  o->lvars[sgb_lifetime]--;

  o->lvars[sgb_lastx]=o->x;
  o->lvars[sgb_lasty]=o->y;
  o->lvars[sgb_speed]=o->lvars[sgb_speed]*6/5;

  int32_t ang=o->lvars[sgb_angle];
  int32_t mag=o->lvars[sgb_speed];

  int32_t xvel=(lisp_cos(ang))*(mag);
  current_object->set_xvel(xvel>>16);
  current_object->set_fxvel((xvel&0xffff)>>8);
  int32_t yvel=-(lisp_sin(ang))*(mag);
  current_object->set_yvel(yvel>>16);
  current_object->set_fyvel((yvel&0xffff)>>8);


  int whit=0;
  game_object *who=o->bmove(whit, o->total_objects() ? o->get_object(0) : 0);

  if (whit || (who && figures[who->otype]->get_cflag(CFLAG_UNACTIVE_SHIELD) && who->total_objects() &&
           who->get_object(0)->aistate()==0))
  {
    o->lvars[sgb_lifetime]=0;
    if (RemasterConfig::get().enabled)
    {
      int p_type = 0;
      extern class palette *pal;
      if (pal && pal->red(o->lvars[sgb_bright_color]) > 200 && pal->green(o->lvars[sgb_bright_color]) < 60)
        p_type = 2; // red laser
      float nx = 0.0f, ny = 0.0f;
      if (last_hit_has_normal)
      {
        nx = last_hit_normal_x;
        ny = last_hit_normal_y;
        last_hit_has_normal = false;
      }
      RemasterParticles::get().spawn_bullet_impact((float)o->x, (float)o->y, (float)ang, p_type, nx, ny);
    }
    else
    {
      game_object *n=create(S_EXPLODE5,o->x+jrand()%4,o->y+jrand()%4);
      current_level->add_object(n);
    }
  } else if (who && figures[who->otype]->get_cflag(CFLAG_HURTABLE))
  {
    o->lvars[sgb_lifetime]=0;
    if (RemasterConfig::get().enabled)
    {
      RemasterParticles::get().spawn_flesh_impact((float)o->x, (float)o->y, (float)ang);
    }
    game_object *n=create(S_EXPLODE3,o->x+jrand()%4,o->y+jrand()%4);
    current_level->add_object(n);
    who->do_damage(5,o,o->x,o->y,(lisp_cos(ang)*10)>>16,(lisp_sin(ang)*10)>>16);
  }
  return true_symbol;
}



void *mover_ai()
{
  game_object *o=current_object;
  if (o->total_objects()==2)
  {
    if (o->aistate()<2)
    {
      game_object *obj=o->get_object(1);
      o->remove_object(obj);
      game_object *d=o->get_object(0);
      d->add_object(obj);
      d->set_aistate(d->aitype());
    } else
    {
      o->set_aistate(o->aistate()-1);
      game_object *d=o->get_object(0);
      game_object *obj=o->get_object(1);

      obj->x=d->x-(d->x-o->x)*o->aistate()/o->aitype();
      obj->y=d->y-(d->y-o->y)*o->aistate()/o->aitype();
    }
  }
  return true_symbol;
}


void *respawn_ai()
{
 game_object *o=current_object;
 int x=o->total_objects();
 if (x)
 {
   game_object *last=o->get_object(x-1);
   if (last->x==o->x && last->y==o->y)
   {
     if (last->fade_count())
       last->set_fade_count(last->fade_count()-1);
     o->set_aistate_time(0);
   } else if (o->aistate_time()>o->xvel())
   {
     int type=o->get_object(jrandom(x))->otype;
     game_object *n=create(type,o->x,o->y);
     current_level->add_object(n);
     o->add_object(n);
     n->set_fade_count(15);
     o->set_aistate_time(0);
   }
 }
 return true_symbol;
}

static int compare_players(const void *a, const void *b)
{
  if  ( ((view **)a)[0]->kills > ((view **)b)[0]->kills)
    return -1;
  else if  ( ((view **)a)[0]->kills < ((view **)b)[0]->kills)
    return 1;
  else if (((view **)a)[0]->player_number > ((view **)b)[0]->player_number)
    return -1;
  else if (((view **)a)[0]->player_number < ((view **)b)[0]->player_number)
    return 1;
  else return 0;
}

void *score_draw()
{
  view *sorted_players[16],*local=NULL;
  int tp=0;
  view *f=player_list;
  for (; f; f=f->next)
  {
    sorted_players[tp]=f;
    tp++;
    if (f->local_player()) local=f;
  }

  JCFont *fnt=wm->font();
  if (local)
  {
    qsort(sorted_players,tp,sizeof(view *),compare_players);

    ivec2 pos = local->m_aa;
    char msg[200];

    int i;
    for (i=0; i<tp; i++)
    {
      int color=lnumber_value(((LArray *)((LSymbol *)l_player_text_color)->GetValue())->Get(sorted_players[i]->get_tint()));
      sprintf(msg,"%3ld %s",(long)sorted_players[i]->kills,sorted_players[i]->name);
      if (sorted_players[i]==local)
        strcat(msg," <<");

      fnt->PutString(main_screen, pos, msg, color);
      pos.y += fnt->Size().y;
    }
  }
  return NULL;
}


extern void fade_in(image *im, int steps);
extern void fade_out(int steps);

void *show_kills()
{
	//AR I think this is for multiplayer, the numbers don't match enemies in game

  fade_out(8);
  wm->SetMousePos(ivec2(0, 0));
  main_screen->clear();
  image *im=cache.img(cache.reg("art/frame.spe","end_level_screen",SPEC_IMAGE,1));
  main_screen->PutImage(im, ivec2(0, 0));
  int x1=im->Size().x+1,y1=0,y2=main_screen->Size().y;
  JCFont *fnt=wm->font();

  view *v=player_list; int tp=0,i;
  for (v=player_list; v; v=v->next) tp++;

  int y=(y1+y2)/2-(tp+2)*fnt->Size().y/2,x=x1+10;
  char const *header_str = symbol_str("score_header");
  fnt->PutString(main_screen, ivec2(x, y), header_str, wm->bright_color());
  y += fnt->Size().y;

  main_screen->WidgetBar(ivec2(x, y + 2),
                         ivec2(x + strlen(header_str) * fnt->Size().x,
                               y + fnt->Size().y - 3),
                         wm->bright_color(), wm->medium_color(),
                         wm->dark_color());
  y += fnt->Size().y;
  v = player_list;
  for (i=0; i<tp; i++)
  {
    enum { NAME_LEN=18 } ;
    int color=lnumber_value(((LArray *)((LSymbol *)l_player_text_color)->GetValue())->Get(v->get_tint()));
    char max_name[NAME_LEN];
    strncpy(max_name,v->name,NAME_LEN-1);
    max_name[NAME_LEN-1]=0;
    char msg[100];


    sprintf(msg,"%-17s %3ld  %3ld",max_name,(long)v->kills,(long)(v->tkills+v->kills));
    fnt->PutString(main_screen, ivec2(x, y), msg, color);

    y += fnt->Size().y;
    v = v->next;
  }

  wm->flush_screen();
  Timer now; now.WaitMs(4000);   // wait 4 seconds

  return NULL;
}

bool get_player_muzzle_pos(view *v, int &muzzle_x, int &muzzle_y, float &dir_x, float &dir_y)
{
  if (!v || !v->m_focus)
    return false;

  game_object *bot = v->m_focus;
  game_object *top = (bot->total_objects() > 0) ? bot->get_object(0) : nullptr;

  if (!top || top->current_frame < 0 || top->current_frame >= 24)
  {
    muzzle_x = v->x_center();
    muzzle_y = v->y_center() - 16;
    dir_x = (bot->direction < 0) ? -1.0f : 1.0f;
    dir_y = 0.0f;
    return true;
  }

  signed char *fire_off = (top->otype == S_DFRIS_TOP || top->otype == S_ROCKET_TOP || top->otype == S_BFG_TOP)
                              ? large_fire_off
                              : small_fire_off;

  int bx = bot->x;
  if (bot->direction < 0)
    bx += 4;

  int iy = fire_off[1];
  int ix = fire_off[6 * 2];
  int shoulder_x = bx + ix;
  int shoulder_y = bot->y - iy;

  int target_x = v->pointer_x;
  int target_y = v->pointer_y;
  if (target_x == 0 && target_y == 0 && wm && the_game)
  {
    ivec2 mp = wm->GetMousePos();
    ivec2 mg = the_game->MouseToGame(mp, v);
    target_x = mg.x;
    target_y = mg.y;
  }

  float dx = (float)(target_x - shoulder_x);
  float dy = (float)(shoulder_y - target_y); // dy > 0 is up
  float len = std::hypot(dx, dy);

  float aim_rad = 0.0f;
  if (len > 0.001f)
  {
    aim_rad = std::atan2(dy, dx);
    dir_x = dx / len;
    dir_y = -dy / len; // In screen coordinates, +Y is downwards
  }
  else
  {
    dir_x = (bot->direction < 0) ? -1.0f : 1.0f;
    dir_y = 0.0f;
    aim_rad = (bot->direction < 0) ? 3.141592653589793f : 0.0f;
  }

  // Exact distance from shoulder pivot to barrel tip for the active weapon frame
  float vx = (float)(fire_off[top->current_frame * 2] - ix);
  float vy = (float)(fire_off[top->current_frame * 2 + 1] - iy);
  float R = std::hypot(vx, vy);

  // Calibrate R to physical visual barrel tip (fire_off had collision buffer in 1995)
  float R_visual = std::max(4.0f, R - 8.0f);

  muzzle_x = shoulder_x + (int)std::round(R_visual * std::cos(aim_rad));
  muzzle_y = shoulder_y - (int)std::round(R_visual * std::sin(aim_rad));

  return true;
}


