/*  $Header: /home/cvsroot/dvipdfmx/src/type1.c,v 1.19 2004/02/15 12:59:43 hirata Exp $

    This is dvipdfmx, an eXtended version of dvipdfm by Mark A. Wicks.

    Copyright (C) 2002 by Jin-Hwan Cho and Shunsaku Hirata,
    the dvipdfmx project team <dvipdfmx@project.ktug.or.kr>

    Copyright (C) 1998, 1999 by Mark A. Wicks <mwicks@kettering.edu>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
*/

#include <string.h>
/* fabs, sqrt ... */
#include <math.h>

#include "system.h"
#include "mfileio.h"
#include "mem.h"
#include "error.h"
#include "pdfobj.h"
#include "numbers.h"

#include "encodings.h"
#include "unicode.h"

/* mangle_name() */
#include "dpxutil.h"

#include "t1crypt.h"
#include "pst_obj.h"
#include "pst.h"

#include "cff_limits.h"
#include "cff_types.h"
#include "cff_dict.h"
#include "cff.h"

#define TYPE1_NAME_LEN_MAX 127

#define FONT_FLAG_FIXEDPITCH (1 << 0)  /* Fixed-width font */
#define FONT_FLAG_SERIF      (1 << 1)  /* Serif font */
#define FONT_FLAG_SYMBOLIC   (1 << 2)  /* Symbolic font */
#define FONT_FLAG_SCRIPT     (1 << 3)  /* Script font */
#define FONT_FLAG_STANDARD   (1 << 5)  /* Uses the Adobe Standard Character Set */
#define FONT_FLAG_ITALIC     (1 << 6)  /* Italic */
#define FONT_FLAG_ALLCAP     (1 << 16) /* All-cap font */
#define FONT_FLAG_SMALLCAP   (1 << 17) /* Small-cap font */
#define FONT_FLAG_FORCEBOLD  (1 << 18) /* Force bold at small text sizes */

static int __verbose = 0;

#define TYPE1FONT_DEBUG_STR "Type1"
#define TYPE1FONT_DEBUG     3

void
Type1Font_set_verbose (void)
{
  __verbose++;
}

#define TYPE1FONT_FLAG_BASEFONT (1 << 0)
struct Type1Font 
{
  char    *ident;
  char    *fontname;
  char    *filename;
  int      embed;
  int      encoding_id;
  char    *used_chars;
  /* */
  int      flags;
  /* PDF Objects */
  pdf_obj *indirect;
  pdf_obj *fontdict;
  pdf_obj *descriptor;
};

typedef struct Type1Font Type1Font;

Type1Font *
Type1Font_new (void)
{
  Type1Font *font;

  font = NEW(1, Type1Font);
  font->ident    = NULL;
  font->fontname = NULL;
  font->filename = NULL;
  font->embed    = 1;
  font->encoding_id = -1;
  font->used_chars  = NULL;

  font->flags = 0;

  font->indirect   = NULL;
  font->fontdict   = NULL;
  font->descriptor = NULL;

  return font;
}

void
Type1Font_flush (Type1Font *font)
{
  if (!font)
    return;

  if (font->indirect)   pdf_release_obj(font->indirect);
  if (font->fontdict)   pdf_release_obj(font->fontdict);
  if (font->descriptor) pdf_release_obj(font->descriptor);

  font->indirect   = NULL;
  font->fontdict   = NULL;
  font->descriptor = NULL;

  return;
}

void
Type1Font_release (Type1Font *font)
{
  if (!font)
    return;

  if (font->ident)      RELEASE(font->ident);
  if (font->fontname)   RELEASE(font->fontname);
  if (font->filename)   RELEASE(font->filename);
  if (font->used_chars) RELEASE(font->used_chars);

  if (font->indirect)   ERROR("%s: Object not flushed.", TYPE1FONT_DEBUG_STR);
  if (font->fontdict)   ERROR("%s: Object not flushed.", TYPE1FONT_DEBUG_STR);
  if (font->descriptor) ERROR("%s: Object not flushed.", TYPE1FONT_DEBUG_STR);

  return;
}

static char *
new_used_chars (void)
{
  char *used_chars;;

  used_chars = NEW(256, char);
  memset(used_chars, 0, 256*sizeof(char));

  return used_chars;
}

pdf_obj *
Type1Font_get_resource (Type1Font *font)
{
  ASSERT(font);

  if (!font->indirect)
    font->indirect = pdf_ref_obj(font->fontdict);

  return pdf_link_obj(font->indirect);
}

char *
Type1Font_get_usedchars (Type1Font *font)
{
  ASSERT(font);

  return font->used_chars;
}

/******************************* cs_type1.c *******************************/
/*
 * Type 1 -> Type 1C
 */
typedef struct {
  int    count;
  long  *offset;
  long  *length;
  card8 *data;
} t1_subr;

typedef struct {
  int use_seac;
  double wx, wy;
  struct {
    double llx, lly, urx, ury;
  } bbox;
  struct {
    double asb, adx, ady;
    card8 bchar, achar;
  } seac;
} t1_ginfo;

static int  cs1_get_metrics (card8 *src, long srclen, t1_subr *subrs,
			     t1_ginfo *ginfo);

static long cs1_convert_charstring (card8 *dst, long dstlen,
				    card8 *src, long srclen, t1_subr *subrs,
				    double default_width, double nominal_width,
				    t1_ginfo *ginfo);

/* Charstring decoder/encoder status codes */
#define CS_OP_NOSUPPORT -4
#define CS_BUFFER_ERROR -3
#define CS_STACK_ERROR  -2
#define CS_PARSE_ERROR  -1
#define CS_PARSE_OK      0
#define CS_PARSE_END     1
#define CS_SUBR_RETURN   2
#define CS_CHAR_END      3

static int status = CS_PARSE_ERROR;

#define DST_NEED(a,b) {if ((a) < (b)) { status = CS_BUFFER_ERROR ; return ; }}
#define SRC_NEED(a,b) {if ((a) < (b)) { status = CS_PARSE_ERROR  ; return ; }}
#define NEED(a,b)     {if ((a) < (b)) { status = CS_STACK_ERROR  ; return ; }}

#define T1_CS_PHASE_INIT 0
#define T1_CS_PHASE_HINT 1
#define T1_CS_PHASE_PATH 2
#define T1_CS_PHASE_FLEX 3

static int phase = -1;
static int nest  = -1;

#ifndef CS_STEM_ZONE_MAX
#define CS_STEM_ZONE_MAX 96
#endif
#ifndef CS_STEM_GROUP_MAX
#define CS_STEM_GROUP_MAX CS_STEM_ZONE_MAX
#endif
#ifndef CS_ARG_STACK_MAX
#define CS_ARG_STACK_MAX 48
#endif
#ifndef PS_ARG_STACK_MAX
/*
 * Counter control may have CS_STEM_ZONE_MAX*2+2 arguments.
 */
#define PS_ARG_STACK_MAX (CS_STEM_ZONE_MAX*2+2)
#endif

typedef struct t1_cpath {
  int     type;
  int     num_args;
  double  args[CS_ARG_STACK_MAX];
  struct t1_cpath *next;
} t1_cpath;

#define HSTEM 0
#define VSTEM 1
typedef struct {
  int id;
  int dir;
  double pos, del;
} t1_stem;

typedef struct {
  int    num_stems;
  double stems[CS_STEM_ZONE_MAX]; /* int */
} t1_stemgroup;

#define T1_CS_FLAG_NONE 0
#define T1_CS_FLAG_USE_HINTMASK (1 << 0)
#define T1_CS_FLAG_USE_CNTRMASK (1 << 1)
#define T1_CS_FLAG_USE_SEAC     (1 << 2)

typedef struct {
  int flags;
  struct {
    double sbx, sby, wx, wy;
  } sbw;
  struct {
    double llx, lly, urx, ury;
  } bbox;
  struct {
    double asb, adx, ady;
    card8 bchar, achar;
  } seac;
  int       num_stems;
  t1_stem   stems[CS_STEM_ZONE_MAX];
  t1_cpath *charpath;
  t1_cpath *lastpath;
} t1_chardesc;

static int cs_stack_top = 0;
static int ps_stack_top = 0;

/* [vh]stem support require one more stack size. */
static double cs_arg_stack[CS_ARG_STACK_MAX+1];
static double ps_arg_stack[PS_ARG_STACK_MAX];

#define CS_HINT_DECL -1
#define CS_FLEX_CTRL -2
#define CS_CNTR_CTRL -3

/*
 * Type 1/2 CharString encoding
 */

/*
 * 1-byte CharString operaotrs:
 *  cs_escape is first byte of two-byte operator
 */

/*      RESERVED      0 */
#define cs_hstem      1
/*      RESERVED      2 */
#define cs_vstem      3
#define cs_vmoveto    4
#define cs_rlineto    5
#define cs_hlineto    6
#define cs_vlineto    7
#define cs_rrcurveto  8
#define cs_closepath  9
#define cs_callsubr   10
#define cs_return     11
#define cs_escape     12
#define cs_hsbw       13
#define cs_endchar    14
/*      RESERVED      15 */
/*      RESERVED      16 */
/*      RESERVED      17 */
#define cs_hstemhm    18
#define cs_hintmask   19
#define cs_cntrmask   20
#define cs_rmoveto    21
#define cs_hmoveto    22
#define cs_vstemhm    23
#define cs_rcurveline 24
#define cs_rlinecurve 25
#define cs_vvcurveto  26
#define cs_hhcurveto  27
/*      SHORTINT      28 : first byte of shortint*/
#define cs_callgsubr  29
#define cs_vhcurveto  30
#define cs_hvcurveto  31

/* 2-byte op. flex 34-37 used. */

/*
 * 2-byte CharString operaotrs:
 *  "dotsection" is obsoleted in Type 2 charstring.
 */

#define cs_dotsection 0
#define cs_vstem3     1
#define cs_hstem3     2
#define cs_and        3
#define cs_or         4
#define cs_not        5
#define cs_seac       6
#define cs_sbw        7
/*      RESERVED      8  */
#define cs_abs        9
#define cs_add        10
#define cs_sub        11
#define cs_div        12
/*      RESERVED      13 */
#define cs_neg        14
#define cs_eq         15
#define cs_callothersubr 16
#define cs_pop        17
#define cs_drop       18
/*      RESERVED      19 */
#define cs_put        20
#define cs_get        21
#define cs_ifelse     22 
#define cs_random     23
#define cs_mul        24
/*      RESERVED      25 */
#define cs_sqrt       26
#define cs_dup        27
#define cs_exch       28
#define cs_index      29
#define cs_roll       30
/*      RESERVED      31 */
/*      RESERVED      32 */
#define cs_setcurrentpoint 33
#define cs_hflex      34
#define cs_flex       35
#define cs_hflex1     36
#define cs_flex1      37

#define IS_PATH_OPERATOR(o) (((o) >= cs_vmoveto && (o) <= cs_closepath) || \
                             ((o) >= cs_rmoveto && (o) <= cs_hvcurveto && \
                              (o) != cs_vstemhm && (o) != cs_callgsubr && (o) != 28)\
                            )

/*
 * Stem:
 *
 *   1. Stems must be sorted in the increasing bottom/left edge order.
 *   2. The encoded values are all relative; The value x(y) of the first
 *      stem is relative to 0 for Type 2 charstring and is relative to
 *      the left(bottom) side-bearing for Type 1 charstring.
 *   3. A width of -20(-21) specifies the top/right(bottom/left) edge
 *      of an edge hint in Type 2 charstring. But the width of 'ghost'
 *      hint in Type 1 charstring is positive with value 20 or 21.
 *   4. The h(v)stemhm MUST be used instead of h(v)stem if charstring
 *      contains hintmask operator.
 *
 * TODO:
 *
 *  Convert ghost hint to edge hint, Counter control for hstem3/vstem3.
 */

static int CDECL
stem_compare (const void *v1, const void *v2)
{
  int cmp = 0;
  t1_stem *s1, *s2;

  s1 = (t1_stem *) v1;
  s2 = (t1_stem *) v2;
  if (s1->dir == s2->dir) {
    if (s1->pos == s2->pos) {
      if (s1->del == s2->del)
	cmp = 0;
      else
	cmp = (s1->del < s2->del) ? -1 : 1;
    } else {
      cmp = (s1->pos < s2->pos) ? -1 : 1;
    }
  } else {
    cmp = (s1->dir == HSTEM) ? -1 : 1;
  }

  return cmp;
}

#define SORT_STEMS(cd) qsort((cd)->stems,(cd)->num_stems,sizeof(t1_stem),stem_compare)

static int
get_stem (t1_chardesc *cd, int stem_id)
{
  int i;

  for (i = 0; i < cd->num_stems; i++) {
    if (cd->stems[i].id == stem_id)
      break;
  }

  return ((i < cd->num_stems) ? i : -1);
}

static int
add_stem (t1_chardesc *cd, double pos, double del, int dir)
{
  int i;

  ASSERT(cd);

  pos += (dir == HSTEM) ? cd->sbw.sby : cd->sbw.sbx;
  for (i = 0; i < cd->num_stems; i++) {
    if (cd->stems[i].dir == dir &&
	cd->stems[i].pos == pos &&
	cd->stems[i].del == del)
      break;
  }
  if (i == cd->num_stems) {
    if (cd->num_stems == CS_STEM_ZONE_MAX)
      return -1;
    cd->stems[i].dir = dir;
    cd->stems[i].pos = pos;
    cd->stems[i].del = del;
    cd->stems[i].id  = cd->num_stems;
    (cd->num_stems)++;
  }

  return cd->stems[i].id;
}


static void
copy_args (double *args1, double *args2, int count)
{
  while (count-- > 0) {
    *args1 = *args2;
    args1++; args2++;
  }
}

/*
 * Stack:
 */
#define LIMITCHECK(n) do {\
                           if (cs_stack_top+(n) > CS_ARG_STACK_MAX) {\
                             status = CS_STACK_ERROR;\
                             return;\
                           }\
                      } while (0)
#define CHECKSTACK(n) do {\
                           if (cs_stack_top < (n)) {\
                             status = CS_STACK_ERROR;\
                             return;\
                           }\
                      } while (0)
#define CLEARSTACK()  do {\
                           cs_stack_top = 0;\
                      } while (0)

/*
 * Path construction:
 */
/* Get operands from cs_arg_stack[] */
static void
add_charpath (t1_chardesc *cd, int type, double *argv, int argn)
{
  t1_cpath *p;

  ASSERT(cd);
  ASSERT(argn <= CS_ARG_STACK_MAX);

  p = NEW(1, t1_cpath);
  p->type     = type;
  p->num_args = argn;
  p->next     = NULL;

  while (argn-- > 0)
    p->args[argn] = argv[argn];

  if (!cd->charpath)
    cd->charpath = p;
  if (cd->lastpath)
    cd->lastpath->next = p;
  cd->lastpath = p;

  if (type >= 0 &&
      phase != T1_CS_PHASE_FLEX && IS_PATH_OPERATOR(type))
    phase = T1_CS_PHASE_PATH;
}

