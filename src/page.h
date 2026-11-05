#ifndef PENSHIP_PAGE_H
#define PENSHIP_PAGE_H

#include <stddef.h>
#include <stdint.h>

enum {
    PAGE_PAPER_R = 250,
    PAGE_PAPER_G = 247,
    PAGE_PAPER_B = 242,
    PAGE_INK_R = 28,
    PAGE_INK_G = 27,
    PAGE_INK_B = 25,
    PAGE_HEAD_R = 150,
    PAGE_HEAD_G = 60,
    PAGE_HEAD_B = 20,
    PAGE_EM_R = 130,
    PAGE_EM_G = 40,
    PAGE_EM_B = 110,
    PAGE_CODE_R = 20,
    PAGE_CODE_G = 110,
    PAGE_CODE_B = 80,
    PAGE_LINK_R = 30,
    PAGE_LINK_G = 60,
    PAGE_LINK_B = 160,
    PAGE_SEL_R = 186,
    PAGE_SEL_G = 210,
    PAGE_SEL_B = 232,
    PAGE_LIST_R = 90,
    PAGE_LIST_G = 90,
    PAGE_LIST_B = 30,
    PAGE_QUOTE_R = 80,
    PAGE_QUOTE_G = 60,
    PAGE_QUOTE_B = 130,
    PAGE_BLOCK_R = 210,
    PAGE_BLOCK_G = 196,
    PAGE_BLOCK_B = 230,
    PAGE_STRIKE_R = 120,
    PAGE_STRIKE_G = 70,
    PAGE_STRIKE_B = 50,
    PAGE_RULE_R = 70,
    PAGE_RULE_G = 110,
    PAGE_RULE_B = 140,
    PAGE_NIGHT_PAPER_R = 32,
    PAGE_NIGHT_PAPER_G = 32,
    PAGE_NIGHT_PAPER_B = 36,
    PAGE_NIGHT_INK_R = 230,
    PAGE_NIGHT_INK_G = 226,
    PAGE_NIGHT_INK_B = 218
};

/* Day palette is 0. Night palette is 1. */
void page_set_theme(int theme);
void page_set_typewriter(int on);
void page_set_focus(int on);
void page_set_source(int on);
/* Edit mode is opened by a double tap. The caret alone does not open it.
   While it is on, the caret's line, or its whole fence or table, stays source. */
void page_set_editing(int on);
int page_editing(void);
/* True when both offsets sit in the same line, fence, or table. */
int page_same_unit(const char *text, size_t text_len, size_t a, size_t b);
int page_theme(void);
int page_typewriter(void);
int page_focus(void);
int page_source(void);

/* Counts from the last page_draw. A word is a run of ASCII letters, digits, or bytes at or above 128. */
int page_words(void);
int page_lines(void);

/* Byte offset of the next or previous heading. dir is 1 or -1. Returns from when there is none. */
size_t page_heading(const char *text, size_t len, size_t from, int dir);

/* ttf must stay alive for every later page_draw. Returns 0, or -1 if it is not a TrueType font. */
int page_set_font(const unsigned char *ttf, int len);

/* Inset of the text inside the content box. */
int page_margin(int font_px);

/* Fill px, tightly packed RGBA, top row first. text may be NULL when text_len is 0.
   The box is the visible content area in buffer pixels. A non-positive box uses the whole buffer.
   The caret is a byte offset into text and is kept inside the box.
   anchor is the other end of the selection. Equal bounds paint no selection. */
void page_draw(uint8_t *px, int width, int height, int box_x, int box_y, int box_w, int box_h,
               const char *text, size_t text_len, size_t caret, size_t anchor, int font_px);

/* Byte offset under a point in the same buffer space as page_draw. */
size_t page_hit(int box_x, int box_y, int box_w, int box_h, const char *text, size_t text_len, size_t caret,
                int font_px, int x, int y);

/* Follow keeps the caret in view. A scroll nudge turns follow off until it is set again. */
void page_set_follow(int on);
void page_scroll_by(int dy);

/* In typewriter mode, dy pixels become whole rows for the caret and the return is that row count.
   Otherwise the page scrolls by dy and the return is 0. */
int page_center_drag(int dy);

/* True when the point is close to the caret drawn by the last page_draw. */
int page_caret_near(int x, int y, int slop);

/* 0 while a press is still deciding. 1 scrolls. 2 selects.
   held is a press that stayed inside the slop long enough to select. */
int page_drag_kind(int past_slop, int held, int near_caret, int typewriter);

/* -1 when y is in the top zone, 1 in the bottom zone, otherwise 0. */
int page_edge(int y, int top, int bottom, int zone);

/* Draw short labels inside the given cells. A wide layout draws the hairline on the right.
   An empty label draws nothing. */
void page_draw_labels(uint8_t *px, int width, int height, int n, const int *xs, const int *ys, const int *ws,
                      const int *hs, const char *const *labels, int vertical, int font_px);

/* A menu mark: a paper disc and three bars. side is the cell. */
void page_draw_menu(uint8_t *px, int width, int height, int x, int y, int side);

#endif
