# Penship

A native note on a phone, painted from the markdown you type. This file is the living sample. When a mark starts working, it gets a section here, in this same document.

Penship
========

The caret line stays source, so the marks you are editing remain visible. Every other line is rendered. Turn on Source and the whole note shows its marks. Turn on Typewriter and that caret line sits in the vertical middle while the text slides under it.

## How to read this note

Drag the page to move. Tap to put the caret on a line. Drag from the caret to select. Ctrl+A selects everything, Ctrl+C copies, Ctrl+X cuts, and Ctrl+V pastes. Arrow keys are here for testing and for a keyboard: left and right step one character, up and down step one row, and Shift extends the selection.

*Italic words* sit in the same line as their stars. **Strong words** use two stars. A ~~struck phrase~~ carries a line through the words. `inline code` keeps its ticks on this line only. An image shows its words, as in ![a small ship](ship.png), and hides the address when you are not on that line.

A link looks like [the penship note](https://penship.app) and is painted as a link, not fetched.

---

## Lists

- A bullet replaces the hyphen when the caret is elsewhere.
- The next item stays a sibling.
- A nested item sits further right.
    - This one is indented by four spaces.
    - Its sibling stays at that indent.

1. Ordered items keep a number and a dot.
2. The marker becomes a list mark when the line is rendered.
3. The count is not renumbered for you.

- [x] Open a markdown file from the share sheet.
- [x] Paint headings, emphasis, code, links, lists, quotes, and fences.
- [x] Paint rules, tasks, setext headings, pipe tables, and images as their alt text.
- [x] Draw a line through struck words, and indent a nested list.
- [ ] Move between table cells with Tab, and add a row from the keyboard.
- [ ] Draw pictures and math, and export.

## A quotation

> The buffer stays markdown. Rendering is paint on the text, not a page in a browser.
>
> That is the whole idea of the editor. The file you save is the file you read.

## A rule, then a table

The line above this paragraph is a thematic break, three dashes on their own line. A dash line directly under text is a setext heading instead, which is why the title near the top is underlined with equals.

| Mark | Where it lives | Status |
| --- | :---: | ---: |
| Heading | a line of hashes, or setext | painted |
| Emphasis | one star or underscore pair | painted |
| Strong | two stars or underscores | painted |
| Strike | two tildes | painted, with a line |
| Code | one tick, or a fence of backticks or tildes | painted |
| Image | an exclamation and a link | alt text |
| Link | brackets then parentheses | painted |
| List | hyphen, plus, star, or a number | painted |
| Task | a box after the bullet | painted |
| Quote | a greater-than mark | painted |
| Rule | three dashes, stars, or underscores | painted |
| Table | a header, a delimiter, then rows | painted |
| Math | dollar fences | not yet |

Qty-style numbers sit on the right. A centered column uses colons on both sides of the dashes. A left column uses plain dashes.

## A fenced block

```
page_draw paints the lines that intersect the view.
The caret line of a fence stays source with the rest of the fence.
```

A tilde fence is the same kind of block.

~~~
opened with tildes
closed with tildes
~~~

Inside a paragraph, `page_draw` is inline code. The fence above is a block, and the wash behind it stays even when the ticks are hidden.

## What the caret does to a block

A fence is one unit. A table is one unit. Put the caret in either and every line of that block shows its source. Step outside and the ticks or the pipes disappear. A heading you are not on hides its hashes. A list you are not on hides its hyphen and draws a bullet or a box.

## Still ahead

An image still shows words rather than a picture. Math would wait for a real renderer. Export would write HTML from this same buffer. AsciiDoc, MyST, and Typst would be compilers beside this painter, not a second syntax invented here.

Until then, this note grows in place. The next feature appends a section rather than starting a new file.

## A longer stretch, for scrolling

The point of a long note is that the page can move without losing the caret.

One. The line index is rebuilt when the page is drawn. Only the lines in view are rasterized.

Two. Heights and scroll use wide integers, so a note of many lines does not wrap the scroll offset.

Three. A drag away from the caret scrolls. In Typewriter mode the drag moves rows instead, and the caret bar stays on the center line.

Four. A drag that starts on the caret selects. Shift and the arrow keys select too.

Five. Copy and paste go through the Android clipboard. A clip longer than a mebibyte is ignored.

Six. The type size steps from the Smaller and Larger controls. Font cycles the three Liberation faces. Theme switches the night paper. Focus dims every line except the one you are on.

Seven. The same controls sit in a column when the window is wide, and in rows of three when it is narrow.

Eight. Open with offers penship for text and for the markdown names it knows. A read-only open falls back to a private copy.

Nine. Saving still writes the whole file. That is fine for this sample and wrong for a huge one.

Ten. Come back to this section when the next mark exists, and add an eleven.