static void
init_charpath (t1_chardesc *cd)
{
  cd->flags = T1_CS_FLAG_NONE;
  cd->num_stems = 0;
  cd->sbw.wx  = cd->sbw.wy  = 0.0;
  cd->sbw.sbx = cd->sbw.sby = 0.0;
  cd->bbox.llx = cd->bbox.lly = cd->bbox.urx = cd->bbox.ury = 0.0;
  cd->charpath = cd->lastpath = NULL;
}

static void
release_charpath (t1_chardesc *cd)
{
  t1_cpath *curr, *next;

  ASSERT(cd);

  curr = cd->charpath;
  while (curr != NULL) {
    next = curr->next;
    RELEASE(curr);
    curr = next;
  }

  cd->charpath = cd->lastpath = NULL;
}

/*
 * Type 1 charstring operators:
 */
#define ADD_PATH(p,t,n) add_charpath((p),(t),&(cs_arg_stack[cs_stack_top-(n)]),(n))

/*
 * Single byte operators:
 */
static void
do_operator1 (t1_chardesc *cd, card8 **data, card8 *endptr)
{
  card8 op = **data;

  *data += 1;

  switch (op) {
  case cs_closepath:
    /*
     * From T1 spec.:
     *  Note that, unlike the closepath command in the PostScript language,
     *  this command does not reposition the current point. Any subsequent
     *  rmoveto must be relative to the current point in force before the
     *  Type 1 font format closepath command was given.
     */
    /* noop */
    CLEARSTACK();
    break;
  case cs_hsbw:
    CHECKSTACK(2);
    cd->sbw.wx  = cs_arg_stack[--cs_stack_top];
    cd->sbw.wy  = 0;
    cd->sbw.sbx = cs_arg_stack[--cs_stack_top];
    cd->sbw.sby = 0;
    CLEARSTACK();
    /* hsbw does NOT set currentpoint. */
    break;
  case cs_hstem:
  case cs_vstem:
    CHECKSTACK(2);
    {
      int stem_id;
      stem_id = add_stem(cd,
			 cs_arg_stack[cs_stack_top-2],
			 cs_arg_stack[cs_stack_top-1],
			 ((op == cs_hstem) ? HSTEM : VSTEM));
      if (stem_id < 0) {
	WARN("Too many hints...");
	status = CS_PARSE_ERROR;
	return;
      }
      /* Put stem_id onto the stack... */
      cs_arg_stack[cs_stack_top++] = stem_id;
      ADD_PATH(cd, CS_HINT_DECL, 1);
    }
    CLEARSTACK();
    break;
  case cs_rmoveto:
    /*
     * Reference point is (0, 0) in Type 2 charstring.
     */
    CHECKSTACK(2);
    {
      if (phase < T1_CS_PHASE_PATH) {
	cs_arg_stack[cs_stack_top-2] += cd->sbw.sbx;
	cs_arg_stack[cs_stack_top-1] += cd->sbw.sby;
      }
      ADD_PATH(cd, op, 2);
    }
    CLEARSTACK();
    break;
  case cs_hmoveto:
  case cs_vmoveto:
    CHECKSTACK(1);
    {
      int argn = 1;
      if (phase < T1_CS_PHASE_PATH) {
	/*
	 * The reference point for the first moveto operator is diferrent
	 * between Type 1 charstring and Type 2 charstring. We compensate it.
	 */
	if (op == cs_hmoveto) {
	  cs_arg_stack[cs_stack_top-1] += cd->sbw.sbx;
	  if (cd->sbw.sby != 0.0) {
	    cs_arg_stack[cs_stack_top++] = cd->sbw.sby;
	    argn = 2;
	    op = cs_rmoveto;
	  }
	} else {
	  cs_arg_stack[cs_stack_top-1] += cd->sbw.sby;
	  if (cd->sbw.sbx != 0.0) {
	    cs_arg_stack[cs_stack_top]   = cs_arg_stack[cs_stack_top-1];
	    cs_arg_stack[cs_stack_top-1] = cd->sbw.sbx;
	    cs_stack_top++;
	    argn = 2;
	    op = cs_rmoveto;
	  }
	}
      }
      ADD_PATH(cd, op, argn);
    }
    CLEARSTACK();
    break;
  case cs_endchar:
    status = CS_CHAR_END;
    CLEARSTACK();
    break;
  /* above oprators are candidate for first stack-clearing operator */
  case cs_rlineto:
    CHECKSTACK(2);
    ADD_PATH(cd, op, 2);
    CLEARSTACK();
    break;
  case cs_hlineto:
  case cs_vlineto:
    CHECKSTACK(1);
    ADD_PATH(cd, op, 1);
    CLEARSTACK();
    break;
  case cs_rrcurveto:
    CHECKSTACK(6);
    ADD_PATH(cd, op, 6);
    CLEARSTACK();
    break;
  case cs_vhcurveto:
  case cs_hvcurveto:
    CHECKSTACK(4);
    ADD_PATH(cd, op, 4);
    CLEARSTACK();
    break;
    /* all operotors above are stack-clearing operator */
    /* no output */
  case cs_return:
    break;
  case cs_callsubr:
    ERROR("Unexpected callsubr.");
    break;
  default:
    /* no-op ? */
    WARN("Unknown charstring operator: 0x%02x", op);
    status = CS_PARSE_ERROR;
    break;
  }

  return;
}

/*
 * OtherSubrs:
 *
 *  arg0 arg1 ... argn n othersubr# callothersubr
 *
 *   0: Build flex:
 *      fd x y 3 0 callothersubr
 *      Othersubr #0 pushes x and y to PostScript interpreter operand stack.
 *   1: Start flex
 *      0 1 callothersubr
 *   2: Mark flex control points
 *      0 2 callothersubr
 *   3: Discard hint
 *      subr# 1 3 callothersubr pop callsubr
 *      Subroutine subr# (only) contains stem declaration.
 *      Othersubr #3 pushes subr# to PostScript interpreter operand stack.
 *  12: Counter control
 *      A subr to avoid stack overflow.
 *  13: Counter control
 */

/*
 * Convert six control points marked as CS_FLEX_CTRL to a flex path.
 */
static void
do_othersubr0 (t1_chardesc *cd)
{
  t1_cpath *flex, *cur, *next;

  if (ps_stack_top < 1) {
    status = CS_PARSE_ERROR;
    return;
  }

  /* Seek first CS_FLEX_CTRL mark */
  for (cur = cd->charpath; cur != NULL && cur->type != CS_FLEX_CTRL; cur = cur->next);
  flex = cur;
  {
    int i;
    cur = cur->next;
    for (i = 1; i < 7; i++) {
      if (cur == NULL || cur->type != CS_FLEX_CTRL ||
	  cur->num_args != 2) {
	status = CS_PARSE_ERROR;
	return;
      }
      if (i == 1) {
	flex->args[0] += cur->args[0];
	flex->args[1] += cur->args[1];
      } else {
	copy_args(&(flex->args[2*i-2]), cur->args, 2);
      }
      next = cur->next;
      RELEASE(cur);
      cur = next;
    }
  }
  if (cur != NULL) {
    status = CS_PARSE_ERROR;
    return;
  }
  /*
   * Now 'flex' have all six control points, the first pair is relative
   * from starting point.
   */
  flex->type = cs_flex;
  flex->args[12] = ps_arg_stack[--ps_stack_top]; /* flex depth */
  flex->num_args = 13;
  flex->next   = NULL;
  cd->lastpath = flex;

  phase = T1_CS_PHASE_PATH;
}

/* Start flex */
static void
do_othersubr1 (t1_chardesc *cd)
{
  phase = T1_CS_PHASE_FLEX;
}

/* Mark flex control point */
static void
do_othersubr2 (t1_chardesc *cd)
{
  if (phase != T1_CS_PHASE_FLEX || !cd->lastpath) {
    status = CS_PARSE_ERROR;
    return;
  }

  switch (cd->lastpath->type) {
  case cs_rmoveto:
    break;
  case cs_hmoveto:
    cd->lastpath->num_args = 2;
    cd->lastpath->args[1] = 0.0;
    break;
  case cs_vmoveto:
    cd->lastpath->num_args = 2;
    cd->lastpath->args[1] = cd->lastpath->args[0];
    cd->lastpath->args[0] = 0.0;
    break;
  default:
    status = CS_PARSE_ERROR;
    return;
  }
  cd->lastpath->type = CS_FLEX_CTRL;
}

/*
 * Hint Replacement:
 *  "Adobe Type 1 Font Format", Chapter 8.
 */
static void
do_othersubr3 (t1_chardesc *cd)
{
  cd->flags |= T1_CS_FLAG_USE_HINTMASK;
}

static void
do_othersubr12 (t1_chardesc *cd)
{
  /* Othersubr12 call must immediately follow the hsbw or sbw. */
  if (phase != T1_CS_PHASE_INIT) {
    status = CS_PARSE_ERROR;
    return;
  }
  /* noop */
}

static void
do_othersubr13 (t1_chardesc *cd)
{
  t1_stemgroup stemgroups[CS_STEM_GROUP_MAX];
  int num_hgroups, num_vgroups, n, stem_id;
  double pos, del;

  /* After #12 callothersubr or hsbw or sbw. */
  if (phase != T1_CS_PHASE_INIT) {
    status = CS_PARSE_ERROR;
    return;
  }
  for (n = 0; n < CS_STEM_GROUP_MAX; n++) {
    stemgroups[n].num_stems = 0;
  }

  num_hgroups = (int) ps_arg_stack[--ps_stack_top];
  if (num_hgroups < 0 || num_hgroups > CS_STEM_GROUP_MAX) {
    status = CS_PARSE_ERROR;
    return;
  }
  n = 0; pos = 0.0;
  while (ps_stack_top >= 2 && n < num_hgroups) {
    /* add_stem() add sidebearing */
    pos += ps_arg_stack[--ps_stack_top];
    del  = ps_arg_stack[--ps_stack_top];
    stem_id = add_stem(cd,
		       (del < 0.0) ? pos + del : pos,
		       (del < 0.0) ? -del : del,
		       HSTEM);
    stemgroups[n].stems[stemgroups[n].num_stems] = stem_id;
    stemgroups[n].num_stems += 1;
    pos += del;
    if (del < 0.0) {
      pos = 0.0;
      n++;
    }
  }
  if (n != num_hgroups) {
    status = CS_STACK_ERROR;
    return;
  }

  num_vgroups = (int) ps_arg_stack[--ps_stack_top];
  if (num_vgroups < 0 || num_vgroups > CS_STEM_GROUP_MAX) {
    status = CS_PARSE_ERROR;
    return;
  }
  n = 0; pos = 0.0;
  while (ps_stack_top >= 2 && n < num_vgroups) {
    /* add_stem() add sidebearing */
    pos += ps_arg_stack[--ps_stack_top];
    del  = ps_arg_stack[--ps_stack_top];
    stem_id = add_stem(cd,
		       (del < 0.0) ? pos + del : pos,
		       (del < 0.0) ? -del : del,
		       VSTEM);
    stemgroups[n].stems[stemgroups[n].num_stems] = stem_id;
    stemgroups[n].num_stems += 1;
    pos += del;
    if (del < 0.0) {
      pos = 0.0;
      n++;
    }
  }
  if (n != num_vgroups) {
    status = CS_STACK_ERROR;
    return;
  }

  for (n = 0; n < MAX(num_hgroups, num_vgroups); n++) {
    add_charpath(cd, cs_cntrmask,
		 stemgroups[n].stems, stemgroups[n].num_stems);
  }

  cd->flags |= T1_CS_FLAG_USE_CNTRMASK;
}

static void
do_callothersubr (t1_chardesc *cd)
{
  int argn, subrno;

  CHECKSTACK(2);
  subrno = (int) cs_arg_stack[--cs_stack_top];
  argn   = (int) cs_arg_stack[--cs_stack_top];

  CHECKSTACK(argn);
  if (ps_stack_top+argn > PS_ARG_STACK_MAX) {
    status = CS_PARSE_ERROR;
    return;
  }
  while (argn-- > 0)
    ps_arg_stack[ps_stack_top++] = cs_arg_stack[--cs_stack_top];

  switch (subrno) {
  case 0: do_othersubr0(cd); break;
  case 1: do_othersubr1(cd); break;
  case 2: do_othersubr2(cd); break;
  case 3: do_othersubr3(cd); break;
  case 12: do_othersubr12(cd); break;
  case 13: do_othersubr13(cd); break;
  default:
    ERROR("Unknown othersubr #%ld.", subrno);
    break;
  }
}

/*
 * Double byte operators:
 */
static void
do_operator2 (t1_chardesc *cd, card8 **data, card8 *endptr)
{
  card8 op;

  *data += 1;

  SRC_NEED(endptr, *data + 1);

  op = **data;
  *data += 1;

  switch(op) {
  case cs_sbw:
    CHECKSTACK(4);
    cd->sbw.wy  = cs_arg_stack[--cs_stack_top];
    cd->sbw.wx  = cs_arg_stack[--cs_stack_top];
    cd->sbw.sby = cs_arg_stack[--cs_stack_top];
    cd->sbw.sbx = cs_arg_stack[--cs_stack_top];
    CLEARSTACK();
    break;
  case cs_hstem3:
  case cs_vstem3:
    /*
     * TODO:
     *  The counter control can be used for hstem3 and vstem3
     *  operator if LanguageGroup is not equal to 1.
     */
    CHECKSTACK(6);
    {
      int i;
      for (i = 2; i >= 0; i--) {
	int stem_id;
	stem_id = add_stem(cd,
			   cs_arg_stack[cs_stack_top-2*i-2],
			   cs_arg_stack[cs_stack_top-2*i-1],
			   ((op == cs_hstem3) ? HSTEM : VSTEM));
	if (stem_id < 0) {
	  WARN("Too many hints...");
	  status = CS_PARSE_ERROR;
	  return;
	}
	/* Put stem_id onto the stack... */
	cs_arg_stack[cs_stack_top++] = stem_id;
	ADD_PATH(cd, CS_HINT_DECL, 1);
	cs_stack_top--;
      }
    }
    CLEARSTACK();
    break;
  case cs_setcurrentpoint:
    CHECKSTACK(2);
    /* noop */
    CLEARSTACK();
    break;
    /* all operator above are stack-clearing */
  case cs_pop:
    /*
     * Transfer a operand from PS interpreter operand stack to BuildChar
     * operand stack.
     */
    if (ps_stack_top < 1) {
      status = CS_PARSE_ERROR;
      return;
    }
    LIMITCHECK(1);
    cs_arg_stack[cs_stack_top++] = ps_arg_stack[--ps_stack_top];
    break;
  case cs_dotsection:
    /*
     * If the hint replacement feature is used in the font, the
     * "dotsection" operator exist only for compatibility to older
     * (more than 10 years old) Type 1 font rasterizer which can't
     * perform hint replacement. In this case, we silently ignore
     * the "dotsection" operator.
     *
     * The following code will wrongly warn about "dotsection" when
     * the charstring only contains dot (e.g., "bullet") where the
     * hint replacement is not necessary.
     *
     * Adobe ATM renderers always treat this operator as a no-op.
     * (See, Adobe Technical Note #5177, Appendix C)
     */
    if (!(cd->flags & T1_CS_FLAG_USE_HINTMASK)) {
      if (__verbose > 1)
	WARN("Obsolete Type 1 charstring operator \"dotsection\" not supported.");
    }
    /* noop */
    break;
  case cs_div: /* TODO: check overflow */
    CHECKSTACK(2);
    cs_arg_stack[cs_stack_top-2] /= cs_arg_stack[cs_stack_top-1];
    cs_stack_top--;
    break;
  case cs_callothersubr:
    do_callothersubr(cd);
    break;
  case cs_seac:
    CHECKSTACK(5);
    cd->flags |= T1_CS_FLAG_USE_SEAC;
    cd->seac.achar = (card8) cs_arg_stack[--cs_stack_top];
    cd->seac.bchar = (card8) cs_arg_stack[--cs_stack_top];
    cd->seac.ady   = cs_arg_stack[--cs_stack_top];
    cd->seac.adx   = cs_arg_stack[--cs_stack_top];
    /* We must compensate the difference of the glyph origin. */
    cd->seac.ady += cd->sbw.sby;
    cd->seac.adx += cd->sbw.sbx - cs_arg_stack[--cs_stack_top];
    CLEARSTACK();
    break;
  default:
    /* no-op ? */
    WARN("Unknown charstring operator: 0x0c%02x", op);
    status = CS_PARSE_ERROR;
    break;
  }

  return;
}

