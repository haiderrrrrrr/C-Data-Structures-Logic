#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <direct.h>
#  include <windows.h>
#  ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#    define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#  endif
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/types.h>

/*  ANSI colors (Win10+ / all Unix)  */
#define RST  "\033[0m"
#define BOLD "\033[1m"
#define DIM  "\033[2m"
#define RED  "\033[91m"
#define GRN  "\033[92m"
#define YLW  "\033[93m"
#define BLU  "\033[94m"
#define MAG  "\033[95m"
#define CYN  "\033[96m"

/*  File paths  */
#define F_USERS  "users.txt"
#define F_LIB    "library.txt"
#define F_TEMP   "library_temp.txt"
#define F_HIST   "history/%s_history.txt"

/*  Config  */
#define LOAN_DAYS 14
#define LBUF      1024

/*  Structs  */
typedef struct { int id; char title[256]; char author[128]; int year; int borrowed; } Book;
typedef struct { char username[50]; uint32_t pwhash; int is_admin; } User;

/*  Prototypes  */
void          enableANSI(void);
void          ensureDir(const char *path);
void          seedDefaults(void);
void          printBanner(void);
void          printSep(void);
int           readInt(int *out);
void          readLine(char *buf, int max);
uint32_t      djb2(const char *s);
int           isStrongPw(const char *pw);
int           userExists(const char *uname);
int           loginUser(char *outUser, int *outAdmin);
void          doSignup(void);
void          listBooks(void);
void          searchBooks(void);
void          borrowBook(const char *uname);
void          returnBook(const char *uname);
void          viewHistory(const char *uname);
void          addBook(void);
void          removeBook(void);
void          viewAllUsers(void);
void          viewAllHistory(void);
int           nextBookId(void);
void          userMenu(const char *uname);
void          adminMenu(const char *uname);
void          mainMenu(void);

/* 
   SYSTEM UTILITIES
    */

void enableANSI(void) {
#ifdef _WIN32
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD m;
    if (GetConsoleMode(h, &m))
        SetConsoleMode(h, m | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
#endif
}

void ensureDir(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) {
#ifdef _WIN32
        _mkdir(path);
#else
        mkdir(path, 0700);
#endif
    }
}

/* Seed library.txt / users.txt if empty or missing */
void seedDefaults(void) {
    struct stat st;

    if (stat(F_LIB, &st) != 0 || st.st_size == 0) {
        FILE *f = fopen(F_LIB, "w");
        if (!f) return;
        fprintf(f, "1|The Hitchhiker's Guide to the Galaxy|Douglas Adams|1979|0\n");
        fprintf(f, "2|Nineteen Eighty-Four|George Orwell|1949|0\n");
        fprintf(f, "3|The Great Gatsby|F. Scott Fitzgerald|1925|0\n");
        fprintf(f, "4|To Kill a Mockingbird|Harper Lee|1960|0\n");
        fprintf(f, "5|Brave New World|Aldous Huxley|1932|0\n");
        fprintf(f, "6|The Catcher in the Rye|J.D. Salinger|1951|0\n");
        fprintf(f, "7|Animal Farm|George Orwell|1945|0\n");
        fprintf(f, "8|Harry Potter and the Sorcerer's Stone|J.K. Rowling|1997|0\n");
        fprintf(f, "9|The Lord of the Rings|J.R.R. Tolkien|1954|0\n");
        fprintf(f, "10|Pride and Prejudice|Jane Austen|1813|0\n");
        fclose(f);
    }

    if (stat(F_USERS, &st) != 0 || st.st_size == 0) {
        FILE *f = fopen(F_USERS, "w");
        if (!f) return;
        fprintf(f, "admin|%u|1\n", djb2("Admin@123"));
        fclose(f);
    }
}

/* 
   UI HELPERS
    */

void printBanner(void) {
    printf(CYN BOLD);
    printf("\n  +------------------------------------------+\n");
    printf(  "  |        LIBRARY MANAGEMENT SYSTEM         |\n");
    printf(  "  |              Built in Pure C             |\n");
    printf(  "  +------------------------------------------+\n");
    printf(RST "\n");
}

void printSep(void) {
    printf(DIM "  --------------------------------------------------------------\n" RST);
}

/* 
   INPUT HELPERS
    */

/* Reads a line and parses a single integer. Returns 1 on success, 0 on failure. */
int readInt(int *out) {
    char buf[64];
    if (!fgets(buf, sizeof buf, stdin)) return 0;
    char *end;
    long v = strtol(buf, &end, 10);
    if (end == buf || (*end != '\n' && *end != '\0' && *end != '\r')) return 0;
    *out = (int)v;
    return 1;
}

