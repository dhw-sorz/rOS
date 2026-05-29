#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

#undef CTRL
#define CTRL(k)       ((k) & 0x1f)
#define ab_adds(a,s)  ab_add(a, s, strlen(s))
#define MAX_YANK  (1 << 16)
#define MAX_CMD   512
#define MAX_PATH  512
#define MAX_MSG   256
#define MAX_LANG  64
#define NTHEMES         14
#define MAX_COMPLETIONS 256
#define MAX_TABS        8
#define TAB_BAR_H       1

enum {
    KEY_UP = 0x1000, KEY_DOWN, KEY_LEFT, KEY_RIGHT,
    KEY_HOME, KEY_END, KEY_PGUP, KEY_PGDN, KEY_DEL,
    KEY_BS = 127,
    KEY_MOUSE_CLICK = 0x2000,
    KEY_MOUSE_SCROLL_UP,
    KEY_MOUSE_SCROLL_DOWN
};
enum {
    HL_NORMAL = 0, HL_COMMENT, HL_MLCOMMENT,
    HL_KEYWORD, HL_TYPE, HL_STRING, HL_NUMBER, HL_SEARCH
};

typedef enum { NORMAL, INSERT, COMMAND, SEARCH } Mode;
typedef struct { int r, c; } Pos;
typedef struct { Pos a, h; } Sel;

typedef struct {
    char *d;
    int len, cap;
    int hl_open;
} Row;

typedef struct {
    Row *row;
    int nrow;
    char path[MAX_PATH];
    int dirty;
    Sel sel;
    int sel_line;
    int off_r, off_c;
    Mode mode;
    char cmd[MAX_CMD];
    int cmd_len;
    char msg[MAX_MSG];
    int lang_idx;
    int pend_g, pend_r, pend_gt, pend_lt;
    char search[MAX_CMD];
    int search_len;
    int search_dir;
    int search_hl;
    int cmd_comp;
    char cmd_base[MAX_CMD];
    int is_new;
    char new_input[MAX_PATH];
    int new_input_len;
    int new_comp_idx;
} TabState;

typedef struct {
    char name[32];
    int tab;
    const char **kw;
    const char **types;
    const char *lc;
    const char *bc_open;
    const char *bc_close;
    const char *str_chars;
} Lang;

typedef struct {
    const char *name;
    const char *normal, *comment, *keyword, *type;
    const char *string, *number, *status, *tilde, *sel;
    const char *search;
    const char *curline;
    const char *status_i;
    const char *status_c;
} Theme;

#define PROMPT_PREFIX       "\xe2\x80\xba Open file: "
#define PROMPT_PREFIX_VCOLS 13

static const char *ASCII_ART[] = {
    "__________   __          ",
    "\\______   \\_/  |_______  ",
    " |     ___/\\   __\\____ \\ ",
    " |    |     |  | |  |_> >",
    " |____|     |__| |   __/ ",
    "                 |__|    ",
    NULL
};

static Theme themes[NTHEMES] = {
    { "default",
      "\033[m", "\033[90m", "\033[1;35m", "\033[36m",
      "\033[32m", "\033[33m", "\033[7m", "\033[34m", "\033[7m",
      "\033[1;7m", "\033[m", "\033[1;32;7m", "\033[1;33;7m" },
    { "vibrant",
      "\033[38;5;252;48;5;235m", "\033[38;5;102;48;5;235m",
      "\033[1;38;5;213;48;5;235m", "\033[38;5;81;48;5;235m",
      "\033[38;5;114;48;5;235m", "\033[38;5;215;48;5;235m",
      "\033[38;5;235;48;5;141m", "\033[38;5;240;48;5;235m",
      "\033[38;5;235;48;5;117m",
      "\033[38;5;235;48;5;228m", "\033[38;5;252;48;5;237m",
      "\033[38;5;235;48;5;114m", "\033[38;5;235;48;5;215m" },
    { "white-vibrant",
      "\033[38;5;235;48;5;231m", "\033[38;5;102;48;5;231m",
      "\033[1;38;5;161;48;5;231m", "\033[38;5;25;48;5;231m",
      "\033[38;5;28;48;5;231m", "\033[38;5;166;48;5;231m",
      "\033[38;5;231;48;5;25m", "\033[38;5;250;48;5;231m",
      "\033[38;5;231;48;5;32m",
      "\033[38;5;235;48;5;228m", "\033[38;5;235;48;5;229m",
      "\033[38;5;231;48;5;28m",  "\033[38;5;231;48;5;130m" },
    { "nordic",
      "\033[38;5;253;48;5;236m", "\033[38;5;103;48;5;236m",
      "\033[1;38;5;111;48;5;236m", "\033[38;5;150;48;5;236m",
      "\033[38;5;114;48;5;236m", "\033[38;5;179;48;5;236m",
      "\033[38;5;236;48;5;111m", "\033[38;5;240;48;5;236m",
      "\033[38;5;236;48;5;153m",
      "\033[38;5;236;48;5;179m", "\033[38;5;253;48;5;237m",
      "\033[38;5;236;48;5;114m", "\033[38;5;236;48;5;179m" },
    { "gruvbox",
      "\033[38;5;223;48;5;235m", "\033[38;5;102;48;5;235m",
      "\033[1;38;5;214;48;5;235m", "\033[38;5;108;48;5;235m",
      "\033[38;5;142;48;5;235m", "\033[38;5;175;48;5;235m",
      "\033[38;5;235;48;5;214m", "\033[38;5;241;48;5;235m",
      "\033[38;5;235;48;5;109m",
      "\033[38;5;235;48;5;208m", "\033[38;5;223;48;5;236m",
      "\033[38;5;235;48;5;142m", "\033[38;5;235;48;5;208m" },
    { "dracula",
      "\033[38;5;253;48;5;235m", "\033[38;5;103;48;5;235m",
      "\033[1;38;5;212;48;5;235m", "\033[38;5;117;48;5;235m",
      "\033[38;5;84;48;5;235m",  "\033[38;5;141;48;5;235m",
      "\033[38;5;235;48;5;212m", "\033[38;5;240;48;5;235m",
      "\033[38;5;235;48;5;117m",
      "\033[38;5;235;48;5;228m", "\033[38;5;253;48;5;236m",
      "\033[38;5;235;48;5;84m",  "\033[38;5;235;48;5;228m" },
    { "catppuccin",
      "\033[38;5;189;48;5;234m", "\033[38;5;103;48;5;234m",
      "\033[1;38;5;183;48;5;234m", "\033[38;5;153;48;5;234m",
      "\033[38;5;150;48;5;234m", "\033[38;5;216;48;5;234m",
      "\033[38;5;234;48;5;183m", "\033[38;5;238;48;5;234m",
      "\033[38;5;234;48;5;153m",
      "\033[38;5;234;48;5;222m", "\033[38;5;189;48;5;235m",
      "\033[38;5;234;48;5;150m", "\033[38;5;234;48;5;222m" },
    { "everforest",
      "\033[38;5;223;48;5;236m", "\033[38;5;101;48;5;236m",
      "\033[1;38;5;142;48;5;236m", "\033[38;5;108;48;5;236m",
      "\033[38;5;114;48;5;236m", "\033[38;5;179;48;5;236m",
      "\033[38;5;236;48;5;142m", "\033[38;5;240;48;5;236m",
      "\033[38;5;236;48;5;108m",
      "\033[38;5;236;48;5;179m", "\033[38;5;223;48;5;237m",
      "\033[38;5;236;48;5;114m", "\033[38;5;236;48;5;179m" },
    { "dragon",
      "\033[38;5;183;48;5;232m", "\033[38;5;61;48;5;232m",
      "\033[1;38;5;135;48;5;232m", "\033[38;5;171;48;5;232m",
      "\033[38;5;141;48;5;232m", "\033[38;5;147;48;5;232m",
      "\033[38;5;183;48;5;54m",  "\033[38;5;238;48;5;232m",
      "\033[38;5;232;48;5;93m",
      "\033[38;5;232;48;5;165m", "\033[38;5;183;48;5;233m",
      "\033[38;5;232;48;5;135m", "\033[38;5;232;48;5;171m" },
    { "tokyonight",
      "\033[38;5;189;48;5;235m", "\033[38;5;61;48;5;235m",
      "\033[1;38;5;141;48;5;235m", "\033[38;5;117;48;5;235m",
      "\033[38;5;149;48;5;235m", "\033[38;5;215;48;5;235m",
      "\033[38;5;189;48;5;61m",  "\033[38;5;237;48;5;235m",
      "\033[38;5;235;48;5;117m",
      "\033[38;5;235;48;5;228m", "\033[38;5;189;48;5;236m",
      "\033[38;5;235;48;5;149m", "\033[38;5;235;48;5;215m" },
    { "onedark",
      "\033[38;5;250;48;5;236m", "\033[38;5;59;48;5;236m",
      "\033[1;38;5;176;48;5;236m", "\033[38;5;222;48;5;236m",
      "\033[38;5;114;48;5;236m", "\033[38;5;180;48;5;236m",
      "\033[38;5;250;48;5;60m",  "\033[38;5;240;48;5;236m",
      "\033[38;5;236;48;5;110m",
      "\033[38;5;236;48;5;222m", "\033[38;5;250;48;5;237m",
      "\033[38;5;236;48;5;114m", "\033[38;5;236;48;5;180m" },
    { "monokai",
      "\033[38;5;255;48;5;235m", "\033[38;5;101;48;5;235m",
      "\033[1;38;5;197;48;5;235m", "\033[38;5;117;48;5;235m",
      "\033[38;5;228;48;5;235m", "\033[38;5;141;48;5;235m",
      "\033[38;5;235;48;5;197m", "\033[38;5;240;48;5;235m",
      "\033[38;5;235;48;5;117m",
      "\033[38;5;235;48;5;228m", "\033[38;5;255;48;5;237m",
      "\033[38;5;235;48;5;148m", "\033[38;5;235;48;5;228m" },
    { "rosepine",
      "\033[38;5;189;48;5;234m", "\033[38;5;60;48;5;234m",
      "\033[1;38;5;183;48;5;234m", "\033[38;5;116;48;5;234m",
      "\033[38;5;222;48;5;234m", "\033[38;5;217;48;5;234m",
      "\033[38;5;234;48;5;183m", "\033[38;5;238;48;5;234m",
      "\033[38;5;234;48;5;116m",
      "\033[38;5;234;48;5;222m", "\033[38;5;189;48;5;235m",
      "\033[38;5;234;48;5;183m", "\033[38;5;234;48;5;222m" },
    { "solarized",
      "\033[38;5;102;48;5;234m", "\033[38;5;66;48;5;234m",
      "\033[1;38;5;33;48;5;234m", "\033[38;5;136;48;5;234m",
      "\033[38;5;36;48;5;234m",  "\033[38;5;162;48;5;234m",
      "\033[38;5;234;48;5;33m",  "\033[38;5;238;48;5;234m",
      "\033[38;5;234;48;5;136m",
      "\033[38;5;234;48;5;166m", "\033[38;5;102;48;5;235m",
      "\033[38;5;234;48;5;36m",  "\033[38;5;234;48;5;33m" },
};

static int find_theme(const char *name)
{
    for (int i = 0; i < NTHEMES; i++)
        if (strcmp(themes[i].name, name) == 0) return i;
    return -1;
}

static struct {
    struct termios orig;
    int rows, cols;
    Row *row;
    int nrow;
    char path[MAX_PATH];
    int dirty;
    Sel sel;
    int sel_line;
    int off_r, off_c;
    Mode mode;
    char cmd[MAX_CMD];
    int cmd_len;
    char msg[MAX_MSG];
    int quit;
    char yank[MAX_YANK];
    int yank_line;
    Lang langs[MAX_LANG];
    int nlang;
    int lang_idx;
    int pend_g;
    int theme_idx;
    int cmd_comp;
    char cmd_base[MAX_CMD];
    char search[MAX_CMD];
    int  search_len;
    int  search_dir;
    int  search_hl;
    int  pend_r;
    int  pend_gt;
    int  pend_lt;
    int  lnum_w;
} E;

static TabState g_tabs[MAX_TABS];
static int g_ntabs = 1;
static int g_cur_tab = 0;

static int mouse_x = 0, mouse_y = 0;
static int g_pending_tab_switch = -1;
static int g_tab_col_start[MAX_TABS];
static int g_tab_col_end[MAX_TABS];
static int g_tab_close_col[MAX_TABS];
static int g_plus_col = 0;