/*
 * Charstring encoding:
 *  Copied from cs_type2.c
 *  Note:
 *   The Type 2 interpretation of a number encoded in five-bytes (those with
 *   an initial byte value of 255) differs from how it is interpreted in the
 *   Type 1 format.
 */

/* Type 2 5-bytes encoding used. */
static void
put_numbers (double *argv, int argn, card8 **dest, card8 *limit)
{
  int i;

  for (i = 0; i < argn; i++) {
    double value;
    long   ivalue;
    value  = argv[i];
    /* Nearest integer value */
    ivalue = (long) floor(value+0.5);
    if (value >= 0x8000L || value <= (-0x8000L - 1)) {
      /*
       * This number cannot be represented as a single operand.
       * We must use `a b mul ...' or `a c div' to represent large values.
       */
      ERROR("Argument value too large. (This is bug)");
    } else if (fabs(value - ivalue) > 3.0e-5) {
      /* 16.16-bit signed fixed value  */
      DST_NEED(limit, *dest + 5);
      *(*dest)++ = 255;
      ivalue = (long) floor(value); /* mantissa */
      *(*dest)++ = (ivalue >> 8) & 0xff;
      *(*dest)++ = ivalue & 0xff;
      ivalue = (long)((value - ivalue) * 0x10000l); /* fraction */
      *(*dest)++ = (ivalue >> 8) & 0xff;
      *(*dest)++ = ivalue & 0xff;
      /* Everything else are integers. */
    } else if (ivalue >= -107 && ivalue <= 107) {
      DST_NEED(limit, *dest + 1);
      *(*dest)++ = ivalue + 139;
    } else if (ivalue >= 108 && ivalue <= 1131) {
      DST_NEED(limit, *dest + 2);
      ivalue = 0xf700u + ivalue - 108;
      *(*dest)++ = (ivalue >> 8) & 0xff;
      *(*dest)++ = ivalue & 0xff;
    } else if (ivalue >= -1131 && ivalue <= -108) {
      DST_NEED(limit, *dest + 2);
      ivalue = 0xfb00u - ivalue - 108;
      *(*dest)++ = (ivalue >> 8) & 0xff;
      *(*dest)++ = ivalue & 0xff;
    } else if (ivalue >= -32768 && ivalue <= 32767) { /* shortint */
      DST_NEED(limit, *dest + 3);
      *(*dest)++ = 28;
      *(*dest)++ = (ivalue >> 8) & 0xff;
      *(*dest)++ = (ivalue) & 0xff;
    } else { /* Shouldn't come here */
      ERROR("Unexpected error.");
    }
  }

  return;
}

static void
get_integer (card8 **data, card8 *endptr)
{
  long result = 0;
  card8 b0 = **data, b1, b2;

  *data += 1;

  if (b0 == 28) { /* shortint */
    SRC_NEED(endptr, *data + 2);
    b1 = **data;
    b2 = *(*data+1);
    result = b1*256+b2;
    if (result > 0x7fff)
      result -= 0x10000L;
    *data += 2;
  } else if (b0 >= 32 && b0 <= 246) { /* int (1) */
    result = b0 - 139;
  } else if (b0 >= 247 && b0 <= 250) { /* int (2) */
    SRC_NEED(endptr, *data + 1);
    b1 = **data;
    result = (b0-247)*256+b1+108;
    *data += 1;
  } else if (b0 >= 251 && b0 <= 254) {
    SRC_NEED(endptr, *data + 1);
    b1 = **data;
    result = -(b0-251)*256-b1-108;
    *data += 1;
  } else {
    status = CS_PARSE_ERROR;
    return;
  }

  LIMITCHECK(1);
  cs_arg_stack[cs_stack_top++] = (double) result;

  return;
}

/* Type 1 */
static void
get_longint (card8 **data, card8 *endptr)
{
  long result = 0;
  int  i;

  *data += 1;
  SRC_NEED(endptr, *data + 4);
  result = **data;
  if (result >= 0x80L)
    result -= 0x100L;
  *data += 1;
  for (i = 1; i < 4; i++) {
    result = result*256 + (**data);
    *data += 1;
  }

  LIMITCHECK(1);
  cs_arg_stack[cs_stack_top++] = (double) result;

  return;
}

/*
 * TODO:
 *  Check "seac"
 *   We cannot do backword parsing due to subroutine, div etc.
 */

/* Parse charstring and build charpath. */
static void
cs1_build_charpath (t1_chardesc *cd,
		    card8 **data, card8 *endptr, t1_subr *subrs)
{
  card8 b0 = 0, *subr;
  long len;

  if (nest > CS_SUBR_NEST_MAX)
    ERROR("Subroutine nested too deeply.");

  nest++;
  while (*data < endptr && status == CS_PARSE_OK) {
    b0 = **data;
    if (b0 == 255) {
      get_longint(data, endptr); /* Type 1 */
    } else if (b0 == cs_return) {
      status = CS_SUBR_RETURN;
    } else if (b0 == cs_callsubr) {
      if (cs_stack_top < 1) {
	status = CS_STACK_ERROR;
      } else {
	int idx = cs_arg_stack[--cs_stack_top];
	if (!subrs || idx >= subrs->count)
	  ERROR("Invalid Subr#.");
	subr = subrs->data + subrs->offset[idx];
	len  = subrs->length[idx];
	cs1_build_charpath(cd, &subr, subr+len, subrs);
	*data += 1;
      }
    } else if (b0 == cs_escape) {
      do_operator2(cd, data, endptr);
    } else if (b0 < 32 && b0 != 28) { /* 19, 20 need mask */
      do_operator1(cd, data, endptr);
    } else if ((b0 <= 22 && b0 >= 27) || b0 == 31) { /* reserved */
      status = CS_PARSE_ERROR; /* not an error ? */
    } else { /* integer */
      get_integer(data, endptr);
    }
  }

  if (status == CS_SUBR_RETURN) {
    status = CS_PARSE_OK;
  } else if (status == CS_CHAR_END && *data < endptr) {
    if (!(*data == endptr - 1 && **data == cs_return))
      WARN("Garbage after endchar. (%ld bytes)", (long) (endptr - *data));
  } else if (status < CS_PARSE_OK) { /* error */
    ERROR("Parsing charstring failed: (status=%d, stack=%d)", status, cs_stack_top);
  }

  nest--;

  return;
}

/*
 * Calculate BoundingBox and compress path.
 *  The essentials of PDF size reduction is not Type 2 charstring compression
 *  but Type 1 charstring encryption. Encryption makes lossless compression
 *  useless. We will only do very simple charstring compression.
 */
void
do_postproc (t1_chardesc *cd)
{
  int i;
  t1_cpath *cur, *prev, *next;
  double x, y;

  cd->bbox.llx = cd->bbox.urx = cd->sbw.sbx;
  cd->bbox.lly = cd->bbox.ury = cd->sbw.sby;

  if (!cd->charpath)
    return;

  cur  = cd->charpath;
  prev = NULL;
  x = y = 0.0;

#define UPDATE_BBOX(b,x,y) do {\
                               if ((b).llx > (x)) (b).llx = (x);\
                               if ((b).urx < (x)) (b).urx = (x);\
                               if ((b).lly > (y)) (b).lly = (y);\
                               if ((b).ury < (y)) (b).ury = (y);\
                         } while (0)
#define TRY_COMPACT (prev && cur && ((prev->num_args + cur->num_args) <= CS_ARG_STACK_MAX))

  /* Set dummy large value. */
  cd->bbox.llx += cd->sbw.wx;
  cd->bbox.lly += cd->sbw.wy;
  while (cur != NULL) {
    next = cur->next;
    switch (cur->type) {
    case cs_rmoveto:
      x += cur->args[0]; y += cur->args[1];
      UPDATE_BBOX(cd->bbox, x, y);
      break;
    case cs_rlineto:
      x += cur->args[0]; y += cur->args[1];
      UPDATE_BBOX(cd->bbox, x, y);
      if (TRY_COMPACT) {
	if (prev->type == cs_rlineto) {
	  copy_args(prev->args+prev->num_args, cur->args, cur->num_args);
	  prev->num_args += cur->num_args;
	  prev->next = next;
	  RELEASE(cur); cur = NULL;
	} else if (prev->type == cs_rrcurveto) {
	  copy_args(prev->args+prev->num_args, cur->args, cur->num_args);
	  prev->num_args += cur->num_args;
	  prev->type = cs_rcurveline;
	  prev->next = next;
	  RELEASE(cur); cur = NULL;
	}
      }
      break;
    case cs_hmoveto:
      x += cur->args[0];
      UPDATE_BBOX(cd->bbox, x, y);
      break;
    case cs_hlineto:
      x += cur->args[0];
      UPDATE_BBOX(cd->bbox, x, y);
      if (TRY_COMPACT) {
	if ((prev->type == cs_vlineto && (prev->num_args % 2) == 1) ||
	    (prev->type == cs_hlineto && (prev->num_args % 2) == 0)) {
	  copy_args(prev->args+prev->num_args, cur->args, cur->num_args);
	  prev->num_args += cur->num_args;
	  prev->next = next;
	  RELEASE(cur); cur = NULL;
	}
      }
      break;
    case cs_vmoveto:
      y += cur->args[0];
      UPDATE_BBOX(cd->bbox, x, y);
      break;
    case cs_vlineto:
      y += cur->args[0];
      UPDATE_BBOX(cd->bbox, x, y);
      if (TRY_COMPACT) {
	if ((prev->type == cs_hlineto && (prev->num_args % 2) == 1) ||
	    (prev->type == cs_vlineto && (prev->num_args % 2) == 0)) {
	  copy_args(prev->args+prev->num_args, cur->args, cur->num_args);
	  prev->num_args += cur->num_args;
	  prev->next = next;
	  RELEASE(cur); cur = NULL;
	}
      }
      break;
    case cs_rrcurveto:
      for (i = 0; i < 3; i++) {
	x += cur->args[2*i]; y += cur->args[2*i+1];
	UPDATE_BBOX(cd->bbox, x, y);
      }
      if (TRY_COMPACT) {
	if (prev->type == cs_rrcurveto) {
	  copy_args(prev->args+prev->num_args, cur->args, cur->num_args);
	  prev->num_args += cur->num_args;
	  prev->next = next;
	  RELEASE(cur); cur = NULL;
	} else if (prev->type == cs_rlineto) {
	  copy_args(prev->args+prev->num_args, cur->args, cur->num_args);
	  prev->num_args += cur->num_args;
	  prev->type = cs_rlinecurve;
	  prev->next = next;
	  RELEASE(cur); cur = NULL;
	}
      }
      break;
    case cs_vhcurveto:
      y += cur->args[0];
      UPDATE_BBOX(cd->bbox, x, y);
      x += cur->args[1]; y += cur->args[2];
      UPDATE_BBOX(cd->bbox, x, y);
      x += cur->args[3];
      UPDATE_BBOX(cd->bbox, x, y);
      if (TRY_COMPACT) {
	if ((prev->type == cs_hvcurveto && ((prev->num_args / 4) % 2) == 1) ||
	    (prev->type == cs_vhcurveto && ((prev->num_args / 4) % 2) == 0)) {
	  copy_args(prev->args+prev->num_args, cur->args, cur->num_args);
	  prev->num_args += cur->num_args;
	  prev->next = next;
	  RELEASE(cur); cur = NULL;
	}
      }
      break;
    case cs_hvcurveto:
      x += cur->args[0];
      UPDATE_BBOX(cd->bbox, x, y);
      x += cur->args[1]; y += cur->args[2];
      UPDATE_BBOX(cd->bbox, x, y);
      y += cur->args[3];
      UPDATE_BBOX(cd->bbox, x, y);
      if (TRY_COMPACT) {
	if ((prev->type == cs_vhcurveto && ((prev->num_args / 4) % 2) == 1) ||
	    (prev->type == cs_hvcurveto && ((prev->num_args / 4) % 2) == 0)) {
	  copy_args(prev->args+prev->num_args, cur->args, cur->num_args);
	  prev->num_args += cur->num_args;
	  prev->next = next;
	  RELEASE(cur); cur = NULL;
	}
      }
      break;
    case cs_flex:
      for (i = 0; i < 6; i++) {
	x += cur->args[2*i]; y += cur->args[2*1+1];
	UPDATE_BBOX(cd->bbox, x, y);
      }
      if (cur->args[12] == 50.0) {
	if (cur->args[1] == 0.0 && cur->args[11] == 0.0 &&
	    cur->args[5] == 0.0 && cur->args[7] == 0.0 &&
	    cur->args[3] + cur->args[9] == 0.0) {
	  /* cur->args[0] = cur->args[0];  dx1 */
	  cur->args[1] = cur->args[2];  /* dx2 */
	  cur->args[2] = cur->args[3];  /* dy2 */
	  cur->args[3] = cur->args[4];  /* dx3 */
	  cur->args[4] = cur->args[6];  /* dx4 */
	  cur->args[5] = cur->args[8];  /* dx5 */
	  cur->args[6] = cur->args[10]; /* dx6 */
	  cur->num_args = 7;
	  cur->type = cs_hflex;
	} else if (cur->args[5] == 0.0 && cur->args[7] == 0.0 &&
		   (cur->args[1] + cur->args[3] +
		    cur->args[9] + cur->args[11]) == 0) {
	  /* cur->args[0] = cur->args[0];  dx1 */
	  /* cur->args[1] = cur->args[1];  dy1 */
	  /* cur->args[2] = cur->args[2];  dx2 */
	  /* cur->args[3] = cur->args[3];  dy2 */
	  /* cur->args[4] = cur->args[4];  dx3 */
	  cur->args[5] = cur->args[6];  /* dx4 */
	  cur->args[6] = cur->args[8];  /* dx5 */
	  cur->args[7] = cur->args[9];  /* dy5 */
	  cur->args[8] = cur->args[10]; /* dx6 */
	  cur->num_args = 9;
	  cur->type = cs_hflex1;
	}
      }
      break;
    case CS_HINT_DECL:
    case cs_cntrmask:
      /* noop */
      break;
    default:
      ERROR("Unexpected Type 2 charstring command %d.", cur->type);
      break;
    }
    if (cur != NULL)
      prev = cur;
    cur = next;
  }

  /* Had no path. Fix lower-left point. */
  if (cd->bbox.llx > cd->bbox.urx)
    cd->bbox.llx = cd->bbox.urx;
  if (cd->bbox.lly > cd->bbox.ury)
    cd->bbox.lly = cd->bbox.ury;

  return;
}

