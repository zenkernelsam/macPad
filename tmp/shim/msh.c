/*
 * msh — minimal self-contained shell for the macOS rootfs chroot.
 * No libSystem dependency at all: raw arm64/arm64e Darwin syscalls only.
 * dyld maps the image, finds zero imports, jumps to _start.
 *
 * Supports:
 *   msh -c "cmd args; cmd2 | ..."
 *   msh            (interactive REPL on fd0/fd1)
 * Builtins: cd, pwd, echo, exit, env
 * External: PATH search (/bin:/usr/bin:/sbin:/usr/sbin) + fork/execve/wait4
 * Redirection: > >> < 2> (simple forms only)
 */
typedef unsigned long u64;
typedef long i64;

#define SYS_exit     1
#define SYS_fork     2
#define SYS_read     3
#define SYS_write    4
#define SYS_open     5
#define SYS_close    6
#define SYS_wait4    7
#define SYS_chdir    12
#define SYS_dup      41
#define SYS_execve   59
#define SYS_dup2     90
#define SYS_fcntl    92
#define SYS_getpid   20

#define O_RDONLY 0
#define O_WRONLY 1
#define O_CREAT  0x200
#define O_TRUNC  0x400
#define O_APPEND 0x8
#define F_GETPATH 50

static inline i64 s1(i64 n,i64 a){register i64 x16 __asm__("x16")=n;register i64 x0 __asm__("x0")=a;__asm__ volatile("svc #0x80":"+r"(x0):"r"(x16):"memory","cc");return x0;}
static inline i64 s2(i64 n,i64 a,i64 b){register i64 x16 __asm__("x16")=n;register i64 x0 __asm__("x0")=a;register i64 x1 __asm__("x1")=b;__asm__ volatile("svc #0x80":"+r"(x0):"r"(x16),"r"(x1):"memory","cc");return x0;}
static inline i64 s3(i64 n,i64 a,i64 b,i64 c){register i64 x16 __asm__("x16")=n;register i64 x0 __asm__("x0")=a;register i64 x1 __asm__("x1")=b;register i64 x2 __asm__("x2")=c;__asm__ volatile("svc #0x80":"+r"(x0):"r"(x16),"r"(x1),"r"(x2):"memory","cc");return x0;}
static inline i64 s4(i64 n,i64 a,i64 b,i64 c,i64 d){register i64 x16 __asm__("x16")=n;register i64 x0 __asm__("x0")=a;register i64 x1 __asm__("x1")=b;register i64 x2 __asm__("x2")=c;register i64 x3 __asm__("x3")=d;__asm__ volatile("svc #0x80":"+r"(x0):"r"(x16),"r"(x1),"r"(x2),"r"(x3):"memory","cc");return x0;}

static u64 slen(const char*s){u64 n=0;while(s[n])n++;return n;}
static int seq(const char*a,const char*b){while(*a&&*a==*b){a++;b++;}return *a==*b;}
static int sncmp(const char*a,const char*b,u64 n){while(n--&&*a&&*a==*b){a++;b++;}return n==(u64)-1?0:(*a?*a-*b:-*b);}
static void puts_fd(int fd,const char*s){s3(SYS_write,fd,(i64)s,slen(s));}
static void errputs(const char*s){puts_fd(2,s);}
static void putnum(int fd,i64 v){char b[24];int i=0;if(v<0){b[i++]='-';v=-v;}char t[24];int j=0;do{t[j++]='0'+(v%10);v/=10;}while(v&&j<24);while(j)b[i++]=t[--j];s3(SYS_write,fd,(i64)b,i);}

static char **g_envp;
static char g_cwd[1024] = "/";

/* tokenize into argv; returns count, splits on space/tab, supports '..' ".." */
static int tokenize(char *line, char **argv, int max) {
    int argc = 0;
    char *p = line;
    while (*p && argc < max - 1) {
        while (*p == ' ' || *p == '\t' || *p == '\n') p++;
        if (!*p) break;
        char *out;
        if (*p == '\'' || *p == '"') {
            char q = *p++;
            argv[argc] = p;
            while (*p && *p != q) p++;
            out = p;
            if (*p) *p++ = 0;
        } else {
            argv[argc] = p;
            while (*p && *p != ' ' && *p != '\t' && *p != '\n') p++;
            out = p;
            if (*p) *p++ = 0;
        }
        (void)out;
        argc++;
    }
    argv[argc] = 0;
    return argc;
}