/* Reads a line, strips the trailing newline. */
void readLine(char *buf, int max) {
    if (!fgets(buf, max, stdin)) { buf[0] = '\0'; return; }
    buf[strcspn(buf, "\r\n")] = '\0';
}

/* 
   CRYPTO / VALIDATION
    */

uint32_t djb2(const char *s) {
    uint32_t h = 5381;
    int c;
    while ((c = (unsigned char)*s++)) h = ((h << 5) + h) ^ (uint32_t)c;
    return h;
}

int isStrongPw(const char *pw) {
    if (strlen(pw) < 8) return 0;
    int u = 0, l = 0, d = 0, s = 0;
    for (const char *p = pw; *p; p++) {
        if      (isupper((unsigned char)*p)) u = 1;
        else if (islower((unsigned char)*p)) l = 1;
        else if (isdigit((unsigned char)*p)) d = 1;
        else                                 s = 1;
    }
    return u && l && d && s;
}

int userExists(const char *uname) {
    FILE *f = fopen(F_USERS, "r");
    if (!f) return 0;
    char line[LBUF];
    User u;
    while (fgets(line, sizeof line, f)) {
        if (sscanf(line, "%49[^|]|%u|%d", u.username, &u.pwhash, &u.is_admin) == 3)
            if (strcmp(u.username, uname) == 0) { fclose(f); return 1; }
    }
    fclose(f);
    return 0;
}

/* 
   AUTH
    */

int loginUser(char *outUser, int *outAdmin) {
    char uname[50], pw[50];
    printf(CYN "  Username: " RST); readLine(uname, sizeof uname);
    printf(CYN "  Password: " RST); readLine(pw,    sizeof pw);
    unsigned long h = djb2(pw);

    FILE *f = fopen(F_USERS, "r");
    if (!f) { printf(RED "  Error: cannot open users file.\n" RST); return 0; }

    char line[LBUF];
    User u;
    while (fgets(line, sizeof line, f)) {
        if (sscanf(line, "%49[^|]|%u|%d", u.username, &u.pwhash, &u.is_admin) == 3) {
            if (strcmp(uname, u.username) == 0 && h == u.pwhash) {
                fclose(f);
                strcpy(outUser, u.username);
                *outAdmin = u.is_admin;
                printf(GRN "\n  Login successful. Welcome, %s%s!\n" RST,
                       u.username, u.is_admin ? " " MAG "(Admin)" RST : "");
                return 1;
            }
        }
    }
    fclose(f);
    printf(RED "\n  Invalid username or password.\n" RST);
    return 0;
}

void doSignup(void) {
    char uname[50], pw[50];
    printf(CYN "  Choose a username: " RST);
    readLine(uname, sizeof uname);

    if (strlen(uname) < 3) {
        printf(RED "  Username must be at least 3 characters.\n" RST); return;
    }
    if (userExists(uname)) {
        printf(RED "  Username \"%s\" is already taken.\n" RST, uname); return;
    }

    printf(CYN "  Choose a password\n");
    printf(DIM "  (min 8 chars, must include uppercase, lowercase, digit, special char): " RST);
    readLine(pw, sizeof pw);

    if (!isStrongPw(pw)) {
        printf(RED "  Password does not meet the requirements above.\n" RST); return;
    }

    FILE *f = fopen(F_USERS, "a");
    if (!f) { printf(RED "  Error: cannot open users file.\n" RST); return; }
    fprintf(f, "%s|%u|0\n", uname, djb2(pw));
    fclose(f);
    printf(GRN "\n  Account created! You can now log in.\n" RST);
}

/* 
   LIBRARY  READ OPERATIONS
    */

void listBooks(void) {
    FILE *f = fopen(F_LIB, "r");
    if (!f) { printf(RED "  Error: cannot open library file.\n" RST); return; }

    printf("\n");
    printf(BOLD CYN "  %-4s  %-42s  %-26s  %-6s  %s\n" RST,
           "ID", "Title", "Author", "Year", "Status");
    printSep();

    char line[LBUF];
    Book b;
    int count = 0;
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (sscanf(line, "%d|%255[^|]|%127[^|]|%d|%d",
                   &b.id, b.title, b.author, &b.year, &b.borrowed) != 5) continue;
        const char *s = b.borrowed ? RED "Borrowed " RST : GRN "Available" RST;
        printf("  %-4d  %-42s  %-26s  %-6d  %s\n",
               b.id, b.title, b.author, b.year, s);
        count++;
    }
    fclose(f);
    if (!count) printf(YLW "  The library is empty.\n" RST);
    printSep();
    printf(DIM "  %d book(s) in catalog\n\n" RST, count);
}