#define RESET_STATE() do {\
                       status = CS_PARSE_OK;\
                       phase  = T1_CS_PHASE_INIT;\
                       nest   = 0;\
                       ps_stack_top = 0;\
                      } while (0)

static int
cs1_get_metrics (card8 *src, long srclen, t1_subr *subrs, t1_ginfo *ginfo)
{
  t1_chardesc t1char, *cd;

  cd = &t1char;
  init_charpath(cd);
  RESET_STATE();
  CLEARSTACK();
  cs1_build_charpath(cd, &src, src+srclen, subrs);
  if (cs_stack_top != 0 || ps_stack_top != 0)
    WARN("Stack not empty. (%d, %d)", cs_stack_top, ps_stack_top);
  do_postproc(cd);
  if (ginfo) {
    ginfo->wx = cd->sbw.wx;
    ginfo->wy = cd->sbw.wy;
    ginfo->bbox.llx = cd->bbox.llx;
    ginfo->bbox.lly = cd->bbox.lly;
    ginfo->bbox.urx = cd->bbox.urx;
    ginfo->bbox.ury = cd->bbox.ury;
    if (cd->flags & T1_CS_FLAG_USE_SEAC) {
      ginfo->use_seac = 1;
      ginfo->seac.adx = cd->seac.adx;
      ginfo->seac.ady = cd->seac.ady;
      ginfo->seac.bchar = cd->seac.bchar;
      ginfo->seac.achar = cd->seac.achar;
    } else {
      ginfo->use_seac = 0;
    }
  }
  release_charpath(cd);

  return 0;
}

#define CHECK_BUFFER(n) if (dst+(n) >= endptr) {\
                         ERROR("Buffer overflow.");\
                        }
#define CHECK_STATUS()  if (status != CS_PARSE_OK) {\
                         ERROR("Charstring encoder error: %d", status);\
                        }

/*
 * Encode Charpath as a Type 2 Charstring
 */
static long
cs1_encode_charpath (t1_chardesc *cd, double default_width, double nominal_width,
		     card8 *dst, card8 *endptr)
{
  card8    *save;
  t1_cpath *curr;

  ASSERT(cd);

  save = dst;
  curr = cd->charpath;

  RESET_STATE();
  CLEARSTACK(); 
  /*
   * Advance Width
   */
  if (cd->sbw.wx != default_width) {
    double wx = cd->sbw.wx - nominal_width;
    put_numbers(&wx, 1, &dst, endptr);
    CHECK_STATUS();
  }
  /*
   * Hint Declaration
   */
  {
    int num_hstems = 0, num_vstems = 0;
    int i, reset = 1;
    double stem[2];

    for (i = 0; i < cd->num_stems && cd->stems[i].dir == HSTEM; i++) {
      num_hstems++;
      stem[0] = (reset ?
		 (cd->stems[i].pos) :
		 (cd->stems[i].pos - (cd->stems[i-1].pos + cd->stems[i-1].del)));
      stem[1] = cd->stems[i].del;
      put_numbers(stem, 2, &dst, endptr);
      CHECK_STATUS();
      reset = 0;
      if (2*num_hstems == CS_ARG_STACK_MAX) {
	CHECK_BUFFER(1);
	*dst++ = (card8) ((cd->flags & T1_CS_FLAG_USE_HINTMASK) ? cs_hstemhm : cs_hstem);
	reset = 1;
      }
    }
    if (reset == 0) {
      CHECK_BUFFER(1);
      *dst++ = (card8) ((cd->flags & T1_CS_FLAG_USE_HINTMASK) ? cs_hstemhm : cs_hstem);
    }
    reset = 1;
    if (cd->num_stems - num_hstems > 0) {
      for (i = num_hstems; i < cd->num_stems; i++) {
	num_vstems++;
	stem[0] = (reset ?
		   (cd->stems[i].pos) :
		   (cd->stems[i].pos - (cd->stems[i-1].pos + cd->stems[i-1].del)));
	stem[1] = cd->stems[i].del;
	put_numbers(stem, 2, &dst, endptr);
	CHECK_STATUS();
	reset = 0;
	if (2*num_vstems == CS_ARG_STACK_MAX) {
	  CHECK_BUFFER(1);
	  *dst++ = (card8) ((cd->flags & T1_CS_FLAG_USE_HINTMASK) ? cs_vstemhm : cs_vstem);
	  reset = 1;
	}
      }
      if (reset == 0) {
	CHECK_BUFFER(1);
	if ((cd->flags & T1_CS_FLAG_USE_HINTMASK) ||
	    (cd->flags & T1_CS_FLAG_USE_CNTRMASK)) {
	  /*
	   * The vstem hint operator can be ommited if hstem and vstem hints
	   * are both declared at the beginning of a charstring, and is
	   * followed directly by the hintmask or cntrmask operators.
	   */
	  if (curr->type != CS_HINT_DECL &&
	      curr->type != cs_cntrmask) {
	    *dst++ = (card8) cs_vstemhm;
	  }
	} else {
	  *dst++ = (card8) cs_vstem;
	}
      }
    }
  }
  /*
   * Path Construction and Hint Replacement
   */
  while (curr != NULL && curr->type != cs_endchar) {
    switch (curr->type) {
    case CS_HINT_DECL:
      {
	card8 hintmask[(CS_STEM_ZONE_MAX+7)/8];

	memset(hintmask, 0, (cd->num_stems+7)/8);
	while (curr != NULL && curr->type == CS_HINT_DECL) {
	  int stem_idx;

	  stem_idx = get_stem(cd, (int) curr->args[0]);
	  ASSERT(stem_idx < cd->num_stems);
	  hintmask[stem_idx/8] |= (1 << (7 - (stem_idx % 8)));
	  curr = curr->next;
	}
	if (cd->flags & T1_CS_FLAG_USE_HINTMASK) {
	  CHECK_BUFFER((cd->num_stems+7)/8 + 1);
	  *dst++ = (card8) cs_hintmask;
	  memcpy(dst, hintmask, (cd->num_stems+7)/8);
	  dst += (cd->num_stems+7)/8;
	}
      }
      break;
    case cs_cntrmask:
      {
	card8 cntrmask[(CS_STEM_ZONE_MAX+7)/8];
	int   stem_idx, i;

	memset(cntrmask, 0, (cd->num_stems+7)/8);
	for (i = 0; i < curr->num_args; i++) {
	  stem_idx = get_stem(cd, (int) curr->args[i]);
	  ASSERT(stem_idx < cd->num_stems);
	  cntrmask[stem_idx/8] |= (1 << (7 - (stem_idx % 8)));
	}
	CHECK_BUFFER((cd->num_stems+7)/8 + 1);
	*dst++ = (card8) cs_cntrmask;
	memcpy(dst, cntrmask, (cd->num_stems+7)/8);
	dst += (cd->num_stems+7)/8;
	curr = curr->next;
      }
      break;
    case cs_rmoveto: case cs_hmoveto: case cs_vmoveto:
    case cs_rlineto: case cs_hlineto: case cs_vlineto:
    case cs_rrcurveto:  case cs_hvcurveto: case cs_vhcurveto:
    case cs_rlinecurve: case cs_rcurveline:
      {
	put_numbers(curr->args, curr->num_args, &dst, endptr);
	CHECK_STATUS();
	CHECK_BUFFER(1);
	*dst++ = (card8) curr->type;
	curr = curr->next;
      }
      break;
    case cs_flex: case cs_hflex:
    case cs_hflex1:
      {
	put_numbers(curr->args, curr->num_args, &dst, endptr);
	CHECK_STATUS();
	CHECK_BUFFER(2);
	*dst++ = (card8) cs_escape;
	*dst++ = (card8) curr->type;
	curr = curr->next;
      }
      break;
    default:
      ERROR("Unknown Type 2 charstring command: %d", curr->type);
      break;
    }
  }

  /*
   * (adx ady bchar achar) endchar
   */
  if (cd->flags & T1_CS_FLAG_USE_SEAC) {
    double seac[4];
    seac[0] = cd->seac.adx;
    seac[1] = cd->seac.ady;
    seac[2] = cd->seac.bchar;
    seac[3] = cd->seac.achar;
    put_numbers(seac, 4, &dst, endptr);
    CHECK_STATUS();
    CHECK_BUFFER(2);
    WARN("Obsolete four arguments of \"endchar\" will be used for Type 1 \"seac\" operator.");
  }
  CHECK_BUFFER(1);
  *dst++ = (card8) cs_endchar;

  return (long) (dst - save);
}

static long
cs1_convert_charstring (card8 *dst, long dstlen,
			card8 *src, long srclen, t1_subr *subrs,
			double default_width, double nominal_width,
			t1_ginfo *ginfo)
{
  long length;
  t1_chardesc t1char, *cd;

  cd = &t1char;
  init_charpath(cd);
  RESET_STATE();
  CLEARSTACK();
  cs1_build_charpath(cd, &src, src+srclen, subrs);
  if (cs_stack_top != 0 || ps_stack_top != 0)
    WARN("Stack not empty. (%d, %d)", cs_stack_top, ps_stack_top);
  do_postproc(cd);
  SORT_STEMS(cd);

  length = cs1_encode_charpath(cd, default_width, nominal_width, dst, dst+dstlen);

  if (ginfo) {
    ginfo->wx = cd->sbw.wx;
    ginfo->wy = cd->sbw.wy;
    ginfo->bbox.llx = cd->bbox.llx;
    ginfo->bbox.lly = cd->bbox.lly;
    ginfo->bbox.urx = cd->bbox.urx;
    ginfo->bbox.ury = cd->bbox.ury;
    if (cd->flags & T1_CS_FLAG_USE_SEAC) {
      ginfo->use_seac = 1;
      ginfo->seac.adx = cd->seac.adx;
      ginfo->seac.ady = cd->seac.ady;
      ginfo->seac.bchar = cd->seac.bchar;
      ginfo->seac.achar = cd->seac.achar;
    } else {
      ginfo->use_seac = 0;
    }
  }
  release_charpath(cd);

  return length;
}

/******************************* pfb_parse.c *******************************/

typedef struct {
  card16 count;
  char **glyphs;
} t1_charset;

#define MATCH_NAME(t,n) ((t) && PST_NAMETYPE((t))    && !strncmp(pst_data_ptr((t)),(n),strlen((n))))
#define MATCH_OP(t,n)   ((t) && PST_UNKNOWNTYPE((t)) && !strncmp(pst_data_ptr((t)),(n),strlen((n))))

#define RELEASE_TOK(t) if ((t) != NULL) {\
                         pst_release_obj((t));\
                         (t) = NULL;\
                       }

static char *
get_next_key (unsigned char **start, unsigned char *end)
{
  char *key = NULL;
  pst_obj *tok;

  while (*start < end &&
	 (tok = pst_get_token(start, end)) != NULL) {
    if (PST_NAMETYPE(tok)) {
      key = pst_getSV(tok);
      RELEASE_TOK(tok);
      break;
    }
    RELEASE_TOK(tok);
  }

  return key;
}

static int
seek_operator (unsigned char **start, unsigned char *end, const char *op)
{
  pst_obj *tok = NULL;

  while (*start < end &&
	 (tok = pst_get_token(start, end)) != NULL) {
    if (MATCH_OP(tok, op)) {
      break;
    }
    RELEASE_TOK(tok);
  }

  if (tok == NULL)
    return -1;
  RELEASE_TOK(tok);

  return 0;
}


static int
parse_svalue (unsigned char **start, unsigned char *end, char **value)
{
  pst_obj *tok;

  tok = pst_get_token(start, end);
  if (tok == NULL)
    return -1;
  else if (PST_NAMETYPE(tok) || PST_STRINGTYPE(tok))
    *value = (char *) pst_getSV(tok);
  else {
    RELEASE_TOK(tok);
    return -1;
  }
  RELEASE_TOK(tok);

  return 1;
}

static int
parse_bvalue (unsigned char **start, unsigned char *end, double *value)
{
  pst_obj *tok;

  tok = pst_get_token(start, end);
  if (tok == NULL)
    return -1;
  else if (PST_BOOLEANTYPE(tok))
    *value = (double) pst_getIV(tok);
  else {
    RELEASE_TOK(tok);
    return -1;
  }
  RELEASE_TOK(tok);

  return 1;
}

static int
parse_nvalue (unsigned char **start, unsigned char *end, double *value, int max)
{
  int argn = 0;
  pst_obj *tok;

  tok = pst_get_token(start, end);
  if (tok == NULL)
    return -1;
  /*
   * All array elements must be numeric token. (ATM compatible)
   */
  if (PST_NUMBERTYPE(tok) && max > 0) {
    value[0] = (double) pst_getRV(tok);
    argn = 1;
  } else if (PST_MARKTYPE(tok)) {
    /* It does not distinguish '[' and '{'... */
    RELEASE_TOK(tok);
    while (*start < end &&
	   (tok = pst_get_token(start, end)) != NULL &&
	   PST_NUMBERTYPE(tok) && argn < max) {
      value[argn++] = (double) pst_getRV(tok);
      RELEASE_TOK(tok);
    }
    if (tok == NULL)
      return -1;
    if (!MATCH_OP(tok, "]") && !MATCH_OP(tok, "}")) {
      argn = -1;
    }
  }
  RELEASE_TOK(tok);

  return argn;
}

