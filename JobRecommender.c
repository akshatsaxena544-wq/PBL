#include <stdio.h>
#include <string.h>
#include <strings.h>

#define MAX 100
#define SKILLS 8

struct Job {
    char title[50];
    char domain[50];
    int minExp;
    char skills[SKILLS][30];
    int nSkills;
};

struct Student {
    char name[50];
    char interest[50];
    int exp;
    char skills[SKILLS][30];
    int nSkills;
};

void readSkills(char str[], char skills[][30], int *n) {
    char *token = strtok(str, ";");
    *n = 0;

    while (token != NULL && *n < SKILLS) {
        while (*token == ' ')
            token++;

        strcpy(skills[*n], token);
        (*n)++;
        token = strtok(NULL, ";");
    }
}

int hasSkill(struct Student s, char skill[]) {
    for (int i = 0; i < s.nSkills; i++) {
        if (strcasecmp(s.skills[i], skill) == 0)
            return 1;
    }
    return 0;
}

double calculateScore(struct Student s, struct Job j) {
    int common = 0;

    for (int i = 0; i < j.nSkills; i++) {
        if (hasSkill(s, j.skills[i]))
            common++;
    }

    double skillScore = 0;

    if (j.nSkills > 0)
        skillScore = (double)common / j.nSkills;

    double expScore = 1.0;

    if (j.minExp > 0) {
        expScore = (double)s.exp / j.minExp;
        if (expScore > 1.0)
            expScore = 1.0;
    }

    double interestScore =
        strcasecmp(s.interest, j.domain) == 0 ? 1.0 : 0.0;

    return 100 * (0.6 * skillScore +
                  0.2 * expScore +
                  0.2 * interestScore);
}

void sortJobs(int order[], double scores[], int n) {
    for (int i = 0; i < n - 1; i++) {
        for (int j = i + 1; j < n; j++) {
            if (scores[order[j]] > scores[order[i]]) {
                int temp = order[i];
                order[i] = order[j];
                order[j] = temp;
            }
        }
    }
}

int main(void) {
    struct Job jobs[MAX] = {
        {"Java Backend Developer", "Software Development", 0,
         {"java", "oop", "sql", "spring"}, 4},
        {"Full Stack Developer", "Software Development", 1,
         {"java", "javascript", "html", "css", "sql"}, 5},
        {"Android Developer", "Software Development", 0,
         {"java", "kotlin", "android", "oop"}, 4},
        {"Software Test Engineer", "Software Development", 0,
         {"java", "testing", "sql", "selenium"}, 4},
        {"Data Scientist", "Data Science", 1,
         {"python", "statistics", "machine learning", "sql"}, 4},
        {"ML Engineer", "Artificial Intelligence", 1,
         {"python", "machine learning", "deep learning"}, 3}
    };

    int nJobs = 6;
    struct Student s;
    char input[256];

    printf("====================================\n");
    printf("     AI JOB RECOMMENDATION SYSTEM\n");
    printf("====================================\n");

    printf("Enter your name: ");
    scanf(" %49[^\n]", s.name);

    printf("Enter your career interest: ");
    scanf(" %49[^\n]", s.interest);

    printf("Enter experience in years: ");
    scanf("%d", &s.exp);

    printf("Enter skills separated by semicolons: ");
    scanf(" %255[^\n]", input);

    readSkills(input, s.skills, &s.nSkills);

    double scores[MAX];
    int order[MAX];

    for (int i = 0; i < nJobs; i++) {
        scores[i] = calculateScore(s, jobs[i]);
        order[i] = i;
    }

    sortJobs(order, scores, nJobs);

    printf("\n====================================\n");
    printf("       TOP JOB RECOMMENDATIONS\n");
    printf("====================================\n");
    printf("Candidate: %s\n", s.name);

    int limit = nJobs < 3 ? nJobs : 3;

    for (int i = 0; i < limit; i++) {
        int k = order[i];

        printf("\nRank: %d\n", i + 1);
        printf("Job Title : %s\n", jobs[k].title);
        printf("Domain    : %s\n", jobs[k].domain);
        printf("Match     : %.2f%%\n", scores[k]);
    }

    printf("\n====================================\n");
    printf("        RECOMMENDATION COMPLETE\n");
    printf("====================================\n");

    return 0;
}