static const char *kw_c[] = {
    "if","else","for","while","do","switch","case","break","continue","return",
    "goto","default","struct","union","enum","typedef","static","extern","inline",
    "volatile","const","sizeof","typeof","NULL","true","false","nullptr",
    "#include","#define","#ifdef","#ifndef","#endif","#else","#elif","#pragma",
    "#undef","#if","#error","#warning",NULL
};
static const char *ty_c[] = {
    "int","char","float","double","long","short","unsigned","signed","void","auto",
    "register","size_t","ssize_t","ptrdiff_t","bool","_Bool","wchar_t","FILE",
    "uint8_t","uint16_t","uint32_t","uint64_t","int8_t","int16_t","int32_t","int64_t",
    "intptr_t","uintptr_t","off_t","pid_t","uid_t","gid_t",NULL
};
static const char *kw_cpp[] = {
    "if","else","for","while","do","switch","case","break","continue","return",
    "goto","default","struct","union","enum","class","typedef","namespace","using",
    "template","typename","static","extern","inline","virtual","override","final",
    "explicit","volatile","const","constexpr","consteval","constinit","sizeof",
    "typeof","typeid","decltype","public","private","protected","friend","operator",
    "new","delete","this","nullptr","true","false","try","catch","throw","noexcept",
    "static_cast","dynamic_cast","reinterpret_cast","const_cast","co_await",
    "co_return","co_yield","#include","#define","#ifdef","#ifndef","#endif",NULL
};
static const char *ty_cpp[] = {
    "int","char","float","double","long","short","unsigned","signed","void","auto",
    "bool","size_t","ptrdiff_t","wchar_t","char8_t","char16_t","char32_t",
    "uint8_t","uint16_t","uint32_t","uint64_t","int8_t","int16_t","int32_t","int64_t",
    "string","vector","map","unordered_map","set","unordered_set","list","deque",
    "array","unique_ptr","shared_ptr","weak_ptr","optional","variant","tuple",
    "pair","span","string_view","any","function",NULL
};
static const char *kw_rust[] = {
    "as","async","await","break","const","continue","crate","dyn","else","enum",
    "extern","false","fn","for","if","impl","in","let","loop","match","mod","move",
    "mut","pub","ref","return","self","Self","static","struct","super","trait",
    "true","type","union","unsafe","use","where","while","yield","box",NULL
};
static const char *ty_rust[] = {
    "i8","i16","i32","i64","i128","isize","u8","u16","u32","u64","u128","usize",
    "f32","f64","bool","char","str","String","Vec","Option","Result","Box","Rc",
    "Arc","Cell","RefCell","Mutex","RwLock","HashMap","HashSet","BTreeMap","BTreeSet",
    "VecDeque","LinkedList","BinaryHeap","Cow","ToOwned","Clone","Copy","Send",
    "Sync","Sized","Default","Debug","Display","From","Into","Iterator",NULL
};
static const char *kw_py[] = {
    "and","as","assert","async","await","break","class","continue","def","del",
    "elif","else","except","False","finally","for","from","global","if","import",
    "in","is","lambda","None","nonlocal","not","or","pass","raise","return",
    "True","try","while","with","yield",NULL
};
static const char *ty_py[] = {
    "int","float","str","bool","list","dict","set","tuple","bytes","bytearray",
    "type","object","super","property","staticmethod","classmethod","print","len",
    "range","enumerate","zip","map","filter","sorted","reversed","open",
    "isinstance","issubclass","hasattr","getattr","setattr","abs","max","min",
    "sum","any","all","round","repr","format","input","id","hash","iter","next",NULL
};
static const char *kw_js[] = {
    "break","case","catch","class","const","continue","debugger","default","delete",
    "do","else","export","extends","false","finally","for","from","function","if",
    "import","in","instanceof","let","new","null","of","return","static","super",
    "switch","this","throw","true","try","typeof","undefined","var","void","while",
    "with","yield","async","await","get","set",NULL
};
static const char *ty_js[] = {
    "Array","Object","String","Number","Boolean","Symbol","BigInt","Map","Set",
    "WeakMap","WeakSet","Promise","Proxy","Reflect","Error","TypeError","RangeError",
    "SyntaxError","RegExp","Date","Math","JSON","console","window","document",
    "navigator","location","fetch","URL","URLSearchParams","FormData","Headers",NULL
};
static const char *kw_go[] = {
    "break","case","chan","const","continue","default","defer","else","fallthrough",
    "for","func","go","goto","if","import","interface","map","package","range",
    "return","select","struct","switch","type","var","true","false","nil","iota",NULL
};
static const char *ty_go[] = {
    "bool","byte","complex64","complex128","error","float32","float64","int","int8",
    "int16","int32","int64","rune","string","uint","uint8","uint16","uint32","uint64",
    "uintptr","any","comparable","make","new","len","cap","append","copy","delete",
    "close","panic","recover","print","println",NULL
};
static const char *kw_java[] = {
    "abstract","assert","break","case","catch","class","const","continue","default",
    "do","else","enum","extends","final","finally","for","goto","if","implements",
    "import","instanceof","interface","native","new","package","private","protected",
    "public","return","static","strictfp","super","switch","synchronized","this",
    "throw","throws","transient","try","volatile","while","true","false","null",
    "var","record","sealed","permits","yield",NULL
};
static const char *ty_java[] = {
    "boolean","byte","char","double","float","int","long","short","void","String",
    "Object","Integer","Long","Double","Float","Boolean","Character","Byte","Short",
    "Number","Math","System","Thread","Runnable","Exception","RuntimeException",
    "List","ArrayList","Map","HashMap","Set","HashSet","Optional",NULL
};
static const char *kw_lua[] = {
    "and","break","do","else","elseif","end","false","for","function","goto","if",
    "in","local","nil","not","or","repeat","return","then","true","until","while",NULL
};
static const char *ty_lua[] = {
    "print","tostring","tonumber","type","pairs","ipairs","next","select","rawget",
    "rawset","rawequal","rawlen","setmetatable","getmetatable","require","load",
    "loadfile","dofile","pcall","xpcall","error","assert","unpack","table","string",
    "math","io","os","coroutine","package",NULL
};
static const char *kw_bash[] = {
    "if","then","else","elif","fi","for","while","do","done","case","esac","in",
    "function","return","break","continue","exit","local","declare","export",
    "readonly","unset","shift","eval","exec","source","alias","true","false",NULL
};
static const char *ty_bash[] = {
    "echo","printf","read","test","cd","ls","cp","mv","rm","mkdir","rmdir","touch",
    "cat","grep","sed","awk","find","xargs","sort","uniq","wc","head","tail",
    "curl","wget","git","ssh","chmod","chown","kill","ps","pwd","env","which",NULL
};
static const char *kw_zig[] = {
    "addrspace","align","allowzero","and","anyframe","anytype","asm","async","await",
    "break","callconv","catch","comptime","const","continue","defer","else","enum",
    "errdefer","error","export","extern","fn","for","if","inline","noalias","noinline",
    "nosuspend","opaque","or","orelse","packed","pub","resume","return","struct",
    "suspend","switch","test","threadlocal","try","union","unreachable","var",
    "volatile","while","true","false","null","undefined",NULL
};
static const char *ty_zig[] = {
    "bool","f16","f32","f64","f80","f128","i8","i16","i32","i64","i128","u8","u16",
    "u32","u64","u128","isize","usize","void","noreturn","type","anyerror",
    "comptime_int","comptime_float","anyopaque",NULL
};

static const char *hl_color(int hl)
{
    Theme *t = &themes[E.theme_idx];
    switch (hl) {
    case HL_COMMENT:
    case HL_MLCOMMENT: return t->comment;
    case HL_KEYWORD:   return t->keyword;
    case HL_TYPE:      return t->type;
    case HL_STRING:    return t->string;
    case HL_NUMBER:    return t->number;
    case HL_SEARCH:    return t->search;
    default:           return t->normal;
    }
}

static void die(const char *s)
{
    write(STDOUT_FILENO, "\033[2J\033[H", 7);
    perror(s);
    exit(1);
}

static void raw_off(void)
{
    write(STDOUT_FILENO, "\033[?1000l\033[?1006l", 16);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig);
    write(STDOUT_FILENO, "\033[?1049l\033[H", 11);
}

static void raw_on(void)
{
    if (tcgetattr(STDIN_FILENO, &E.orig) < 0)
        die("tcgetattr");
    atexit(raw_off);
    write(STDOUT_FILENO, "\033[?1049h\033[H", 11);
    struct termios t = E.orig;
    t.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    t.c_oflag &= ~OPOST;
    t.c_cflag |= CS8;
    t.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    t.c_cc[VMIN] = 0;
    t.c_cc[VTIME] = 1;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &t) < 0)
        die("tcsetattr");
    write(STDOUT_FILENO, "\033[?1000h\033[?1006h", 16);
}

static void win_size(void)
{
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) < 0 || !ws.ws_col) {
        E.rows = 24; E.cols = 80;
    } else {
        E.rows = ws.ws_row; E.cols = ws.ws_col;
    }
}

static volatile sig_atomic_t need_resize = 0;
static void on_resize(int sig) { (void)sig; need_resize = 1; }

static unsigned char mb_pending[4];
static int mb_pending_n = 0, mb_pending_i = 0;

static int read_key(void)
{
    if (mb_pending_i < mb_pending_n)
        return mb_pending[mb_pending_i++];
    mb_pending_n = mb_pending_i = 0;

    char c;
    int n;
    while ((n = read(STDIN_FILENO, &c, 1)) != 1) {
        if (n < 0 && errno != EAGAIN && errno != EINTR) die("read");
        if (need_resize) return 0;
    }
    unsigned char uc = (unsigned char)c;
    if (uc >= 0xC0) {
        int extra = (uc >= 0xF0) ? 3 : (uc >= 0xE0) ? 2 : 1;
        for (int i = 0; i < extra; i++) {
            char cc;
            if (read(STDIN_FILENO, &cc, 1) != 1) break;
            if (((unsigned char)cc & 0xC0) != 0x80) break;
            mb_pending[mb_pending_n++] = (unsigned char)cc;
        }
    }
    if (c != '\033') return uc;
    char s[4] = {0};
    if (read(STDIN_FILENO, &s[0], 1) != 1) return '\033';
    if (read(STDIN_FILENO, &s[1], 1) != 1) return '\033';
    if (s[0] == '[') {
        if (s[1] == '<') {
            char mbuf[32];
            int mi = 0;
            char mc;
            while (mi < 31) {
                if (read(STDIN_FILENO, &mc, 1) != 1) break;
                mbuf[mi++] = mc;
                if (mc == 'M' || mc == 'm') break;
            }
            if (mi > 0) {
                mbuf[mi] = 0;
                int btn = 0, mx = 0, my = 0;
                if (sscanf(mbuf, "%d;%d;%d", &btn, &mx, &my) == 3
                        && mbuf[mi-1] == 'M') {
                    mouse_x = mx; mouse_y = my;
                    if ((btn & 0x43) == 0) return KEY_MOUSE_CLICK;
                    if (btn == 64) return KEY_MOUSE_SCROLL_UP;
                    if (btn == 65) return KEY_MOUSE_SCROLL_DOWN;
                }
            }
            return 0;
        }
        if (s[1] == 'M') {
            unsigned char b = 0, bx = 0, by = 0;
            if (read(STDIN_FILENO, (char *)&b,  1) == 1 &&
                read(STDIN_FILENO, (char *)&bx, 1) == 1 &&
                read(STDIN_FILENO, (char *)&by, 1) == 1 &&
                bx > 0x20 && by > 0x20) {
                mouse_x = bx - 0x20;
                mouse_y = by - 0x20;
                unsigned char bb = b - 0x20;
                if ((bb & 0x43) == 0) return KEY_MOUSE_CLICK;
                if (bb == 64) return KEY_MOUSE_SCROLL_UP;
                if (bb == 65) return KEY_MOUSE_SCROLL_DOWN;
            }
            return 0;
        }
        if (s[1] >= '0' && s[1] <= '9') {
            char x;
            if (read(STDIN_FILENO, &x, 1) != 1) return '\033';
            if (x == '~') switch (s[1]) {
                case '3': return KEY_DEL;
                case '5': return KEY_PGUP;
                case '6': return KEY_PGDN;
                case '1': case '7': return KEY_HOME;
                case '4': case '8': return KEY_END;
            }
        } else switch (s[1]) {
            case 'A': return KEY_UP;
            case 'B': return KEY_DOWN;
            case 'C': return KEY_RIGHT;
            case 'D': return KEY_LEFT;
            case 'H': return KEY_HOME;
            case 'F': return KEY_END;
        }
    } else if (s[0] == 'O') switch (s[1]) {
        case 'H': return KEY_HOME;
        case 'F': return KEY_END;
    }
    return '\033';
}