static const char *const StandardEncoding[256] = {
  ".notdef", ".notdef", ".notdef", ".notdef", ".notdef",
  ".notdef", ".notdef", ".notdef", ".notdef", ".notdef",
  ".notdef", ".notdef", ".notdef", ".notdef", ".notdef",
  ".notdef", ".notdef", ".notdef", ".notdef", ".notdef",
  ".notdef", ".notdef", ".notdef", ".notdef", ".notdef",
  ".notdef", ".notdef", ".notdef", ".notdef", ".notdef",
  ".notdef", ".notdef", "space", "exclam", "quotedbl",
  "numbersign", "dollar", "percent", "ampersand", "quoteright",
  "parenleft", "parenright", "asterisk", "plus", "comma",
  "hyphen", "period", "slash", "zero", "one",
  "two", "three", "four", "five", "six",
  "seven", "eight", "nine", "colon", "semicolon",
  "less", "equal", "greater", "question", "at",
  "A", "B", "C", "D", "E",
  "F", "G", "H", "I", "J",
  "K", "L", "M", "N", "O",
  "P", "Q", "R", "S", "T",
  "U", "V", "W", "X", "Y",
  "Z", "bracketleft", "backslash", "bracketright", "asciicircum",
  "underscore", "quoteleft", "a", "b", "c",
  "d", "e", "f", "g", "h",
  "i", "j", "k", "l", "m",
  "n", "o", "p", "q", "r",
  "s", "t", "u", "v", "w",
  "x", "y", "z", "braceleft", "bar",
  "braceright", "asciitilde", ".notdef", ".notdef", ".notdef",
  ".notdef", ".notdef", ".notdef", ".notdef", ".notdef",
  ".notdef", ".notdef", ".notdef", ".notdef", ".notdef",
  ".notdef", ".notdef", ".notdef", ".notdef", ".notdef",
  ".notdef", ".notdef", ".notdef", ".notdef", ".notdef",
  ".notdef", ".notdef", ".notdef", ".notdef", ".notdef",
  ".notdef", ".notdef", ".notdef", ".notdef", ".notdef",
  ".notdef", "exclamdown", "cent", "sterling", "fraction",
  "yen", "florin", "section", "currency", "quotesingle",
  "quotedblleft", "guillemotleft", "guilsinglleft", "guilsinglright", "fi",
  "fl", ".notdef", "endash", "dagger", "daggerdbl",
  "periodcentered", ".notdef", "paragraph", "bullet", "quotesinglbase",
  "quotedblbase", "quotedblright", "guillemotright", "ellipsis", "perthousand",
  ".notdef", "questiondown", ".notdef", "grave", "acute",
  "circumflex", "tilde", "macron", "breve", "dotaccent",
  "dieresis", ".notdef", "ring", "cedilla", ".notdef",
  "hungarumlaut", "ogonek", "caron", "emdash", ".notdef",
  ".notdef", ".notdef", ".notdef", ".notdef", ".notdef",
  ".notdef", ".notdef", ".notdef", ".notdef", ".notdef",
  ".notdef", ".notdef", ".notdef", ".notdef", ".notdef",
  "AE", ".notdef", "ordfeminine", ".notdef", ".notdef",
  ".notdef", ".notdef", "Lslash", "Oslash", "OE",
  "ordmasculine", ".notdef", ".notdef", ".notdef", ".notdef",
  ".notdef", "ae", ".notdef", ".notdef", ".notdef",
  "dotlessi", ".notdef", ".notdef", "lslash", "oslash",
  "oe", "germandbls", ".notdef", ".notdef", ".notdef",
  ".notdef"
};

static const char *const ISOLatin1Encoding[256] = {
  ".notdef", ".notdef", ".notdef", ".notdef", ".notdef",
  ".notdef", ".notdef", ".notdef", ".notdef", ".notdef",
  ".notdef", ".notdef", ".notdef", ".notdef", ".notdef",
  ".notdef", ".notdef", ".notdef", ".notdef", ".notdef",
  ".notdef", ".notdef", ".notdef", ".notdef", ".notdef",
  ".notdef", ".notdef", ".notdef", ".notdef", ".notdef",
  ".notdef", ".notdef", "space", "exclam", "quotedbl",
  "numbersign", "dollar", "percent", "ampersand", "quotesingle",
  "parenleft", "parenright", "asterisk", "plus", "comma",
  "hyphen", "period", "slash", "zero", "one",
  "two", "three", "four", "five", "six",
  "seven", "eight", "nine", "colon", "semicolon",
  "less", "equal", "greater", "question", "at",
  "A", "B", "C", "D", "E",
  "F", "G", "H", "I", "J",
  "K", "L", "M", "N", "O",
  "P", "Q", "R", "S", "T",
  "U", "V", "W", "X", "Y",
  "Z", "bracketleft", "backslash", "bracketright", "asciicircum",
  "underscore", "grave", "a", "b", "c",
  "d", "e", "f", "g", "h",
  "i", "j", "k", "l", "m",
  "n", "o", "p", "q", "r",
  "s", "t", "u", "v", "w",
  "x", "y", "z", "braceleft", "bar",
  "braceright", "asciitilde", ".notdef", ".notdef", ".notdef",
  ".notdef", ".notdef", ".notdef", ".notdef", ".notdef",
  ".notdef", ".notdef", ".notdef", ".notdef", ".notdef",
  ".notdef", ".notdef", ".notdef", ".notdef", "dotlessi",
  "quoteleft", "quoteright", "circumflex", "tilde", "macron",
  "breve", "dotaccent", "dieresis", ".notdef", "ring",
  "cedilla", ".notdef", "hungarumlaut", "ogonek", "caron",
  "space", "exclamdown", "cent", "sterling", "currency",
  "yen", "brokenbar", "section", "dieresis", "copyright",
  "ordfeminine", "guillemotleft", "logicalnot", "hyphen",
  "registered",
  "macron", "degree", "plusminus", "twosuperior", "threesuperior",
  "acute", "mu", "paragraph", "periodcentered", "cedilla",
  "onesuperior", "ordmasculine", "guillemotright", "onequarter",
  "onehalf",
  "threequarters", "questiondown", "Agrave", "Aacute", "Acircumflex",
  "Atilde", "Adieresis", "Aring", "AE", "Ccedilla",
  "Egrave", "Eacute", "Ecircumflex", "Edieresis", "Igrave",
  "Iacute", "Icircumflex", "Idieresis", "Eth", "Ntilde",
  "Ograve", "Oacute", "Ocircumflex", "Otilde", "Odieresis",
  "multiply", "Oslash", "Ugrave", "Uacute", "Ucircumflex",
  "Udieresis", "Yacute", "Thorn", "germandbls", "agrave",
  "aacute", "acircumflex", "atilde", "adieresis", "aring",
  "ae", "ccedilla", "egrave", "eacute", "ecircumflex",
  "edieresis", "igrave", "iacute", "icircumflex", "idieresis",
  "eth", "ntilde", "ograve", "oacute", "ocircumflex",
  "otilde", "odieresis", "divide", "oslash", "ugrave",
  "uacute", "ucircumflex", "udieresis", "yacute", "thorn",
  "ydieresis"
};

static int
parse_encoding (unsigned char **start, unsigned char *end, char **enc_vec)
{
  pst_obj *tok;
  int      code;

  /*
   *  StandardEncoding def
   * or
   *  ISOLatin1Encoding def
   * or
   *  0 1 255 {1 index exch /.notdef put } for
   *  dup int name put
   *  ...
   *  [readonly] def
   */
  tok = pst_get_token(start, end);
  if (MATCH_OP(tok, "StandardEncoding")) {
    RELEASE_TOK(tok);
    if (enc_vec) {
      for (code = 0; code < 256; code++) {
	if (StandardEncoding[code] &&
	    strcmp(StandardEncoding[code], ".notdef") != 0)
	  enc_vec[code] = strdup(StandardEncoding[code]);
	else
	  enc_vec[code] = NULL;
      }
    }
  } else if (MATCH_OP(tok, "ISOLatin1Encoding")) {
    RELEASE_TOK(tok);
    if (enc_vec) {
      for (code = 0; code < 256; code++) {
	if (ISOLatin1Encoding[code] &&
	    strcmp(ISOLatin1Encoding[code], ".notdef") != 0)
	  enc_vec[code] = strdup(ISOLatin1Encoding[code]);
	else
	  enc_vec[code] = NULL;
      }
    }
  } else if (MATCH_OP(tok, "ExpertEncoding")) {
    if (enc_vec) {
      WARN("ExpertEncoding not supported.");
      RELEASE_TOK(tok);
      return -1;
    }
    /*
     * Not supported yet.
     */
  } else {
    seek_operator(start, end, "array");
    /*
     * Pick all seaquences that matches "dup n /Name put" until
     * occurrence of "def" or "readonly".
     */
    while (*start < end &&
	   (tok = pst_get_token(start, end)) != NULL) {
      if (MATCH_OP(tok, "def") || MATCH_OP(tok, "readonly")) {
	RELEASE_TOK(tok);
	break;
      } else if (!MATCH_OP(tok, "dup")) {
	RELEASE_TOK(tok);
	continue;
      }
      RELEASE_TOK(tok);

      tok = pst_get_token(start, end);
      if (!tok || !PST_INTEGERTYPE(tok) ||
	  (code = pst_getIV(tok)) > 255 || code < 0) {
	RELEASE_TOK(tok);
	continue;
      }
      RELEASE_TOK(tok);

      tok = pst_get_token(start, end);
      if (!tok || !PST_NAMETYPE(tok)) {
	RELEASE_TOK(tok);
	continue;
      }
      if (enc_vec) {
	if (enc_vec[code])
	  RELEASE(enc_vec[code]);
	enc_vec[code] = pst_getSV(tok);
      }
      RELEASE_TOK(tok);

      tok = pst_get_token(start, end);
      if (!MATCH_OP(tok, "put")) {
	if (enc_vec[code]) {
	  RELEASE(enc_vec[code]);
	  enc_vec[code] = NULL;
	}
	RELEASE_TOK(tok);
	continue;
      }
      RELEASE_TOK(tok);
    }
  }

  return 0;
}

#ifndef CS_STR_LEN_MAX
#define CS_STR_LEN_MAX 65536UL
#endif
#ifndef CFF_GLYPH_MAX
#define CFF_GLYPH_MAX CFF_SID_MAX
#endif

static int
parse_subrs (unsigned char **start, unsigned char *end, int lenIV, t1_subr *subrs)
{
  pst_obj *tok;
  int      fail = 0;
  long     count, max, offset;

  tok = pst_get_token(start, end);
  if (!PST_INTEGERTYPE(tok) || pst_getIV(tok) < 0) {
    WARN("Parsing Subrs failed.");
    RELEASE_TOK(tok);
    return -1;
  }
  subrs->count = pst_getIV(tok);
  RELEASE_TOK(tok);

  tok = pst_get_token(start, end);
  if (!MATCH_OP(tok, "array")) {
    subrs->count = 0;
    RELEASE_TOK(tok);
    return -1;
  }
  RELEASE_TOK(tok);

  max    = CS_STR_LEN_MAX;
  offset = 0;
  subrs->data   = NEW(max, card8);
  subrs->length = NEW(subrs->count, long);
  subrs->offset = NEW(subrs->count, long);

  /* dup subr# n-bytes RD n-binary-bytes NP */
  for (count = 0; count < subrs->count;) {
    long idx, len;

    tok = pst_get_token(start, end);
    if (!tok) {
      fail = 1;
      break;
    } else if (MATCH_OP(tok, "ND") ||
	       MATCH_OP(tok, "|-") || MATCH_OP(tok, "def")) {
      RELEASE_TOK(tok);
      break;
    } else if (!MATCH_OP(tok, "dup")) {
      RELEASE_TOK(tok);
      continue;
    }
    RELEASE_TOK(tok);

    /* Found "dup" */
    tok = pst_get_token(start, end);
    if (!PST_INTEGERTYPE(tok) || pst_getIV(tok) < 0 ||
	pst_getIV(tok) >= subrs->count) {
      fail = 1;
      break;
    }
    idx = pst_getIV(tok);
    RELEASE_TOK(tok);

    tok = pst_get_token(start, end);
    if (!PST_INTEGERTYPE(tok) || pst_getIV(tok) < 0 ||
	pst_getIV(tok) > CS_STR_LEN_MAX) {
      fail = 1;
      break;
    }
    len = pst_getIV(tok);
    RELEASE_TOK(tok);

    tok = pst_get_token(start, end);
    if (!MATCH_OP(tok, "RD") && !MATCH_OP(tok, "-|") &&
	seek_operator(start, end, "readstring") < 0) {
      fail = 1;
      break;
    }
    RELEASE_TOK(tok);

    *start += 1;
    if (*start + len >= end) {
      fail = 1;
      break;
    }
    if (offset + len >= max) {
      max += CS_STR_LEN_MAX;
      subrs->data = RENEW(subrs->data, max, card8);
    }
    if (lenIV >= 0) {
      long j;
      subrs->offset[idx] = offset;
      subrs->length[idx] = len - lenIV;
      t1_crypt_init(CHARKEY);
      for (j = 0; j < len; j++) {
	card8 c;
	c = (card8) t1_decrypt(*((*start)+j));
	if (j >= lenIV) {
	  *(subrs->data + offset) = c;
	  offset++;
	}
      }
    } else {
      subrs->offset[idx] = offset;
      subrs->length[idx] = len;
      memcpy(subrs->data + offset, *start, len);
      offset += len;
    }
    *start += len;
    count++;
  }

  if (fail) {
    RELEASE_TOK(tok);
    if (subrs->length)
      RELEASE(subrs->length);
    if (subrs->offset)
      RELEASE(subrs->offset);
    if (subrs->data)
      RELEASE(subrs->data);
    subrs->length = NULL;
    subrs->offset = NULL;
    subrs->data   = NULL;
    subrs->count  = 0;
    return -1;
  }

  return 0;
}

static int
parse_charstrings (unsigned char **start, unsigned char *end, int lenIV,
		   t1_subr *charstrings, t1_charset *charset)
{
  pst_obj *tok;
  int      fail = 0;
  long     idx, max, offset;

  /* /CharStrings n dict dup begin
   * /GlyphName n-bytes RD -n-binary-bytes- ND
   * ...
   * end
   *  - stack - ... /CharStrings dict
   */
  tok = pst_get_token(start, end);
  if (!PST_INTEGERTYPE(tok) ||
      pst_getIV(tok) < 0 || pst_getIV(tok) > CFF_GLYPH_MAX) {
    charstrings->count = 0;
    RELEASE_TOK(tok);
    return -1;
  }
  charstrings->count = pst_getIV(tok);
  RELEASE_TOK(tok);

  max    = CS_STR_LEN_MAX;
  offset = 0;
  charstrings->length = NEW(charstrings->count, long);
  charstrings->offset = NEW(charstrings->count, long);
  charstrings->data = NEW(max, card8);

  charset->count  = charstrings->count;
  charset->glyphs = NEW(charset->count, char *);
  for (idx = 0; idx < charset->count; idx++) {
    charset->glyphs[idx] = NULL;
  }

  seek_operator(start, end, "begin");
  for (idx = 0; idx < charstrings->count; idx++) {
    long len;

    charset->glyphs[idx] = get_next_key(start, end);
    if (charset->glyphs[idx] == NULL) {
      fail = 1;
      break;
    }
    tok = pst_get_token(start, end);
    if (!PST_INTEGERTYPE(tok) ||
	pst_getIV(tok) < 0 || pst_getIV(tok) > CS_STR_LEN_MAX) {
      fail = 1;
      break;
    }
    len = pst_getIV(tok);
    RELEASE_TOK(tok);

    tok = pst_get_token(start, end);
    if (!MATCH_OP(tok, "RD") && !MATCH_OP(tok, "-|") &&
	seek_operator(start, end, "readstring") < 0) {
      fail = 1;
      break;
    }
    RELEASE_TOK(tok);

    if (*start + len + 1 >= end) {
      fail = 1;
      break;
    }
    if (offset + len >= max) {
      max += MAX(len, CS_STR_LEN_MAX);
      charstrings->data = RENEW(charstrings->data, max, card8);
      if (!charstrings->data)
	ERROR("Memory allocation failed.");
    }
    *start += 1;
    if (lenIV >= 0) {
      long j;
      charstrings->offset[idx] = offset;
      charstrings->length[idx] = len - lenIV;
      t1_crypt_init(CHARKEY);
      for (j = 0; j < len; j++) {
	card8 c;
	c = (card8) t1_decrypt(*((*start)+j));
	if (j >= lenIV) {
	  *(charstrings->data + offset) = c;
	  offset++;
	}
      }
    } else {
      charstrings->offset[idx] = offset;
      charstrings->length[idx] = len;
      memcpy(charstrings->data + offset, *start, len);
      offset += len;
    }
    *start += len;
  }

  if (fail) {
    RELEASE_TOK(tok);
    if (charstrings->length)
      RELEASE(charstrings->length);
    if (charstrings->offset)
      RELEASE(charstrings->offset);
    if (charstrings->data)
      RELEASE(charstrings->data);
    charstrings->length = NULL;
    charstrings->offset = NULL;
    charstrings->data   = NULL;
    for (idx = 0; idx < charset->count; idx++) {
      if (charset->glyphs[idx])
	RELEASE(charset->glyphs[idx]);
    }
    RELEASE(charset->glyphs);
    charstrings->count = charset->count = 0;
    return -1;
  }

  return 0;
}