void searchBooks(void) {
    char query[256];
    printf(CYN "  Search (title or author): " RST);
    readLine(query, sizeof query);

    /* build lowercase query */
    char lq[256];
    int i;
    for (i = 0; query[i]; i++) lq[i] = (char)tolower((unsigned char)query[i]);
    lq[i] = '\0';

    FILE *f = fopen(F_LIB, "r");
    if (!f) { printf(RED "  Error: cannot open library file.\n" RST); return; }

    printf("\n");
    printf(BOLD CYN "  %-4s  %-42s  %-26s  %-6s  %s\n" RST,
           "ID", "Title", "Author", "Year", "Status");
    printSep();

    char line[LBUF];
    Book b;
    int found = 0;
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (sscanf(line, "%d|%255[^|]|%127[^|]|%d|%d",
                   &b.id, b.title, b.author, &b.year, &b.borrowed) != 5) continue;

        char lt[256], la[128];
        for (i = 0; b.title[i];  i++) lt[i] = (char)tolower((unsigned char)b.title[i]);
        lt[i] = '\0';
        for (i = 0; b.author[i]; i++) la[i] = (char)tolower((unsigned char)b.author[i]);
        la[i] = '\0';

        if (!strstr(lt, lq) && !strstr(la, lq)) continue;
        const char *s = b.borrowed ? RED "Borrowed " RST : GRN "Available" RST;
        printf("  %-4d  %-42s  %-26s  %-6d  %s\n",
               b.id, b.title, b.author, b.year, s);
        found++;
    }
    fclose(f);
    if (!found) printf(YLW "  No books matched \"%s\".\n" RST, query);
    printSep();
}

/* 
   LIBRARY  BORROW / RETURN
    */

void borrowBook(const char *uname) {
    int id;
    printf(CYN "  Enter Book ID to borrow: " RST);
    if (!readInt(&id)) { printf(RED "  Invalid input  enter a number.\n" RST); return; }

    FILE *f = fopen(F_LIB, "r");
    FILE *t = fopen(F_TEMP, "w");
    if (!f || !t) {
        printf(RED "  File error.\n" RST);
        if (f) fclose(f);
        if (t) fclose(t);
        return;
    }

    char line[LBUF];
    Book b;
    int result = 0; /* 1=borrowed, -1=already out, -2=not found */

    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (sscanf(line, "%d|%255[^|]|%127[^|]|%d|%d",
                   &b.id, b.title, b.author, &b.year, &b.borrowed) != 5) {
            fprintf(t, "%s\n", line); continue;
        }
        if (b.id == id) {
            if (!b.borrowed) {
                b.borrowed = 1;
                result = 1;
                /* append to user history */
                char hpath[150];
                snprintf(hpath, sizeof hpath, F_HIST, uname);
                FILE *h = fopen(hpath, "a");
                if (h) {
                    time_t now = time(NULL);
                    time_t due = now + (time_t)(LOAN_DAYS * 86400);
                    fprintf(h, "%d|%s|%ld|%ld|ACTIVE\n",
                            b.id, b.title, (long)now, (long)due);
                    fclose(h);
                }
            } else {
                result = -1;
            }
        }
        fprintf(t, "%d|%s|%s|%d|%d\n", b.id, b.title, b.author, b.year, b.borrowed);
    }
    fclose(f); fclose(t);

    if (result == 1 || result == 0) { remove(F_LIB); rename(F_TEMP, F_LIB); }
    else                              remove(F_TEMP);

    if (result == 1) {
        time_t due = time(NULL) + (time_t)(LOAN_DAYS * 86400);
        char dstr[20];
        strftime(dstr, sizeof dstr, "%Y-%m-%d", localtime(&due));
        printf(GRN "\n  Book borrowed successfully!\n" RST);
        printf(YLW "  Due date: %s  (%d days)\n\n" RST, dstr, LOAN_DAYS);
    } else if (result == -1) {
        printf(RED "\n  This book is already borrowed by someone else.\n" RST);
    } else {
        printf(RED "\n  Book ID %d not found.\n" RST, id);
    }
}