typedef struct { char *b; int len; } Ab;
#define AB_INIT { NULL, 0 }

static void ab_add(Ab *a, const char *s, int n)
{
    char *p = realloc(a->b, a->len + n);
    if (!p) return;
    memcpy(p + a->len, s, n);
    a->b = p;
    a->len += n;
}

static void ab_addf(Ab *a, const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n > 0) ab_add(a, buf, n);
}

static void ab_free(Ab *a) { free(a->b); a->b = NULL; a->len = 0; }

static void row_grow(Row *r, int need)
{
    if (r->cap >= need) return;
    r->cap = need < 16 ? 16 : need * 2;
    r->d = realloc(r->d, r->cap);
}

static void row_ins_char(Row *r, int at, char c)
{
    row_grow(r, r->len + 2);
    if (at > r->len) at = r->len;
    if (at < 0) at = 0;
    memmove(r->d + at + 1, r->d + at, r->len - at);
    r->d[at] = c;
    r->d[++r->len] = 0;
}

static void row_del_char(Row *r, int at)
{
    if (at < 0 || at >= r->len) return;
    memmove(r->d + at, r->d + at + 1, r->len - at);
    r->d[--r->len] = 0;
}

static void row_append_str(Row *r, const char *s, int n)
{
    if (n <= 0) return;
    row_grow(r, r->len + n + 1);
    memcpy(r->d + r->len, s, n);
    r->len += n;
    r->d[r->len] = 0;
}

static void buf_ins_row(int at, const char *s, int n)
{
    E.row = realloc(E.row, (E.nrow + 1) * sizeof(Row));
    memmove(E.row + at + 1, E.row + at, (E.nrow - at) * sizeof(Row));
    E.row[at] = (Row){NULL, 0, 0, 0};
    if (n > 0) row_append_str(&E.row[at], s, n);
    else { row_grow(&E.row[at], 1); E.row[at].d[0] = 0; }
    E.nrow++;
    E.dirty = 1;
}

static void buf_del_row(int at)
{
    if (at < 0 || at >= E.nrow) return;
    free(E.row[at].d);
    memmove(E.row + at, E.row + at + 1, (E.nrow - at - 1) * sizeof(Row));
    E.nrow--;
    E.dirty = 1;
}

static void buf_open(const char *path)
{
    strncpy(E.path, path, MAX_PATH - 1);
    FILE *f = fopen(path, "r");
    if (!f) {
        if (errno != ENOENT)
            snprintf(E.msg, MAX_MSG, "Cannot open: %s", strerror(errno));
        return;
    }
    char *line = NULL;
    size_t lsz = 0;
    ssize_t ln;
    while ((ln = getline(&line, &lsz, f)) != -1) {
        while (ln > 0 && (line[ln-1] == '\n' || line[ln-1] == '\r')) ln--;
        buf_ins_row(E.nrow, line, (int)ln);
    }
    free(line);
    fclose(f);
    E.dirty = 0;
}

static int buf_save(const char *path)
{
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    for (int i = 0; i < E.nrow; i++) {
        if (E.row[i].d) fwrite(E.row[i].d, 1, E.row[i].len, f);
        fputc('\n', f);
    }
    fclose(f);
    E.dirty = 0;
    return 0;
}

static void init_langs(void)
{
    static const struct {
        const char *nm; int tab;
        const char **kw, **ty;
        const char *lc, *bco, *bcc, *sc;
    } T[] = {
        {"c",          4, kw_c,    ty_c,    "//", "/*",    "*/",   "\"'"},
        {"cpp",        4, kw_cpp,  ty_cpp,  "//", "/*",    "*/",   "\"'"},
        {"rust",       4, kw_rust, ty_rust, "//", "/*",    "*/",   "\""},
        {"python",     4, kw_py,   ty_py,   "#",  NULL,    NULL,   "\"'"},
        {"javascript", 2, kw_js,   ty_js,   "//", "/*",    "*/",   "\"'`"},
        {"typescript", 2, kw_js,   ty_js,   "//", "/*",    "*/",   "\"'`"},
        {"go",         4, kw_go,   ty_go,   "//", "/*",    "*/",   "\"'`"},
        {"java",       4, kw_java, ty_java, "//", "/*",    "*/",   "\"'"},
        {"lua",        2, kw_lua,  ty_lua,  "--", "--[[",  "]]",   "\"'"},
        {"zig",        4, kw_zig,  ty_zig,  "//", NULL,    NULL,   "\""},
        {"html",       2, NULL,    NULL,    NULL, "<!--",  "-->",  "\"'"},
        {"css",        2, NULL,    NULL,    NULL, "/*",    "*/",   "\"'"},
        {"json",       2, NULL,    NULL,    NULL, NULL,    NULL,   "\""},
        {"yaml",       2, NULL,    NULL,    "#",  NULL,    NULL,   "\"'"},
        {"toml",       2, NULL,    NULL,    "#",  NULL,    NULL,   "\"'"},
        {"markdown",   2, NULL,    NULL,    NULL, NULL,    NULL,   "\"'"},
        {"bash",       2, kw_bash, ty_bash, "#",  NULL,    NULL,   "\"'"},
        {"haskell",    2, NULL,    NULL,    "--", "{-",    "-}",   "\"'"},
        {"ocaml",      2, NULL,    NULL,    NULL, "(*",    "*)",   "\"'"},
        {"ruby",       2, NULL,    NULL,    "#",  "=begin","=end", "\"'"},
        {"php",        4, NULL,    NULL,    "//", "/*",    "*/",   "\"'"},
        {"swift",      4, NULL,    NULL,    "//", "/*",    "*/",   "\""},
        {"kotlin",     4, NULL,    NULL,    "//", "/*",    "*/",   "\"'"},
        {"scala",      2, NULL,    NULL,    "//", "/*",    "*/",   "\"'"},
        {"elixir",     2, NULL,    NULL,    "#",  NULL,    NULL,   "\"'"},
        {"nim",        2, NULL,    NULL,    "#",  NULL,    NULL,   "\"'"},
        {"v",          4, NULL,    NULL,    "//", "/*",    "*/",   "\"'"},
        {"odin",       4, NULL,    NULL,    "//", "/*",    "*/",   "\"'"},
        {"d",          4, NULL,    NULL,    "//", "/*",    "*/",   "\"'"},
        {"ada",        3, NULL,    NULL,    "--", NULL,    NULL,   "\"'"},
        {"asm",        8, NULL,    NULL,    ";",  NULL,    NULL,   "\"'"},
        {"r",          2, NULL,    NULL,    "#",  NULL,    NULL,   "\"'"},
        {"julia",      4, NULL,    NULL,    "#",  "#=",    "=#",   "\"'"},
        {"dart",       2, NULL,    NULL,    "//", "/*",    "*/",   "\"'"},
        {"clojure",    2, NULL,    NULL,    ";",  NULL,    NULL,   "\""},
        {"erlang",     4, NULL,    NULL,    "%",  NULL,    NULL,   "\"'"},
        {"fsharp",     4, NULL,    NULL,    "//", "(*",    "*)",   "\"'"},
        {"groovy",     4, NULL,    NULL,    "//", "/*",    "*/",   "\"'"},
        {"perl",       4, NULL,    NULL,    "#",  NULL,    NULL,   "\"'"},
        {"powershell", 4, NULL,    NULL,    "#",  "<#",    "#>",   "\"'"},
        {"tcl",        4, NULL,    NULL,    "#",  NULL,    NULL,   "\"'"},
        {"fortran",    2, NULL,    NULL,    "!",  NULL,    NULL,   "\"'"},
        {"cobol",      4, NULL,    NULL,    NULL, NULL,    NULL,   "\"'"},
        {"prolog",     2, NULL,    NULL,    "%",  "/*",    "*/",   "\"'"},
        {"scheme",     2, NULL,    NULL,    ";",  "#|",    "|#",   "\""},
        {NULL}
    };
    for (int i = 0; T[i].nm && E.nlang < MAX_LANG; i++) {
        Lang *l = &E.langs[E.nlang++];
        strncpy(l->name, T[i].nm, 31);
        l->tab = T[i].tab; l->kw = T[i].kw; l->types = T[i].ty;
        l->lc = T[i].lc; l->bc_open = T[i].bco; l->bc_close = T[i].bcc;
        l->str_chars = T[i].sc;
    }
}

static int find_lang(const char *name)
{
    for (int i = 0; i < E.nlang; i++)
        if (strcasecmp(E.langs[i].name, name) == 0) return i;
    return -1;
}

static void detect_lang(const char *path)
{
    const char *ext = strrchr(path, '.');
    if (!ext++) return;
    static const char *map[][2] = {
        {"c","c"},{"h","c"},{"cpp","cpp"},{"cc","cpp"},{"cxx","cpp"},{"hpp","cpp"},
        {"rs","rust"},{"py","python"},{"pyw","python"},
        {"js","javascript"},{"mjs","javascript"},{"jsx","javascript"},
        {"ts","typescript"},{"tsx","typescript"},
        {"go","go"},{"java","java"},{"lua","lua"},{"zig","zig"},
        {"html","html"},{"htm","html"},{"css","css"},{"scss","css"},
        {"json","json"},{"yaml","yaml"},{"yml","yaml"},{"toml","toml"},
        {"md","markdown"},{"sh","bash"},{"bash","bash"},{"zsh","bash"},
        {"hs","haskell"},{"ml","ocaml"},{"mli","ocaml"},{"rb","ruby"},
        {"php","php"},{"swift","swift"},{"kt","kotlin"},{"scala","scala"},
        {"ex","elixir"},{"exs","elixir"},{"nim","nim"},{"v","v"},
        {"odin","odin"},{"d","d"},{"ada","ada"},{"adb","ada"},{"ads","ada"},
        {"asm","asm"},{"s","asm"},{"r","r"},{"R","r"},{"jl","julia"},
        {"dart","dart"},{"clj","clojure"},{"cljs","clojure"},
        {"erl","erlang"},{"hrl","erlang"},{"fs","fsharp"},{"fsx","fsharp"},
        {"groovy","groovy"},{"pl","perl"},{"pm","perl"},{"ps1","powershell"},
        {"tcl","tcl"},{"f90","fortran"},{"f","fortran"},{"cob","cobol"},
        {"pl","prolog"},{"scm","scheme"},{NULL,NULL}
    };
    for (int i = 0; map[i][0]; i++) {
        if (strcasecmp(ext, map[i][0]) == 0) {
            int idx = find_lang(map[i][1]);
            if (idx >= 0) E.lang_idx = idx;
            return;
        }
    }
}

static int is_sep(char c) { return !isalnum((unsigned char)c) && c != '_'; }

static int match_word(const char *s, int i, int len, const char *const *words)
{
    if (!words) return 0;
    for (int wi = 0; words[wi]; wi++) {
        int wl = (int)strlen(words[wi]);
        if (i + wl > len || strncmp(s + i, words[wi], wl) != 0) continue;
        if (i + wl >= len || is_sep(s[i + wl])) return wl;
    }
    return 0;
}