#define CHECK_ARGN_EQ(n) if (argn != (n)) {\
                           WARN("%d values expected but only %d read.", (n), argn);\
                           RELEASE(key);\
                           return -1;\
                         }
#define CHECK_ARGN_GE(n) if (argn < (n)) {\
                           WARN("%d values expected but only %d read.", (n), argn);\
                           RELEASE(key);\
                           return -1;\
                         }
#define MAX_ARGS 127
static int
parse_part2 (unsigned char **start, unsigned char *end,
	     t1_subr *subrs, t1_subr *charstrings, t1_charset *charset, cff_font *cff)
{
  char  *key;
  double argv[MAX_ARGS];
  int    argn, lenIV = 4;
#if 0
  char *RD, *ND, *NP;
#endif

  while (*start < end &&
	 (key = get_next_key(start, end)) != NULL) {
    if (!strcmp(key, "Subrs")) {
      if (parse_subrs(start, end, lenIV, subrs) < 0) {
	RELEASE(key);
	return -1;
      }
#if 0
    } else if (!strcmp(key, "RD")) {
      RD = "RD";
    } else if (!strcmp(key, "ND")) {
      ND = "ND";
    } else if (!strcmp(key, "NP")) {
      NP = "NP";
    } else if (!strcmp(key, "-|")) {
      RD = "-|";
    } else if (!strcmp(key, "|-")) {
      ND = "|-";
    } else if (!strcmp(key, "|")) {
      NP = "|";
#endif
    } else if (!strcmp(key, "CharStrings")) {
      if (parse_charstrings(start, end, lenIV, charstrings, charset) < 0) {
	RELEASE(key);
	return -1;
      }
    } else if (!strcmp(key, "lenIV")) {
      argn = parse_nvalue(start, end, argv, 1);
      CHECK_ARGN_EQ(1);
      lenIV = (int) argv[0];
    } else if (!strcmp(key, "BlueValues") ||
	       !strcmp(key, "OtherBlues") ||
	       !strcmp(key, "FamilyBlues") ||
	       !strcmp(key, "FamilyOtherBlues") ||
	       !strcmp(key, "StemSnapH") ||
	       !strcmp(key, "StemSnapV")) {
      /*
       * Operand values are delta in CFF font dictionary encoding.
       */
      argn = parse_nvalue(start, end, argv, MAX_ARGS);
      CHECK_ARGN_GE(0);
      cff_dict_add(cff->private[0], key, argn);
      while (argn-- > 0) {
	cff_dict_set(cff->private[0], key, argn,
		     (argn == 0) ? argv[argn] : argv[argn] - argv[argn-1]);
      }
    } else if (!strcmp(key, "StdHW") ||
	       !strcmp(key, "StdVW") ||
	       !strcmp(key, "BlueScale") ||
	       !strcmp(key, "BlueShift") ||
	       !strcmp(key, "BlueFuzz")  ||
	       !strcmp(key, "LanguageGroup") ||
	       !strcmp(key, "ExpansionFactor")) {
      /*
       * Value of StdHW and StdVW is described as an array in the
       * Type 1 Font Specification but is a number in CFF format.
       */
      argn = parse_nvalue(start, end, argv, 1);
      CHECK_ARGN_EQ(1);
      cff_dict_add(cff->private[0], key, 1);
      cff_dict_set(cff->private[0], key, 0, argv[0]);
    } else if (!strcmp(key, "ForceBold")) {
      argn = parse_bvalue(start, end, &(argv[0]));
      CHECK_ARGN_EQ(1);
      if (argv[0] != 0) {
	cff_dict_add(cff->private[0], key, 1);
	cff_dict_set(cff->private[0], key, 0, 1);
      }
    }
    /*
     * MinFeature, RndStemUp, UniqueID, Password ignored.
     */
    RELEASE(key);
  }

  return 0;
}

static long
parse_part1 (unsigned char **start, unsigned char *end,
	     char *fontname, char **encoding, cff_font *cff)
{
  char  *key, *strval;
  double argv[MAX_ARGS];
  int    argn; /* Macro CHECK_ARGN_XX assume 'argn' is used. */

  /*
   * We skip PostScript code inserted before the start of 
   * font dictionary so that it will not be confused with.
   * See LMRoman10-Regular (lmr10.pfb) for example.
   */
  if (seek_operator(start, end, "begin") < 0)
    return -1;

  while (*start < end &&
	 (key = get_next_key(start, end)) != NULL) {
    if (!strcmp(key, "Encoding")) {
      if (parse_encoding(start, end, encoding) < 0) {
	RELEASE(key);
	return -1;
      }
    } else if (!strcmp(key, "FontName")) {
      argn = parse_svalue(start, end, &strval);
      CHECK_ARGN_EQ(1);
      if (strlen(strval) > TYPE1_NAME_LEN_MAX) {
	WARN("FontName too long: %d bytes", strlen(strval));
	strval[TYPE1_NAME_LEN_MAX+1] = '\0';
      }
      if (fontname) {
	strcpy(fontname, strval);
      }
      RELEASE(strval);
    } else if (!strcmp(key, "FontType")) {
      argn = parse_nvalue(start, end, argv, 1);
      CHECK_ARGN_EQ(1);
      if (argv[0] != 1.0) {
	WARN("FontType %d not supported.", (int) argv[0]);
	RELEASE(key);
	return -1;
      }
    } else if (!strcmp(key, "UniqueID")) {
      argn = parse_nvalue(start, end, argv, 1);
      CHECK_ARGN_EQ(1);
      cff_dict_add(cff->topdict, key, 1);
      cff_dict_set(cff->topdict, key, 0, argv[0]);
    } else if (!strcmp(key, "ItalicAngle") ||
	       !strcmp(key, "StrokeWidth") ||
	       !strcmp(key, "PaintType")) {
      argn = parse_nvalue(start, end, argv, 1);
      CHECK_ARGN_EQ(1);
      if (argv[0] != 0.0) {
#if 0
	/*
	 * Positive value in Bitstream CharterBT-Italic ???
	 */
	if (!strcmp(key, "ItalicAngle") && argv[0] > 0) {
	  WARN("Positive ItalicAngle value: %g", argv[0]);
	  argv[0] *= -1;
	}
#endif
	cff_dict_add(cff->topdict, key, 1);
	cff_dict_set(cff->topdict, key, 0, argv[0]);
      }
    } else if (!strcmp(key, "UnderLinePosition") ||
	       !strcmp(key, "UnderLineThickness")) {
      argn = parse_nvalue(start, end, argv, 1);
      CHECK_ARGN_EQ(1);
      cff_dict_add(cff->topdict, key, 1);
      cff_dict_set(cff->topdict, key, 0, argv[0]);
    } else if (!strcmp(key, "FontBBox")) {
      argn = parse_nvalue(start, end, argv, 4);
      CHECK_ARGN_EQ(4);
      cff_dict_add(cff->topdict, key, 4);
      while (argn-- > 0) {
	cff_dict_set(cff->topdict, key, argn, argv[argn]);
      }
    } else if (!strcmp(key, "FontMatrix")) {
      argn = parse_nvalue(start, end, argv, 6);
      CHECK_ARGN_EQ(6);
      if (argv[0] != 0.001 || argv[1] != 0.0 || argv[2] != 0.0 ||
	  argv[3] != 0.001 || argv[4] != 0.0 || argv[5] != 0.0) {
	cff_dict_add(cff->topdict, key, 6);
	while (argn-- > 0) {
	  cff_dict_set(cff->topdict, key, argn, argv[argn]);
	}
      }
    } else if (!strcmp(key, "version")  || !strcmp(key, "Notice") ||
	       !strcmp(key, "FullName") || !strcmp(key, "FamilyName") ||
	       !strcmp(key, "Weight")   || !strcmp(key, "Copyright")) {
      /*
       * FontInfo
       */
      argn = parse_svalue(start, end, &strval);
      CHECK_ARGN_EQ(1);
      {
	s_SID sid;
	cff_dict_add(cff->topdict, key, 1);
	if ((sid = cff_get_sid(cff, strval)) == CFF_STRING_NOTDEF)
	  sid = cff_add_string(cff, strval); /* FIXME */
	cff_dict_set(cff->topdict, key, 0, sid);
      }
      RELEASE(strval);
    } else if (!strcmp(key, "IsFixedPitch")) {
      argn = parse_bvalue(start, end, &(argv[0]));
      CHECK_ARGN_EQ(1);
      if (argv[0] != 0.0) {
	cff_dict_add(cff->private[0], key, 1);
	cff_dict_set(cff->private[0], key, 0, 1);
      }
    }
    RELEASE(key);
  }

  return 0;
}

static int
is_pfb (FILE *fp)
{
  char sig[15];
  int i, ch;

  rewind(fp);
  if ((ch = fgetc(fp)) != 128 ||
      (ch = fgetc (fp)) < 0 || ch > 3) {
    return 0;
  }
  for (i = 0; i < 4; i++) {
    if ((ch = fgetc(fp)) < 0) {
      return 0;
    }
  }
  for (i = 0; i < 14; i++) {
    if ((ch = fgetc(fp)) < 0) {
      return 0;
    }
    sig[i] = (char) ch;
  }
  if (!memcmp(sig, "%!PS-AdobeFont", 14) ||
      !memcmp(sig, "%!FontType1", 11)) {
    return 1;
  } else if (!memcmp(sig, "%!PS", 4)) {
    sig[14] = '\0';
    WARN("Ambiguous PostScript resource type: %s", sig);
    return 1;
  } else {
    WARN("Not a PFB font file?");
    return 0;
  }

  return 0;
}


#define PFB_SEG_TYPE_ASCII  1
#define PFB_SEG_TYPE_BINARY 2

static unsigned char *
get_pfb_segment (FILE *fp, int expected_type, long *length)
{
  unsigned char *buffer;
  long bytesread;

  buffer = NULL; bytesread = 0;
  for (;;) {
    int ch;

    ch = fgetc(fp);
    if (ch < 0) {
      break;
    } else if (ch != 128) {
      ERROR("Not a pfb file?");
    }
    ch = fgetc(fp);
    if (ch < 0 || ch != expected_type) {
      seek_relative(fp, -2);
      break;
    }
    {
      long slen, rlen;
      int  i;

      slen = 0;
      for (i = 0; i < 4; i++) {
	if ((ch = fgetc(fp)) < 0) {
	  if (buffer)
	    RELEASE(buffer);
	  return NULL;
	}
	slen = slen + (ch << (8*i));
      }
      buffer = RENEW(buffer, bytesread + slen, unsigned char);
      while (slen > 0) {
	rlen = fread(buffer + bytesread, sizeof(unsigned char), slen, fp);
	if (rlen < 0) {
	  if (buffer)
	    RELEASE(buffer);
	  return NULL;
	}
	slen -= rlen;
	bytesread += rlen;
      }
    }
  }
  if (bytesread == 0) {
    ERROR("PFB segment length zero?");
  }

  if (length)
    *length = bytesread;
  return buffer;
}

static int
get_fontname (FILE *fp, char *fontname)
{
  unsigned char *buffer, *start, *end;
  long  length;
  char *key;

  rewind(fp);
  buffer = get_pfb_segment(fp, PFB_SEG_TYPE_ASCII, &length);
  if (buffer == NULL || length == 0)
    ERROR("Reading PFB (ASCII part) file failed.");
  start = buffer;
  end   = buffer + length;

  if (seek_operator(&start, end, "begin") < 0) {
    RELEASE(buffer);
    return -1;
  }

  while (start < end &&
	 (key = get_next_key(&start, end)) != NULL) {
    if (!strcmp(key, "FontName")) {
      char *strval;
      if (parse_svalue(&start, end, &strval) != 1) {
	RELEASE(key);
	RELEASE(buffer);
	return -1;
      }
      if (strlen(strval) > TYPE1_NAME_LEN_MAX)
	WARN("FontName too long. (%d bytes)", strlen(strval));
      strncpy(fontname, strval,
	      (strlen(strval) > TYPE1_NAME_LEN_MAX) ? TYPE1_NAME_LEN_MAX : strlen(strval));
      RELEASE(strval);
    }
    RELEASE(key);
  }
  RELEASE(buffer);

  return 0;
}

static long
read_font (FILE *fp,
	   char *fontname, char **enc_vec,
	   t1_subr *subrs, t1_subr *charstrings, t1_charset *charset, cff_font *cff)
{
  long length;
  unsigned char *buffer, *start, *end;

  rewind(fp);
  /* ASCII section */
  buffer = get_pfb_segment(fp, PFB_SEG_TYPE_ASCII, &length);
  if (buffer == NULL || length == 0)
    ERROR("Reading PFB (ASCII part) file failed.");
  start = buffer; end = buffer + length;
  if (parse_part1(&start, end, fontname, enc_vec, cff) < 0)
    ERROR("Reading PFB (ASCII part) file failed.");
  RELEASE(buffer);

  /* Binary section */
  buffer = get_pfb_segment(fp, PFB_SEG_TYPE_BINARY, &length);
  if (buffer == NULL || length == 0)
    ERROR("Reading PFB (BINARY part) file failed.");
  {
    int  i;
    t1_crypt_init(EEKEY);
    for (i = 0; i < length; i++) {
      buffer[i] = (unsigned char) t1_decrypt(buffer[i]);
    }
  }
  start = buffer + 4; end = buffer + length;
  if (parse_part2(&start, end, subrs, charstrings, charset, cff) < 0)
    ERROR("Reading PFB (BINARY part) file failed.");
  RELEASE(buffer);

  /* Remaining section ommited. */

  return length;
}

