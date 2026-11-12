# Penship

A native note on a phone, painted from the markdown you type. This file is the living sample. When a mark starts working, it gets a section here, in this same document, and the document stays long enough to scroll.

Penship
========

A double tap or a press-and-hold selects the word and offers Copy. Cut and Paste are on that menu only while you can edit. A long press where there is no word offers Paste at the caret, and Read only hides it. The ends of the highlight can be dragged. A single tap only moves the caret. In the ordinary mode the line with the caret is the editor, and every other line stays rendered. Move the caret and that one line changes with it. A code fence opens as a whole block, from the opening marks through the close. A table stays a grid: the caret sits in the cell, Tab steps across, and Enter moves down the column. Read only is the mode that paints the whole note and ignores typing. Source shows the marks of the whole note, and it turns Read only off. The format row is a strip of symbols along the bottom, and it hides in Read only. Back closes a menu, then the keyboard, and it stays in the note. A heading of one hash is taller than a heading of two. Settings also opens Typewriter, and with Typewriter on the caret line sits in the vertical middle while the text slides under it. Line paints a light band across the caret line, and the words stay readable through it.

## How to read this note

The corner counts words from the same pass that measures lines. Ctrl and up or down jumps to the next heading. Tap a task box to check it. Tab steps through a table, and Tab on the last cell adds a row. Enter on a list, a task, or a quote continues that marker. Enter on the empty marker leaves it. An ordered item continues as the next number, and a new task box starts open.

Drag the page to move. Tap to put the caret on a line. Drag from the caret to select. Ctrl+A selects everything, Ctrl+C copies, Ctrl+X cuts, and Ctrl+V pastes. Enter starts a line. Shift extends a selection when the keys that reach the editor are arrows. A key that arrives as Enter still starts a line, and the characters after the caret move down with it.

## Emphasis and strong

*Italic words* stay in the emphasis color. The stars hide when the caret is on another line, and they return when you step onto this one.

**Strong words** are heavier than italic. When a bold face has been downloaded, those words use it. Until then the strong word is stamped a second time, one pixel to the right, in the ink color. Two stars or two underscores both do this. The marks hide off the caret line, the same way the italic stars do.

A line can hold both, as in *a quiet word* beside **a heavier word**, and the colors stay distinct.

## Strike, code, and the ticks

A ~~struck phrase~~ carries a line through the words. The tildes hide when you are not on the line.

`inline code` hides its ticks when the caret is on another line. The word stays in the code color. Step onto the line and the ticks come back, because that line is source.

Inside a paragraph, `page_draw` is inline code. A fence, further down, is a block. The two are not the same mark.

## Pictures and links, as words

An image shows its words, as in ![a small ship](ship.png), and hides the address when you are not on that line. The picture itself is not drawn. The alt text is the stand-in, in the link color, until a real image paint exists.