static void compute_hl(int ri, unsigned char *hl)
{
    Row *row = &E.row[ri];
    Lang *lang = &E.langs[E.lang_idx];
    int len = row->len;
    memset(hl, HL_NORMAL, len);
    if (!lang->lc && !lang->bc_open && !lang->kw && !lang->types && !lang->str_chars)
        return;

    int in_ml = row->hl_open, in_str = 0;
    char str_c = 0;
    int prev_sep = 1;
    int i = 0;

    int bco_len = lang->bc_open  ? (int)strlen(lang->bc_open)  : 0;
    int bcc_len = lang->bc_close ? (int)strlen(lang->bc_close) : 0;
    int lc_len  = lang->lc       ? (int)strlen(lang->lc)       : 0;

    while (i < len) {
        char c = row->d[i];

        if (in_ml) {
            hl[i] = HL_MLCOMMENT;
            if (bcc_len && i + bcc_len <= len &&
                strncmp(row->d + i, lang->bc_close, bcc_len) == 0) {
                for (int j = 1; j < bcc_len; j++) hl[i + j] = HL_MLCOMMENT;
                i += bcc_len;
                in_ml = 0;
                prev_sep = 1;
            } else {
                i++;
            }
            continue;
        }

        if (in_str) {
            hl[i] = HL_STRING;
            if (c == '\\' && i + 1 < len) { hl[i+1] = HL_STRING; i += 2; continue; }
            if (c == str_c) in_str = 0;
            i++;
            prev_sep = 0;
            continue;
        }

        if (bco_len && i + bco_len <= len &&
            strncmp(row->d + i, lang->bc_open, bco_len) == 0) {
            for (int j = 0; j < bco_len; j++) hl[i + j] = HL_MLCOMMENT;
            i += bco_len;
            in_ml = 1;
            continue;
        }

        if (lc_len && i + lc_len <= len &&
            strncmp(row->d + i, lang->lc, lc_len) == 0) {
            for (int j = i; j < len; j++) hl[j] = HL_COMMENT;
            break;
        }

        if (lang->str_chars && strchr(lang->str_chars, c)) {
            in_str = 1; str_c = c; hl[i++] = HL_STRING; prev_sep = 0; continue;
        }

        if (prev_sep && (isdigit((unsigned char)c) ||
            (c == '.' && i+1 < len && isdigit((unsigned char)row->d[i+1])))) {
            while (i < len) {
                char nc = row->d[i];
                if (!isdigit((unsigned char)nc) && nc != '.' && nc != 'x' && nc != 'X' &&
                    !(nc >= 'a' && nc <= 'f') && !(nc >= 'A' && nc <= 'F') &&
                    nc != '_' && nc != 'u' && nc != 'U' && nc != 'l' && nc != 'L' &&
                    nc != 'f' && nc != 'F' && nc != 'e' && nc != 'E') break;
                hl[i++] = HL_NUMBER;
            }
            prev_sep = 0;
            continue;
        }

        if (prev_sep) {
            int kl = match_word(row->d, i, len, lang->kw);
            if (kl) {
                for (int j = 0; j < kl; j++) hl[i+j] = HL_KEYWORD;
                i += kl; prev_sep = 0; continue;
            }
            int tl = match_word(row->d, i, len, lang->types);
            if (tl) {
                for (int j = 0; j < tl; j++) hl[i+j] = HL_TYPE;
                i += tl; prev_sep = 0; continue;
            }
        }

        prev_sep = is_sep(c);
        i++;
    }

    if (ri + 1 < E.nrow && E.row[ri + 1].hl_open != in_ml)
        E.row[ri + 1].hl_open = in_ml;
}

static void propagate_hl(int from)
{
    Lang *lang = &E.langs[E.lang_idx];
    if (!lang->bc_open || !lang->bc_close) return;
    int bco = (int)strlen(lang->bc_open);
    int bcc = (int)strlen(lang->bc_close);
    int lc  = lang->lc ? (int)strlen(lang->lc) : 0;
    for (int ri = from; ri < E.nrow; ri++) {
        Row *row = &E.row[ri];
        int in_ml = row->hl_open, in_str = 0;
        char sc = 0;
        for (int i = 0; i < row->len; ) {
            char c = row->d[i];
            if (in_ml) {
                if (i + bcc <= row->len && strncmp(row->d+i, lang->bc_close, bcc) == 0)
                    { in_ml = 0; i += bcc; }
                else i++;
            } else if (in_str) {
                if (c == '\\' && i+1 < row->len) i += 2;
                else { if (c == sc) in_str = 0; i++; }
            } else if (lang->str_chars && strchr(lang->str_chars, c)) {
                in_str = 1; sc = c; i++;
            } else if (lc && i + lc <= row->len && strncmp(row->d+i, lang->lc, lc) == 0) {
                break;
            } else if (i + bco <= row->len && strncmp(row->d+i, lang->bc_open, bco) == 0) {
                in_ml = 1; i += bco;
            } else i++;
        }
        if (ri + 1 >= E.nrow) break;
        if (E.row[ri+1].hl_open == in_ml) break;
        E.row[ri+1].hl_open = in_ml;
    }
}

static int cur_r(void) { return E.sel.h.r; }
static int cur_c(void) { return E.sel.h.c; }

static void set_cur(int r, int c)
{
    if (!E.nrow) { E.sel.h.r = 0; E.sel.h.c = 0; E.sel.a = E.sel.h; return; }
    if (r < 0) r = 0;
    if (r >= E.nrow) r = E.nrow - 1;
    int mx = E.row[r].len;
    if (c < 0) c = 0;
    if (c > mx) c = mx;
    E.sel.h.r = r;
    E.sel.h.c = c;
    E.sel.a = E.sel.h;
    E.sel_line = 0;
}

static void clamp_cur(void) { set_cur(E.sel.h.r, E.sel.h.c); }

static void scroll(void)
{
    int r = cur_r(), c = cur_c(), vr = E.rows - 2 - TAB_BAR_H;
    if (r < E.off_r) E.off_r = r;
    if (r >= E.off_r + vr) E.off_r = r - vr + 1;
    int tcols = E.cols - E.lnum_w;
    if (tcols < 1) tcols = 1;
    if (c < E.off_c) E.off_c = c;
    if (c >= E.off_c + tcols) E.off_c = c - tcols + 1;
    if (E.off_r < 0) E.off_r = 0;
    if (E.off_c < 0) E.off_c = 0;
}

static void sel_bounds(Pos *lo, Pos *hi)
{
    Pos a = E.sel.a, h = E.sel.h;
    if (a.r < h.r || (a.r == h.r && a.c <= h.c)) { *lo = a; *hi = h; }
    else { *lo = h; *hi = a; }
}

static int in_sel(int r, int c)
{
    Pos a = E.sel.a, h = E.sel.h;
    if (a.r == h.r && a.c == h.c) return 0;
    Pos lo, hi;
    sel_bounds(&lo, &hi);
    if (r < lo.r || r > hi.r) return 0;
    int sc = (r == lo.r) ? lo.c : 0;
    int ec = (r == hi.r) ? hi.c : E.row[r].len;
    return c >= sc && c < ec;
}

static void get_completions(const char *base, int blen,
    const char **out, int *n)
{
    static const char *list[] = {
        "q","q!","w","wq","x",
        "e ","w ",
        "set-language ","set-theme ",
        "langs","themes","noh",
        NULL
    };
    *n = 0;
    for (int i = 0; list[i] && *n < 16; i++)
        if (blen == 0 || strncmp(list[i], base, blen) == 0)
            out[(*n)++] = list[i];
}

static int is_arg_cmd(const char *s)
{
    return strncmp(s, "set-theme ", 10) == 0 ||
           strncmp(s, "set-language ", 13) == 0;
}

#define MAX_ARG_COMP 64
static char arg_comp_buf[MAX_ARG_COMP][MAX_CMD];
static const char *arg_comp_ptr[MAX_ARG_COMP];
static int arg_comp_n = 0;

static void build_arg_completions(const char *base)
{
    arg_comp_n = 0;
    if (strncmp(base, "set-theme ", 10) == 0) {
        const char *arg = base + 10;
        int alen = (int)strlen(arg);
        for (int i = 0; i < NTHEMES && arg_comp_n < MAX_ARG_COMP; i++) {
            if (alen == 0 || strncmp(themes[i].name, arg, alen) == 0) {
                snprintf(arg_comp_buf[arg_comp_n], MAX_CMD, "set-theme %s", themes[i].name);
                arg_comp_ptr[arg_comp_n] = arg_comp_buf[arg_comp_n];
                arg_comp_n++;
            }
        }
    } else if (strncmp(base, "set-language ", 13) == 0) {
        const char *arg = base + 13;
        int alen = (int)strlen(arg);
        for (int i = 0; i < E.nlang && arg_comp_n < MAX_ARG_COMP; i++) {
            if (alen == 0 || strncmp(E.langs[i].name, arg, alen) == 0) {
                snprintf(arg_comp_buf[arg_comp_n], MAX_CMD, "set-language %s", E.langs[i].name);
                arg_comp_ptr[arg_comp_n] = arg_comp_buf[arg_comp_n];
                arg_comp_n++;
            }
        }
    }
}

static void tab_save(void)
{
    TabState *t = &g_tabs[g_cur_tab];
    t->row = E.row; t->nrow = E.nrow;
    memcpy(t->path, E.path, MAX_PATH);
    t->dirty = E.dirty;
    t->sel = E.sel; t->sel_line = E.sel_line;
    t->off_r = E.off_r; t->off_c = E.off_c;
    t->mode = E.mode;
    memcpy(t->cmd, E.cmd, MAX_CMD); t->cmd_len = E.cmd_len;
    memcpy(t->msg, E.msg, MAX_MSG);
    t->lang_idx = E.lang_idx;
    t->pend_g = E.pend_g; t->pend_r = E.pend_r;
    t->pend_gt = E.pend_gt; t->pend_lt = E.pend_lt;
    memcpy(t->search, E.search, MAX_CMD);
    t->search_len = E.search_len; t->search_dir = E.search_dir;
    t->search_hl = E.search_hl;
    t->cmd_comp = E.cmd_comp;
    memcpy(t->cmd_base, E.cmd_base, MAX_CMD);
}

static void tab_load(void)
{
    TabState *t = &g_tabs[g_cur_tab];
    E.row = t->row; E.nrow = t->nrow;
    memcpy(E.path, t->path, MAX_PATH);
    E.dirty = t->dirty;
    E.sel = t->sel; E.sel_line = t->sel_line;
    E.off_r = t->off_r; E.off_c = t->off_c;
    E.mode = t->mode;
    memcpy(E.cmd, t->cmd, MAX_CMD); E.cmd_len = t->cmd_len;
    memcpy(E.msg, t->msg, MAX_MSG);
    E.lang_idx = t->lang_idx;
    E.pend_g = t->pend_g; E.pend_r = t->pend_r;
    E.pend_gt = t->pend_gt; E.pend_lt = t->pend_lt;
    memcpy(E.search, t->search, MAX_CMD);
    E.search_len = t->search_len; E.search_dir = t->search_dir;
    E.search_hl = t->search_hl;
    E.cmd_comp = t->cmd_comp;
    memcpy(E.cmd_base, t->cmd_base, MAX_CMD);
}

static void tab_switch(int idx)
{
    if (idx < 0 || idx >= g_ntabs || idx == g_cur_tab) return;
    tab_save();
    g_cur_tab = idx;
    tab_load();
    E.msg[0] = 0;
}

static void tab_close(void)
{
    if (g_ntabs <= 1) { E.quit = 1; return; }
    for (int i = 0; i < E.nrow; i++) free(E.row[i].d);
    free(E.row);
    E.row = NULL; E.nrow = 0;
    int old = g_cur_tab;
    for (int i = old; i < g_ntabs - 1; i++) g_tabs[i] = g_tabs[i + 1];
    g_ntabs--;
    if (g_cur_tab >= g_ntabs) g_cur_tab = g_ntabs - 1;
    tab_load();
    E.msg[0] = 0;
}

static void tab_close_idx(int idx)
{
    if (g_ntabs <= 1) { E.quit = 1; return; }
    if (idx == g_cur_tab) {
        for (int i = 0; i < E.nrow; i++) free(E.row[i].d);
        free(E.row);
        E.row = NULL; E.nrow = 0;
        for (int i = idx; i < g_ntabs - 1; i++) g_tabs[i] = g_tabs[i + 1];
        g_ntabs--;
        if (g_cur_tab >= g_ntabs) g_cur_tab = g_ntabs - 1;
        tab_load();
    } else {
        tab_save();
        TabState *ts = &g_tabs[idx];
        for (int i = 0; i < ts->nrow; i++) free(ts->row[i].d);
        free(ts->row);
        ts->row = NULL; ts->nrow = 0;
        for (int i = idx; i < g_ntabs - 1; i++) g_tabs[i] = g_tabs[i + 1];
        g_ntabs--;
        if (g_cur_tab > idx) g_cur_tab--;
        tab_load();
    }
    E.msg[0] = 0;
}

static char *show_start(void);
static void build_completions(const char *input);
static void draw_start(Ab *ab, const char *input, int input_len);
static void draw_new_completion(Ab *ab, int sel);