void returnBook(const char *uname) {
    int id;
    printf(CYN "  Enter Book ID to return: " RST);
    if (!readInt(&id)) { printf(RED "  Invalid input  enter a number.\n" RST); return; }

    FILE *f = fopen(F_LIB, "r");
    FILE *t = fopen(F_TEMP, "w");
    if (!f || !t) {
        printf(RED "  File error.\n" RST);
        if (f) fclose(f);
        if (t) fclose(t);
        return;
    }

    char line[LBUF];
    Book b;
    int result = 0;

    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (sscanf(line, "%d|%255[^|]|%127[^|]|%d|%d",
                   &b.id, b.title, b.author, &b.year, &b.borrowed) != 5) {
            fprintf(t, "%s\n", line); continue;
        }
        if (b.id == id) {
            if (b.borrowed) { b.borrowed = 0; result = 1; }
            else              result = -1;
        }
        fprintf(t, "%d|%s|%s|%d|%d\n", b.id, b.title, b.author, b.year, b.borrowed);
    }
    fclose(f); fclose(t);

    if (result == 1) { remove(F_LIB); rename(F_TEMP, F_LIB); }
    else              remove(F_TEMP);

    if (result != 1) {
        if (result == -1) printf(YLW "\n  That book is not currently borrowed.\n" RST);
        else              printf(RED "\n  Book ID %d not found.\n" RST, id);
        return;
    }

    /* update history: mark the ACTIVE entry for this book as RETURNED */
    char hpath[150], htmp[160];
    snprintf(hpath, sizeof hpath, F_HIST, uname);
    snprintf(htmp,  sizeof htmp,  "history/.tmp_%s.txt", uname);

    FILE *hf = fopen(hpath, "r");
    FILE *ht = fopen(htmp,  "w");
    time_t due_stored = 0;
    int hist_updated = 0;

    if (hf && ht) {
        char hl[LBUF];
        int  bid; char btitle[256]; long bt, dt; char status[16];
        while (fgets(hl, sizeof hl, hf)) {
            hl[strcspn(hl, "\r\n")] = '\0';
            if (!hist_updated &&
                sscanf(hl, "%d|%255[^|]|%ld|%ld|%15s", &bid, btitle, &bt, &dt, status) == 5 &&
                bid == id && strcmp(status, "ACTIVE") == 0) {
                due_stored = (time_t)dt;
                fprintf(ht, "%d|%s|%ld|%ld|RETURNED\n", bid, btitle, bt, dt);
                hist_updated = 1;
            } else {
                fprintf(ht, "%s\n", hl);
            }
        }
        fclose(hf); fclose(ht);
        remove(hpath); rename(htmp, hpath);
    } else {
        if (hf) fclose(hf);
        if (ht) fclose(ht);
    }

    printf(GRN "\n  Book returned successfully!\n" RST);
    if (due_stored > 0 && time(NULL) > due_stored) {
        long days_late = (long)((time(NULL) - due_stored) / 86400);
        printf(RED "  Note: %ld day(s) overdue.\n\n" RST, days_late);
    } else {
        printf(GRN "  Returned on time  thank you!\n\n" RST);
    }
}

void viewHistory(const char *uname) {
    char hpath[150];
    snprintf(hpath, sizeof hpath, F_HIST, uname);

    FILE *f = fopen(hpath, "r");
    if (!f) { printf(YLW "\n  No borrowing history found.\n" RST); return; }

    printf("\n" BOLD CYN "  Borrowing History  %s\n" RST, uname);
    printf(BOLD "  %-4s  %-42s  %-12s  %-12s  %s\n" RST,
           "ID", "Title", "Borrowed", "Due", "Status");
    printSep();

    char line[LBUF];
    int bid; char btitle[256]; long btime, dtime; char status[16];
    int count = 0;
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (sscanf(line, "%d|%255[^|]|%ld|%ld|%15s",
                   &bid, btitle, &btime, &dtime, status) != 5) continue;
        char bdate[14], ddate[14];
        time_t bt = (time_t)btime, dt = (time_t)dtime;
        strftime(bdate, sizeof bdate, "%Y-%m-%d", localtime(&bt));
        strftime(ddate, sizeof ddate, "%Y-%m-%d", localtime(&dt));
        const char *sc = strcmp(status, "ACTIVE") == 0 ? YLW : GRN;
        printf("  %-4d  %-42s  %-12s  %-12s  %s%s" RST "\n",
               bid, btitle, bdate, ddate, sc, status);
        count++;
    }
    fclose(f);
    if (!count) printf(YLW "  No entries yet.\n" RST);
    printSep();
    printf(DIM "  %d total borrow(s)\n\n" RST, count);
}

