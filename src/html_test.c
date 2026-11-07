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
    expect(strstr(html, "---") == NULL, "the delimiter row is not a body row");
    free(html);
    if (fail) {
        return 1;
    }
    printf("html_test ok\n");
    return 0;
}