static void new_tab(void)
{
    if (g_ntabs >= MAX_TABS) {
        snprintf(E.msg, MAX_MSG, "Max tabs reached (%d)", MAX_TABS);
        return;
    }
    tab_save();
    int idx = g_ntabs++;
    g_cur_tab = idx;
    memset(&g_tabs[idx], 0, sizeof(g_tabs[idx]));
    g_tabs[idx].cmd_comp = -1;
    g_tabs[idx].is_new = 1;
    g_tabs[idx].new_comp_idx = -1;
    tab_load();
}

static void draw_tabbar(Ab *ab)
{
    Theme *t = &themes[E.theme_idx];
    ab_add(ab, "\033[1;1H", 6);
    ab_adds(ab, t->status);
    ab_add(ab, "\033[2K", 4);
    int col = 1;
    for (int i = 0; i < g_ntabs; i++) {
        const char *pth = (i == g_cur_tab) ? E.path : g_tabs[i].path;
        const char *name;
        if (!pth[0]) {
            name = "new";
        } else {
            const char *sl = strrchr(pth, '/');
            name = sl ? sl + 1 : pth;
        }
        int name_disp = (int)strlen(name);
        if (name_disp > 28) name_disp = 28;
        char buf[48];
        int n = snprintf(buf, sizeof(buf), " %.*s x ", 28, name);
        g_tab_col_start[i] = col;
        g_tab_close_col[i] = col + name_disp + 2;
        if (i == g_cur_tab) {
            ab_adds(ab, t->sel);
            ab_add(ab, buf, n);
            ab_adds(ab, t->status);
        } else {
            ab_add(ab, buf, n);
        }
        col += n;
        g_tab_col_end[i] = col;
    }
    if (g_ntabs < MAX_TABS) {
        ab_adds(ab, t->normal);
        g_plus_col = col + 1;
        ab_add(ab, " + ", 3);
        col += 3;
        ab_adds(ab, t->status);
    } else {
        g_plus_col = 0;
    }
    ab_adds(ab, t->normal);
}

static void draw_rows(Ab *ab)
{
    Theme *t = &themes[E.theme_idx];
    int vr = E.rows - 2 - TAB_BAR_H;
    int tcols = E.cols - E.lnum_w;
    int has_sel = (E.sel.a.r != E.sel.h.r || E.sel.a.c != E.sel.h.c);
    unsigned char *hl = NULL;
    int hl_cap = 0;

    for (int y = 0; y < vr; y++) {
        int fr = y + E.off_r;
        int is_cur = (E.nrow > 0 && fr == cur_r());
        const char *base = is_cur ? t->curline : t->normal;

        ab_addf(ab, "\033[%d;1H", y + 1 + TAB_BAR_H);
        ab_adds(ab, t->normal);

        ab_adds(ab, t->tilde);
        if (fr < E.nrow) {
            if (is_cur)
                ab_addf(ab, "\033[1m%*d \033[m", E.lnum_w - 1, fr + 1);
            else
                ab_addf(ab, "%*d ", E.lnum_w - 1, fr + 1);
        } else {
            ab_addf(ab, "%*s", E.lnum_w, "");
        }

        ab_adds(ab, base);
        ab_add(ab, "\033[K", 3);

        if (fr >= E.nrow) {
            ab_adds(ab, t->tilde);
            ab_add(ab, "~", 1);
            ab_adds(ab, t->normal);
            continue;
        }

        Row *row = &E.row[fr];
        int from = E.off_c, to = from + tcols;
        if (to > row->len) to = row->len;
        if (from > row->len) continue;

        if (row->len >= hl_cap) {
            hl_cap = row->len + 1;
            hl = realloc(hl, hl_cap);
        }
        if (hl) {
            memset(hl, HL_NORMAL, row->len);
            compute_hl(fr, hl);
            if (E.search_hl && E.search_len > 0) {
                char *p = row->d;
                while (p - row->d + E.search_len <= row->len) {
                    char *f = strstr(p, E.search);
                    if (!f) break;
                    int idx = (int)(f - row->d);
                    for (int j = 0; j < E.search_len && idx+j < row->len; j++)
                        hl[idx+j] = HL_SEARCH;
                    p = f + 1;
                }
            }
        }

        int cur_hl = HL_NORMAL;
        for (int x = from; x < to; x++) {
            if (has_sel && in_sel(fr, x)) {
                if (cur_hl != -1) { ab_adds(ab, base); cur_hl = -1; }
                ab_adds(ab, t->sel);
                ab_add(ab, row->d + x, 1);
                ab_adds(ab, base);
            } else {
                int h = hl ? (int)hl[x] : HL_NORMAL;
                const char *col = (h == HL_NORMAL) ? base : hl_color(h);
                if (h != cur_hl) { ab_adds(ab, col); cur_hl = h; }
                ab_add(ab, row->d + x, 1);
            }
        }
        if (cur_hl != HL_NORMAL && cur_hl != -1) ab_adds(ab, t->normal);
    }
    free(hl);
}

static const char *mode_name(Mode m)
{
    switch (m) {
    case INSERT:  return "INSERT";
    case COMMAND: return "COMMAND";
    case SEARCH:  return "SEARCH";
    default:      return "NORMAL";
    }
}

static const char *mode_color(Mode m, const Theme *t)
{
    switch (m) {
    case INSERT:  return t->status_i;
    case COMMAND:
    case SEARCH:  return t->status_c;
    default:      return t->status;
    }
}

static void draw_status(Ab *ab)
{
    Theme *t = &themes[E.theme_idx];
    ab_addf(ab, "\033[%d;1H", E.rows - 1);
    ab_add(ab, "\033[2K", 4);

    const char *mcol = mode_color(E.mode, t);
    const char *mstr = mode_name(E.mode);

    char mblock[32];
    int mbl = snprintf(mblock, sizeof(mblock), " %s ", mstr);
    ab_adds(ab, mcol);
    ab_add(ab, mblock, mbl);

    ab_adds(ab, t->status);
    const char *lang = E.lang_idx < E.nlang ? E.langs[E.lang_idx].name : "plain";
    char mid[192], right[48];
    int ml = snprintf(mid, sizeof(mid), " \xe2\x94\x82 %s%s ",
                      E.path[0] ? E.path : "[No Name]", E.dirty ? " [+]" : "");
    int rl = snprintf(right, sizeof(right), " %s \xe2\x94\x82 %d:%d ",
                      lang, cur_r()+1, cur_c()+1);
    ab_add(ab, mid, ml);
    int pad = E.cols - mbl - (ml - 2) - (rl - 2);
    if (pad < 0) pad = 0;
    for (int i = 0; i < pad; i++) ab_add(ab, " ", 1);
    ab_add(ab, right, rl);
    ab_adds(ab, t->normal);
}

static void draw_cmdbar(Ab *ab)
{
    Theme *t = &themes[E.theme_idx];
    ab_addf(ab, "\033[%d;1H", E.rows);
    ab_adds(ab, t->normal);
    ab_add(ab, "\033[2K", 4);
    if (E.mode == COMMAND) {
        ab_add(ab, ":", 1); ab_add(ab, E.cmd, E.cmd_len);
    } else if (E.mode == SEARCH) {
        char dc = E.search_dir > 0 ? '/' : '?';
        ab_add(ab, &dc, 1);
        ab_add(ab, E.search, E.search_len);
    } else if (E.msg[0]) {
        ab_add(ab, E.msg, strlen(E.msg));
    }
}

static void draw_cursor(Ab *ab)
{
    if (E.mode == COMMAND) {
        ab_addf(ab, "\033[%d;%dH\033[6 q", E.rows, E.cmd_len + 2);
    } else if (E.mode == SEARCH) {
        ab_addf(ab, "\033[%d;%dH\033[6 q", E.rows, E.search_len + 2);
    } else {
        int r = cur_r() - E.off_r + 1 + TAB_BAR_H, c = cur_c() - E.off_c + 1 + E.lnum_w;
        if (r < 1) r = 1;
        if (c < 1) c = 1;
        ab_addf(ab, "\033[%d;%dH%s", r, c, E.mode == INSERT ? "\033[6 q" : "\033[2 q");
    }
}

static void draw_completion(Ab *ab)
{
    if (E.mode != COMMAND) return;
    Theme *t = &themes[E.theme_idx];
    const char *base = (E.cmd_comp >= 0) ? E.cmd_base : E.cmd;
    int blen = (E.cmd_comp >= 0) ? (int)strlen(E.cmd_base) : E.cmd_len;
    const char **matches;
    int nm;
    const char *cmd_matches[16];
    if (is_arg_cmd(base)) {
        build_arg_completions(base);
        matches = arg_comp_ptr;
        nm = arg_comp_n;
    } else {
        get_completions(base, blen, cmd_matches, &nm);
        matches = cmd_matches;
    }
    if (!nm) return;
    int maxshow = nm < 10 ? nm : 10;
    int start = 0;
    if (E.cmd_comp >= 0) {
        int sel = E.cmd_comp % nm;
        if (sel >= maxshow) start = sel - maxshow + 1;
    }
    int show = (start + maxshow <= nm) ? maxshow : nm - start;
    int sel_idx = (E.cmd_comp >= 0) ? E.cmd_comp % nm : -1;
    for (int i = 0; i < show; i++) {
        int idx = start + i;
        int row = E.rows - 1 - show + i;
        if (row <= TAB_BAR_H) continue;
        ab_addf(ab, "\033[%d;1H", row);
        ab_adds(ab, (idx == sel_idx) ? t->sel : t->normal);
        ab_addf(ab, " %s", matches[idx]);
        ab_add(ab, "\033[K", 3);
        ab_adds(ab, t->normal);
    }
}

static int line_num_width(void)
{
    int n = E.nrow > 0 ? E.nrow : 1;
    int digits = 0;
    while (n > 0) { digits++; n /= 10; }
    return digits + 1;
}

static void refresh(void)
{
    E.lnum_w = line_num_width();
    clamp_cur();
    scroll();
    Ab ab = AB_INIT;
    ab_add(&ab, "\033[?25l", 6);
    draw_tabbar(&ab);
    if (g_tabs[g_cur_tab].is_new) {
        draw_start(&ab, g_tabs[g_cur_tab].new_input, g_tabs[g_cur_tab].new_input_len);
        draw_new_completion(&ab, g_tabs[g_cur_tab].new_comp_idx);
    } else {
        draw_rows(&ab);
        draw_status(&ab);
        draw_cmdbar(&ab);
        draw_completion(&ab);
        draw_cursor(&ab);
    }
    ab_add(&ab, "\033[?25h", 6);
    write(STDOUT_FILENO, ab.b, ab.len);
    ab_free(&ab);
}

static int tab_size(void)
{
    return (E.lang_idx < E.nlang) ? E.langs[E.lang_idx].tab : 4;
}

static void insert_newline(void)
{
    int r = cur_r(), c = cur_c();
    if (!E.nrow) { buf_ins_row(0,"",0); buf_ins_row(1,"",0); set_cur(1,0); return; }
    Row *row = &E.row[r];
    buf_ins_row(r + 1, row->d + c, row->len - c);
    row->len = c;
    if (row->d) row->d[c] = 0;
    E.dirty = 1;
    propagate_hl(r);
    set_cur(r + 1, 0);
}

static void del_char_before(void)
{
    int r = cur_r(), c = cur_c();
    if (c == 0 && r == 0) return;
    if (c == 0) {
        int plen = E.row[r-1].len;
        row_append_str(&E.row[r-1], E.row[r].d, E.row[r].len);
        buf_del_row(r);
        propagate_hl(r-1);
        set_cur(r-1, plen);
    } else {
        row_del_char(&E.row[r], c-1);
        set_cur(r, c-1);
        propagate_hl(r);
        E.dirty = 1;
    }
}

static void del_char_at(void)
{
    int r = cur_r(), c = cur_c();
    if (!E.nrow) return;
    Row *row = &E.row[r];
    if (c < row->len) {
        row_del_char(row, c);
        propagate_hl(r);
        E.dirty = 1;
    } else if (r + 1 < E.nrow) {
        row_append_str(row, E.row[r+1].d, E.row[r+1].len);
        buf_del_row(r+1);
        propagate_hl(r);
    }
}