/* 
   ADMIN  BOOK MANAGEMENT
    */

int nextBookId(void) {
    FILE *f = fopen(F_LIB, "r");
    if (!f) return 1;
    char line[LBUF];
    int maxId = 0, id;
    while (fgets(line, sizeof line, f))
        if (sscanf(line, "%d|", &id) == 1 && id > maxId) maxId = id;
    fclose(f);
    return maxId + 1;
}

void addBook(void) {
    Book b;
    b.id       = nextBookId();
    b.borrowed = 0;

    printf(CYN "  Title:  " RST); readLine(b.title,  sizeof b.title);
    printf(CYN "  Author: " RST); readLine(b.author, sizeof b.author);
    printf(CYN "  Year:   " RST);
    if (!readInt(&b.year)) { printf(RED "  Invalid year.\n" RST); return; }

    if (!strlen(b.title) || !strlen(b.author)) {
        printf(RED "  Title and author cannot be empty.\n" RST); return;
    }

    FILE *f = fopen(F_LIB, "a");
    if (!f) { printf(RED "  Error: cannot open library file.\n" RST); return; }
    fprintf(f, "%d|%s|%s|%d|0\n", b.id, b.title, b.author, b.year);
    fclose(f);
    printf(GRN "\n  \"%s\" added to the catalog (ID %d).\n\n" RST, b.title, b.id);
}

void removeBook(void) {
    int id;
    printf(CYN "  Enter Book ID to remove: " RST);
    if (!readInt(&id)) { printf(RED "  Invalid input.\n" RST); return; }

    FILE *f = fopen(F_LIB, "r");
    FILE *t = fopen(F_TEMP, "w");
    if (!f || !t) {
        printf(RED "  File error.\n" RST);
        if (f) fclose(f);
        if (t) fclose(t);
        return;
    }

    char line[LBUF];
    Book b;
    int result = 0;

    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (sscanf(line, "%d|%255[^|]|%127[^|]|%d|%d",
                   &b.id, b.title, b.author, &b.year, &b.borrowed) == 5 && b.id == id) {
            if (b.borrowed) { result = -1; fprintf(t, "%s\n", line); } /* keep it */
            else              result =  1;                               /* omit = delete */
        } else {
            fprintf(t, "%s\n", line);
        }
    }
    fclose(f); fclose(t);

    if (result == 1) {
        remove(F_LIB); rename(F_TEMP, F_LIB);
        printf(GRN "\n  Book removed from catalog.\n\n" RST);
    } else {
        remove(F_TEMP);
        if (result == -1) printf(RED "\n  Cannot remove: book is currently borrowed.\n" RST);
        else              printf(RED "\n  Book ID %d not found.\n" RST, id);
    }
}

/* 
   ADMIN  USER / HISTORY VIEWS
    */

void viewAllUsers(void) {
    FILE *f = fopen(F_USERS, "r");
    if (!f) { printf(RED "  Error: cannot open users file.\n" RST); return; }

    printf("\n" BOLD CYN "  %-22s  %s\n" RST, "Username", "Role");
    printSep();

    char line[LBUF];
    User u;
    int count = 0;
    while (fgets(line, sizeof line, f)) {
        if (sscanf(line, "%49[^|]|%u|%d", u.username, &u.pwhash, &u.is_admin) == 3) {
            printf("  %-22s  %s\n",
                   u.username, u.is_admin ? MAG "Admin" RST : "User");
            count++;
        }
    }
    fclose(f);
    printSep();
    printf(DIM "  %d registered user(s)\n\n" RST, count);
}

