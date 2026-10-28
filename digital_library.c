/*
 * Digital Library Platform - C / POSIX Threads
 * Based on the uploaded PBL report.
 *
 * Build:
 *   gcc -std=c11 -Wall -Wextra -O2 -pthread digital_library.c -o digital_library
 * Run:
 *   ./digital_library
 *
 * This is a compact, single-file educational implementation. It demonstrates
 * structs, a hash-table catalogue, semaphores, mutexes, borrowing/returning,
 * overdue fines, and simple text-file persistence.
 */

#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define TABLE_SIZE 211
#define ID_LEN 16
#define TEXT_LEN 80
#define FINE_PER_DAY 2

typedef struct {
    char id[ID_LEN];
    char title[TEXT_LEN];
    char author[TEXT_LEN];
    char category[TEXT_LEN];
    int total_copies;
    sem_t available;
    pthread_mutex_t lock;
} Resource;

typedef struct {
    char id[ID_LEN];
    char name[TEXT_LEN];
    int max_loans;
    pthread_mutex_t lock;
} User;

typedef struct {
    char user_id[ID_LEN];
    char resource_id[ID_LEN];
    time_t issued_at;
    time_t due_at;
    time_t returned_at; /* 0 means the loan is active */
} Loan;

typedef struct ResourceNode {
    Resource *resource;
    struct ResourceNode *next;
} ResourceNode;

static ResourceNode *catalog[TABLE_SIZE];
static User users[100];
static size_t user_count;
static Loan loans[2000];
static size_t loan_count;
static pthread_mutex_t ledger_lock = PTHREAD_MUTEX_INITIALIZER;

static void copy_text(char *dst, size_t size, const char *src) {
    if (size == 0) return;
    snprintf(dst, size, "%s", src ? src : "");
    for (size_t i = 0; dst[i]; ++i) {
        if (dst[i] == '|' || dst[i] == '\n' || dst[i] == '\r')
            dst[i] = '/';
    }
}

static unsigned hash_id(const char *s) {
    unsigned hash = 5381U;
    while (*s) hash = ((hash << 5) + hash) + (unsigned char)*s++;
    return hash % TABLE_SIZE;
}

static Resource *find_resource(const char *id) {
    for (ResourceNode *n = catalog[hash_id(id)]; n; n = n->next)
        if (strcmp(n->resource->id, id) == 0) return n->resource;
    return NULL;
}

static int add_resource(const char *id, const char *title, const char *author,
                        const char *category, int copies) {
    if (!id || !*id || !title || !*title || copies < 1 ||
        find_resource(id) != NULL) return 0;

    Resource *r = calloc(1, sizeof(*r));
    ResourceNode *node = malloc(sizeof(*node));
    if (!r || !node) {
        free(r);
        free(node);
        return 0;
    }

    copy_text(r->id, sizeof(r->id), id);
    copy_text(r->title, sizeof(r->title), title);
    copy_text(r->author, sizeof(r->author), author);
    copy_text(r->category, sizeof(r->category), category);
    r->total_copies = copies;

    if (sem_init(&r->available, 0, (unsigned)copies) != 0) {
        free(r);
        free(node);
        return 0;
    }
    if (pthread_mutex_init(&r->lock, NULL) != 0) {
        sem_destroy(&r->available);
        free(r);
        free(node);
        return 0;
    }

    unsigned slot = hash_id(id);
    node->resource = r;
    node->next = catalog[slot];
    catalog[slot] = node;
    return 1;
}

static User *find_user(const char *id) {
    for (size_t i = 0; i < user_count; ++i)
        if (strcmp(users[i].id, id) == 0) return &users[i];
    return NULL;
}

static int add_user(const char *id, const char *name, int max_loans) {
    if (!id || !*id || user_count >= 100 || find_user(id) ||
        max_loans < 1) return 0;

    User *u = &users[user_count];
    copy_text(u->id, sizeof(u->id), id);
    copy_text(u->name, sizeof(u->name), name);
    u->max_loans = max_loans;
    if (pthread_mutex_init(&u->lock, NULL) != 0) return 0;
    ++user_count;
    return 1;
}

static size_t active_loans_for(const char *user_id) {
    size_t count = 0;
    for (size_t i = 0; i < loan_count; ++i)
        if (strcmp(loans[i].user_id, user_id) == 0 &&
            loans[i].returned_at == 0) ++count;
    return count;
}

static int user_has_active_loan(const char *user_id, const char *resource_id) {
    for (size_t i = 0; i < loan_count; ++i)
        if (strcmp(loans[i].user_id, user_id) == 0 &&
            strcmp(loans[i].resource_id, resource_id) == 0 &&
            loans[i].returned_at == 0) return 1;
    return 0;
}

static time_t add_days(time_t date, int days) {
    return date + (time_t)days * 24 * 60 * 60;
}

static void print_date(time_t date) {
    char buffer[32];
    struct tm tm_value;
    if (localtime_r(&date, &tm_value) &&
        strftime(buffer, sizeof(buffer), "%Y-%m-%d", &tm_value))
        printf("%s", buffer);
    else
        printf("unknown");
}