static void yank_sel(void)
{
    E.yank_line = 0;
    if (E.sel_line || (E.sel.a.r == E.sel.h.r && E.sel.a.c == E.sel.h.c)) {
        int r = cur_r();
        if (!E.nrow) { E.yank[0] = 0; return; }
        int n = E.row[r].len;
        if (n >= MAX_YANK) n = MAX_YANK - 1;
        memcpy(E.yank, E.row[r].d, n);
        E.yank[n] = 0;
        E.yank_line = 1;
        return;
    }
    Pos lo, hi;
    sel_bounds(&lo, &hi);
    int idx = 0;
    for (int r = lo.r; r <= hi.r && idx < MAX_YANK - 1; r++) {
        Row *row = &E.row[r];
        int sc = (r == lo.r) ? lo.c : 0;
        int ec = (r == hi.r) ? hi.c : row->len;
        int n = ec - sc;
        if (idx + n >= MAX_YANK - 1) n = MAX_YANK - 1 - idx;
        if (n > 0) memcpy(E.yank + idx, row->d + sc, n);
        idx += n;
        if (r < hi.r && idx < MAX_YANK - 1) E.yank[idx++] = '\n';
    }
    E.yank[idx] = 0;
}

static void del_sel(void)
{
    if (E.sel_line) {
        int r = cur_r();
        if (E.nrow <= 1) {
            E.row[0].len = 0;
            if (E.row[0].d) E.row[0].d[0] = 0;
        } else {
            buf_del_row(r);
            if (r >= E.nrow) r = E.nrow - 1;
        }
        set_cur(r, 0);
        E.sel_line = 0;
        propagate_hl(r);
        return;
    }
    if (E.sel.a.r == E.sel.h.r && E.sel.a.c == E.sel.h.c) { del_char_at(); return; }
    Pos lo, hi;
    sel_bounds(&lo, &hi);
    if (lo.r == hi.r) {
        Row *row = &E.row[lo.r];
        int n = hi.c - lo.c;
        if (lo.c + n > row->len) n = row->len - lo.c;
        if (n > 0) {
            memmove(row->d+lo.c, row->d+lo.c+n, row->len-lo.c-n);
            row->len -= n;
            row->d[row->len] = 0;
        }
    } else {
        Row *lo_row = &E.row[lo.r], *hi_row = &E.row[hi.r];
        int tail = hi_row->len - hi.c;
        lo_row->len = lo.c;
        if (lo_row->d) lo_row->d[lo.c] = 0;
        if (tail > 0) row_append_str(lo_row, hi_row->d + hi.c, tail);
        for (int i = lo.r + 1; i <= hi.r; i++) buf_del_row(lo.r + 1);
    }
    set_cur(lo.r, lo.c);
    propagate_hl(lo.r);
    E.dirty = 1;
}

static void kill_to_eol(int r, int c)
{
    Row *row = &E.row[r];
    int tlen = row->len - c;
    if (tlen > 0) {
        memcpy(E.yank, row->d + c, tlen);
        E.yank[tlen] = 0;
        E.yank_line = 0;
    }
    row->len = c;
    if (row->d) row->d[c] = 0;
    E.dirty = 1;
    propagate_hl(r);
}

static void paste(int after)
{
    if (!E.yank[0] && !E.yank_line) return;
    if (E.yank_line) {
        int at = cur_r() + (after ? 1 : 0);
        buf_ins_row(at, E.yank, strlen(E.yank));
        set_cur(at, 0);
        return;
    }
    if (!E.nrow) buf_ins_row(0, "", 0);
    int r = cur_r(), c = cur_c();
    if (after && c < E.row[r].len) c++;
    for (char *p = E.yank; *p; p++) {
        if (*p == '\n') {
            insert_newline();
            r = cur_r();
            c = cur_c();
        } else {
            row_ins_char(&E.row[r], c, *p);
            c++;
            E.dirty = 1;
        }
    }
    set_cur(r, c > 0 ? c - 1 : 0);
}

static void paste_after(void)  { paste(1); }
static void paste_before(void) { paste(0); }

static int find_next_match(int dir)
{
    if (!E.search[0] || !E.nrow) return 0;
    int plen = (int)strlen(E.search);
    int sr = cur_r();
    for (int step = 0; step < E.nrow; step++) {
        int r = ((dir > 0) ? (sr + step) : (sr - step + E.nrow)) % E.nrow;
        Row *row = &E.row[r];
        char *hit = NULL;
        if (dir > 0) {
            int from = (step == 0) ? cur_c() + 1 : 0;
            if (row->len > 0 && from + plen <= row->len + 1)
                hit = strstr(row->d + from, E.search);
        } else {
            int limit = (step == 0) ? cur_c() : row->len + 1;
            char *p = row->d, *last = NULL;
            while (p && (int)(p - row->d) + plen <= row->len) {
                char *f = strstr(p, E.search);
                if (!f || (int)(f - row->d) >= limit) break;
                last = f; p = f + 1;
            }
            hit = last;
        }
        if (hit) { set_cur(r, (int)(hit - row->d)); return 1; }
    }
    snprintf(E.msg, MAX_MSG, "Pattern not found: %s", E.search);
    return 0;
}

static void search_word_under_cursor(void)
{
    if (!E.nrow) return;
    int r = cur_r(), c = cur_c();
    Row *row = &E.row[r];
    if (c >= row->len) return;
    if (!isalnum((unsigned char)row->d[c]) && row->d[c] != '_') return;
    int start = c;
    while (start > 0 && (isalnum((unsigned char)row->d[start-1]) || row->d[start-1] == '_'))
        start--;
    int end = c;
    while (end < row->len && (isalnum((unsigned char)row->d[end]) || row->d[end] == '_'))
        end++;
    int len = end - start;
    if (len <= 0 || len >= MAX_CMD) return;
    memcpy(E.search, row->d + start, len);
    E.search[len] = 0;
    E.search_len = len;
    E.search_dir = 1;
    E.search_hl = 1;
    find_next_match(1);
}

static void indent_line(int r, int dir)
{
    if (!E.nrow || r >= E.nrow) return;
    int ts = tab_size();
    Row *row = &E.row[r];
    if (dir > 0) {
        for (int i = 0; i < ts; i++) row_ins_char(row, 0, ' ');
    } else {
        int rem = 0;
        while (rem < ts && rem < row->len && row->d[rem] == ' ') rem++;
        for (int i = 0; i < rem; i++) row_del_char(row, 0);
    }
    propagate_hl(r);
    E.dirty = 1;
}

static void jump_matching_bracket(void)
{
    if (!E.nrow) return;
    int r = cur_r(), c = cur_c();
    Row *row = &E.row[r];
    if (c >= row->len) return;
    char ch = row->d[c];
    char open, close; int dir;
    if      (ch == '(') { open='('; close=')'; dir= 1; }
    else if (ch == ')') { open='('; close=')'; dir=-1; }
    else if (ch == '[') { open='['; close=']'; dir= 1; }
    else if (ch == ']') { open='['; close=']'; dir=-1; }
    else if (ch == '{') { open='{'; close='}'; dir= 1; }
    else if (ch == '}') { open='{'; close='}'; dir=-1; }
    else return;
    int depth = 1, cr = r, cc = c + dir;
    while (cr >= 0 && cr < E.nrow) {
        Row *cur = &E.row[cr];
        while (cc >= 0 && cc < cur->len) {
            char nc = cur->d[cc];
            if (nc == (dir > 0 ? close : open)) { if (--depth == 0) { set_cur(cr, cc); return; } }
            else if (nc == (dir > 0 ? open : close)) depth++;
            cc += dir;
        }
        cr += dir;
        if (cr >= 0 && cr < E.nrow)
            cc = (dir > 0) ? 0 : E.row[cr].len - 1;
    }
    snprintf(E.msg, MAX_MSG, "No matching bracket");
}

static void move_word_fwd(void)
{
    int r = cur_r(), c = cur_c();
    if (!E.nrow) return;
    Row *row = &E.row[r];
    if (c < row->len && isalnum((unsigned char)row->d[c]))
        while (c < row->len && isalnum((unsigned char)row->d[c])) c++;
    else
        while (c < row->len && !isalnum((unsigned char)row->d[c])) c++;
    if (c >= row->len && r + 1 < E.nrow) { r++; c = 0; }
    set_cur(r, c);
}

static void move_word_bwd(void)
{
    int r = cur_r(), c = cur_c();
    if (!E.nrow) return;
    if (c == 0) { if (r > 0) set_cur(r-1, E.row[r-1].len); return; }
    c--;
    Row *row = &E.row[r];
    while (c > 0 && !isalnum((unsigned char)row->d[c])) c--;
    while (c > 0 && isalnum((unsigned char)row->d[c-1])) c--;
    set_cur(r, c);
}

static void save_theme(void)
{
    char dir[MAX_PATH], path[MAX_PATH];
    const char *home = getenv("HOME");
    if (!home) home = "/tmp";
    snprintf(dir,  sizeof(dir),  "%s/.config/petalpaper", home);
    snprintf(path, sizeof(path), "%s/theme", dir);
    mkdir(dir, 0755);
    FILE *f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "%s\n", themes[E.theme_idx].name);
    fclose(f);
}

static void load_theme(void)
{
    char path[MAX_PATH];
    const char *home = getenv("HOME");
    if (!home) home = "/tmp";
    snprintf(path, sizeof(path), "%s/.config/petalpaper/theme", home);
    FILE *f = fopen(path, "r");
    if (!f) return;
    char name[64] = {0};
    if (fgets(name, sizeof(name), f)) {
        name[strcspn(name, "\n\r")] = 0;
        int idx = find_theme(name);
        if (idx >= 0) E.theme_idx = idx;
    }
    fclose(f);
}

static int do_save(const char *path)
{
    if (buf_save(path) < 0) {
        snprintf(E.msg, MAX_MSG, "Save failed: %s", strerror(errno));
        return -1;
    }
    snprintf(E.msg, MAX_MSG, "Saved \"%.*s\"", MAX_MSG - 10, path);
    return 0;
}

static void exec_cmd(void)
{
    char *cmd = E.cmd;
    E.msg[0] = 0;
    if (strcmp(cmd, "q") == 0) {
        if (E.dirty) snprintf(E.msg, MAX_MSG, "Unsaved changes! Use :q! to force quit.");
        else if (g_ntabs > 1) tab_close();
        else E.quit = 1;
    } else if (strcmp(cmd, "q!") == 0) {
        if (g_ntabs > 1) tab_close();
        else E.quit = 1;
    } else if (strcmp(cmd, "w") == 0) {
        if (!E.path[0]) snprintf(E.msg, MAX_MSG, "No filename. Use :w <file>");
        else do_save(E.path);
    } else if (strncmp(cmd, "w ", 2) == 0) {
        snprintf(E.path, MAX_PATH, "%.*s", MAX_PATH - 1, cmd + 2);
        do_save(E.path);
    } else if (strcmp(cmd, "wq") == 0 || strcmp(cmd, "x") == 0) {
        if (!E.path[0]) snprintf(E.msg, MAX_MSG, "No filename. Use :w <file>");
        else if (do_save(E.path) == 0) E.quit = 1;
    } else if (strncmp(cmd, "e ", 2) == 0) {
        while (E.nrow) buf_del_row(0);
        buf_open(cmd+2);
        detect_lang(cmd+2);
        if (!E.nrow) buf_ins_row(0, "", 0);
        set_cur(0, 0);
        propagate_hl(0);
    } else if (strncmp(cmd, "set-language ", 13) == 0) {
        const char *lang = cmd + 13;
        while (*lang == ' ') lang++;
        int idx = find_lang(lang);
        if (idx < 0) {
            snprintf(E.msg, MAX_MSG, "Unknown language: %.200s", lang);
        } else {
            E.lang_idx = idx;
            for (int i = 0; i < E.nrow; i++) E.row[i].hl_open = 0;
            propagate_hl(0);
            snprintf(E.msg, MAX_MSG, "Language: %s (tab=%d)", E.langs[idx].name, E.langs[idx].tab);
        }
    } else if (strncmp(cmd, "set-theme ", 10) == 0) {
        const char *name = cmd + 10;
        while (*name == ' ') name++;
        int idx = find_theme(name);
        if (idx < 0) snprintf(E.msg, MAX_MSG, "Unknown theme: %.200s", name);
        else {
            E.theme_idx = idx;
            save_theme();
            snprintf(E.msg, MAX_MSG, "Theme: %s", themes[idx].name);
        }
    } else if (strcmp(cmd, "langs") == 0) {
        char buf[MAX_MSG] = {0};
        int pos = 0;
        for (int i = 0; i < E.nlang && pos < MAX_MSG - 32; i++)
            pos += snprintf(buf+pos, MAX_MSG-pos, "%s%s", i?" ":"", E.langs[i].name);
        snprintf(E.msg, MAX_MSG, "%s", buf);
    } else if (strcmp(cmd, "themes") == 0) {
        char buf[MAX_MSG] = {0};
        int pos = 0;
        for (int i = 0; i < NTHEMES && pos < MAX_MSG - 32; i++)
            pos += snprintf(buf+pos, MAX_MSG-pos, "%s%s", i?" ":"", themes[i].name);
        snprintf(E.msg, MAX_MSG, "%s", buf);
    } else if (strcmp(cmd, "noh") == 0) {
        E.search_hl = 0;
        snprintf(E.msg, MAX_MSG, "Search highlight cleared");
    } else if (strcmp(cmd, "tabnew") == 0) {
        new_tab();
    } else if (strcmp(cmd, "tabn") == 0) {
        tab_switch((g_cur_tab + 1) % g_ntabs);
    } else if (strcmp(cmd, "tabp") == 0) {
        tab_switch((g_cur_tab - 1 + g_ntabs) % g_ntabs);
    } else if (strcmp(cmd, "tabc") == 0) {
        if (E.dirty) snprintf(E.msg, MAX_MSG, "Unsaved changes! Use :tabc! to force.");
        else tab_close();
    } else if (strcmp(cmd, "tabc!") == 0) {
        tab_close();
    } else {
        int is_num = (E.cmd_len > 0);
        for (int i = 0; i < E.cmd_len && is_num; i++)
            if (!isdigit((unsigned char)cmd[i])) is_num = 0;
        if (is_num) {
            int line = atoi(cmd);
            if (line < 1) line = 1;
            if (line > E.nrow) line = E.nrow;
            set_cur(line - 1, 0);
            snprintf(E.msg, MAX_MSG, "Line %d", line);
        } else {
            snprintf(E.msg, MAX_MSG, "Unknown command: :%s", cmd);
        }
    }
}

