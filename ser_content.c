#include "ser_content.h"

static char continue_message[] =
    "HTTP/1.1 100 Continue\r\n"
    "\r\n";

static char error_404[] =
    "HTTP/1.1 404 Not Found\r\n"
    "\r\n";

static char reply_header[] = 
    "HTTP/1.1 100 Continue\r\n"
    "\r\n"
    "HTTP/1.1 200 OK\r\n"
    "Server: Brugger\r\n"
    "Connection: keep-alive\r\n";

static char page1_content[] = 
    "<!DOCTYPE html>\r\n"
    "<html>\r\n"
    "    <head>\r\n"
    "        <meta charset=\"utf-8\">\r\n"
    "        <title>Brugphobia</title>\r\n"
    "    </head>\r\n"
    "    <body>\r\n"
    "        <h1>This is epic</h1>\r\n"
    "        <p>Bruh</p>\r\n"
    "        <a href=\"test.html\">Click me</a>\r\n"
    "        <a href=\"video.html\">Video</a>\r\n"
    "        <a href=\"vids.html\">Video List</a>\r\n"
    "    </body>\r\n"
    "</html>\r\n";

static char page2_content[] = 
    "<!DOCTYPE html>\r\n"
    "<html>\r\n"
    "    <head>\r\n"
    "        <meta charset=\"utf-8\">\r\n"
    "        <title>Brugphobia</title>\r\n"
    "    </head>\r\n"
    "    <body>\r\n"
    "        <h1>This is yet more epicker</h1>\r\n"
    "        <div>\r\n"
    "            <img src=\"https://cdn.7tv.app/emote/60ccf4479f5edeff9938fa77/3x.webp\">\r\n"
    "        </div>\r\n"
    "        <a href=\"/\">Click me again.</a>\r\n"
    "    </body>\r\n"
    "</html>\r\n";

static char page3_content[] = 
    "<!DOCTYPE html>\r\n"
    "<html>\r\n"
    "    <head>\r\n"
    "        <meta charset=\"utf-8\">\r\n"
    "        <script type=\"text/javascript\" src=\"script.js\"></script>\r\n"
    "        <title>Brugphobia</title>\r\n"
    "    </head>\r\n"
    "    <body>\r\n"
    "        <h1>Le ebin video</h1>\r\n"
    "        <div>\r\n"
    "            <iframe id=\"lol\" width=\"1440\" height=\"810\"\r\n"
    "                    src=\"https://www.youtube.com/embed/3rwhiJXa5KQ\">\r\n"
    "            </iframe>\r\n"
    "        <div>\r\n"
    "        <input type=\"button\" value=\"FULL\" onclick=\"fullscreen();\" />\r\n"
    "        <a href=\"/\">&lt; Back</a>\r\n"
    "    </body>\r\n"
    "</html>\r\n";

// Evil
static char script_content[] =
    "const fullscreen = () => {\r\n"
    "    const el = document.getElementById(\"lol\");\r\n"
    "    const requestMethod = el.requestFullScreen\r\n"
    "                       || el.webkitRequestFullScreen\r\n"
    "                       || el.mozRequestFullScreen\r\n"
    "                       || el.msRequestFullscreen;\r\n"
    "    if (requestMethod) {\r\n"
    "        requestMethod.call(el);\r\n"
    "    } else if (typeof window.ActiveXObject !== \"undefined\") {\r\n"
    "        var wscript = new ActiveXObject(\"WScript.Shell\");\r\n"
    "        if (wscript !== null) {\r\n"
    "            wscript.SendKeys(\"{F11}\");\r\n"
    "        }\r\n"
    "    }\r\n"
    "    return false\r\n"
    "}\r\n";


static void generate_vids(arena_t *arena, channels_t *channels)
{
    arena_append_str(arena,
        "<!DOCTYPE html>\r\n"
        "<html>\r\n"
        "    <head>\r\n"
        "        <meta charset=\"utf-8\">\r\n"
        "        <title>VidiVici</title>\r\n"
        "        <link rel=\"stylesheet\" href=\"style.css\">\r\n"
        "    </head>\r\n"
        "    <body>\r\n"
        "        <div color=\"red\">\r\n"
        "            <a href=\"/\">&lt; Back</a>\r\n"
        "        </div>\r\n"
    );

    char *base = channels->text.data;

    for (int entry_id = 0; entry_id < channels->entry_count; ++entry_id) {
        entry_t entry = channels->entries[entry_id];

        arena_append_str(arena,
            "<div>\r\n"
            "    <div>\r\n"
            "        <img src=\""
        );

        arena_append_sv(arena, rsv_get(base, entry.data[EntryId_Thumbnail]));

        arena_append_str(arena,
            "\">\r\n"
            "    </div>\r\n"
            "    <div>\r\n"
            "        <h2>"
        );

        arena_append_sv(arena, rsv_get(base, entry.data[EntryId_Title]));

        arena_append_str(arena,
            "</h2>\r\n"
            "        <p>"
        );

        arena_append_sv(arena, rsv_get(base, entry.data[EntryId_Description]));

        arena_append_str(arena,
            "</p>\r\n"
            "    </div>\r\n"
            "</div>\r\n"
        );
    }

    arena_append_str(arena,
        "    </body>\r\n"
        "</html>\r\n"
    );
}

#ifdef BRUH
ser_content_so_t content = {
#else
ser_content_so_t object = {
#endif
    .continue_res  = { .begin = continue_message,
                       .end   = continue_message + sizeof(continue_message) - 1 },
    .error_404_res = { .begin = error_404,
                       .end   = error_404 + sizeof(error_404) - 1 },
    .header_res    = { .begin = reply_header,
                       .end   = reply_header + sizeof(reply_header) - 1 },
    .page1_res     = { .begin = page1_content,
                       .end   = page1_content + sizeof(page1_content) - 1 },
    .page2_res     = { .begin = page2_content,
                       .end   = page2_content + sizeof(page2_content) - 1 },
    .page3_res     = { .begin = page3_content,
                       .end   = page3_content + sizeof(page3_content) - 1 },
    .script_res    = { .begin = script_content,
                       .end   = script_content + sizeof(script_content) - 1 },

    .generate_vids = &generate_vids,
};