void viewAllHistory(void) {
    FILE *f = fopen(F_USERS, "r");
    if (!f) { printf(RED "  Error: cannot open users file.\n" RST); return; }

    char line[LBUF];
    User u;
    printf("\n" BOLD CYN "  All Borrowing History\n\n" RST);

    while (fgets(line, sizeof line, f)) {
        if (sscanf(line, "%49[^|]|%u|%d", u.username, &u.pwhash, &u.is_admin) != 3) continue;

        char hpath[150];
        snprintf(hpath, sizeof hpath, F_HIST, u.username);
        FILE *hf = fopen(hpath, "r");
        if (!hf) continue;

        printf(BOLD YLW "  [ %s ]\n" RST, u.username);
        char hl[LBUF];
        int bid; char btitle[256]; long bt, dt; char status[16];
        int count = 0;
        while (fgets(hl, sizeof hl, hf)) {
            hl[strcspn(hl, "\r\n")] = '\0';
            if (sscanf(hl, "%d|%255[^|]|%ld|%ld|%15s",
                       &bid, btitle, &bt, &dt, status) != 5) continue;
            char bdate[14];
            time_t btt = (time_t)bt;
            strftime(bdate, sizeof bdate, "%Y-%m-%d", localtime(&btt));
            const char *sc = strcmp(status, "ACTIVE") == 0 ? YLW : GRN;
            printf("    [%s]  ID %-4d  %-42s  %s%s" RST "\n",
                   bdate, bid, btitle, sc, status);
            count++;
        }
        fclose(hf);
        if (!count) printf(DIM "    No entries.\n" RST);
        printf("\n");
    }
    fclose(f);
}

/* 
   MENUS
    */

void userMenu(const char *uname) {
    int choice;
    for (;;) {
        printf(BOLD "\n   User Menu  " CYN "(%s)" RST BOLD " \n" RST, uname);
        printf("  1. List All Books\n");
        printf("  2. Search Books\n");
        printf("  3. Borrow a Book\n");
        printf("  4. Return a Book\n");
        printf("  5. My Borrowing History\n");
        printf("  6. Logout\n");
        printf(CYN "\n  Choice: " RST);
        if (!readInt(&choice)) { printf(RED "  Please enter a number (1-6).\n" RST); continue; }
        switch (choice) {
            case 1: listBooks();        break;
            case 2: searchBooks();      break;
            case 3: borrowBook(uname);  break;
            case 4: returnBook(uname);  break;
            case 5: viewHistory(uname); break;
            case 6: printf(YLW "\n  Logged out. Goodbye, %s!\n" RST, uname); return;
            default: printf(RED "  Invalid choice.\n" RST);
        }
    }
}

void adminMenu(const char *uname) {
    int choice;
    for (;;) {
        printf(BOLD "\n   Admin Menu  " MAG "(%s)" RST BOLD " \n" RST, uname);
        printf(DIM "  Library\n" RST);
        printf("   1. List All Books\n");
        printf("   2. Search Books\n");
        printf("   3. Add a Book\n");
        printf("   4. Remove a Book\n");
        printf(DIM "  Borrowing\n" RST);
        printf("   5. Borrow a Book\n");
        printf("   6. Return a Book\n");
        printf("   7. My Borrowing History\n");
        printf(DIM "  System\n" RST);
        printf("   8. View All Users\n");
        printf("   9. View All Borrowing History\n");
        printf("  10. Logout\n");
        printf(CYN "\n  Choice: " RST);
        if (!readInt(&choice)) { printf(RED "  Please enter a number (1-10).\n" RST); continue; }
        switch (choice) {
            case 1:  listBooks();           break;
            case 2:  searchBooks();         break;
            case 3:  addBook();             break;
            case 4:  removeBook();          break;
            case 5:  borrowBook(uname);     break;
            case 6:  returnBook(uname);     break;
            case 7:  viewHistory(uname);    break;
            case 8:  viewAllUsers();        break;
            case 9:  viewAllHistory();      break;
            case 10: printf(YLW "\n  Logged out. Goodbye, %s!\n" RST, uname); return;
            default: printf(RED "  Invalid choice.\n" RST);
        }
    }
}

void mainMenu(void) {
    int choice;
    for (;;) {
        printBanner();
        printf("  1. Login\n");
        printf("  2. Sign Up\n");
        printf("  3. Exit\n");
        printf(CYN "\n  Choice: " RST);
        if (!readInt(&choice)) { printf(RED "  Please enter 1, 2, or 3.\n" RST); continue; }
        switch (choice) {
            case 1: {
                char uname[50]; int isAdmin = 0;
                if (loginUser(uname, &isAdmin)) {
                    if (isAdmin) adminMenu(uname);
                    else         userMenu(uname);
                }
                break;
            }
            case 2: doSignup(); break;
            case 3: printf(CYN "\n  Goodbye!\n\n" RST); exit(0);
            default: printf(RED "  Invalid choice.\n" RST);
        }
    }
}

/* 
   ENTRY POINT
    */

int main(void) {
    enableANSI();
    ensureDir("history");
    seedDefaults();
    mainMenu();
    return 0;
}