static void process_normal(int k)
{
    int r = cur_r(), c = cur_c(), vr = E.rows - 2 - TAB_BAR_H;
    if (E.pend_g) {
        E.pend_g = 0;
        if (k == 'g') { set_cur(0, 0); return; }
        if (k == 't') { tab_switch((g_cur_tab + 1) % g_ntabs); return; }
        if (k == 'T') { tab_switch((g_cur_tab - 1 + g_ntabs) % g_ntabs); return; }
    }
    if (E.pend_r) {
        E.pend_r = 0;
        if (k >= 32 && k != 127 && E.nrow) {
            row_del_char(&E.row[r], c);
            row_ins_char(&E.row[r], c, (char)k);
            propagate_hl(r); E.dirty = 1;
        }
        return;
    }
    if (E.pend_gt) { E.pend_gt = 0; if (k == '>') { indent_line(r,  1); } return; }
    if (E.pend_lt) { E.pend_lt = 0; if (k == '<') { indent_line(r, -1); } return; }
    switch (k) {
    case 'h': case KEY_LEFT:  set_cur(r, c-1); break;
    case 'j': case KEY_DOWN:  set_cur(r+1, c); break;
    case 'k': case KEY_UP:    set_cur(r-1, c); break;
    case 'l': case KEY_RIGHT: set_cur(r, c+1); break;
    case '0': case KEY_HOME:  set_cur(r, 0); break;
    case '$': case KEY_END:   set_cur(r, E.nrow ? E.row[r].len : 0); break;
    case 'g': E.pend_g = 1; break;
    case 'G': set_cur(E.nrow > 0 ? E.nrow-1 : 0, 0); break;
    case 'w': move_word_fwd(); break;
    case 'b': move_word_bwd(); break;
    case CTRL('d'): set_cur(r + vr/2, c); break;
    case CTRL('u'): set_cur(r - vr/2, c); break;
    case CTRL('f'): case KEY_PGDN: set_cur(r + vr, c); break;
    case CTRL('b'): case KEY_PGUP: set_cur(r - vr, c); break;
    case 'x':
        if (!E.nrow) break;
        E.sel.a.r = r; E.sel.a.c = 0;
        E.sel.h.r = r; E.sel.h.c = E.row[r].len;
        E.sel_line = 1;
        break;
    case 'd': yank_sel(); del_sel(); snprintf(E.msg, MAX_MSG, "Deleted"); break;
    case 'y': yank_sel(); snprintf(E.msg, MAX_MSG, "Yanked"); break;
    case 'p': paste_after(); break;
    case 'i': E.mode = INSERT; E.msg[0] = 0; break;
    case 'I': set_cur(r, 0); E.mode = INSERT; E.msg[0] = 0; break;
    case 'a':
        if (E.nrow && c < E.row[r].len) E.sel.h.c = c+1;
        E.mode = INSERT; E.msg[0] = 0; break;
    case 'A':
        if (E.nrow) set_cur(r, E.row[r].len);
        E.mode = INSERT; E.msg[0] = 0; break;
    case 'o': case 'O': {
        int nr = cur_r() + (k == 'o');
        if (!E.nrow) buf_ins_row(0, "", 0);
        buf_ins_row(nr, "", 0);
        set_cur(nr, 0);
        E.mode = INSERT; E.msg[0] = 0; break;
    }
    case 'c': yank_sel(); del_sel(); E.mode = INSERT; E.msg[0] = 0; break;
    case ':':
        E.mode = COMMAND;
        E.cmd[0] = 0; E.cmd_len = 0; E.cmd_comp = -1; E.msg[0] = 0;
        break;
    case '\033': E.msg[0] = 0; break;
    case '/':
        E.mode = SEARCH; E.search_dir =  1;
        E.search[0] = 0; E.search_len = 0; E.msg[0] = 0; break;
    case '?':
        E.mode = SEARCH; E.search_dir = -1;
        E.search[0] = 0; E.search_len = 0; E.msg[0] = 0; break;
    case 'n': find_next_match(E.search_dir);  break;
    case 'N': find_next_match(-E.search_dir); break;
    case '*': search_word_under_cursor(); break;
    case 'r': if (E.nrow) E.pend_r = 1; break;
    case 'D':
        if (E.nrow && E.row[r].len - c > 0) kill_to_eol(r, c);
        break;
    case 'C':
        if (E.nrow) {
            kill_to_eol(r, c);
            E.mode = INSERT; E.msg[0] = 0;
        }
        break;
    case 'P': paste_before(); break;
    case 'J':
        if (!E.nrow || r + 1 >= E.nrow) break;
        {
            Row *cr2 = &E.row[r], *nr = &E.row[r + 1];
            int old_len = cr2->len;
            if (cr2->len > 0 && nr->len > 0) row_append_str(cr2, " ", 1);
            row_append_str(cr2, nr->d, nr->len);
            buf_del_row(r + 1);
            propagate_hl(r); E.dirty = 1;
            set_cur(r, old_len);
        }
        break;
    case '%': jump_matching_bracket(); break;
    case '>': E.pend_gt = 1; break;
    case '<': E.pend_lt = 1; break;
    case CTRL('t'): new_tab(); break;
    }
}

static void process_insert(int k)
{
    switch (k) {
    case '\033':
        E.mode = NORMAL;
        if (E.nrow) {
            int r = cur_r();
            if (E.sel.h.c > 0 && E.sel.h.c >= E.row[r].len)
                E.sel.h.c = E.row[r].len > 0 ? E.row[r].len - 1 : 0;
            E.sel.a = E.sel.h;
        }
        break;
    case KEY_BS: case CTRL('h'): del_char_before(); break;
    case KEY_DEL: del_char_at(); break;
    case '\r': case '\n': insert_newline(); break;
    case '\t': {
        int ts = tab_size(), r = cur_r(), c = cur_c();
        if (!E.nrow) { buf_ins_row(0, "", 0); r = 0; c = 0; }
        int sp = ts - (c % ts);
        for (int i = 0; i < sp; i++) row_ins_char(&E.row[r], c+i, ' ');
        set_cur(r, c+sp);
        propagate_hl(r);
        E.dirty = 1;
        break;
    }
    case KEY_LEFT:  set_cur(cur_r(), cur_c()-1); break;
    case KEY_RIGHT: set_cur(cur_r(), cur_c()+1); break;
    case KEY_UP:    set_cur(cur_r()-1, cur_c()); break;
    case KEY_DOWN:  set_cur(cur_r()+1, cur_c()); break;
    case KEY_HOME:  set_cur(cur_r(), 0); break;
    case KEY_END:
        if (E.nrow) set_cur(cur_r(), E.row[cur_r()].len);
        break;
    default:
        if (k >= 32 && k != 127) {
            int r = cur_r(), c = cur_c();
            if (!E.nrow) { buf_ins_row(0, "", 0); r = 0; c = 0; }
            row_ins_char(&E.row[r], c, (char)k);
            set_cur(r, c+1);
            propagate_hl(r);
            E.dirty = 1;
        }
        break;
    }
}

static void process_command(int k)
{
    switch (k) {
    case '\033':
        E.mode = NORMAL;
        E.cmd[0] = 0; E.cmd_len = 0; E.cmd_comp = -1;
        break;
    case '\r': case '\n':
        E.cmd[E.cmd_len] = 0;
        exec_cmd();
        E.mode = NORMAL;
        E.cmd[0] = 0; E.cmd_len = 0; E.cmd_comp = -1;
        break;
    case '\t': {
        if (E.cmd_comp < 0) {
            strncpy(E.cmd_base, E.cmd, MAX_CMD - 1);
            E.cmd_base[MAX_CMD - 1] = 0;
        }
        if (is_arg_cmd(E.cmd_base)) {
            build_arg_completions(E.cmd_base);
            if (!arg_comp_n) break;
            E.cmd_comp = (E.cmd_comp < 0) ? 0 : (E.cmd_comp + 1) % arg_comp_n;
            strncpy(E.cmd, arg_comp_buf[E.cmd_comp], MAX_CMD - 1);
            E.cmd_len = (int)strlen(E.cmd);
        } else {
            const char *matches[16];
            int nm = 0;
            get_completions(E.cmd_base, strlen(E.cmd_base), matches, &nm);
            if (!nm) break;
            E.cmd_comp = (E.cmd_comp < 0) ? 0 : (E.cmd_comp + 1) % nm;
            strncpy(E.cmd, matches[E.cmd_comp], MAX_CMD - 1);
            E.cmd_len = (int)strlen(E.cmd);
        }
        break;
    }
    case KEY_BS: case CTRL('h'):
        E.cmd_comp = -1;
        if (E.cmd_len > 0) E.cmd[--E.cmd_len] = 0;
        else E.mode = NORMAL;
        break;
    default:
        if (k >= 32 && k != 127 && E.cmd_len < MAX_CMD - 1) {
            E.cmd_comp = -1;
            E.cmd[E.cmd_len++] = (char)k;
            E.cmd[E.cmd_len] = 0;
        }
        break;
    }
}

static void process_search(int k)
{
    switch (k) {
    case '\033':
        E.mode = NORMAL;
        E.search_hl = 0;
        E.search[0] = 0; E.search_len = 0;
        break;
    case '\r': case '\n':
        E.mode = NORMAL;
        if (E.search_len > 0) {
            E.search_hl = 1;
            if (!find_next_match(E.search_dir))
                E.search_hl = 0;
        }
        break;
    case KEY_BS: case CTRL('h'):
        if (E.search_len > 0) E.search[--E.search_len] = 0;
        break;
    default:
        if (k >= 32 && k != 127 && E.search_len < MAX_CMD - 1) {
            E.search[E.search_len++] = (char)k;
            E.search[E.search_len] = 0;
        }
        break;
    }
}

static void process_key(int k)
{
    switch (E.mode) {
    case NORMAL:  process_normal(k);  break;
    case INSERT:  process_insert(k);  break;
    case COMMAND: process_command(k); break;
    case SEARCH:  process_search(k);  break;
    }
}

static char comp_base[MAX_PATH];
static char comp_matches[MAX_COMPLETIONS][MAX_PATH];
static int  comp_n   = 0;
static int  comp_idx = -1;