static char *
get_standard_glyph (int code)
{
  if (!StandardEncoding[code])
    return NULL;

  return (char *) StandardEncoding[code];
}

static long
lookup_charset (t1_charset *charset, const char *glyph)
{
  long gid;

  for (gid = 0; gid < charset->count; gid++) {
    if (charset->glyphs[gid] && !strcmp(charset->glyphs[gid], glyph))
      return gid;
  }

  return -1;
}

static int
is_basefont (const char *name)
{
  static const char *basefonts[] = {
    "Courier",                  "Courier-Bold",          "Courier-Oblique",
    "Courier-BoldOblique",      "Helvetica",             "Helvetica-Bold",
    "Helvetica-Oblique",        "Helvetica-BoldOblique", "Symbol",
    "Times-Roman",              "Times-Bold",            "Times-Italic",
    "Times-BoldItalic",         "ZapfDingbats"
  };
  int i;
  for (i = 0; i < 14; i++) {
    if (!strcmp(name, basefonts[i]))
      return 1;
  }
  return 0;
}

static int
Type1Font_is_basefont (Type1Font *font)
{
  ASSERT(font);

  return (font->flags & TYPE1FONT_FLAG_BASEFONT);
}

int
Type1Font_open (Type1Font *font, const char *name, int encoding_id, int embed)
{
  FILE *fp;
  char *fullname = NULL;
  char  fontname[TYPE1_NAME_LEN_MAX+1];

  ASSERT(font);

  if (is_basefont(name)) {
    font->ident       = strdup(name);
    font->fontname    = strdup(name);
    font->filename    = NULL;
    font->encoding_id = encoding_id;
    font->embed       = 0;
    font->fontdict    = pdf_new_dict();
    font->descriptor  = NULL;

    font->flags |= TYPE1FONT_FLAG_BASEFONT;

    pdf_add_dict(font->fontdict,
		 pdf_new_name("Type"), pdf_new_name("Font"));
    pdf_add_dict(font->fontdict,
		 pdf_new_name("Subtype"), pdf_new_name("Type1"));
    pdf_add_dict(font->fontdict,
		 pdf_new_name("BaseFont"), pdf_new_name(font->fontname));
    return 0;
  }

  if ((fullname = kpse_find_file(name, kpse_type1_format, 1))
      == NULL ||
      (fp = MFOPEN(fullname, FOPEN_RBIN_MODE)) == NULL) {
    return -1;
  }
  memset(fontname, 0, TYPE1_NAME_LEN_MAX+1);
  if (!is_pfb(fp) || get_fontname(fp, fontname) < 0) {
    MFCLOSE(fp);
    return -1;
  }
  MFCLOSE(fp);

  font->ident       = strdup(name);
  font->fontname    = NEW(strlen(fontname)+8, char);
  strcpy(font->fontname, fontname);
  font->filename    = strdup(fullname);
  font->encoding_id = encoding_id;
  font->embed       = embed;
  font->fontdict    = pdf_new_dict();
  font->descriptor  = pdf_new_dict();

  pdf_add_dict(font->fontdict,
	       pdf_new_name("Type"),
	       pdf_new_name("Font"));
  pdf_add_dict(font->fontdict,
	       pdf_new_name("Subtype"),
	       pdf_new_name("Type1"));
  pdf_add_dict(font->descriptor,
	       pdf_new_name("Type"),
	       pdf_new_name("FontDescriptor"));

  if (font->embed)
    mangle_name(font->fontname);

  pdf_add_dict(font->fontdict,
	       pdf_new_name("BaseFont"),
	       pdf_new_name(font->fontname));
  pdf_add_dict(font->descriptor,
	       pdf_new_name("FontName"),
	       pdf_new_name(font->fontname));
  if (font->embed)
    font->used_chars = new_used_chars();

  return 0;
}

