# Penship

A native note on a phone, painted from the markdown you type. This file is the living sample. When a mark starts working, it gets a section here, in this same document, and the document stays long enough to scroll.

Penship
========

A double tap opens that line, or the whole fence or table, so the marks stay visible while you edit it. A single tap only moves the caret, and the line stays rendered. Every other line is rendered. A heading of one hash is taller than a heading of two. Settings opens Source, and with Source on the whole note shows its marks. Settings also opens Typewriter, and with Typewriter on the caret line sits in the vertical middle while the text slides under it.

## How to read this note

The corner counts words from the same pass that measures lines. Ctrl and up or down jumps to the next heading. Tap a task box to check it. Tab steps through a table, and Tab on the last cell adds a row. Enter on a list, a task, or a quote continues that marker. Enter on the empty marker leaves it. An ordered item continues as the next number, and a new task box starts open.

Drag the page to move. Tap to put the caret on a line. Drag from the caret to select. Ctrl+A selects everything, Ctrl+C copies, Ctrl+X cuts, and Ctrl+V pastes. Enter starts a line. Shift extends a selection when the keys that reach the editor are arrows. A key that arrives as Enter still starts a line, and the characters after the caret move down with it.

## Emphasis and strong

*Italic words* stay in the emphasis color. The stars hide when the caret is on another line, and they return when you step onto this one.

**Strong words** are heavier than italic. The face on the phone is the regular cut, so the strong word is stamped a second time, one pixel to the right, in the ink color. Two stars or two underscores both do this. The marks hide off the caret line, the same way the italic stars do.

A line can hold both, as in *a quiet word* beside **a heavier word**, and the colors stay distinct.

## Strike, code, and the ticks

A ~~struck phrase~~ carries a line through the words. The tildes hide when you are not on the line.

`inline code` hides its ticks when the caret is on another line. The word stays in the code color. Step onto the line and the ticks come back, because that line is source.

Inside a paragraph, `page_draw` is inline code. A fence, further down, is a block. The two are not the same mark.

## Pictures and links, as words

An image shows its words, as in ![a small ship](ship.png), and hides the address when you are not on that line. The picture itself is not drawn. The alt text is the stand-in, in the link color, until a real image paint exists.