static void process_new_tab(int k)
{
    TabState *t = &g_tabs[g_cur_tab];
    if (k == '\033' || k == CTRL('c')) {
        tab_close_idx(g_cur_tab);
        return;
    }
    if (k == CTRL('t')) { new_tab(); return; }
    if (k == '\t') {
        if (t->new_comp_idx < 0) {
            strncpy(comp_base, t->new_input, MAX_PATH - 1);
            comp_base[MAX_PATH - 1] = 0;
            build_completions(comp_base);
            t->new_comp_idx = 0;
        } else {
            t->new_comp_idx = (t->new_comp_idx + 1) % (comp_n > 0 ? comp_n : 1);
        }
        if (comp_n > 0) {
            strncpy(t->new_input, comp_matches[t->new_comp_idx], MAX_PATH - 1);
            t->new_input[MAX_PATH - 1] = 0;
            t->new_input_len = (int)strlen(t->new_input);
        }
        return;
    }
    if (k == KEY_BS || k == CTRL('h')) {
        t->new_comp_idx = -1;
        if (t->new_input_len > 0) t->new_input[--t->new_input_len] = 0;
        return;
    }
    if (k == '\r' || k == '\n') {
        if (t->new_input_len > 0) {
            if (t->new_input[t->new_input_len - 1] == '/') {
                build_completions(t->new_input);
                t->new_comp_idx = 0;
                return;
            }
            char path[MAX_PATH];
            strncpy(path, t->new_input, MAX_PATH - 1);
            path[MAX_PATH - 1] = 0;
            if (path[0] == '~' && (path[1] == '/' || path[1] == '\0')) {
                const char *home = getenv("HOME");
                if (home) {
                    char exp[MAX_PATH];
                    snprintf(exp, MAX_PATH, "%s%s", home, path + 1);
                    strncpy(path, exp, MAX_PATH - 1);
                    path[MAX_PATH - 1] = 0;
                }
            }
            t->is_new = 0;
            t->new_input[0] = 0;
            t->new_input_len = 0;
            t->new_comp_idx = -1;
            buf_open(path);
            detect_lang(path);
            if (!E.nrow) buf_ins_row(0, "", 0);
            propagate_hl(0);
        }
        return;
    }
    if (k >= 32 && k != 127 && t->new_input_len < MAX_PATH - 1) {
        t->new_comp_idx = -1;
        t->new_input[t->new_input_len++] = (char)k;
        t->new_input[t->new_input_len] = 0;
    }
}

static int cmp_comp(const void *a, const void *b)
{
    return strcmp((const char *)a, (const char *)b);
}

static void build_completions(const char *input)
{
    comp_n = 0;

    const char *last_slash = strrchr(input, '/');
    char dir_buf[MAX_PATH];
    const char *file_prefix;
    int prefix_len;

    if (last_slash) {
        int dlen = (int)(last_slash + 1 - input);
        if (dlen >= MAX_PATH) return;
        memcpy(dir_buf, input, dlen);
        dir_buf[dlen] = 0;
        file_prefix = last_slash + 1;
    } else {
        dir_buf[0] = 0;
        file_prefix = input;
    }
    prefix_len = (int)strlen(file_prefix);

    char open_dir[MAX_PATH];
    if (dir_buf[0] == '~') {
        const char *home = getenv("HOME");
        if (home)
            snprintf(open_dir, MAX_PATH, "%s%s", home, dir_buf + 1);
        else
            strncpy(open_dir, dir_buf, MAX_PATH - 1);
    } else if (dir_buf[0] == 0) {
        strcpy(open_dir, ".");
    } else {
        strncpy(open_dir, dir_buf, MAX_PATH - 1);
    }
    open_dir[MAX_PATH - 1] = 0;

    DIR *d = opendir(open_dir);
    if (!d) return;

    struct dirent *ent;
    while ((ent = readdir(d)) && comp_n < MAX_COMPLETIONS) {
        const char *name = ent->d_name;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
        if (name[0] == '.' && (prefix_len == 0 || file_prefix[0] != '.')) continue;
        if (prefix_len > 0 && strncmp(name, file_prefix, prefix_len) != 0) continue;

        size_t od_len = strlen(open_dir);
        char stat_path[MAX_PATH];
        if (od_len > 0 && open_dir[od_len - 1] == '/')
            snprintf(stat_path, MAX_PATH, "%s%s", open_dir, name);
        else
            snprintf(stat_path, MAX_PATH, "%s/%s", open_dir, name);
        struct stat st;
        int is_dir = (stat(stat_path, &st) == 0 && S_ISDIR(st.st_mode));

        snprintf(comp_matches[comp_n], MAX_PATH, "%s%s%s",
                 dir_buf, name, is_dir ? "/" : "");
        comp_n++;
    }
    closedir(d);

    if (comp_n > 1)
        qsort(comp_matches, comp_n, MAX_PATH, cmp_comp);
}

static void draw_new_completion(Ab *ab, int sel)
{
    if (comp_n <= 0 || sel < 0) return;
    Theme *t = &themes[E.theme_idx];
    int maxshow = comp_n < 10 ? comp_n : 10;
    int start = 0;
    if (sel >= 0) {
        int s = sel % comp_n;
        if (s >= maxshow) start = s - maxshow + 1;
    }
    int show = (start + maxshow <= comp_n) ? maxshow : comp_n - start;
    int sel_idx = (sel >= 0) ? sel % comp_n : -1;
    for (int i = 0; i < show; i++) {
        int idx = start + i;
        int row = E.rows - show + i;
        if (row <= TAB_BAR_H) continue;
        ab_addf(ab, "\033[%d;1H", row);
        ab_adds(ab, (idx == sel_idx) ? t->sel : t->normal);
        ab_addf(ab, " %s", comp_matches[idx]);
        ab_add(ab, "\033[K", 3);
        ab_adds(ab, t->normal);
    }
}

static void draw_start(Ab *ab, const char *input, int input_len)
{
    ab_addf(ab, "\033[%d;1H\033[J", 1 + TAB_BAR_H);

    int nlines = 0, max_w = 0;
    for (int i = 0; ASCII_ART[i]; i++) {
        int w = (int)strlen(ASCII_ART[i]);
        if (w > max_w) max_w = w;
        nlines++;
    }

    int avail = E.rows - TAB_BAR_H;
    int block_h = nlines + 2;
    int start_row = (avail - block_h) / 2 + TAB_BAR_H;
    if (start_row < 1 + TAB_BAR_H) start_row = 1 + TAB_BAR_H;

    int ascii_pad = (E.cols - max_w) / 2;
    if (ascii_pad < 0) ascii_pad = 0;

    for (int i = 0; i < nlines; i++) {
        ab_addf(ab, "\033[%d;1H", start_row + i);
        for (int p = 0; p < ascii_pad; p++) ab_add(ab, " ", 1);
        ab_adds(ab, ASCII_ART[i]);
    }

    int prompt_total = PROMPT_PREFIX_VCOLS + input_len;
    int prompt_pad = (E.cols - prompt_total) / 2;
    if (prompt_pad < 0) prompt_pad = 0;
    int prompt_row = start_row + nlines + 1;

    ab_addf(ab, "\033[%d;1H", prompt_row);
    for (int p = 0; p < prompt_pad; p++) ab_add(ab, " ", 1);
    ab_adds(ab, PROMPT_PREFIX);
    if (input_len > 0) ab_add(ab, input, input_len);

    ab_addf(ab, "\033[%d;%dH\033[6 q", prompt_row, prompt_pad + prompt_total + 1);
}

static char *show_start(void)
{
    static char input[MAX_PATH];
    int input_len = 0;
    input[0] = 0;
    comp_n = 0;
    comp_idx = -1;

    for (;;) {
        Ab ab = AB_INIT;
        ab_add(&ab, "\033[?25l", 6);
        draw_tabbar(&ab);
        draw_start(&ab, input, input_len);
        ab_add(&ab, "\033[?25h", 6);
        write(STDOUT_FILENO, ab.b, ab.len);
        ab_free(&ab);

        int k = read_key();
        if (k == 0) continue;
        if (k == '\033') return NULL;
        if (k == '\t') {
            if (comp_idx < 0) {
                strncpy(comp_base, input, MAX_PATH - 1);
                comp_base[MAX_PATH - 1] = 0;
                build_completions(comp_base);
                comp_idx = 0;
            } else {
                comp_idx = (comp_idx + 1) % (comp_n > 0 ? comp_n : 1);
            }
            if (comp_n > 0) {
                strncpy(input, comp_matches[comp_idx], MAX_PATH - 1);
                input[MAX_PATH - 1] = 0;
                input_len = (int)strlen(input);
            }
        } else if (k == KEY_BS || k == CTRL('h')) {
            comp_idx = -1;
            if (input_len > 0) input[--input_len] = 0;
        } else if (k == '\r' || k == '\n') {
            if (input_len > 0) {
                if (input[0] == '~' && (input[1] == '/' || input[1] == '\0')) {
                    const char *home = getenv("HOME");
                    if (home) {
                        char exp[MAX_PATH];
                        snprintf(exp, MAX_PATH, "%s%s", home, input + 1);
                        strncpy(input, exp, MAX_PATH - 1);
                        input[MAX_PATH - 1] = 0;
                    }
                }
                return input;
            }
        } else if (k == KEY_MOUSE_CLICK) {
            if (mouse_y == 1) {
                for (int i = 0; i < g_ntabs; i++) {
                    if (i != g_cur_tab &&
                        mouse_x >= g_tab_col_start[i] &&
                        mouse_x < g_tab_col_end[i] &&
                        mouse_x != g_tab_close_col[i]) {
                        g_pending_tab_switch = i;
                        return NULL;
                    }
                }
            }
        } else if (k >= 32 && k != 127 && input_len < MAX_PATH - 1) {
            comp_idx = -1;
            input[input_len++] = (char)k;
            input[input_len] = 0;
        }
    }
}

int main(int argc, char *argv[])
{
    memset(&E, 0, sizeof(E));
    E.cmd_comp = -1;
    load_theme();
    init_langs();
    raw_on();
    win_size();
    g_ntabs = 1;
    g_cur_tab = 0;
    memset(&g_tabs[0], 0, sizeof(g_tabs[0]));
    g_tabs[0].cmd_comp = -1;
    if (argc > 1) {
        buf_open(argv[1]);
        detect_lang(argv[1]);
    } else {
        char *path = show_start();
        if (!path) return 0;
        buf_open(path);
        detect_lang(path);
    }
    if (!E.nrow) buf_ins_row(0, "", 0);
    propagate_hl(0);
    signal(SIGWINCH, on_resize);
    while (!E.quit) {
        if (need_resize) { win_size(); need_resize = 0; }
        refresh();
        int k = read_key();
        if (k == 0) continue;
        if (k == KEY_MOUSE_SCROLL_UP) {
            set_cur(cur_r() - 3, cur_c());
        } else if (k == KEY_MOUSE_SCROLL_DOWN) {
            set_cur(cur_r() + 3, cur_c());
        } else if (k == KEY_MOUSE_CLICK) {
            if (mouse_y == 1) {
                if (g_plus_col > 0 && mouse_x >= g_plus_col - 1 && mouse_x <= g_plus_col + 1) {
                    new_tab();
                } else {
                    for (int i = 0; i < g_ntabs; i++) {
                        if (mouse_x >= g_tab_col_start[i] && mouse_x < g_tab_col_end[i]) {
                            if (mouse_x == g_tab_close_col[i]) {
                                int dirty = (i == g_cur_tab) ? E.dirty : g_tabs[i].dirty;
                                if (dirty)
                                    snprintf(E.msg, MAX_MSG, "Unsaved changes! Use :tabc! to force.");
                                else
                                    tab_close_idx(i);
                            } else {
                                tab_switch(i);
                            }
                            break;
                        }
                    }
                }
            } else if (!g_tabs[g_cur_tab].is_new && E.mode == NORMAL) {
                int fr = E.off_r + (mouse_y - 1 - TAB_BAR_H);
                int fc = E.off_c + (mouse_x - 1 - E.lnum_w);
                if (fc < 0) fc = 0;
                set_cur(fr, fc);
            }
        } else if (g_tabs[g_cur_tab].is_new) {
            process_new_tab(k);
        } else if (k == CTRL('c') && E.mode == INSERT) {
            E.mode = NORMAL;
        } else {
            process_key(k);
        }
    }
    return 0;
}