void
Type1Font_dofont (Type1Font *font)
{
  cff_font  *cff;
  cff_index *topdict;
  t1_subr    subroutines;
  t1_subr    charstrings;
  t1_charset charset;
  char     **enc_vec, fontname[TYPE1_NAME_LEN_MAX+1];
  double  defaultwidth, nominalwidth;
  double *widths;
  card16 *GIDMap, num_glyphs = 0;
  FILE *fp;

  ASSERT(font);

  if (!font->indirect)
    return;

  if (Type1Font_is_basefont(font)) {
    if (font->encoding_id >= 0) {
      Encoding *enc = Encoding_cache_get(font->encoding_id);
      pdf_add_dict(font->fontdict,
		   pdf_new_name("Encoding"),
		   Encoding_get_resource(enc));
    }
    return;
  }

  pdf_add_dict(font->fontdict, 
	       pdf_new_name("FontDescriptor"),
	       pdf_link_obj(pdf_ref_obj(font->descriptor)));

  /*
   * Open Type 1 font.
   */
  {
    if (!Type1Font_get_usedchars(font))
      ERROR("%s: Unexpected error: No used_chars?", TYPE1FONT_DEBUG_STR);
    if ((fp = MFOPEN(font->filename, FOPEN_RBIN_MODE)) == NULL) {
      ERROR("%s: Could not open Type1 font.", TYPE1FONT_DEBUG_STR);
    }
  }

  {
    cff = NEW(1, cff_font);
    cff->flag = FONTTYPE_FONT;
    cff->offset = 0;
    cff->header.major = 1;
    cff->header.minor = 0;
    cff->header.hdr_size = 4;
    cff->header.offsize  = 4;
    cff->name  = NULL;
    cff->gsubr = cff_new_index(0); /* No Global Subr */
    cff->subrs = NULL;
    cff->cstrings = NULL;
    cff->fontname  = NULL;
    /* cff_set_name(cff, cff->fontname); */
    cff->num_fds = 1;
    cff->topdict = cff_new_dict();
    cff->string  = NULL;
    cff->_string = cff_new_index(0);
    cff->fdarray  = NULL;
    cff->fdselect = NULL;
    cff->private  = NEW(1, cff_dict *);
    cff->private[0] = cff_new_dict();
  }

  {
    subroutines.length = NULL;
    subroutines.offset = NULL;
    subroutines.data   = NULL;
    charstrings.length = NULL;
    charstrings.offset = NULL;
    charstrings.data   = NULL;
    charset.glyphs = NULL;
    GIDMap = NULL;
    num_glyphs = 0;
  }

  if (font->encoding_id >= 0) {
    enc_vec = NULL;
  } else {
    int code;
    enc_vec = NEW(256, char *);
    for (code = 0; code < 256; code++)
      enc_vec[code] = NULL;
  }
  read_font(fp, fontname, enc_vec, &subroutines, &charstrings, &charset, cff);
  MFCLOSE(fp);
  /*
   * Encoding related things.
   */
  if (font->encoding_id >= 0) {
    Encoding *enc = Encoding_cache_get(font->encoding_id);
    if (Encoding_is_predefined(enc)) {
      pdf_add_dict(font->fontdict,
		   pdf_new_name("Encoding"),
		   pdf_new_name(Encoding_get_name(enc)));
    } else {
#if 0
      /*
       * Gs not working with this.
       */
      pdf_add_dict(font->fontdict,
		   pdf_new_name("Encoding"),
		   Encoding_get_resource(enc));
#endif
      UC_add_ToUnicode_CMap(font->fontdict, enc);
    }
    enc_vec = Encoding_get_encoding(enc);
  } else {
    pdf_obj *tounicode;
    char    *tmpname;
    int      code;
    /*
     * Create enc_vec and ToUnicode CMap for built-in encoding.
     */
    ASSERT(enc_vec);
    for (code = 0; code < 256; code++) {
      if (!font->used_chars[code] && enc_vec[code]) {
	RELEASE(enc_vec[code]);
	enc_vec[code] = NULL;
      }
    }
    /*
     * The naming convention for embedded ToUnicode is not clear.
     * We use PseudoUniqTag+FontName-UTF16 here. Not -UCS2, it is to-UTF16BE CMap.
     */
    tmpname = NEW(strlen(font->fontname)+7, char);
    sprintf(tmpname, "%s-UTF16", font->fontname);
    tounicode = UC_make_ToUnicode_CMap(tmpname, enc_vec, NULL);
    if (tounicode) {
      pdf_add_dict(font->fontdict,
		   pdf_new_name("ToUnicode"), pdf_link_obj(pdf_ref_obj(tounicode)));
      pdf_release_obj(tounicode);
    }
    RELEASE(tmpname);
  }

  cff_set_name(cff, font->fontname);

  defaultwidth = 500.0;
  nominalwidth = 0.0;
  {
    double italicangle, capheight, ascent, descent, stemv;
    long flags = 0;
    long gid;
    t1_ginfo gm;
    /*
     * CapHeight, Ascent, and Descent is meaningfull only for Latin/Greek/Cyrillic.
     * The BlueValues and OtherBlues also have those information.
     */
    if (cff_dict_known(cff->topdict, "FontBBox")) {
      /* Default values */
      capheight = ascent = cff_dict_get(cff->topdict, "FontBBox", 3);
      descent = cff_dict_get(cff->topdict, "FontBBox", 1);
    } else {
      capheight =  680.0;
      ascent    =  690.0;
      descent   = -190.0;
    }
    if (cff_dict_known(cff->private[0], "StdVW")) {
      stemv = cff_dict_get(cff->private[0], "StdVW", 0);
    } else {
      /*
       * We may use the following values for StemV:
       *  Thin - ExtraLight: <= 50
       *  Light: 71
       *  Regular(Normal): 88
       *  Medium: 109
       *  SemiBold(DemiBold): 135
       *  Bold - Heavy: >= 166
       */
      stemv = 88.0;
    }
    if (cff_dict_known(cff->topdict, "ItalicAngle")) {
      italicangle = cff_dict_get(cff->topdict, "ItalicAngle", 0);
      if (italicangle != 0.0)
	flags |= FONT_FLAG_ITALIC;
    } else {
      italicangle = 0.0;
    }

    /*
     * Use "space", "H", "p", and "b" for various values.
     * Those characters should not "seac". (no accent)
     */
    gid = lookup_charset(&charset, "space");
    if (gid >= 0 && gid < charstrings.count) {
      cs1_get_metrics(charstrings.data + charstrings.offset[gid],
		      charstrings.length[gid], &subroutines, &gm);
      defaultwidth = gm.wx;
    }
    gid = lookup_charset(&charset, "H");
    if (gid >= 0 && gid < charstrings.count) {
      cs1_get_metrics(charstrings.data + charstrings.offset[gid],
		      charstrings.length[gid], &subroutines, &gm);
      capheight = gm.bbox.ury;
    }
    gid = lookup_charset(&charset, "p");
    if (gid >= 0 && gid < charstrings.count) {
      cs1_get_metrics(charstrings.data + charstrings.offset[gid],
		      charstrings.length[gid], &subroutines, &gm);
      descent = gm.bbox.lly;
    }
    gid = lookup_charset(&charset, "b");
    if (gid >= 0 && gid < charstrings.count) {
      cs1_get_metrics(charstrings.data + charstrings.offset[gid],
		      charstrings.length[gid], &subroutines, &gm);
      ascent = gm.bbox.ury;
    }

    if (defaultwidth != 0.0) {
      cff_dict_add(cff->private[0], "defaultWidthX", 1);
      cff_dict_set(cff->private[0], "defaultWidthX", 0, defaultwidth);
    }
    if (nominalwidth != 0.0) {
      cff_dict_add(cff->private[0], "nominalWidthX", 1);
      cff_dict_set(cff->private[0], "nominalWidthX", 0, nominalwidth);
    }
    if (cff_dict_known(cff->private[0], "ForceBold") &&
	cff_dict_get(cff->private[0], "ForceBold", 0)) {
      flags |= FONT_FLAG_FORCEBOLD;
    }
    if (cff_dict_known(cff->private[0], "IsFixedPitch") &&
	cff_dict_get(cff->private[0], "IsFixedPitch", 0)) {
      flags |= FONT_FLAG_FIXEDPITCH;
    }
    if (!strstr(fontname, "Sans")) {
      flags |= FONT_FLAG_SERIF;
    }
    flags |= FONT_FLAG_SYMBOLIC;

    pdf_add_dict(font->descriptor,
		 pdf_new_name("CapHeight"), pdf_new_number(capheight));
    pdf_add_dict(font->descriptor,
		 pdf_new_name("Ascent"), pdf_new_number(ascent));
    pdf_add_dict(font->descriptor,
		 pdf_new_name("Descent"), pdf_new_number(descent));
    pdf_add_dict(font->descriptor,
		 pdf_new_name("ItalicAngle"), pdf_new_number(italicangle));
    pdf_add_dict(font->descriptor,
		 pdf_new_name("StemV"), pdf_new_number(stemv));
    pdf_add_dict(font->descriptor,
		 pdf_new_name("Flags"), pdf_new_number(flags));
  }

#define MAX_GLYPHS 1024
  GIDMap = NEW(MAX_GLYPHS, card16);
  {
    int  code, prev, i;
    long gid;

    cff->encoding = NEW(1, cff_encoding);
    cff->encoding->format      = 1;
    cff->encoding->num_entries = 0;
    cff->encoding->data.range1 = NEW(256, cff_range1);
    cff->encoding->num_supps   = 0;
    cff->encoding->supp        = NEW(256, cff_map);

    cff->charsets = NEW(1, cff_charsets);
    cff->charsets->format = 0;
    cff->charsets->num_entries = 0;
    cff->charsets->data.glyphs = NEW(MAX_GLYPHS, s_SID);

    gid = lookup_charset(&charset, ".notdef");
    if (gid < 0)
      ERROR("Type 1 font with no \".notdef\" glyph???");
    GIDMap[0] = (card16) gid;
    if (__verbose > 2)
      MESG("[glyphs:/.notdef");
    num_glyphs =  1;
    prev  = -2;
    for (code = 0; code < 256; code++) {
      char *glyph = enc_vec[code];
      s_SID sid;
      if (font->used_chars[code] && glyph && strcmp(glyph, ".notdef")) {
	gid = lookup_charset(&charset, glyph);
	if (gid < 0) {
	  WARN("Glyph \"%s\" missing in font.", glyph);
	  font->used_chars[code] = 0;
	  continue;
	}
	for (i = 0; i < code; i++) {
	  if (font->used_chars[i] &&
	      enc_vec[i] && !strcmp(enc_vec[i], glyph))
	    break;
	}
	sid = cff_add_string(cff, glyph); /* FIXME */
	if (i != code) {
	  cff->encoding->supp[cff->encoding->num_supps].code  = i;
	  cff->encoding->supp[cff->encoding->num_supps].glyph = sid;
	  cff->encoding->num_supps += 1;
	} else {
	  GIDMap[num_glyphs] = (card16) gid;
	  cff->charsets->data.glyphs[cff->charsets->num_entries] = sid;
	  cff->charsets->num_entries += 1;
	  if (code != prev + 1) {
	    cff->encoding->num_entries += 1;
	    cff->encoding->data.range1[cff->encoding->num_entries-1].first  = code;
	    cff->encoding->data.range1[cff->encoding->num_entries-1].n_left = 0;
	  } else
	    cff->encoding->data.range1[cff->encoding->num_entries-1].n_left += 1;
	  prev = code;
	  num_glyphs++;
	  if (__verbose > 2)
	    MESG("/%s", glyph);
	}
      } else {
	font->used_chars[code] = 0;
      }
    }
    if (cff->encoding->num_supps > 0)
      cff->encoding->format |= 0x80;
  }

  widths = NEW(charstrings.count, double);
  /*
   * No more string.
   * The Type 1 seac operator may add another glyph but the glyph name of
   * those glyphs are contained in standard string. The String Index will
   * not be modified.
   */
  cff_update_string(cff);
  {
    cff_index *cs;
    t1_ginfo   gm;
    card16 gid, ogid;
    long   max = 0, offset = 0;
    double bbox[4];
    int    have_seac = 0;

    bbox[0] = bbox[1] =  1000.0;
    bbox[2] = bbox[3] = -1000.0;

    cs = cff_new_index(charstrings.count);
    cs->data = NULL;
    cs->offset[0] = 1;
    for (gid = 0; gid < num_glyphs; gid++) {
      if (offset + CS_STR_LEN_MAX >= max) {
	max += CS_STR_LEN_MAX*2;
	cs->data = RENEW(cs->data, max, card8);
      }
      ogid = GIDMap[gid];
      offset += cs1_convert_charstring(cs->data + cs->offset[gid] - 1, CS_STR_LEN_MAX,
				       charstrings.data + charstrings.offset[ogid],
				       charstrings.length[ogid],
				       &subroutines, defaultwidth, nominalwidth,
				       &gm);
      cs->offset[gid+1] = offset + 1;
      if (gm.use_seac) {
	long  comp_gid;
	char *comp_name;
	/*
	 * NOTE:
	 *  1. seac.achar and seac.bchar must be contained in the CFF standard string.
	 *  2. Those characters need not to be encoded.
	 *  3. num_glyphs == cff->charsets->num_entries + 1.
	 */
	comp_name = get_standard_glyph(gm.seac.achar);
	if (cff_glyph_lookup(cff, comp_name) == 0) {
	  comp_gid = lookup_charset(&charset, comp_name);
	  if (comp_gid < 0) {
	    WARN("Accent char \"%s\" not found.", comp_name);
	  }
	  if (__verbose > 2)
	    MESG("/%s", comp_name);
	  GIDMap[num_glyphs++] = comp_gid;
	  cff->charsets->data.glyphs[cff->charsets->num_entries] = cff_get_sid(cff, comp_name);
	  cff->charsets->num_entries += 1;
	}
	comp_name = get_standard_glyph(gm.seac.bchar);
	if (cff_glyph_lookup(cff, comp_name) == 0) {
	  comp_gid = lookup_charset(&charset, comp_name);
	  if (comp_gid < 0) {
	    WARN("Base char \"%s\" not found.", comp_name);
	  }
	  if (__verbose > 2)
	    MESG("/%s", comp_name);
	  GIDMap[num_glyphs++] = comp_gid;
	  cff->charsets->data.glyphs[cff->charsets->num_entries] = cff_get_sid(cff, comp_name);
	  cff->charsets->num_entries += 1;
	}
	have_seac = 1;
      }
      widths[gid] = gm.wx;
      if (gm.bbox.llx < bbox[0]) bbox[0] = gm.bbox.llx;
      if (gm.bbox.lly < bbox[1]) bbox[1] = gm.bbox.lly;
      if (gm.bbox.urx > bbox[2]) bbox[2] = gm.bbox.urx;
      if (gm.bbox.ury > bbox[3]) bbox[3] = gm.bbox.ury;
    }
    cs->count = num_glyphs;
    cff->cstrings = cs;
    /*
     * FontBBox.
     */
    {
      pdf_obj *tmp;
      int i;
      tmp = pdf_new_array();
      if (!have_seac) {
	if (!cff_dict_known(cff->topdict, "FontBBox"))
	  cff_dict_add(cff->topdict, "FontBBox", 4);
	for (i = 0; i < 4; i++) {
	  pdf_add_array(tmp, pdf_new_number(ROUND(bbox[i], 1.0)));
	  cff_dict_set(cff->topdict, "FontBBox", i, bbox[i]);
	}
      } else {
	/*
	 * Correct FontBBox not obtained due to seac.
	 */
	if (!cff_dict_known(cff->topdict, "FontBBox"))
	  ERROR("No FontBBox?");
	for (i = 0; i < 4; i++) {
	  double val = cff_dict_get(cff->topdict, "FontBBox", i);
	  pdf_add_array(tmp, pdf_new_number(ROUND(val, 1.0)));
	}
      }
      pdf_add_dict(font->descriptor, pdf_new_name("FontBBox"), tmp);
    }
    {
      int code, firstchar, lastchar;
      pdf_obj *tmp;

      tmp = pdf_new_array();
      if (num_glyphs <= 1) {
	/* This should be error. */
	firstchar = lastchar = 0;
	pdf_add_array(tmp, pdf_new_number(0.0));
      } else {
	firstchar = 255; lastchar = 0;
	for (code = 0; code < 256; code++) {
	  if (font->used_chars[code]) {
	    if (code < firstchar) firstchar = code;
	    if (code > lastchar)  lastchar  = code;
	  }
	}
	tmp = pdf_new_array();
	for (code = firstchar; code <= lastchar; code++) {
	  if (font->used_chars[code]) {
	    card16 gid = cff_encoding_lookup(cff, code);
	    pdf_add_array(tmp, pdf_new_number(ROUND(widths[gid], 1.0)));
	  } else {
	    pdf_add_array(tmp, pdf_new_number(0.0));
	  }
	}
      }
      pdf_add_dict(font->fontdict,
		   pdf_new_name("FirstChar"), pdf_new_number(firstchar));
      pdf_add_dict(font->fontdict,
		   pdf_new_name("LastChar"),  pdf_new_number(lastchar));
      pdf_add_dict(font->fontdict, pdf_new_name("Widths"), pdf_ref_obj(tmp));
      pdf_release_obj(tmp);
    }
  }
  if (__verbose > 2)
    MESG("]");
  {
    long private_size, destlen, charstring_len;
    long topdict_offset, offset;
    card8 *dest;
    {
#define WORK_BUFFER_SIZE 1024
      char work_buffer[WORK_BUFFER_SIZE];

      topdict = cff_new_index(1);
      /*
       * Force existence of Encoding.
       */
      if (!cff_dict_known(cff->topdict, "CharStrings"))
	cff_dict_add(cff->topdict, "CharStrings", 1);
      if (!cff_dict_known(cff->topdict, "charset"))
	cff_dict_add(cff->topdict, "charset", 1);
      if (!cff_dict_known(cff->topdict, "Encoding"))
	cff_dict_add(cff->topdict, "Encoding", 1);
      private_size = cff_dict_pack((cff->private)[0],
				   (card8 *) work_buffer, WORK_BUFFER_SIZE);
      if (private_size > 0 && !cff_dict_known(cff->topdict, "Private"))
	cff_dict_add(cff->topdict, "Private", 2);
      (topdict->offset)[1] = cff_dict_pack(cff->topdict,
					   (card8 *) work_buffer,
					   WORK_BUFFER_SIZE) + 1;
    }
    /*
     * Estimate total size of fontfile.
     */
    charstring_len = cff_index_size(cff->cstrings);
    {
      destlen = 4; /* header size */
      destlen += cff_index_size(cff->name);
      destlen += cff_index_size(topdict);
      destlen += cff_index_size(cff->string);
      destlen += cff_index_size(cff->gsubr);
      /*
       * We are using format 1 for Encoding and format 0 for charset.
       * TODO: Should implement cff_xxx_size().
       */
      destlen += 2 + (cff->encoding->num_entries)*2 + 1 + (cff->encoding->num_supps)*3;
      destlen += 1 + (cff->charsets->num_entries)*2;
      destlen += charstring_len;
      destlen += private_size;
    }
    /*
     * Now we create FontFile data.
     */
    {
      dest = NEW(destlen, card8);
      /*
       * Data Layout order as described in CFF spec., sec 2 "Data Layout".
       */
      offset = 0;
      /* Header */
      offset += cff_put_header(cff, dest + offset, destlen - offset);
      /* Name */
      offset += cff_pack_index(cff->name, dest + offset, destlen - offset);
      /* Top DICT */
      topdict_offset = offset;
      offset += cff_index_size(topdict);
      /* Strings */
      offset += cff_pack_index(cff->string, dest + offset, destlen - offset);
      /* Global Subrs */
      offset += cff_pack_index(cff->gsubr, dest + offset, destlen - offset);
      /* Encoding */
      cff_dict_set(cff->topdict, "Encoding", 0, offset);
      offset += cff_pack_encoding(cff, dest + offset, destlen - offset);
      /* charset */
      cff_dict_set(cff->topdict, "charset", 0, offset);
      offset += cff_pack_charsets(cff, dest + offset, destlen - offset);
      /* CharStrings */
      cff_dict_set(cff->topdict, "CharStrings", 0, offset);
      offset += cff_pack_index(cff->cstrings, dest + offset, charstring_len);
      /* Private */
      if ((cff->private)[0] && private_size > 0) {
	private_size = cff_dict_pack((cff->private)[0], dest + offset, private_size);
	cff_dict_set(cff->topdict, "Private", 1, offset);
	cff_dict_set(cff->topdict, "Private", 0, private_size);
      }
      offset += private_size;

      /* Finally Top DICT */
      topdict->data = NEW(topdict->offset[1] - 1, card8);
      cff_dict_pack(cff->topdict, topdict->data, topdict->offset[1] - 1);
      cff_pack_index(topdict, dest + topdict_offset, cff_index_size(topdict));
      cff_release_index(topdict);

      /* Copyright and Trademark Notice ommited. */

      {
	pdf_obj *fontfile, *stream_dict;

	fontfile    = pdf_new_stream(STREAM_COMPRESS);
	stream_dict = pdf_stream_dict(fontfile);
	pdf_add_dict(font->descriptor,
		     pdf_new_name("FontFile3"),
		     pdf_link_obj(pdf_ref_obj(fontfile)));
	pdf_add_dict(stream_dict,
		     pdf_new_name("Subtype"),
		     pdf_new_name("Type1C"));
	pdf_add_stream(fontfile, (char *) dest, offset);
	pdf_release_obj(fontfile);
	RELEASE(dest);
      }
      cff_close(cff); cff = NULL;
    }

    if (__verbose > 1)
      MESG("[%u glyphs][%ld bytes]", num_glyphs, offset);
  }

  /* Cleanup */
  {
    int    code;
    card16 gid;

    if (subroutines.length)
      RELEASE(subroutines.length);
    if (subroutines.offset)
      RELEASE(subroutines.offset);
    if (subroutines.data)
      RELEASE(subroutines.data);
    if (charstrings.length)
      RELEASE(charstrings.length);
    if (charstrings.offset)
      RELEASE(charstrings.offset);
    if (charstrings.data)
      RELEASE(charstrings.data);
    if (charset.glyphs) {
      for (gid = 0; gid < charset.count; gid++)
	if (charset.glyphs[gid])
	  RELEASE(charset.glyphs[gid]);
      RELEASE(charset.glyphs);
    }
    if (font->encoding_id < 0 && enc_vec) {
      for (code = 0; code < 256; code++) {
	if (enc_vec[code])
	  RELEASE(enc_vec[code]);
      }
      RELEASE(enc_vec);
    }
    if (widths)
      RELEASE(widths);
    if (GIDMap)
      RELEASE(GIDMap);
  }

  /*
   * Maybe writing Charset is recommended for subsetted font.
   */

  return;
}


/******************************** CACHE ********************************/

#define CACHE_ALLOC_SIZE 16u

struct FontCache {
  int num;
  int max;
  Type1Font **fonts;
};

static struct FontCache *__cache = NULL;

void
Type1Font_cache_init (void)
{
  if (__cache)
    ERROR("%s: Already initialized.", TYPE1FONT_DEBUG_STR);

  __cache = NEW(1, struct FontCache);
  __cache->num   = 0;
  __cache->max   = CACHE_ALLOC_SIZE;
  __cache->fonts = NEW(__cache->max, Type1Font *);
}

#define CHECK_ID(n) do {\
                        if (! __cache)\
                           ERROR("%s: Font cache not initialized.", TYPE1FONT_DEBUG_STR);\
                        if ((n) < 0 || (n) >= __cache->num)\
                           ERROR("%s: Invalid ID %d", TYPE1FONT_DEBUG_STR, (n));\
                    } while (0)

Type1Font *
Type1Font_cache_get (int font_id)
{
  CHECK_ID(font_id);

  return __cache->fonts[font_id];
}

/*
 * tfm_id, remap ... 
 */
int
Type1Font_cache_find (const char *map_name, char *res_name, int encoding_id,
		      int tfm_id, int remap)
{
  Type1Font *font;
  int font_id;

  if (!__cache)
    Type1Font_cache_init();
  ASSERT(__cache);

  for (font_id = 0; font_id < __cache->num; font_id++) {
    font = __cache->fonts[font_id];
    if (map_name && font->ident && !strcmp(font->ident, map_name)
	&& (encoding_id == font->encoding_id))
      return font_id;
  }

  font_id = __cache->num;
  font    = Type1Font_new();
  if (Type1Font_open(font, map_name, encoding_id, 1) < 0) {
    Type1Font_release(font);
    return -1;
  }

  if (__cache->num >= __cache->max) {
    __cache->max  += CACHE_ALLOC_SIZE;
    __cache->fonts = RENEW(__cache->fonts, __cache->max, Type1Font *);
  }
  __cache->fonts[font_id] = font;
  (__cache->num)++;

  return font_id;
}

void
Type1Font_cache_close (void)
{
  if (__cache) {
    int i;
    for (i = 0; i < __cache->num; i++) {
      Type1Font *font = __cache->fonts[i];
      if (__verbose) {
	MESG("(Type1:%s", font->ident);
	if (__verbose > 1)
	  MESG("[%s][%s]", font->filename, font->fontname);
      }
      Type1Font_dofont (font);
      Type1Font_flush  (font);
      Type1Font_release(font);
      if (__verbose)
	MESG(")");
    }
    RELEASE(__cache);
  }
}


/******************************** COMPAT ********************************/

void
type1_disable_partial (void)
{
  WARN("Only subsetted embedding supported for Type 1 font.");
}

void
type1_set_verbose (void)
{
  Type1Font_set_verbose();
}

pdf_obj *
type1_font_resource (int font_id)
{
  return Type1Font_get_resource(Type1Font_cache_get(font_id));
}

char *
type1_font_used (int font_id)
{
  return Type1Font_get_usedchars(Type1Font_cache_get(font_id));
}

int
type1_font (const char *tex_name, int tfm_id, char *resource_name, int encoding_id, int remap)
{
  return Type1Font_cache_find(tex_name, resource_name, encoding_id, tfm_id, remap);
}

void
type1_set_mapfile (const char *name)
{
  return;
}

void
type1_close_all (void)
{
  Type1Font_cache_close();
}