static void _norm_cwd(char *out, const char *cwd, const char *arg){
    /* join cwd+arg, collapse . .. // — arg==0 means stay */
    char buf[1024]; int i=0;
    if (arg && arg[0]=='/') { buf[i++]='/'; }
    else { const char *c=cwd; while(*c&&i<1000) buf[i++]=*c++; if(i>1) buf[i++]='/'; }
    if (arg) { const char *a=arg; while(*a&&i<1000) buf[i++]=*a++; }
    buf[i]=0;
    /* collapse components */
    char *o=out; char *p=buf;
    *o++='/';
    while (*p) {
        while (*p=='/') p++;
        if (!*p) break;
        char seg[256]; int n=0;
        while (*p&&*p!='/'&&n<255) seg[n++]=*p++;
        seg[n]=0;
        if (seg[0]=='.'&&!seg[1]) continue;
        if (seg[0]=='.'&&seg[1]=='.'&&!seg[2]) {
            if (o>out+1){o--;while(o>out&&*o!='/')o--;*o++='/';}
            continue;
        }
        if (o>out+1 && o[-1]!='/') *o++='/';
        char *s=seg; while(*s) *o++=*s++;
    }
    if (o==out+1) {} else if (o[-1]=='/'&&o>out+1) o--;
    if (o==out) *o++='/';
    *o=0;
}
static int builtin_cd(char **argv){
    if (!argv[1]) return 0;
    i64 r = s1(SYS_chdir,(i64)argv[1]);
    if (r==0) _norm_cwd(g_cwd,g_cwd,argv[1]);
    return (int)r;
}
static void builtin_pwd(void){ puts_fd(1,g_cwd); puts_fd(1,"\n"); }
static void builtin_echo(char **argv){
    for(int i=1;argv[i];i++){if(i>1)puts_fd(1," ");puts_fd(1,argv[i]);}
    puts_fd(1,"\n");
}
static void builtin_env(void){if(g_envp)for(char**e=g_envp;*e;e++){puts_fd(1,*e);puts_fd(1,"\n");}}

static const char *PATHS[] = {"/bin","/usr/bin","/sbin","/usr/sbin",0};

static int is_builtin(const char *n){
    return seq(n,"exit")||seq(n,"cd")||seq(n,"pwd")||seq(n,"echo")||seq(n,"env");
}
static int do_builtin(char **argv) {
    if (seq(argv[0],"exit"))  s1(SYS_exit,0);
    if (seq(argv[0],"cd"))  { int r=builtin_cd(argv); if(r){errputs("cd: failed\n");} return r?1:0; }
    if (seq(argv[0],"pwd")) { builtin_pwd(); return 0; }
    if (seq(argv[0],"echo")){ builtin_echo(argv); return 0; }
    if (seq(argv[0],"env")) { builtin_env(); return 0; }
    return 0;
}

