#include "html.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail;

static void expect(int cond, const char *msg) {
    if (!cond) {
        fprintf(stderr, "fail: %s\n", msg);
        fail = 1;
    }
}

static void has(const char *html, const char *needle, const char *msg) {
    expect(html != NULL && strstr(html, needle) != NULL, msg);
}

int main(void) {
    char *html = NULL;
    size_t n = 0;
    const char *note = "# Hi\n\n- milk\n- [x] done\n\n> a < b & c\n\n```\nint x;\n```\n\n| A | B |\n| --- | --- |\n| 1 | 2 |\n\n[Go](http://x.test)\n";
    expect(html_export(note, strlen(note), &html, &n) == 0, "export");
    has(html, "<h1>Hi</h1>", "heading");
    has(html, "<ul>", "list");
    has(html, "checked", "task");
    has(html, "&lt;", "escaped less-than");
    has(html, "&amp;", "escaped ampersand");
    has(html, "<pre><code>", "fence");
    has(html, "<table>", "table");
    has(html, "<th>A</th>", "header cell");
    has(html, "<td>1</td>", "body cell");
    has(html, "href=\"http://x.test\"", "link");
    {
        char *pic = NULL;
        size_t pic_n = 0;
        const char *image = "![a \"ship\"](ship.png)\n";
        expect(html_export(image, strlen(image), &pic, &pic_n) == 0, "image export");
        has(pic, "<img alt=\"a &quot;ship&quot;\" src=\"ship.png\">", "an image keeps its alt text and path");
        free(pic);
    }
    {
        char *more = NULL;
        size_t more_n = 0;
        const char *marks =
            "Penship\n========\n\nFoo\n---\n\n---\n\n"
            "__Bold__ and _slanted_ and *star* and **two** and \\*stars\\* and a_b_c\n\n"
            "See [the note][sample] and [sample] and [missing][nope].\n\n"
            "[sample]: https://penship.app\n\n"
            "Write <https://penship.app> or <me@penship.app>.\n\n"
            "    a < b\n\n"
            "Between.\n\n"
            "    one\n\n"
            "    two\n";
        const char *hr;
        expect(html_export(marks, strlen(marks), &more, &more_n) == 0, "marks export");
        has(more, "<h1>Penship</h1>", "setext title");
        has(more, "<h2>Foo</h2>", "setext section");
        expect(more != NULL && strstr(more, "========") == NULL, "the underline is not copied");
        hr = more == NULL ? NULL : strstr(more, "<hr>");
        expect(hr != NULL && strstr(hr + 4, "<hr>") == NULL, "the underline is not a rule");
        has(more, "<strong>Bold</strong>", "underscore strong");
        has(more, "<em>slanted</em>", "underscore emphasis");
        has(more, "<em>star</em>", "star emphasis");
        has(more, "<strong>two</strong>", "star strong");
        has(more, "*stars*", "a slash keeps the stars");
        expect(more != NULL && strstr(more, "<em>b</em>") == NULL, "an underscore inside a word stays a word");
        expect(more != NULL && strstr(more, "<em>stars</em>") == NULL, "escaped stars are not emphasis");
        has(more, "href=\"https://penship.app\"", "reference address");
        has(more, ">the note</a>", "reference words");
        has(more, ">sample</a>", "shortcut reference");
        has(more, "[missing][nope]", "an unknown reference stays");
        expect(more != NULL && strstr(more, "[sample]") == NULL, "a known label is not left in brackets");
        has(more, "href=\"mailto:me@penship.app\"", "mail address");
        has(more, "<pre><code>a &lt; b\n</code></pre>", "indented code");
        has(more, "<pre><code>one\n\ntwo\n</code></pre>", "a blank line stays in the indented block");
        free(more);
    }
    expect(strstr(html, "---") == NULL, "the delimiter row is not a body row");
    free(html);
    if (fail) {
        return 1;
    }
    printf("html_test ok\n");
    return 0;
}