A link shows its words, as in [the Penship note](https://penship.app), and hides the address when you are not on that line. It is not fetched. Brackets and the address return when the caret is on the line, so you can edit them. The first tap only moves the caret. A later tap on that same http or https link opens it.

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
- [x] Export HTML from this same buffer.
- [ ] Draw pictures and math.

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
The caret line is source. A fence opens with it, from the opener through the close.
The ticks stay visible for the whole block while you edit inside it.
```

A tilde fence is the same kind of block.

~~~
opened with tildes
closed with tildes
the closer has to match the opener
~~~

The wash behind a fence stays even when the ticks are hidden. While the caret is anywhere in the fence, the ticks stay visible from the opener through the close.

## What the caret does to a line

Only the line you are on is source, except a fence, which opens as a whole block, and a table, which stays a grid with the caret in one cell. Step off a line and its ticks or hashes disappear. A heading you are not on hides its hashes. A list you are not on hides its hyphen and draws a bullet, a box, or the number you typed. A code span you are not on hides its ticks. Read only hides the marks on the caret line too, and typing does nothing until you turn it off.

## Settings

A row of symbols sits at the bottom while you can edit: bold, italic, strike, code, link, heading, list, task, quote, rule, table, and block. Drag it sideways for the rest. Bold, italic, strike, and code wrap the selection, and a second tap takes the marks off. Head cycles a heading through three levels. List, task, and quote mark the line and come off again. Link, rule, and block insert that mark with the caret inside it. Table inserts a table. When the caret is already in one, a tap adds a column after that cell, and a hold removes the column. The last column stays. Read only hides the row.

A menu mark floats at the end of the page, under the status inset, so the keyboard does not cover it. It opens Open, Save, Save as, Export, Contents, Find, and Settings. They are not rows of buttons on the page. Save writes the open file. Save as asks the system for a new file and the note becomes that file. Contents lists the headings. Tap one and the caret goes there. Find takes what you type and selects the match.

Settings is its own screen. The format bar stays on the page. Settings opens this list:

- Smaller and Larger step the type size from 14 to 32 sp.
- Font cycles the three Liberation faces: sans, then serif, then mono.
- Font file asks for a TrueType file. If the file loads, it becomes the face and it is remembered. A file the painter cannot read leaves the current face alone. Font lists shows each file face first. A tap on one uses that file again. The rows under those are faces to fetch.
- Typewriter keeps the caret line in the vertical middle.
- Focus dims every line except the one you are on.
- Line paints a transparent band across the caret line. The letters stay visible.
- Glide sets how far a flick carries. It cycles off, short, medium, long, and far. Medium is the coast a browser uses on a long page.
- Theme switches the day paper and the night paper.
- Source shows every mark, on every line, and leaves Read only.
- Read only paints the whole note and ignores typing, backspace, and the format row.
- The menu mark closes the list and returns to the note. Back closes Contents, then the menu, then the keyboard, and it does not leave the note.

Ctrl+Shift+T, Ctrl+Shift+F, Ctrl+Shift+P, and Ctrl+Shift+S still flip Typewriter, Focus, Theme, and Source when those keys reach the editor.

## Indented code and references

Four spaces, and not a list marker, are a code block.

    printf("penship")

The spaces hide when the caret is on another line. The wash stays. A nested list keeps its marker, so four spaces and a hyphen are still a list.

A reference link uses a label instead of an address in the sentence, as in [the note][sample]. The label sits on its own line.

[sample]: https://penship.app

Off that line, the label and the colon hide and the address remains. An unknown label stays ordinary text.

## Find, escapes, and addresses

Find is in the menu. The keys you type go into the query, not the note, and the match is selected. Enter jumps to the next one, and the search wraps. ASCII case does not matter. Back closes Find and leaves the note where the match was.

A backslash before punctuation shows that character and hides the slash when the caret is on another line. \*stars\* are not emphasis. The slash comes back when you step onto the line.

An address in angle brackets is a link, as in <https://penship.app>, and so is <me@penship.app>. The brackets hide off the caret line. Nothing is fetched.

## Still ahead

An image still shows words rather than a picture. Math would wait for a real renderer. Export in the menu writes HTML from this same buffer. AsciiDoc, MyST, and Typst would be compilers beside this painter, not a second syntax invented here.

Until then, this note grows in place. The next feature appends a section rather than starting a new file.

## A longer stretch, for scrolling

The point of a long note is that the page can move without losing the caret. The sections above are the marks. The paragraphs below are the same note written out, so a drag has somewhere to go and a reader has something to read. Each one stays on the behavior it names. None of them is a placeholder.

One. The line index is built when the page is drawn, and it is kept while the text, the width, and the caret stay put. Only the lines that intersect the view are rasterized. A redraw of an unchanged note does not measure the lines again. That is why a long file can sit open without painting every paragraph on every frame. Scroll far enough and the lines that leave the top are simply not drawn. Come back and they are measured from the same bytes.

Two. Heights and scroll use wide integers, so a note of many lines does not wrap the offset back to the top. A short page never notices. This stretch is here so the addition is exercised for real: each paragraph adds its own rows, a wrapped line adds more than one, and a single-hash heading takes two body rows when it is rendered. The caret is kept in view when you type or move it. A tap does not jump the page to follow.

Three. A drag away from the caret scrolls. The finger has to move before the page decides. A small slip is still a tap, and a tap only places the caret. In Typewriter mode the same drag moves whole rows instead of the viewport, and the caret bar stays on the center line while the text slides under it. The last line can sit in the middle, with blank paper below it. That blank is the mode, not a missing paragraph.

Four. A drag that starts on the caret selects. Press and hold, then drag, selects from the word under the finger. Shift and an arrow select too, when the key arrives as an arrow. Once a selection exists, its ends can be tugged. Drag the start or the end and the highlight grows or shrinks along the line. Copy lives on the menu that appears over the selection. Cut and Paste are there only while you can edit. A long press on a gap, where no word is selected, offers Paste at the caret, and Read only does not. The plate is solid paper, so the words under it stay covered. Back dismisses the menu and leaves the selection's text alone.

Five. Copy and paste go through the Android clipboard. A clip longer than a mebibyte is ignored, so a huge paste cannot replace the note by accident. Cut copies first and deletes only when the copy succeeded. In Read only, Cut and Paste are not on the menu. The words can still be copied. Undo is refused there too, because undo is an edit.

Six. The menu is the file: Open, Save, Save as, Export, Contents, Find, and Settings. Settings is the other screen: Smaller, Larger, Font, Typewriter, Focus, Line, Theme, Source, and Read only. The labels are large enough to read on a phone, and each row is at least a thumb tall. If the keyboard would cover the bottom of the list, the list scrolls. Opening the menu hides the keyboard. The menu mark closes the list and returns to the note. Back closes Contents, then Settings, then the menu, then the keyboard, and stays in Penship.

Seven. That menu opens Open, Save, Save as, Export, and Contents, and the same options on a phone and on a wide window. Save writes the open file. If the note is still the private scratch file, or the open file cannot be written, Save asks the system for a place. Save as always asks, and the note becomes the file you pick. Export writes HTML from this buffer and does not replace the markdown you are editing. Contents lists the headings outside fences, up to sixty-four of them, and a tap moves the caret to that heading.

Eight. Open with offers Penship for text and for the markdown names it knows. A share can hand over a file or a piece of text. A read-only open falls back to a private copy, so a file the system will not let you write is not quietly discarded. The edits of that session live in the copy. The original stays where the other app left it.

Nine. Saving still writes the whole file. That is fine for this sample and wrong for a huge one. The bytes on disk are the bytes you typed. Nothing rewrites a heading, renumbers a list, or wraps a paragraph for you on the way out. A pause saves. A failed load does not replace the note that was already open, and it does not save an empty page over your file.

Ten. The word count sits on the Open row of the menu, so it is not painted across a sentence. A word is a run of letters, digits, or a non-ASCII byte. The hashes, stars, and pipes are not words. Heading jumps, task taps, and table Tab share work the page already did while it measured lines. Ctrl and up or down moves to the next heading. Tab on the last cell of a table adds a row. Shift and Tab moves back.

Eleven. Enter at the end of a list, a task, or a quote copies the marker onto the next line. Enter on that empty marker removes it and leaves a plain line. A number goes up by one, and it stops at nine digits. A checked box continues as an open box. A selection replaced by Enter does not continue a marker. A marker longer than the small limit stays a plain newline. The key has to arrive as a newline. An action key that swallows Enter is the bug this paragraph is here to catch.

Twelve. Strong text uses a bold face when Download faces has brought one in. Until then it is a second stamp of the regular face, one pixel to the right. Emphasis uses an italic face the same way, and keeps its own color. A line can hold both. The stars hide when the caret is on another line. Step onto the line and they return, because that line is source. Two underscores do the same work as two stars. Italic does not strip a bold wrap. Tapping italic on `**a heavier word**` nests the marks. It does not peel the strong ones off.

Thirteen. An ordered item keeps the digits you typed. A hyphen, a plus, or a star becomes a square when you are not on the line. A task becomes a box you can tap, checked or open. The tap has to land and stay. A drag is a scroll or a selection, not a toggle. Nested items sit further right, one indent at a time. The number you typed is not rewritten when the items around it change.

Fourteen. A link and an image hide the address until the caret is on that line. The words stay, in the link color. Nothing is fetched, and no picture is decoded. The alt text is the stand-in for the picture. Brackets and the address return on the caret line so you can edit them. The first tap on a link only moves the caret. A later tap on that same http or https address opens it. Any other address stays in the note.

Fifteen. Inline code hides its ticks off the caret line and keeps the code color. A fenced block opens as a whole, from the opener through the matching close, and the wash remains behind it. A tilde fence is the same kind of block as a backtick fence. The ticks stay visible while the caret is anywhere in that block.

Sixteen. The sample is this one file. A short sketch opened for a screenshot is not the sample. When the note looks small, the scratch file was seeded from an older copy, or a different file is open. This document is the one that should be long. New marks get a section here, in this same file, rather than a second sample that drifts out of date.

Seventeen. A tap places the caret and leaves the page where it is. Press and drag to move the page. A flick keeps sliding and then eases to a stop. A slow drag stops where you lift. A thin mark at the right of the column shows where you are, and dragging it moves the page. Press and hold, then drag, to select from that spot. A drag that starts on the caret selects too. A writer who wanted the caret on a word can tap without losing the paragraph they were reading.

Eighteen. Undo keeps thirty-two edits, and a stored deletion is capped. Ctrl+Z walks that stack back. Loading a file clears it. An insert, a backspace, a wrapped mark, and a pasted clip are edits. Moving the caret is not. Read only refuses the ones that would change bytes. The stack is not a history of the file on disk. It is the session.

Nineteen. Private note.md is the launcher document. Opening this sample does not replace it once it exists. A new install with an empty scratch file is seeded from this note. A failed open shows an empty page and does not save over your note. The launcher, after the process has been killed, reopens the last file rather than dumping it for a blank page. Home still leaves the app. Back does not.

Twenty. The caret line can carry a full-width band. It is a setting called Line, and it starts off. Turn it on and the row under the caret takes a light wash of the selection color, across the width of the page. The wash is painted before the letters, so the ink stays readable. It is not a second selection. It does not change what a tap does. Focus still dims the other lines. The band stays on the line you are editing.

Twenty-one. Read only is the other setting, and it is the one that takes the editor away. The whole note is rendered, including the caret line. The format row hides. Typing, backspace, delete, paste, cut, undo, and a task tap do nothing to the bytes. You can still move the caret, select, copy, and scroll. Source and Read only are not on together. Turning one on turns the other off. The Read only row is what brings the editor back.

Twenty-two. The ordinary mode is not a mode you enter. Open the note and the caret line is already source. Leave that line and it renders. Arrive on the next and the marks come back. There is no Done for editing. Back is for menus and for the keyboard. If the keyboard is already hidden and no menu is open, Back stays on the page. The activity is not finished. The note is not minimized by that key.

## A passage, so the page can move

A note that only lists its own features is still a list. The rest of this file is prose, so the scroll has weight and the caret has lines to land on. It is about keeping a record on a phone, in the same markdown this editor paints. Nothing here is fetched. Nothing here is a picture. The words are the note.

You open the app on a train and the last paragraph is where you left it. The caret is in the middle of a sentence you did not finish. The line is source, so you can see the star you typed before the doors closed. The lines above are rendered. A heading is just the words, taller if it has one hash. A list is a column of squares. You do not need the marks to read them. You need the marks to change them, and the change is the line under your thumb.

The first thing you fix is a word. You tap it. The caret lands in the letters and the keyboard is there. You delete two letters and type three. The rest of the paragraph stays where it was. The page does not jump to the top. That is the whole courtesy of a tap.

A stop later you are reading, not writing. Read only is for that. The format row goes away. The caret line renders with the others. A bump of the keyboard does not insert a letter into the sentence you were only trying to follow. You can still drag the page. You can still copy a sentence into another app. When the train is in the station and you want the line back, you turn Read only off. The marks return on the line you are on, and nowhere else.

Some paragraphs are for other people. You write them as quotes, one greater-than mark at the start, and the paint turns that into a bar. Enter continues the bar. Enter on the empty mark ends it. You do not want a quotation to run away down the page because you pressed the key once too often. The empty marker is the way out, and it is the same way out of a list and a task.

The list is the errand. Milk. A battery. The name of the street the shop is on. Ordered items keep the numbers you gave them, because the numbers might be a sequence you meant, not a sequence the editor invented. A task is different. The box is tappable. Checked stays checked until you tap it again. A new line after a checked box starts open, since the next errand is not done.

A table is for the two columns that do not belong in a list. Name, and whether you have it. The header is a heading color when you are not on that line. The grid is rules. Tab moves between the cells. On the last cell it adds a row, which is how the list grows without you typing the pipes by hand. The grid stays up while you edit. The caret sits in the cell you tapped. Enter moves to the same column on the next row, and on the last row it adds one. The table mark adds a column after that cell. Hold the mark and the column goes away. It will not remove the last one.

Code is for the command you will forget. One pair of ticks for a word in a sentence. A fence for the three lines you want to keep together. The wash marks the fence even when the ticks are hidden. The fence opens as one block, from the opener through the close, so the ticks stay while you change any line inside it.

A link shows its words, and the address waits on the caret line. The first tap puts the caret there. A later tap on that same http or https address opens it. An image is the same shape with an exclamation mark, and it is still words. The picture is later. The alt text is what this page can tell the truth about today.

Headings are how you get back. Contents lists them. Ctrl and down walks them when a keyboard with those keys is attached. A long note without headings is a scroll with no rungs. This passage has one heading, on purpose, so the stretch above can be left and found again. The title at the top of the file is a setext heading, a line of equals under the words, which is why it is not a row of hashes.

Typewriter is for the sentences you are drafting rather than the ones you are reviewing. The line stays in the middle. You drag and the rows move. Focus is the other loneliness: every line but the caret line dims. Line, the band, is quieter than either. It marks where you are without moving the page and without fading the paragraph you still want to see. Use it when the caret is hard to find in a long stretch like this one. Leave it off when the wash feels like a selection you did not make.

Night theme is the same note in other colors. The paper goes dark. The ink goes light. The marks keep their jobs. A theme is not a second document. Source is not a second document either. Source is every line showing its marks at once, which is the right way to fix a table that has gone crooked or a fence that was closed with the wrong character. Turn it off and you are back to one line. Read only and Source do not stack. You are reading, or you are looking at every mark, or you are editing the caret line. Those are the three.

Saving is dull, which is what you want from it. The file is the buffer. Save writes it. Save as picks a new name and the note becomes that file. There is no library of notes inside the app, no folder view, no second editor for a preview. The preview is the paint. The file another program opens is the markdown. Export is the exception that makes an HTML copy and leaves this buffer alone.

If this passage is long enough, the top of the file is off the screen while you read this sentence. Drag down to return. The caret stays unless you tap. Tap a heading in Contents if you would rather jump. The word count, when you open the menu, includes the words of this passage and the words of the sections above. It does not include the marks. It is a count of the note, not of the syntax.

What is still missing is stated once, up in Still ahead, and it can stay there. Pictures, math, and the other languages are not given a fake section here. This file grows when a mark starts working. Until then the page should be long enough to scroll, specific enough to test, and ordinary enough to read. That is the sample.