static long overdue_days(time_t due, time_t end) {
    if (end <= due) return 0;
    return (long)((end - due + 86399) / 86400);
}

static int borrow_resource(const char *user_id, const char *resource_id) {
    User *u = find_user(user_id);
    Resource *r = find_resource(resource_id);
    if (!u || !r) {
        puts("Error: user or resource not found.");
        return 0;
    }

    /* Lock order: user -> resource -> ledger. */
    pthread_mutex_lock(&u->lock);
    pthread_mutex_lock(&r->lock);
    pthread_mutex_lock(&ledger_lock);

    if (active_loans_for(user_id) >= (size_t)u->max_loans) {
        puts("Error: loan limit exceeded.");
        pthread_mutex_unlock(&ledger_lock);
        pthread_mutex_unlock(&r->lock);
        pthread_mutex_unlock(&u->lock);
        return 0;
    }
    if (user_has_active_loan(user_id, resource_id)) {
        puts("Error: this user has already borrowed this resource.");
        pthread_mutex_unlock(&ledger_lock);
        pthread_mutex_unlock(&r->lock);
        pthread_mutex_unlock(&u->lock);
        return 0;
    }
    if (loan_count >= sizeof(loans) / sizeof(loans[0])) {
        puts("Error: lending ledger is full.");
        pthread_mutex_unlock(&ledger_lock);
        pthread_mutex_unlock(&r->lock);
        pthread_mutex_unlock(&u->lock);
        return 0;
    }
    if (sem_trywait(&r->available) != 0) {
        puts("Error: no copies available.");
        pthread_mutex_unlock(&ledger_lock);
        pthread_mutex_unlock(&r->lock);
        pthread_mutex_unlock(&u->lock);
        return 0;
    }

    time_t now = time(NULL);
    Loan *loan = &loans[loan_count++];
    copy_text(loan->user_id, sizeof(loan->user_id), user_id);
    copy_text(loan->resource_id, sizeof(loan->resource_id), resource_id);
    loan->issued_at = now;
    loan->due_at = add_days(now, 14);
    loan->returned_at = 0;

    printf("Borrowed: %s - %s. Due date: ", r->id, r->title);
    print_date(loan->due_at);
    putchar('\n');

    pthread_mutex_unlock(&ledger_lock);
    pthread_mutex_unlock(&r->lock);
    pthread_mutex_unlock(&u->lock);
    return 1;
}

static int return_resource(const char *user_id, const char *resource_id) {
    User *u = find_user(user_id);
    Resource *r = find_resource(resource_id);
    if (!u || !r) {
        puts("Error: user or resource not found.");
        return 0;
    }

    pthread_mutex_lock(&u->lock);
    pthread_mutex_lock(&r->lock);
    pthread_mutex_lock(&ledger_lock);

    for (size_t i = 0; i < loan_count; ++i) {
        Loan *loan = &loans[i];
        if (strcmp(loan->user_id, user_id) == 0 &&
            strcmp(loan->resource_id, resource_id) == 0 &&
            loan->returned_at == 0) {
            loan->returned_at = time(NULL);
            long days = overdue_days(loan->due_at, loan->returned_at);
            long fine = days * FINE_PER_DAY;
            sem_post(&r->available);

            printf("Returned: %s. Overdue days: %ld. Fine: Rs. %ld\n",
                   r->title, days, fine);

            pthread_mutex_unlock(&ledger_lock);
            pthread_mutex_unlock(&r->lock);
            pthread_mutex_unlock(&u->lock);
            return 1;
        }
    }

    puts("Error: no active loan found for this resource.");
    pthread_mutex_unlock(&ledger_lock);
    pthread_mutex_unlock(&r->lock);
    pthread_mutex_unlock(&u->lock);
    return 0;
}

static int contains_case_insensitive(const char *text, const char *keyword) {
    if (!keyword || !*keyword) return 1;
    size_t n = strlen(keyword);
    for (; *text; ++text) {
        size_t i = 0;
        while (i < n && text[i] &&
               tolower((unsigned char)text[i]) ==
               tolower((unsigned char)keyword[i])) ++i;
        if (i == n) return 1;
    }
    return 0;
}

static void search_catalog(const char *keyword) {
    int found = 0;
    for (size_t i = 0; i < TABLE_SIZE; ++i) {
        for (ResourceNode *n = catalog[i]; n; n = n->next) {
            Resource *r = n->resource;
            if (contains_case_insensitive(r->title, keyword) ||
                contains_case_insensitive(r->author, keyword) ||
                contains_case_insensitive(r->category, keyword)) {
                int available = 0;
                sem_getvalue(&r->available, &available);
                printf("%s | %s | %s | %s | Available: %d/%d\n",
                       r->id, r->title, r->author, r->category,
                       available, r->total_copies);
                found = 1;
            }
        }
    }
    if (!found) puts("No matching resources found.");
}