static int run_command(char **argv) {
    if (!argv[0]) return 0;

    /* extract simple redirections */
    int out_fd = -1, in_fd = -1, err_fd = -1;
    char *clean[64]; int n = 0;
    for (int i = 0; argv[i] && n < 62; i++) {
        char *a = argv[i];
        if (seq(a,">")  && argv[i+1]) { out_fd=(int)s3(SYS_open,(i64)argv[++i],O_WRONLY|O_CREAT|O_TRUNC,0644); continue; }
        if (seq(a,">>") && argv[i+1]) { out_fd=(int)s3(SYS_open,(i64)argv[++i],O_WRONLY|O_CREAT|O_APPEND,0644); continue; }
        if (seq(a,"<")  && argv[i+1]) { in_fd =(int)s1(SYS_open,(i64)argv[++i]); continue; }
        if (seq(a,"2>") && argv[i+1]) { err_fd=(int)s3(SYS_open,(i64)argv[++i],O_WRONLY|O_CREAT|O_TRUNC,0644); continue; }
        /* fused forms: >file <file 2>file */
        if (a[0]=='>'&&a[1]=='>'&&a[2]) { out_fd=(int)s3(SYS_open,(i64)(a+2),O_WRONLY|O_CREAT|O_APPEND,0644); continue; }
        if (a[0]=='>'&&a[1])           { out_fd=(int)s3(SYS_open,(i64)(a+1),O_WRONLY|O_CREAT|O_TRUNC,0644); continue; }
        if (a[0]=='<'&&a[1])           { in_fd =(int)s1(SYS_open,(i64)(a+1)); continue; }
        if (a[0]=='2'&&a[1]=='>'&&a[2]){ err_fd=(int)s3(SYS_open,(i64)(a+2),O_WRONLY|O_CREAT|O_TRUNC,0644); continue; }
        clean[n++] = a;
    }
    clean[n] = 0;
    if (!n) { if(out_fd>=0)s1(SYS_close,out_fd); if(in_fd>=0)s1(SYS_close,in_fd); if(err_fd>=0)s1(SYS_close,err_fd); return 0; }

    /* builtins: run in-process, but honor redirections via dup save/restore */
    if (is_builtin(clean[0])) {
        i64 s0=-1,s1v=-1,s2v=-1;
        if (out_fd>=0){s1v=s1(SYS_dup,1);s2(SYS_dup2,out_fd,1);}
        if (in_fd >=0){s0 =s1(SYS_dup,0);s2(SYS_dup2,in_fd,0);}
        if (err_fd>=0){s2v=s1(SYS_dup,2);s2(SYS_dup2,err_fd,2);}
        int rc = do_builtin(clean);
        if (s1v>=0)s2(SYS_dup2,s1v,1);
        if (s0 >=0)s2(SYS_dup2,s0 ,0);
        if (s2v>=0)s2(SYS_dup2,s2v,2);
        if (out_fd>=0)s1(SYS_close,out_fd);
        if (in_fd >=0)s1(SYS_close,in_fd);
        if (err_fd>=0)s1(SYS_close,err_fd);
        return rc;
    }

    /* BSD fork arm64 ABI: parent (x0=childpid, x1=0); child (x0=childpid, x1=1) */
    register i64 x16 __asm__("x16") = SYS_fork;
    register i64 x0 __asm__("x0");
    register i64 x1 __asm__("x1");
    __asm__ volatile("svc #0x80" : "=r"(x0), "=r"(x1) : "r"(x16) : "memory","cc");
    i64 pid = x0, is_child = x1;
    if (is_child) {
        /* child */
        if (out_fd>=0){s2(SYS_dup2,out_fd,1);}
        if (in_fd >=0){s2(SYS_dup2,in_fd,0);}
        if (err_fd>=0){s2(SYS_dup2,err_fd,2);}
        char path[512];
        if (clean[0][0]=='/'||clean[0][0]=='.') {
            s3(SYS_execve,(i64)clean[0],(i64)clean,(i64)g_envp);
        } else {
            for (int i=0;PATHS[i];i++) {
                char *w=path; const char *d=PATHS[i]; while(*d)*w++=*d++; *w++='/';
                const char *c=clean[0]; while(*c)*w++=*c++; *w=0;
                s3(SYS_execve,(i64)path,(i64)clean,(i64)g_envp);
            }
        }
        errputs("msh: "); errputs(clean[0]); errputs(": not found\n");
        s1(SYS_exit,127);
    }
    if (out_fd>=0)s1(SYS_close,out_fd);
    if (in_fd >=0)s1(SYS_close,in_fd);
    if (err_fd>=0)s1(SYS_close,err_fd);
    int status=0;
    s4(SYS_wait4,(i64)pid,(i64)&status,0,0);
    return (status>>8)&0xff;
}

static void run_line(char *line) {
    /* split on ';' — no pipes/logic yet */
    char *p = line;
    while (*p) {
        char *start = p;
        while (*p && *p != ';') p++;
        int more = (*p == ';');
        if (more) *p = 0;
        char *argv[64];
        int argc = tokenize(start, argv, 64);
        if (argc) run_command(argv);
        if (!more) break;
        p++;
    }
}

/* LC_MAIN entry: dyld calls it as a function — argc/argv/envp in x0/x1/x2 */
__attribute__((noreturn))
void _start(i64 argc, char **argv, char **envp) {
    g_envp = envp;

    if (argc >= 3 && seq(argv[1],"-c")) {
        run_line(argv[2]);
        s1(SYS_exit,0);
    }
    if (argc >= 2 && argv[1][0] != '-') {
        /* script file mode: msh file */
        int fd=(int)s1(SYS_open,(i64)argv[1]);
        if(fd<0){errputs("msh: cannot open script\n");s1(SYS_exit,1);}
        static char script[8192]; i64 nr=s3(SYS_read,fd,(i64)script,sizeof(script)-1);
        if(nr>0){script[nr]=0;char*l=script;while(*l){char*e=l;while(*e&&*e!='\n')e++;int m=*e=='\n';if(m)*e=0;run_line(l);if(!m)break;l=e+1;}}
        s1(SYS_exit,0);
    }
    /* interactive */
    static char line[1024];
    for (;;) {
        puts_fd(1,"msh$ ");
        int i=0;
        while (i<(int)sizeof(line)-1) {
            char c; i64 r=s3(SYS_read,0,(i64)&c,1);
            if (r<=0) { s1(SYS_exit,0); }
            if (c=='\n') break;
            line[i++]=c;
        }
        line[i]=0;
        if (i==0) continue;
        run_line(line);
    }
}