A link shows its words, as in [the penship note](https://penship.app), and hides the address when you are not on that line. It is not fetched. Brackets and the address return when the caret is on the line, so you can edit them.

---

## Lists

- A bullet replaces the hyphen when the caret is elsewhere.
- The next item stays a sibling.
- A nested item sits further right.
    - This one is indented by four spaces.
    - Its sibling stays at that indent.
        - A second indent sits further right again.

1. Ordered items keep their number when the line is rendered.
2. A bullet item still becomes a square.
3. The count is not renumbered for you.
4. Enter at the end of this item continues as the next number.
5. Enter on an empty number leaves the list.

- [x] Open a markdown file from the share sheet.
- [x] Paint headings, emphasis, code, links, lists, quotes, and fences.
- [x] Paint rules, tasks, setext headings, pipe tables, and images as their alt text.
- [x] Draw a line through struck words, and indent a nested list.
- [x] Move between table cells with Tab. Tab on the last cell adds a row.
- [x] Continue a list, task, or quote with Enter. Enter on the empty marker leaves it.
- [x] Keep an ordered number, and paint strong text heavier than emphasis.
- [x] Hide inline code ticks, link addresses, and image addresses off the caret line.
- [x] Put Smaller, Larger, Font, Typewriter, Focus, Theme, and Source in a Settings view.
- [ ] Draw pictures and math, and export.

## A quotation

> The buffer stays markdown. Rendering is paint on the text, not a page in a browser.
>
> That is the whole idea of the editor. The file you save is the file you read.
>
> A quote continues when you press Enter at the end of a quoted line. Enter on an empty quote mark leaves the quotation.

## A rule, then a table

The line above this paragraph is a thematic break, three dashes on their own line. A dash line directly under text is a setext heading instead, which is why the title near the top is underlined with equals.

| Mark | Where it lives | Status |
| --- | :---: | ---: |
| Heading | a line of hashes, or setext | painted |
| Emphasis | one star or underscore pair | the emphasis color |
| Strong | two stars or underscores | heavier ink |
| Strike | two tildes | a line through the words |
| Code | one tick, or a fence | ticks hide off the caret |
| Image | an exclamation and a link | alt text |
| Link | brackets then parentheses | the words |
| List | hyphen, plus, or star | a square |
| Number | digits, a dot, and a space | the number stays |
| Task | a box after the bullet | tap to check |
| Quote | a greater-than mark | a bar |
| Rule | three dashes, stars, or underscores | a bar |
| Table | a header, a delimiter, then rows | a grid |
| Math | dollar fences | not yet |

Qty-style numbers sit on the right. A centered column uses colons on both sides of the dashes. A left column uses plain dashes. Tab moves between cells. Tab on the last cell adds an empty row. Shift+Tab moves back.

## A fenced block

```
page_draw paints the lines that intersect the view.
A double tap inside a fence opens the whole fence. A single tap leaves it rendered.
A fence is one unit, not a line at a time.
```

A tilde fence is the same kind of block.

~~~
opened with tildes
closed with tildes
the closer has to match the opener
~~~

The wash behind a fence stays even when the ticks are hidden. Put the caret inside the fence and every line of it shows its source together.

## What the caret does to a block

A fence is one unit. A table is one unit. Put the caret in either and every line of that block shows its source. Step outside and the ticks or the pipes disappear. A heading you are not on hides its hashes. A list you are not on hides its hyphen and draws a bullet, a box, or the number you typed. A code span you are not on hides its ticks.

## Settings

A menu mark floats at the end of the page, under the status inset, so the keyboard does not cover it. It opens Open, Save, and the rest of the options. They are not rows of buttons on the page.

Settings opens this list:

- Smaller and Larger step the type size from 14 to 32 sp.
- Font cycles the three Liberation faces: sans, then serif, then mono.
- Typewriter keeps the caret line in the vertical middle.
- Focus dims every line except the one you are on.
- Theme switches the day paper and the night paper.
- Source shows every mark, on every line.
- Done returns to the page. Back does the same.

Ctrl+Shift+T, Ctrl+Shift+F, Ctrl+Shift+P, and Ctrl+Shift+S still flip Typewriter, Focus, Theme, and Source when those keys reach the editor.

## Still ahead

An image still shows words rather than a picture. Math would wait for a real renderer. Export would write HTML from this same buffer. AsciiDoc, MyST, and Typst would be compilers beside this painter, not a second syntax invented here.

Until then, this note grows in place. The next feature appends a section rather than starting a new file.

## A longer stretch, for scrolling

The point of a long note is that the page can move without losing the caret. The sections above are the marks. The lines below are here so the note is long enough to drag.

One. The line index is rebuilt when the page is drawn. Only the lines in view are rasterized.

Two. Heights and scroll use wide integers, so a note of many lines does not wrap the scroll offset.

Three. A drag away from the caret scrolls. In Typewriter mode the drag moves rows instead, and the caret bar stays on the center line.

Four. A drag that starts on the caret selects. Shift and an arrow select too, when the key arrives as an arrow.

Five. Copy and paste go through the Android clipboard. A clip longer than a mebibyte is ignored.

Six. Settings opens Smaller, Larger, Font, Typewriter, Focus, Theme, and Source. Done returns to the page.

Seven. That menu opens Open, Save, and the same options on a phone and on a wide window.

Eight. Open with offers penship for text and for the markdown names it knows. A read-only open falls back to a private copy.

Nine. Saving still writes the whole file. That is fine for this sample and wrong for a huge one.

Ten. The word count sits on the Settings page, so it is not painted across a line. Heading jumps, task taps, and table Tab share work the page already did.

Eleven. Enter at the end of a list, a task, or a quote copies the marker onto the next line. Enter on that empty marker removes it. A number goes up by one. A checked box continues as an open box.

Twelve. Strong text is a second stamp of the regular face. Emphasis keeps its own color. They are not the same paint.

Thirteen. An ordered item keeps the digits you typed. A hyphen, a plus, or a star becomes a square. A task becomes a box you can tap.

Fourteen. A link and an image hide the address until the caret is on that line. The words stay. Nothing is fetched, and no picture is decoded.

Fifteen. Inline code hides its ticks off the caret line and keeps the code color. A fenced block hides its opener when you are outside it, and the wash remains.

Sixteen. The sample is this one file. A short sketch opened for a screenshot is not the sample. When the note looks small, open showcase.md again.

Seventeen. A tap places the caret and leaves the page where it is. Press and drag to move the page. Press and hold, then drag, to select from that spot. A drag that starts on the caret selects too.

Eighteen. Undo keeps thirty-two edits, and the stored deletions are capped. Ctrl+Z walks that stack back. Loading a file clears it.

Nineteen. Private note.md is the launcher document. Opening this sample does not replace it. A failed open shows an empty page and does not save over your note.

Twenty. Come back to this section when the next mark exists, and add a twenty-one. The document should stay long.
