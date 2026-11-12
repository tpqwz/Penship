#include "fontlist.h"

#include <stdio.h>
#include <string.h>

static int fail;

static void expect(int cond, const char *msg) {
    if (!cond) {
        fprintf(stderr, "fail: %s\n", msg);
        fail = 1;
    }
}

int main(void) {
    char names[8][FONT_NAME_MAX];
    const char *css = "/* penship-host: acimwb */"
                      "@font-face{font-family:Literata;src:url(/assets/a.woff2)}"
                      "@font-face{font-family:\"Source Sans 3\";src:url(b.woff2)}"
                      "@font-face{font-family:'Source Sans 3';}"
                      "body{font-family:Georgia,serif}"
                      "@font-face{/* font-family: Hidden */ font-family: Ledger;}"
                      "@font-face{font-family:serif;}"
                      "/* penship-host: tlnotes */"
                      "@font-face{font-family:Literata;}"
                      "@font-face{font-family:\"Monaspace Neon Var\";}";
    int n = fontlist_collect(css, strlen(css), names, 8);
    const char *cut = "@font-face{font-family:One;}@font-face{font-family:Two;}";
    expect(fontlist_collect(NULL, 0, names, 8) == -1, "null css");
    expect(fontlist_collect(css, strlen(css), NULL, 8) == -1, "null out");
    expect(n == 4, "four families");
    expect(n == 4 && strcmp(names[0], "Literata") == 0, "the family is the row");
    expect(n == 4 && strcmp(names[1], "Source Sans 3") == 0, "quoted family once");
    expect(n == 4 && strcmp(names[2], "Ledger") == 0, "comment inside the rule is not a family");
    expect(n == 4 && strcmp(names[3], "Monaspace Neon Var") == 0, "the next site adds only a new family");
    expect(strcmp(names[0], "Literata") == 0, "a family listed on a second site is not repeated");
    expect(fontlist_collect(css, strlen(css), names, 1) == 1, "cap keeps the first");
    expect(strcmp(names[0], "Literata") == 0, "the first name is the one kept");
    expect(fontlist_collect(cut, 28, names, 8) == 1, "bytes past len are not a family");
    expect(strcmp(names[0], "One") == 0, "the family inside len");
    expect(!fontlist_name_ok("serif"), "a generic is not a face");
    expect(!fontlist_name_ok("Bad/Name"), "a slash is not a face");
    expect(fontlist_name_ok("IBM Plex Mono"), "a spaced name is a face");
    {
        char face[FONT_NAME_MAX];
        expect(fontlist_face_name("dir/Libre Baskerville.ttf", face, sizeof face) == 1, "a file name becomes a face");
        expect(strcmp(face, "Libre Baskerville") == 0, "the suffix and path are gone");
        expect(fontlist_face_name("folder/Bad_Name.OTF", face, sizeof face) == 1, "a mark in the name becomes a space");
        expect(strcmp(face, "Bad Name") == 0, "the otf name is readable");
        expect(fontlist_face_name("serif.ttf", face, sizeof face) == 0, "a generic file is not a face");
        expect(face[0] == '\0', "a rejected file name is empty");
        expect(fontlist_face_name(".ttf", face, sizeof face) == 0, "a bare suffix is not a face");
    }
    if (fail) {
        return 1;
    }
    printf("fontlist_test ok\n");
    return 0;
}