static void list_my_loans(const char *user_id) {
    int found = 0;
    time_t now = time(NULL);
    pthread_mutex_lock(&ledger_lock);
    for (size_t i = 0; i < loan_count; ++i) {
        Loan *loan = &loans[i];
        if (strcmp(loan->user_id, user_id) == 0 && loan->returned_at == 0) {
            Resource *r = find_resource(loan->resource_id);
            printf("%s | %s | Due: ", loan->resource_id,
                   r ? r->title : "Unknown resource");
            print_date(loan->due_at);
            printf(" | Current fine: Rs. %ld\n",
                   overdue_days(loan->due_at, now) * FINE_PER_DAY);
            found = 1;
        }
    }
    pthread_mutex_unlock(&ledger_lock);
    if (!found) puts("No active loans.");
}

static int save_data(void) {
    FILE *f = fopen("resources.txt.tmp", "w");
    if (!f) return 0;
    for (size_t i = 0; i < TABLE_SIZE; ++i)
        for (ResourceNode *n = catalog[i]; n; n = n->next) {
            Resource *r = n->resource;
            fprintf(f, "%s|%s|%s|%s|%d\n", r->id, r->title,
                    r->author, r->category, r->total_copies);
        }
    if (fclose(f) != 0 || rename("resources.txt.tmp", "resources.txt") != 0)
        return 0;

    f = fopen("users.txt.tmp", "w");
    if (!f) return 0;
    for (size_t i = 0; i < user_count; ++i)
        fprintf(f, "%s|%s|%d\n", users[i].id, users[i].name,
                users[i].max_loans);
    if (fclose(f) != 0 || rename("users.txt.tmp", "users.txt") != 0)
        return 0;

    pthread_mutex_lock(&ledger_lock);
    f = fopen("ledger.txt.tmp", "w");
    if (!f) {
        pthread_mutex_unlock(&ledger_lock);
        return 0;
    }
    for (size_t i = 0; i < loan_count; ++i) {
        Loan *l = &loans[i];
        fprintf(f, "%s|%s|%lld|%lld|%lld\n", l->user_id, l->resource_id,
                (long long)l->issued_at, (long long)l->due_at,
                (long long)l->returned_at);
    }
    int ok = fclose(f) == 0 && rename("ledger.txt.tmp", "ledger.txt") == 0;
    pthread_mutex_unlock(&ledger_lock);
    return ok;
}

static void seed_data(void) {
    add_resource("B001", "The C Programming Language", "Kernighan and Ritchie",
                 "Programming", 3);
    add_resource("B002", "Operating System Concepts", "Silberschatz",
                 "Operating Systems", 2);
    add_resource("E001", "Introduction to Algorithms", "Cormen",
                 "Algorithms", 2);
    add_resource("J001", "Computing Research Journal", "Editorial Board",
                 "Journal", 1);

    add_user("S001", "Student 1", 3);
    add_user("F001", "Faculty 1", 5);
    add_user("L001", "Librarian 1", 10);
}

static void cleanup(void) {
    for (size_t i = 0; i < TABLE_SIZE; ++i) {
        ResourceNode *n = catalog[i];
        while (n) {
            ResourceNode *next = n->next;
            sem_destroy(&n->resource->available);
            pthread_mutex_destroy(&n->resource->lock);
            free(n->resource);
            free(n);
            n = next;
        }
    }
    for (size_t i = 0; i < user_count; ++i)
        pthread_mutex_destroy(&users[i].lock);
    pthread_mutex_destroy(&ledger_lock);
}

int main(void) {
    seed_data();

    char user_id[ID_LEN];
    printf("=== Digital Library Platform ===\n");
    printf("Sample users: S001 (Student), F001 (Faculty), L001 (Librarian)\n");
    printf("Enter user ID: ");
    if (scanf("%15s", user_id) != 1 || !find_user(user_id)) {
        puts("Invalid user ID.");
        cleanup();
        return 1;
    }

    int choice;
    do {
        printf("\n1. Search catalogue\n2. Borrow resource\n3. Return resource\n"
               "4. My active loans\n5. Save data\n0. Exit\nChoice: ");
        if (scanf("%d", &choice) != 1) {
            puts("Invalid input.");
            break;
        }

        char input[TEXT_LEN];
        switch (choice) {
            case 1:
                printf("Enter title, author, or category keyword: ");
                if (scanf(" %79[^\n]", input) == 1) search_catalog(input);
                break;
            case 2:
                printf("Enter resource ID: ");
                if (scanf("%15s", input) == 1) borrow_resource(user_id, input);
                break;
            case 3:
                printf("Enter resource ID: ");
                if (scanf("%15s", input) == 1) return_resource(user_id, input);
                break;
            case 4:
                list_my_loans(user_id);
                break;
            case 5:
                puts(save_data() ? "Data saved successfully." : "Could not save data.");
                break;
            case 0:
                if (!save_data()) puts("Warning: data could not be saved.");
                puts("Goodbye.");
                break;
            default:
                puts("Choose a valid menu option.");
        }
    } while (choice != 0);

    cleanup();
    return 0;
}
